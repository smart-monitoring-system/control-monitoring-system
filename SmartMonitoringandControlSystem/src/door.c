#include "driver/gpio.h"
#include "door.h"

/*
 * PROVISIONAL GPIO ASSIGNMENTS.
 * Confirm the final GPIO allocation with the SD-card and
 * other hardware roles before physical wiring.
 */
#define ENTRY_DOOR_GPIO GPIO_NUM_25
#define EXIT_DOOR_GPIO  GPIO_NUM_26

static bool door_initialized = false;

esp_err_t door_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask =
            (1ULL << ENTRY_DOOR_GPIO) |
            (1ULL << EXIT_DOOR_GPIO),

        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t result = gpio_config(&config);

    if (result != ESP_OK) {
        return result;
    }

    /* Both simulated doors start closed. */
    gpio_set_level(ENTRY_DOOR_GPIO, 0);
    gpio_set_level(EXIT_DOOR_GPIO, 0);

    door_initialized = true;

    return ESP_OK;
}

esp_err_t door_set_output(system_direction_t direction, bool open)
{
    if (!door_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    gpio_num_t selected_gpio;

    if (direction == DIRECTION_ENTRY) {
        selected_gpio = ENTRY_DOOR_GPIO;
    }
    else if (direction == DIRECTION_EXIT) {
        selected_gpio = EXIT_DOOR_GPIO;
    }
    else {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * HIGH = door unlocked/open (LED ON)
     * LOW  = door locked/closed (LED OFF)
     */
    return gpio_set_level(selected_gpio, open ? 1 : 0);
}