# Runs a student's program, and always stops the car afterwards.
#
# Used three ways (PLAN.md, design point 4):
#   - the page types  import evbot_run; evbot_run.run("main.py")  into the raw REPL
#   - the frozen main.py calls it at power-on and after Ctrl-D
#   - by hand from the REPL or Thonny, the same call
#
# There is no soft reset, so Bluetooth stays connected and the page sees
# the program's output from its first line.
#
# Not run on the car yet: needs the evbot C module (phase 2) and
# evbot_ble (phase 3).

import gc
import sys


def _status(text):
    # Tell the page (if one is connected over Bluetooth) how the run went.
    try:
        import evbot_ble
        evbot_ble.status(text)
    except Exception:
        pass


def _stop_car():
    # The C side stops the wheels (ending drive()/spin) and leaves the robot
    # ready for the next run. Retry once if a second Ctrl-C lands mid-stop.
    for _ in range(2):
        try:
            import evbot
            evbot._program_end()
            return
        except KeyboardInterrupt:
            pass
        except ImportError:
            return


def run(path="main.py"):
    try:
        with open(path) as f:
            source = f.read()
    except OSError:
        print("No program on the robot yet. Write one and press Run.")
        return

    # A fresh namespace, like a newly started program. compile() with the
    # file name makes tracebacks say  File "main.py", line N  so the page
    # can link the line in the editor.
    scope = {"__name__": "__main__", "__file__": path}
    gc.collect()
    _status("RUNNING")
    result = "DONE"
    try:
        exec(compile(source, path, "exec"), scope)
    except KeyboardInterrupt:
        result = "STOPPED"
        print("Stopped.")
    except SystemExit:
        pass
    except Exception as e:
        result = "ERROR"
        sys.print_exception(e)
    finally:
        _stop_car()
        _status(result)
        scope = None
        gc.collect()
