# Everything MicroPython's own ESP32 boards include, plus EVBotPy's frozen
# Python (EVBotPy/firmware/frozen).
include("$(PORT_DIR)/boards/manifest.py")

module("main.py", base_path="$(BOARD_DIR)/../../frozen")
module("evbot_run.py", base_path="$(BOARD_DIR)/../../frozen")
module("evbot_ble.py", base_path="$(BOARD_DIR)/../../frozen")
