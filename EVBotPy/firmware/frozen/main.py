# Frozen into the firmware. MicroPython finds a frozen main.py before the
# one on the car, so this runs at power-on and after Ctrl-D. It runs the
# student's own main.py (on the car's file system) through evbot_run, so the
# car stops when the program ends. See PLAN.md, design point 4.

try:
    import evbot_ble
    evbot_ble.start()  # only if the wireless switch is on; never blocks
except ImportError:
    pass

import evbot_run
evbot_run.run("main.py")
