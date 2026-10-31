
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_err.h"

#include "driver/gpio.h"

#include "app_config.h"
#include "controller.h"
#include "system_types.h"
#include "role1_demo.h"
#include "door.h"
#include "config.h"
#include "serial_cli.h"
#include "rfid.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* =========================================================
 * ROLE 2 HARDWARE CONFIGURATION
 * ========================================================= */

#if !APP_ROLE1_DEMO

static const char *TAG = "RFID_TEST";

/* Two independent RFID reader instances. */
static rfid_reader_t entrance_reader;
static rfid_reader_t exit_reader;

/* Passage confirmation GPIOs. */
#define ENTRY_PASSAGE_GPIO GPIO_NUM_21
#define EXIT_PASSAGE_GPIO  GPIO_NUM_27

/* =========================================================
 * RFID SCANNER STATE
 * ========================================================= */

typedef struct {
    rfid_reader_t *reader;

    system_direction_t direction;

    const char *label;

    system_uid_t previous_uid;

    bool card_latched;

    /*
     * Retain an event if the controller queue
     * temporarily cannot accept it.
     */
    system_event_t pending_event;

    bool event_pending;

} rfid_scan_state_t;

static rfid_scan_state_t entrance_scan;
static rfid_scan_state_t exit_scan;

/* =========================================================
 * PASSAGE BUTTON CONFIGURATION
 * ========================================================= */

typedef struct {
    gpio_num_t gpio;
    system_direction_t direction;
    const char *name;

} passage_button_config_t;

static passage_button_config_t entry_button_config = {
    .gpio = ENTRY_PASSAGE_GPIO,
    .direction = DIRECTION_ENTRY,
    .name = "ENTRY"
};

static passage_button_config_t exit_button_config = {
    .gpio = EXIT_PASSAGE_GPIO,
    .direction = DIRECTION_EXIT,
    .name = "EXIT"
};

/* =========================================================
 * COMPARE TWO CARD UIDs
 * ========================================================= */

static bool same_uid(
    const system_uid_t *first,
    const system_uid_t *second
)
{
    return first->length == second->length &&
           memcmp(
               first->bytes,
               second->bytes,
               first->length
           ) == 0;
}

/* =========================================================
 * SUBMIT A PENDING RFID EVENT
 * ========================================================= */

static bool submit_pending_scan(
    rfid_scan_state_t *state
)
{
    esp_err_t result = controller_post_event(
        &state->pending_event,
        pdMS_TO_TICKS(100)
    );

    if (result != ESP_OK) {

        ESP_LOGE(
            TAG,
            "%s scan could not be queued: %s. Retrying.",
            state->label,
            esp_err_to_name(result)
        );

        return false;
    }

    /*
     * Mark this card as processed only after
     * its event has been accepted by the controller.
     */
    state->previous_uid =
        state->pending_event.data.scan.uid;

    state->card_latched = true;
    state->event_pending = false;

    ESP_LOGI(
        TAG,
        "%s scan successfully queued. Event ID=%lu",
        state->label,
        (unsigned long)state->pending_event.id
    );

    return true;
}

/* =========================================================
 * PROCESS ONE RFID READER
 * ========================================================= */

static void process_rfid_reader(
    rfid_scan_state_t *state
)
{
    /*
     * Retry any pending event first.
     * Do not generate another event until it is accepted.
     */
    if (state->event_pending) {

        submit_pending_scan(state);

        return;
    }

    system_uid_t current_uid = {0};

    esp_err_t result = rfid_read_uid(
        state->reader,
        &current_uid
    );

    if (result == ESP_OK) {

        /*
         * Prevent duplicate events while the same card
         * remains on the reader.
         */
        if (!state->card_latched ||
            !same_uid(
                &state->previous_uid,
                &current_uid
            )) {

            printf("\n--------------------------------\n");
            printf("RFID %s: UID = ", state->label);

            for (int i = 0; i < current_uid.length; i++) {

                printf(
                    "%02X",
                    current_uid.bytes[i]
                );

                if (i < current_uid.length - 1) {
                    printf(":");
                }
            }

            printf("\n--------------------------------\n");

            /*
             * Create a new scan event.
             *
             * Entrance reader sends ENTRY.
             * Exit reader sends EXIT.
             */
            state->pending_event = (system_event_t) {
                .id = controller_next_event_id(),

                .type = EVENT_ACCESS_SCAN,

                .data.scan = {
                    .uid = current_uid,
                    .direction = state->direction
                }
            };

            state->event_pending = true;

            /* Submit immediately. */
            submit_pending_scan(state);
        }
    }

    else if (result == ESP_ERR_NOT_FOUND) {

        /*
         * No card is currently detected.
         * Allow the next scan.
         */
        state->card_latched = false;
    }

    else {

        ESP_LOGW(
            TAG,
            "%s RFID read error: %s",
            state->label,
            esp_err_to_name(result)
        );
    }
}

