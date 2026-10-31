#include <stdio.h>
#include "esp_log.h"
#include "app_config.h"
#include "controller.h"
#include "role1_demo.h"
#include "config.h"
#include "serial_cli.h"

void app_main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ESP_LOGI("startup", "CoCIS | one ESP32 | native ESP-IDF | role1_demo=%d", APP_ROLE1_DEMO);
    ESP_ERROR_CHECK(management_init());
    ESP_ERROR_CHECK(controller_start());
    ESP_ERROR_CHECK(config_start_watcher());
    ESP_ERROR_CHECK(serial_cli_start());
    /* Add teammates' initialization here when their modules are integrated. */
    ESP_LOGW("startup", "Role 1 only: hardware, management and SD modules are not integrated; console logs are not persisted.");
#if APP_ROLE1_DEMO
    ESP_ERROR_CHECK(role1_demo_start());
#else
    ESP_LOGW("startup", "Hardware/policy providers are unfinished; access remains denied. Build role1_demo for synthetic checks.");
#endif
}
