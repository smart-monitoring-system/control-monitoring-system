
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_err.h"

#include "door.h"

/* ---------------------------------------------------------
 * GPIO ASSIGNMENTS
 * --------------------------------------------------------- */

/* Servo PWM pins */
#define ENTRY_SERVO_GPIO GPIO_NUM_32
#define EXIT_SERVO_GPIO  GPIO_NUM_33

/* Door status LED pins */
#define ENTRY_DOOR_LED_GPIO GPIO_NUM_25
#define EXIT_DOOR_LED_GPIO  GPIO_NUM_26

/* ---------------------------------------------------------
 * SERVO CONFIGURATION
 * --------------------------------------------------------- */

/*
 * Standard hobby servo configuration:
 *
 * Frequency: 50 Hz
 * Period:    20,000 microseconds (20 ms)
 * Closed:    1,000 microseconds
 * Open:      2,000 microseconds
 *
 * These positions are starting values. Adjust them
 * according to the physical door mechanism.
 */

#define SERVO_FREQUENCY_HZ       50
#define SERVO_PERIOD_US          20000

#define SERVO_CLOSED_PULSE_US    1000
#define SERVO_OPEN_PULSE_US      2000

#define SERVO_DUTY_RESOLUTION    LEDC_TIMER_13_BIT

#define SERVO_TIMER              LEDC_TIMER_0
#define SERVO_SPEED_MODE         LEDC_LOW_SPEED_MODE

#define ENTRY_SERVO_CHANNEL      LEDC_CHANNEL_0
#define EXIT_SERVO_CHANNEL       LEDC_CHANNEL_1

static const char *TAG = "DOOR";

static bool door_initialized = false;

/* ---------------------------------------------------------
 * CONVERT SERVO PULSE WIDTH TO LEDC DUTY
 * --------------------------------------------------------- */

static uint32_t servo_pulse_to_duty(uint32_t pulse_us)
{
    const uint32_t max_duty =
        (1U << SERVO_DUTY_RESOLUTION);

    return (uint32_t)(
        ((uint64_t)pulse_us * max_duty) /
        SERVO_PERIOD_US
    );
}

/* ---------------------------------------------------------
 * SET SERVO POSITION
 * --------------------------------------------------------- */

static esp_err_t servo_set_position(
    ledc_channel_t channel,
    bool open
)
{
    uint32_t pulse_us = open
        ? SERVO_OPEN_PULSE_US
        : SERVO_CLOSED_PULSE_US;

    uint32_t duty = servo_pulse_to_duty(pulse_us);

    esp_err_t result = ledc_set_duty(
        SERVO_SPEED_MODE,
        channel,
        duty
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Could not set servo duty: %s",
            esp_err_to_name(result)
        );
        return result;
    }

    result = ledc_update_duty(
        SERVO_SPEED_MODE,
        channel
    );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Could not update servo duty: %s",
            esp_err_to_name(result)
        );
        return result;
    }

    return ESP_OK;
}

/* ---------------------------------------------------------
 * INITIALIZE SERVO PWM TIMER
 * --------------------------------------------------------- */

static esp_err_t servo_timer_init(void)
{
    ledc_timer_config_t timer_config = {
        .speed_mode = SERVO_SPEED_MODE,
        .duty_resolution = SERVO_DUTY_RESOLUTION,
        .timer_num = SERVO_TIMER,
        .freq_hz = SERVO_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };

    return ledc_timer_config(&timer_config);
}

/* ---------------------------------------------------------
 * INITIALIZE ONE SERVO CHANNEL
 * --------------------------------------------------------- */

static esp_err_t servo_channel_init(
    gpio_num_t gpio,
    ledc_channel_t channel
)
{
    ledc_channel_config_t channel_config = {
        .gpio_num = gpio,
        .speed_mode = SERVO_SPEED_MODE,
        .channel = channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SERVO_TIMER,
        .duty = 0,
        .hpoint = 0
    };

    return ledc_channel_config(&channel_config);
}

/* ---------------------------------------------------------
 * INITIALIZE DOOR SYSTEM
 * --------------------------------------------------------- */

esp_err_t door_init(void)
{
    if (door_initialized) {
        return ESP_OK;
    }

    /* Configure both status LEDs. */
    gpio_config_t gpio_config_data = {
        .pin_bit_mask =
            (1ULL << ENTRY_DOOR_LED_GPIO) |
            (1ULL << EXIT_DOOR_LED_GPIO),

        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t result = gpio_config(&gpio_config_data);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "LED GPIO initialization failed: %s",
                 esp_err_to_name(result));
        return result;
    }

    /* Start with both LEDs switched off. */
    gpio_set_level(ENTRY_DOOR_LED_GPIO, 0);
    gpio_set_level(EXIT_DOOR_LED_GPIO, 0);

    /* Initialize the common 50 Hz servo timer. */
    result = servo_timer_init();

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Servo timer initialization failed: %s",
                 esp_err_to_name(result));
        return result;
    }

    /* Initialize entrance servo on GPIO32. */
    result = servo_channel_init(
        ENTRY_SERVO_GPIO,
        ENTRY_SERVO_CHANNEL
    );

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Entrance servo initialization failed: %s",
                 esp_err_to_name(result));
        return result;
    }

    /* Initialize exit servo on GPIO33. */
    result = servo_channel_init(
        EXIT_SERVO_GPIO,
        EXIT_SERVO_CHANNEL
    );

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Exit servo initialization failed: %s",
                 esp_err_to_name(result));
        return result;
    }

    door_initialized = true;

    /*
     * Both doors start in the closed position.
     */
    result = servo_set_position(
        ENTRY_SERVO_CHANNEL,
        false
    );

    if (result != ESP_OK) {
        door_initialized = false;
        return result;
    }

    result = servo_set_position(
        EXIT_SERVO_CHANNEL,
        false
    );

    if (result != ESP_OK) {
        door_initialized = false;
        return result;
    }

    ESP_LOGI(
        TAG,
        "Door system initialized successfully."
    );

    ESP_LOGI(
        TAG,
        "Entrance servo: GPIO32, LED: GPIO25"
    );

    ESP_LOGI(
        TAG,
        "Exit servo: GPIO33, LED: GPIO26"
    );

    return ESP_OK;
}

/* ---------------------------------------------------------
 * OPEN OR CLOSE A DOOR
 * --------------------------------------------------------- */

esp_err_t door_set_output(
    system_direction_t direction,
    bool open
)
{
    if (!door_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ledc_channel_t selected_servo;
    gpio_num_t selected_led;

    if (direction == DIRECTION_ENTRY) {

        selected_servo = ENTRY_SERVO_CHANNEL;
        selected_led = ENTRY_DOOR_LED_GPIO;

    } else if (direction == DIRECTION_EXIT) {

        selected_servo = EXIT_SERVO_CHANNEL;
        selected_led = EXIT_DOOR_LED_GPIO;

    } else {
        return ESP_ERR_INVALID_ARG;
    }

    /* Move the appropriate servo. */
    esp_err_t result = servo_set_position(
        selected_servo,
        open
    );

    if (result != ESP_OK) {
        return result;
    }

    /* Update the corresponding door status LED. */
    result = gpio_set_level(
        selected_led,
        open ? 1 : 0
    );

    if (result != ESP_OK) {
        return result;
    }

    ESP_LOGI(
        TAG,
        "%s door %s",
        direction == DIRECTION_ENTRY ? "ENTRY" : "EXIT",
        open ? "OPENED" : "CLOSED"
    );

    return ESP_OK;
}
