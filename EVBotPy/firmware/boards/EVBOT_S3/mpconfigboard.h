#define MICROPY_HW_BOARD_NAME               "EVBot car (ESP32-S3 N16R8)"
#define MICROPY_HW_MCU_NAME                 "ESP32-S3"

// REPL on both the native USB port and UART0, so it works whichever USB
// socket the car's cable uses.
#define MICROPY_HW_ENABLE_UART_REPL         (1)
