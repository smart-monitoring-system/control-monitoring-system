#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include "serial_cli.h"
#include "auth.h"
#include "config.h"
#include "schedule.h"
#include "controller.h"
#include "system_types.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CLI_UART UART_NUM_0
#define LINE_MAX_LEN 80
#define MAX_WORDS 5
#define CLI_TASK_STACK 5120
#define CLI_TASK_PRIORITY 3

static bool started = false;

/* Remembers if the last key was Enter sent as "\r", so a following "\n" is ignored */
static bool last_was_cr = false;

/* ---------- Reading what the user types ---------- */

/* Wait up to 100 ms for one typed character. Returns -1 if nothing arrived. */
static int read_char(void)
{
    uint8_t c = 0;
    int got = uart_read_bytes(CLI_UART, &c, 1, pdMS_TO_TICKS(100));
    if (got == 1) {
        return c;
    }
    return -1;
}

/* Read one line into the buffer. If hide is true, show * instead of the letters. */
static void read_line(char *buffer, size_t size, bool hide)
{
    size_t length = 0;
    fflush(stdout);

    for (;;) {
        int c = read_char();
        if (c < 0) {
            continue;   /* nothing typed yet */
        }

        /* Enter may arrive as "\r", "\n" or "\r\n": count it as one Enter */
        if (c == '\n' && last_was_cr) {
            last_was_cr = false;
            continue;
        }
        last_was_cr = (c == '\r');

        if (c == '\r' || c == '\n') {
            buffer[length] = '\0';
            printf("\n");
            return;
        }

        /* Backspace */
        if (c == 8 || c == 127) {
            if (length > 0) {
                length--;
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }

        /* A normal printable character */
        if (c >= 32 && c < 127 && length < size - 1) {
            buffer[length] = (char)c;
            length++;
            putchar(hide ? '*' : c);
            fflush(stdout);
        }
    }
}

/* Cut the line into words. Spaces become end markers, and words[] points at each word. */
static int split_words(char *line, char *words[])
{
    int count = 0;
    char *p = line;

    while (*p != '\0') {
        if (*p == ' ') {
            *p = '\0';
            p++;
            continue;
        }
        if (count >= MAX_WORDS) {
            break;
        }
        words[count] = p;
        count++;
        while (*p != '\0' && *p != ' ') {
            p++;
        }
    }
    return count;
}

/* Turn text like "22" into the number 22. Returns false if it is not a number. */
static bool parse_number(const char *text, int *value)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }

    int result = 0;
    int digits = 0;
    for (int i = 0; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        result = (result * 10) + (text[i] - '0');
        digits++;
        if (digits > 3) {
            return false;
        }
    }
    *value = result;
    return true;
}

/* Turn text like "08:30" into hour 8 and minute 30. */
static bool parse_clock_text(char *text, int *hour, int *minute)
{
    char *colon = strchr(text, ':');
    if (colon == NULL) {
        return false;
    }
    *colon = '\0';   /* split the text into two pieces at the colon */
    return parse_number(text, hour) && parse_number(colon + 1, minute);
}

/* ---------- Small helpers for printing ---------- */

static const char *room_text(const room_status_t *room)
{
    if (!room->known) {
        return "unknown";
    }
    if (room->occupancy == ROOM_OCCUPIED) {
        return "occupied";
    }
    if (room->occupancy == ROOM_EMPTY) {
        return "empty";
    }
    return "unknown";
}

static void print_change_error(esp_err_t err)
{
    if (err == ESP_ERR_INVALID_STATE) {
        printf("Refused: please log in first.\n");
    } else if (err == ESP_ERR_INVALID_ARG) {
        printf("Refused: those values are not acceptable. Nothing was changed.\n");
    } else {
        printf("Failed: %s\n", esp_err_to_name(err));
    }
}

/* Ask for a new password twice. Returns true only if both match. */
static bool ask_new_password(char *password, size_t size)
{
    char again[LINE_MAX_LEN];

    printf("Choose a password (8 to 31 characters): ");
    read_line(password, size, true);
    printf("Type it again: ");
    read_line(again, sizeof(again), true);

    if (strcmp(password, again) != 0) {
        printf("The two passwords did not match. Nothing was changed.\n");
        return false;
    }
    return true;
}

/* ---------- The commands ---------- */

static void cmd_help(void)
{
    printf("Commands:\n");
    printf("  help                       show this list\n");
    printf("  setup <name>               create the first admin (only once)\n");
    printf("  login <name>               log in (asks for the password)\n");
    printf("  logout                     log out\n");
    printf("  adduser <name>             add the second admin (login needed)\n");
    printf("  status                     overall system information\n");
    printf("  inside                     people inside the building\n");
    printf("  rooms                      state of the lecture rooms\n");
    printf("  logs                       access records\n");
    printf("  schedule show              show the opening hours\n");
    printf("  schedule set <open> <close>  change hours, e.g. schedule set 7 22 (login needed)\n");
    printf("  time                       show the clock\n");
    printf("  time set <HH:MM>           set the clock, e.g. time set 08:30 (login needed)\n");
}

