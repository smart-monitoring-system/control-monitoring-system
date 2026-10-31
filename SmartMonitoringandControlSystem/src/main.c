
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "app_config.h"
#include "controller.h"
#include "role1_demo.h"
#include "door.h"
#include "config.h"
#include "serial_cli.h"
#include "rfid.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if !APP_ROLE1_DEMO

static const char *TAG = "RFID_TEST";
static rfid_reader_t entrance_reader;

static bool same_uid(
    const system_uid_t *first,
    const system_uid_t *second)
{
    return first->length == second->length &&
           memcmp(first->bytes, second->bytes, first->length) == 0;
}

static void entrance_rfid_task(void *argument)
{
    rfid_reader_t *reader = (rfid_reader_t *)argument;

    system_uid_t previous_uid = {0};
    bool card_latched = false;

    ESP_LOGI(TAG, "Present an RFID card at the entrance reader.");

    while (1) {
        system_uid_t current_uid = {0};

        esp_err_t result = rfid_read_uid(reader, &current_uid);

        if (result == ESP_OK) {
            if (!card_latched ||
                !same_uid(&previous_uid, &current_uid)) {

                printf("\n--------------------------------\n");
                printf("RFID ENTRY: UID = ");

                for (int i = 0; i < current_uid.length; i++) {
                    printf("%02X", current_uid.bytes[i]);

                    if (i < current_uid.length - 1) {
                        printf(":");
                    }
                }

                printf("\n--------------------------------\n");

                previous_uid = current_uid;
                card_latched = true;

                system_event_t event = {
                    .id = controller_next_event_id(),
                    .type = EVENT_ACCESS_SCAN,
                    .data.scan = {
                        .uid = current_uid,
                        .direction = DIRECTION_ENTRY
                    }
                };
                result = controller_post_event(&event, pdMS_TO_TICKS(100));
                if (result != ESP_OK) {
                    ESP_LOGE(TAG, "Could not queue entry scan: %s",
                             esp_err_to_name(result));
                }
            }
        }
        else if (result == ESP_ERR_NOT_FOUND) {
            /* No card detected: allow the next scan. */
            card_latched = false;
        }
        else {
            ESP_LOGW(
                TAG,
                "RFID read error: %s",
                esp_err_to_name(result)
            );
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

#endif

void app_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    ESP_LOGI(
        "startup",
        "CoCIS | one ESP32 | native ESP-IDF | role1_demo=%d",
        APP_ROLE1_DEMO
    );

    ESP_ERROR_CHECK(door_init());

    ESP_LOGI("startup", "Role 2 door outputs initialized.");

    ESP_ERROR_CHECK(management_init());
    ESP_ERROR_CHECK(controller_start());

    ESP_ERROR_CHECK(config_start_watcher());
    ESP_ERROR_CHECK(serial_cli_start());
    /* Add teammates' initialization here when their modules are integrated. */
    ESP_LOGW("startup", "Role 1 only: hardware, management and SD modules are not integrated; console logs are not persisted.");
#if APP_ROLE1_DEMO

    ESP_ERROR_CHECK(role1_demo_start());

#else

    /* Both readers share MISO; keep the unused exit reader deselected. */
    const gpio_config_t inactive_exit_reader_cs = {
        .pin_bit_mask = 1ULL << GPIO_NUM_17,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&inactive_exit_reader_cs));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_17, 1));

    ESP_ERROR_CHECK(rfid_spi_bus_init());

    ESP_ERROR_CHECK(
        rfid_reader_init(
            &entrance_reader,
            GPIO_NUM_5,
            GPIO_NUM_22
        )
    );

    if (xTaskCreate(
            entrance_rfid_task,
            "entrance_rfid",
            4096,
            &entrance_reader,
            5,
            NULL
        ) != pdPASS) {
        ESP_ERROR_CHECK(ESP_FAIL);
    }

    ESP_LOGI("startup", "Entrance RFID scanner started.");

#endif
}
