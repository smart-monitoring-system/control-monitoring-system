#include <stdio.h>
#include "esp_log.h"
#include "app_config.h"
#include "controller.h"
#include "role1_demo.h"
#include "door.h"

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

    ESP_ERROR_CHECK(controller_start());

#if APP_ROLE1_DEMO
    ESP_ERROR_CHECK(role1_demo_start());
#else
    ESP_LOGW(
        "startup",
        "RFID scanner and schedule provider are not yet integrated."
    );
#endif
}