static void cmd_setup(int argc, char *argv[])
{
    if (argc != 2) {
        printf("Usage: setup <name>\n");
        return;
    }
    if (auth_any_admin_exists()) {
        printf("Setup is already done. Use: login <name>\n");
        return;
    }

    char password[LINE_MAX_LEN];
    if (!ask_new_password(password, sizeof(password))) {
        return;
    }

    esp_err_t err = auth_create_admin(argv[1], password);
    if (err == ESP_OK) {
        printf("Admin '%s' created. You can now log in.\n", argv[1]);
    } else if (err == ESP_ERR_INVALID_ARG) {
        printf("Refused: name must be 1 to 15 characters, password 8 to 31.\n");
    } else {
        printf("Failed: %s\n", esp_err_to_name(err));
    }
}

static void cmd_login(int argc, char *argv[])
{
    if (argc != 2) {
        printf("Usage: login <name>\n");
        return;
    }
    if (!auth_any_admin_exists()) {
        printf("No admin exists yet. Use: setup <name>\n");
        return;
    }

    char password[LINE_MAX_LEN];
    printf("Password: ");
    read_line(password, sizeof(password), true);

    esp_err_t err = auth_login(argv[1], password);
    if (err == ESP_OK) {
        printf("Logged in as %s.\n", argv[1]);
    } else if (err == ESP_ERR_INVALID_STATE) {
        printf("Too many wrong attempts. Wait 30 seconds and try again.\n");
    } else {
        printf("Wrong name or password.\n");
    }
}

static void cmd_logout(void)
{
    auth_logout();
    printf("Logged out.\n");
}

static void cmd_adduser(int argc, char *argv[])
{
    if (argc != 2) {
        printf("Usage: adduser <name>\n");
        return;
    }
    if (!auth_is_logged_in()) {
        printf("Refused: please log in first.\n");
        return;
    }

    char password[LINE_MAX_LEN];
    if (!ask_new_password(password, sizeof(password))) {
        return;
    }

    esp_err_t err = auth_create_admin(argv[1], password);
    if (err == ESP_OK) {
        printf("Admin '%s' created.\n", argv[1]);
    } else if (err == ESP_ERR_NO_MEM) {
        printf("Both admin accounts are already in use.\n");
    } else if (err == ESP_ERR_INVALID_ARG) {
        printf("Refused: name must be new and 1 to 15 characters, password 8 to 31.\n");
    } else {
        printf("Failed: %s\n", esp_err_to_name(err));
    }
}

static void cmd_status(void)
{
    printf("--- System status ---\n");

    const char *user = auth_current_user();
    if (user != NULL) {
        printf("Logged in as: %s\n", user);
    } else {
        printf("Logged in as: nobody\n");
    }

    int hour = 0;
    int minute = 0;
    bool time_known = (schedule_get_time(&hour, &minute) == ESP_OK);
    if (time_known) {
        printf("Time now: %02d:%02d (Kampala time)\n", hour, minute);
    } else {
        printf("Time now: not set (use: time set HH:MM)\n");
    }

    int open_hour = 0;
    int close_hour = 0;
    if (config_get_hours(&open_hour, &close_hour) == ESP_OK) {
        printf("Opening hours: %02d:00 to %02d:00\n", open_hour, close_hour);
    }

    if (time_known) {
        if (schedule_is_open(hour)) {
            printf("The building is OPEN now.\n");
        } else {
            printf("The building is CLOSED now.\n");
        }
    }

    system_snapshot_t snapshot;
    if (controller_get_snapshot(&snapshot, pdMS_TO_TICKS(100)) == ESP_OK) {
        printf("People inside: %u\n", (unsigned)snapshot.building.count);
    } else {
        printf("People inside: could not be read\n");
    }
}

static void cmd_inside(void)
{
    system_snapshot_t snapshot;
    if (controller_get_snapshot(&snapshot, pdMS_TO_TICKS(100)) != ESP_OK) {
        printf("Could not read the building state.\n");
        return;
    }

    printf("People inside: %u\n", (unsigned)snapshot.building.count);
    for (size_t i = 0; i < snapshot.building.count; i++) {
        printf("  user ID %u\n", (unsigned)snapshot.building.users[i]);
    }
}

static void cmd_rooms(void)
{
    system_snapshot_t snapshot;
    if (controller_get_snapshot(&snapshot, pdMS_TO_TICKS(100)) != ESP_OK) {
        printf("Could not read the room state.\n");
        return;
    }

    for (int i = 0; i < SYSTEM_ROOM_COUNT; i++) {
        printf("Room %d: %s, light %s\n", i + 1,
               room_text(&snapshot.rooms[i]),
               snapshot.rooms[i].light_on ? "ON" : "OFF");
    }
}