/* =========================================================
 * SHARED RFID SCANNER TASK
 *
 * One task alternates between both readers.
 * This prevents the two scanners from independently
 * operating on the shared SPI bus at the same time.
 * ========================================================= */

static void rfid_scanner_task(void *argument)
{
    (void)argument;

    ESP_LOGI(
        TAG,
        "Entrance and exit RFID scanners are ready."
    );

    while (1) {

        /* Check entrance reader. */
        process_rfid_reader(&entrance_scan);

        vTaskDelay(pdMS_TO_TICKS(50));

        /* Check exit reader. */
        process_rfid_reader(&exit_scan);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* =========================================================
 * INITIALIZE PASSAGE BUTTONS
 * ========================================================= */

static esp_err_t passage_buttons_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask =
            (1ULL << ENTRY_PASSAGE_GPIO) |
            (1ULL << EXIT_PASSAGE_GPIO),

        .mode = GPIO_MODE_INPUT,

        .pull_up_en = GPIO_PULLUP_ENABLE,

        .pull_down_en = GPIO_PULLDOWN_DISABLE,

        .intr_type = GPIO_INTR_DISABLE
    };

    return gpio_config(&config);
}

/* =========================================================
 * CONFIRM A PENDING PASSAGE
 * ========================================================= */

static void confirm_passage(
    const passage_button_config_t *button
)
{
    system_snapshot_t snapshot;

    esp_err_t result = controller_get_snapshot(
        &snapshot,
        pdMS_TO_TICKS(100)
    );

    if (result != ESP_OK) {

        ESP_LOGE(
            "PASSAGE",
            "Could not read controller snapshot: %s",
            esp_err_to_name(result)
        );

        return;
    }

    const pending_passage_t *pending =
        &snapshot.pending[button->direction];

    if (!pending->active) {

        ESP_LOGW(
            "PASSAGE",
            "%s button pressed, but no passage is pending.",
            button->name
        );

        return;
    }

    /*
     * Preserve the original scan transaction ID.
     */
    system_event_t event = {
        .id = controller_next_event_id(),

        .type = EVENT_PASSAGE_CONFIRMED,

        .data.passage = {
            .transaction_id = pending->transaction_id,
            .direction = button->direction
        }
    };

    result = controller_post_event(
        &event,
        pdMS_TO_TICKS(100)
    );

    if (result != ESP_OK) {

        ESP_LOGE(
            "PASSAGE",
            "Could not submit %s confirmation: %s",
            button->name,
            esp_err_to_name(result)
        );

        return;
    }

    ESP_LOGI(
        "PASSAGE",
        "%s passage confirmation submitted. Transaction ID=%lu",
        button->name,
        (unsigned long)pending->transaction_id
    );
}

/* =========================================================
 * PASSAGE BUTTON TASK
 * ========================================================= */

