
idf_component_register(
    SRCS
        "main.c"
        "system_types.c"
        "controller.c"
        "controller_core.c"
        "occupancy.c"
        "access_control.c"
        "integration_stubs.c"
        "role1_demo.c"
        "identity.c"
        "door.c"

    INCLUDE_DIRS
        "."
        "../include"

    REQUIRES
        esp_timer
        driver
)
