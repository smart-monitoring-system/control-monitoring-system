# Explicit sources keep headers and future scratch files out of the build.
idf_component_register(
    SRCS
        "main.c" "system_types.c" "controller.c" "controller_core.c"
        "occupancy.c" "access_control.c" "integration_stubs.c" "role1_demo.c" "schedule.c" "auth.c"
    INCLUDE_DIRS "." "../include"
    REQUIRES esp_timer
)