static void passage_button_task(void *argument)
{
    const passage_button_config_t *button =
        (const passage_button_config_t *)argument;

    ESP_LOGI(
        "PASSAGE",
        "%s passage button ready on GPIO%d",
        button->name,
        (int)button->gpio
    );

    while (1) {

        /* Buttons use internal pull-ups: pressed = LOW. */
        if (gpio_get_level(button->gpio) == 0) {

            /* Debounce. */
            vTaskDelay(pdMS_TO_TICKS(30));

            if (gpio_get_level(button->gpio) == 0) {

                confirm_passage(button);

                /* Wait until the button is released. */
                while (gpio_get_level(button->gpio) == 0) {

                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

#endif /* !APP_ROLE1_DEMO */

/* =========================================================
 * MAIN APPLICATION
 * ========================================================= */

void app_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    ESP_LOGI(
        "startup",
        "CoCIS | one ESP32 | native ESP-IDF | role1_demo=%d",
        APP_ROLE1_DEMO
    );

    /* Initialize door mechanisms. */
    ESP_ERROR_CHECK(door_init());

    ESP_LOGI(
        "startup",
        "Role 2 door outputs initialized."
    );

    /* Initialize management and central controller. */
    ESP_ERROR_CHECK(management_init());

    ESP_ERROR_CHECK(controller_start());

    ESP_ERROR_CHECK(config_start_watcher());

    ESP_ERROR_CHECK(serial_cli_start());

    ESP_LOGW(
        "startup",
        "Hardware policy, room sensors and SD storage are not fully integrated."
    );

#if APP_ROLE1_DEMO

    ESP_ERROR_CHECK(role1_demo_start());

#else

    /* -----------------------------------------------------
     * PASSAGE BUTTON INITIALIZATION
     * ----------------------------------------------------- */

    ESP_ERROR_CHECK(passage_buttons_init());

    if (xTaskCreate(
            passage_button_task,
            "entry_passage",
            3072,
            &entry_button_config,
            4,
            NULL
        ) != pdPASS) {

        ESP_ERROR_CHECK(ESP_FAIL);
    }

    if (xTaskCreate(
            passage_button_task,
            "exit_passage",
            3072,
            &exit_button_config,
            4,
            NULL
        ) != pdPASS) {

        ESP_ERROR_CHECK(ESP_FAIL);
    }

    /* -----------------------------------------------------
     * RFID SPI BUS INITIALIZATION
     * ----------------------------------------------------- */

    /*
     * Keep the exit reader deselected before initializing
     * the entrance reader.
     */
    const gpio_config_t inactive_exit_reader_cs = {
        .pin_bit_mask = 1ULL << GPIO_NUM_17,

        .mode = GPIO_MODE_OUTPUT,

        .pull_up_en = GPIO_PULLUP_DISABLE,

        .pull_down_en = GPIO_PULLDOWN_DISABLE,

        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(
        gpio_config(&inactive_exit_reader_cs)
    );

    ESP_ERROR_CHECK(
        gpio_set_level(GPIO_NUM_17, 1)
    );

    /*
     * Initialize the SPI bus once.
     * Both MFRC522 devices share this bus.
     */
    ESP_ERROR_CHECK(rfid_spi_bus_init());

    /* -----------------------------------------------------
     * ENTRANCE RFID READER
     * ----------------------------------------------------- */

    ESP_ERROR_CHECK(
        rfid_reader_init(
            &entrance_reader,
            GPIO_NUM_5,
            GPIO_NUM_22
        )
    );

    /* -----------------------------------------------------
     * EXIT RFID READER
     * ----------------------------------------------------- */

    ESP_ERROR_CHECK(
        rfid_reader_init(
            &exit_reader,
            GPIO_NUM_17,
            GPIO_NUM_16
        )
    );

    /* -----------------------------------------------------
     * CONFIGURE ENTRANCE SCANNER
     * ----------------------------------------------------- */

    entrance_scan = (rfid_scan_state_t) {
        .reader = &entrance_reader,

        .direction = DIRECTION_ENTRY,

        .label = "ENTRY"
    };

    /* -----------------------------------------------------
     * CONFIGURE EXIT SCANNER
     * ----------------------------------------------------- */

    exit_scan = (rfid_scan_state_t) {
        .reader = &exit_reader,

        .direction = DIRECTION_EXIT,

        .label = "EXIT"
    };

    /* -----------------------------------------------------
     * START SHARED RFID SCANNER TASK
     * ----------------------------------------------------- */

    if (xTaskCreate(
            rfid_scanner_task,
            "rfid_scanner",
            6144,
            NULL,
            5,
            NULL
        ) != pdPASS) {

        ESP_ERROR_CHECK(ESP_FAIL);
    }

    ESP_LOGI(
        "startup",
        "Entrance and exit RFID scanners started."
    );

#endif
}
