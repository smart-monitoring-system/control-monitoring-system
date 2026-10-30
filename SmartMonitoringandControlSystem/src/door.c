#include <stdint.h>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "door.h"

#define ENTRY_DOOR_GPIO GPIO_NUM_25
#define EXIT_DOOR_GPIO  GPIO_NUM_26

#define DOOR_OPEN_TIME_MS 3000

static const char *TAG = "DOOR_CONTROL";

static int64_t entry_close_time = 0;
static int64_t exit_close_time = 0;

void door_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << ENTRY_DOOR_GPIO) |
                        (1ULL << EXIT_DOOR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&config));

    gpio_set_level(ENTRY_DOOR_GPIO, 0);
    gpio_set_level(EXIT_DOOR_GPIO, 0);

    ESP_LOGI(TAG, "Entry and exit door outputs initialised.");
}

void open_door(door_direction_t direction)
{
    int64_t close_time =
        esp_timer_get_time() + (DOOR_OPEN_TIME_MS * 1000LL);

    if (direction == DOOR_ENTRY) {
        gpio_set_level(ENTRY_DOOR_GPIO, 1);
        entry_close_time = close_time;

        ESP_LOGI(TAG, "ENTRY door unlocked.");
    }
    else if (direction == DOOR_EXIT) {
        gpio_set_level(EXIT_DOOR_GPIO, 1);
        exit_close_time = close_time;

        ESP_LOGI(TAG, "EXIT door unlocked.");
    }
}

void door_update(void)
{
    int64_t now = esp_timer_get_time();

    if (entry_close_time != 0 && now >= entry_close_time) {
        gpio_set_level(ENTRY_DOOR_GPIO, 0);
        entry_close_time = 0;

        ESP_LOGI(TAG, "ENTRY door locked.");
    }

    if (exit_close_time != 0 && now >= exit_close_time) {
        gpio_set_level(EXIT_DOOR_GPIO, 0);
        exit_close_time = 0;

        ESP_LOGI(TAG, "EXIT door locked.");
    }
}