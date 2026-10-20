# EVBot car: ESP32-S3 N16R8 (16 MiB flash, 8 MiB octal PSRAM).
# Same settings as MicroPython's ESP32_GENERIC_S3 board with its SPIRAM_OCT
# variant, plus EVBotPy's own sdkconfig.board.
set(IDF_TARGET esp32s3)

set(SDKCONFIG_DEFAULTS
    boards/sdkconfig.base
    boards/sdkconfig.ble
    boards/sdkconfig.spiram_quad
    boards/sdkconfig.flash_qio_80m
    boards/sdkconfig.csi
    boards/sdkconfig.240mhz
    boards/sdkconfig.spiram_oct
    ${MICROPY_BOARD_DIR}/sdkconfig.board
)

set(MICROPY_FROZEN_MANIFEST ${MICROPY_BOARD_DIR}/manifest.py)