static void cmd_logs(void)
{
    printf("Access records are not available yet (the storage module is not connected).\n");
}

static void cmd_schedule(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "show") == 0) {
        int open_hour = 0;
        int close_hour = 0;
        if (config_get_hours(&open_hour, &close_hour) == ESP_OK) {
            printf("Opening hours: %02d:00 to %02d:00 (open at the start, closed at the end)\n",
                   open_hour, close_hour);
        } else {
            printf("The schedule could not be read.\n");
        }
        return;
    }

    if (argc == 4 && strcmp(argv[1], "set") == 0) {
        int open_hour = 0;
        int close_hour = 0;
        if (!parse_number(argv[2], &open_hour) || !parse_number(argv[3], &close_hour)) {
            printf("Usage: schedule set <open hour 0-23> <close hour 1-24>\n");
            return;
        }

        esp_err_t err = config_set_hours(open_hour, close_hour);
        if (err == ESP_OK) {
            printf("Opening hours are now %02d:00 to %02d:00. Any pending approvals have been cancelled.\n",
                   open_hour, close_hour);
        } else {
            print_change_error(err);
        }
        return;
    }

    printf("Usage: schedule show   or   schedule set <open> <close>\n");
}

static void cmd_time(int argc, char *argv[])
{
    if (argc == 1) {
        int hour = 0;
        int minute = 0;
        if (schedule_get_time(&hour, &minute) == ESP_OK) {
            printf("Time now: %02d:%02d (Kampala time)\n", hour, minute);
        } else {
            printf("The clock is not set. Use: time set HH:MM\n");
        }
        return;
    }

    if (argc == 3 && strcmp(argv[1], "set") == 0) {
        int hour = 0;
        int minute = 0;
        if (!parse_clock_text(argv[2], &hour, &minute)) {
            printf("Usage: time set HH:MM, for example time set 08:30\n");
            return;
        }

        esp_err_t err = config_set_time(hour, minute);
        if (err == ESP_OK) {
                    printf("Clock set to %02d:%02d (the opening hours were not changed). Any pending approvals have been cancelled.\n", hour, minute);
        } else if (err == ESP_ERR_INVALID_STATE || err == ESP_ERR_INVALID_ARG) {
            print_change_error(err);
        } else {
            printf("Problem: %s. The clock may still have been set; check with: time\n",
                   esp_err_to_name(err));
        }
        return;
    }

    printf("Usage: time   or   time set HH:MM\n");
}

/* Pick the right command for the first word */
static void run_command(int argc, char *argv[])
{
    const char *name = argv[0];

    if (strcmp(name, "help") == 0) {
        cmd_help();
    } else if (strcmp(name, "setup") == 0) {
        cmd_setup(argc, argv);
    } else if (strcmp(name, "login") == 0) {
        cmd_login(argc, argv);
    } else if (strcmp(name, "logout") == 0) {
        cmd_logout();
    } else if (strcmp(name, "adduser") == 0) {
        cmd_adduser(argc, argv);
    } else if (strcmp(name, "status") == 0) {
        cmd_status();
    } else if (strcmp(name, "inside") == 0) {
        cmd_inside();
    } else if (strcmp(name, "rooms") == 0) {
        cmd_rooms();
    } else if (strcmp(name, "logs") == 0) {
        cmd_logs();
    } else if (strcmp(name, "schedule") == 0) {
        cmd_schedule(argc, argv);
    } else if (strcmp(name, "time") == 0) {
        cmd_time(argc, argv);
    } else {
        printf("Unknown command '%s'. Type 'help' for the list.\n", name);
    }
}

/* ---------- The menu task ---------- */

static void cli_task(void *arg)
{
    (void)arg;
    char line[LINE_MAX_LEN];
    char *words[MAX_WORDS];

    printf("\nCoCIS management console. Type 'help' for the commands.\n");
    if (!auth_any_admin_exists()) {
        printf("No admin exists yet. Create one with: setup <name>\n");
    }

    for (;;) {
        printf("> ");
        read_line(line, sizeof(line), false);

        /* Restart the 5 minute countdown (this also logs out an expired session first) */
        if (auth_is_logged_in()) {
            auth_note_activity();
        }

        int count = split_words(line, words);
        if (count == 0) {
            continue;   /* an empty line */
        }
        run_command(count, words);
    }
}

esp_err_t serial_cli_start(void)
{
    if (started) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = uart_driver_install(CLI_UART, 256, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (xTaskCreate(cli_task, "cli", CLI_TASK_STACK, NULL, CLI_TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    started = true;
    return ESP_OK;
}