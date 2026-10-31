
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
        "schedule.c"
        "auth.c"
        "config.c"
        "serial_cli.c"
        "rfid.c"
        "room_control.c"
        "room_control_core.c"

    INCLUDE_DIRS
        "."
        "../include"

    REQUIRES
        esp_timer
        driver
)
