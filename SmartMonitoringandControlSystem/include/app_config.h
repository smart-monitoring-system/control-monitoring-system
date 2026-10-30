#ifndef APP_CONFIG_H
#define APP_CONFIG_H
/* Normal firmware never trusts the synthetic registry/schedule or fake doors. */
#ifndef APP_ROLE1_DEMO
#define APP_ROLE1_DEMO 0
#endif
#define APP_CONTROLLER_STACK_SIZE 4096
#define APP_CONTROLLER_PRIORITY 5
#endif
