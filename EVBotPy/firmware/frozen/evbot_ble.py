# The Bluetooth link to the EVBotPy upload page (EVBotPyUploader.html).
# Frozen into the firmware; the frozen main.py calls start().
#
#   - upload a program (checked with a CRC) and read programs back
#   - the program's output, and a full Python prompt (REPL), over Bluetooth
#   - STOP, and Ctrl-C from the page
#
# It runs only while the car's wireless switch (WL_SW) is on, and starts or
# stops when the switch is flipped. Each car has its own name and PIN, the
# same ones the C++ firmware uses (NVS namespace "evbot").
#
# Protocol (PLAN.md, "BLE protocol"). Everything except PIN and INFO needs
# the right PIN first, on each connection.
#   CMD    write         PIN:<pin>  INFO  FILE_BEGIN:<name>:<bytes>  FILE_END:<crc32 hex>
#                        STOP  GET:<name>  LS  RESTART
#   DATA   write         file bytes after FILE_BEGIN
#          notify        file bytes for GET
#   STATUS notify, read  one line per message ("\n" ends it; long ones arrive in pieces):
#                        AUTH:OK|BAD  INFO:...  FILE:READY  FILE:ACK:<n>  FILE:OK  FILE:ERR:<text>
#                        FILE:SEND:<bytes>:<crc32 hex>  FILE:SENT  LS:<name>=<bytes>,...
#                        STOP:OK  RUNNING  DONE  STOPPED  ERROR  ERR:<text>
#   LOG    notify        everything the car prints (print, say, errors, the prompt)
#   TERM   write         typed into the Python prompt (Ctrl-C works here too)

import binascii
import bluetooth
import io
import machine
import micropython
import os
from micropython import const

import evbot

VERSION = "0.3.0"

# 6e7e01xx: a different service from the C++ firmware's 6e7e00xx, so neither
# page connects to the wrong firmware.
_SERVICE_UUID = bluetooth.UUID("6e7e0101-5ad1-4c1e-9b1a-0e0b0e0b0001")
_CMD_UUID = bluetooth.UUID("6e7e0102-5ad1-4c1e-9b1a-0e0b0e0b0001")
_DATA_UUID = bluetooth.UUID("6e7e0103-5ad1-4c1e-9b1a-0e0b0e0b0001")
_STATUS_UUID = bluetooth.UUID("6e7e0104-5ad1-4c1e-9b1a-0e0b0e0b0001")
_LOG_UUID = bluetooth.UUID("6e7e0105-5ad1-4c1e-9b1a-0e0b0e0b0001")
_TERM_UUID = bluetooth.UUID("6e7e0106-5ad1-4c1e-9b1a-0e0b0e0b0001")

_IRQ_CENTRAL_CONNECT = const(1)
_IRQ_CENTRAL_DISCONNECT = const(2)
_IRQ_GATTS_WRITE = const(3)

_WL_SW_PIN = const(15)  # wireless switch, LOW = on (evbot_config.h)
_WL_LED_PIN = const(16)  # lit while Bluetooth is on
_NVS_NS = "evbot"
_DEFAULT_PIN = "0000"

_MAX_FILE = const(65536)
_ACK_EVERY = const(4096)  # the page waits for FILE:ACK after this many bytes
_TERM_BUFFER = const(1024)  # typed bytes kept while the program isn't reading

_service = None


def _program_running():
    try:
        import evbot_run
        return evbot_run.running
    except ImportError:
        return False


def _good_name(name):
    if not name.endswith(".py") or not 4 <= len(name) <= 32 or name[0] == ".":
        return False
    for c in name:
        if not (c.isalpha() or c.isdigit() or c in "_-."):
            return False
    return True


class _Terminal(io.IOBase):
    # The Bluetooth end of the Python prompt, attached with os.dupterm():
    # MicroPython writes everything it prints here, and reads typed input.

    def __init__(self, service):
        self._service = service
        self._chunks = []  # typed input, oldest first; the Bluetooth task appends

    def feed(self, data):
        if sum(len(c) for c in self._chunks) + len(data) <= _TERM_BUFFER:
            self._chunks.append(data)

    def readinto(self, buf):
        if not self._chunks:
            return None  # nothing yet (0 would mean "closed" and detach the terminal)
        chunk = self._chunks[0]
        n = min(len(buf), len(chunk))
        buf[:n] = chunk[:n]
        if n == len(chunk):
            self._chunks.pop(0)
        else:
            self._chunks[0] = chunk[n:]  # only this side touches item 0
        return n

    def write(self, buf):
        self._service.log(buf)
        return len(buf)

    def ioctl(self, op, arg):
        if op == 3:  # MP_STREAM_POLL
            return 1 if (arg & 1) and self._chunks else 0  # MP_STREAM_POLL_RD
        return 0


class _Service:
    def __init__(self):
        uid = machine.unique_id()
        self.name = evbot._nvs_str(_NVS_NS, "car_name") or "EVBot-%02X%02X" % (uid[4], uid[5])
        self._pin = evbot._nvs_str(_NVS_NS, "pin") or _DEFAULT_PIN

        self._ble = bluetooth.BLE()
        self._ble.active(True)
        self._ble.config(gap_name=self.name, mtu=517)
        self._ble.irq(self._irq)
        ((self._h_cmd, self._h_data, self._h_status, self._h_log, self._h_term),) = self._ble.gatts_register_services((
            (_SERVICE_UUID, (
                (_CMD_UUID, bluetooth.FLAG_WRITE),
                (_DATA_UUID, bluetooth.FLAG_WRITE | bluetooth.FLAG_WRITE_NO_RESPONSE | bluetooth.FLAG_NOTIFY),
                (_STATUS_UUID, bluetooth.FLAG_NOTIFY | bluetooth.FLAG_READ),
                (_LOG_UUID, bluetooth.FLAG_NOTIFY),
                (_TERM_UUID, bluetooth.FLAG_WRITE | bluetooth.FLAG_WRITE_NO_RESPONSE),
            )),
        ))
        # A write carries up to MTU - 3 = 514 bytes; bigger buffers keep it whole.
        for handle, size in ((self._h_cmd, 128), (self._h_data, 520), (self._h_term, 520)):
            self._ble.gatts_set_buffer(handle, size)

        self._conn = None
        self._authed = False
        self._upload = None  # [name, expected bytes, bytearray, next ACK]
        self._term = _Terminal(self)
        self._term_attached = False
        self._led = machine.Pin(_WL_LED_PIN, machine.Pin.OUT, value=1)
        self._advertise()

    def close(self):
        self._detach_term()
        self._ble.active(False)
        self._led.value(0)

    def _advertise(self):
        # Flags, then the service UUID; the name goes in the scan response,
        # since both don't fit in one 31-byte advert.
        adv = b"\x02\x01\x06" + bytes((17, 0x07)) + bytes(_SERVICE_UUID)
        name = self.name.encode()
        self._ble.gap_advertise(100_000, adv_data=adv, resp_data=bytes((len(name) + 1, 0x09)) + name)

    # ---------- sending ----------

    def _notify(self, handle, data, retries):
        # Notifications fail while the stack's buffers are full. From the main
        # task, wait 2 ms for the Bluetooth task to send some, then try again
        # (up to retries times). From the Bluetooth task itself (retries=0)
        # waiting would never help.
        for _ in range(retries + 1):
            try:
                self._ble.gatts_notify(self._conn, handle, data)
                return True
            except (OSError, TypeError):
                if self._conn is None:
                    return False
                if retries:
                    evbot._idle_ms(2)
        return False

    def _send(self, handle, data, retries):
        conn = self._conn
        if conn is None:
            return
        step = evbot._ble_mtu(conn) - 3
        mv = memoryview(data)
        for i in range(0, len(mv), step):
            if not self._notify(handle, mv[i:i + step], retries):
                return

    def status(self, text, retries=0):
        if self._conn is not None:
            self._send(self._h_status, (text + "\n").encode(), retries)

    def log(self, data):
        # Called for everything printed, from the main task. Never raises:
        # an exception here would make MicroPython detach the terminal.
        if self._conn is not None and self._authed:
            try:
                self._send(self._h_log, data, 50)
            except Exception:
                pass

    # ---------- terminal ----------

    def _attach_term(self):
        if not self._term_attached:
            os.dupterm(self._term)
            self._term_attached = True

    def _detach_term(self):
        if self._term_attached:
            os.dupterm(None)
            self._term_attached = False

    def _term_input(self, data):
        if not self._authed:
            return
        if 3 in data and evbot._emergency_stop():
            # A running program doesn't read input, so Ctrl-C must act here. It
            # has interrupted the program; don't also type it into the prompt.
            data = bytes(b for b in data if b != 3)
        if data:
            self._term.feed(bytes(data))

    # ---------- events (these run on the Bluetooth task) ----------

    def _irq(self, event, data):
        if event == _IRQ_CENTRAL_CONNECT:
            conn = data[0]
            if self._conn is not None:
                self._ble.gap_disconnect(conn)  # one page at a time
                return
            self._conn, self._authed, self._upload = conn, False, None
        elif event == _IRQ_CENTRAL_DISCONNECT:
            if data[0] != self._conn:
                return
            was_authed = self._authed
            self._conn, self._authed, self._upload = None, False, None
            self._detach_term()
            if was_authed and not _program_running():
                evbot._emergency_stop()  # e.g. a Live-tab drive() left running
            self._advertise()
        elif event == _IRQ_GATTS_WRITE:
            conn, handle = data
            if conn != self._conn:
                return
            value = self._ble.gatts_read(handle)
            if handle == self._h_cmd:
                self._command(bytes(value).decode().strip())
            elif handle == self._h_data:
                self._data(value)
            elif handle == self._h_term:
                self._term_input(value)

    def _command(self, cmd):
        if cmd.startswith("PIN:"):
            self._authed = cmd[4:] == self._pin
            self.status("AUTH:OK" if self._authed else "AUTH:BAD")
            if self._authed:
                self._attach_term()
            return
        if cmd == "INFO":
            self.status("INFO:name=%s;ver=%s;fw=py;mtu=%d;default_pin=%d"
                        % (self.name, VERSION, evbot._ble_mtu(self._conn), self._pin == _DEFAULT_PIN))
            return
        if not self._authed:
            self.status("ERR:Enter the car's PIN first")
            return
        if cmd.startswith("FILE_BEGIN:"):
            self._file_begin(cmd[11:])
        elif cmd.startswith("FILE_END:"):
            self._file_end(cmd[9:])
        elif cmd == "STOP":
            evbot._emergency_stop()
            self.status("STOP:OK")
        elif cmd.startswith("GET:"):
            micropython.schedule(self._get, cmd[4:])  # long reply: send it from the main task
        elif cmd == "LS":
            files = ["%s=%d" % (f[0], f[3]) for f in os.ilistdir("/") if f[0].endswith(".py")]
            self.status("LS:" + ",".join(sorted(files)))
        elif cmd == "RESTART":
            self.status("RESTARTING")
            machine.reset()
        else:
            self.status("ERR:Unknown command " + cmd[:20])

    # ---------- files ----------

    def _busy(self):
        # Writing flash pauses the step interrupts, so not while wheels turn.
        if _program_running() or evbot._wheels_turning():
            self.status("FILE:ERR:Stop the program first")
            return True
        return False

    def _file_begin(self, arg):
        name, _, size = arg.rpartition(":")
        try:
            size = int(size)
        except ValueError:
            size = -1
        if not _good_name(name):
            self.status("FILE:ERR:Bad file name")
        elif not 0 <= size <= _MAX_FILE:
            self.status("FILE:ERR:Program too big")
        elif not self._busy():
            self._upload = [name, size, bytearray(), _ACK_EVERY]
            self.status("FILE:READY")

    def _data(self, value):
        up = self._upload
        if up is None:
            return
        up[2] += value
        if len(up[2]) > up[1]:
            self._upload = None
            self.status("FILE:ERR:More data than announced")
            return
        while len(up[2]) >= up[3]:
            self.status("FILE:ACK:%d" % up[3])
            up[3] += _ACK_EVERY

    def _file_end(self, crc_hex):
        up, self._upload = self._upload, None
        if up is None:
            self.status("FILE:ERR:No upload in progress")
            return
        name, size, data = up[0], up[1], up[2]
        try:
            crc = int(crc_hex, 16)
        except ValueError:
            crc = -1
        if len(data) != size:
            self.status("FILE:ERR:Got %d of %d bytes" % (len(data), size))
        elif binascii.crc32(data) != crc:
            self.status("FILE:ERR:Data damaged on the way (CRC)")
        elif not self._busy():
            try:
                with open(name + ".tmp", "wb") as f:
                    f.write(data)
                os.rename(name + ".tmp", name)
                self.status("FILE:OK")
            except OSError as e:
                self.status("FILE:ERR:Could not save (%s)" % e)

    def _get(self, name):
        # Scheduled: runs on the main task, where sending may wait and retry.
        try:
            if not _good_name(name):
                raise OSError
            with open(name, "rb") as f:
                data = f.read()
        except OSError:
            self.status("FILE:ERR:No such program", 20)
            return
        self.status("FILE:SEND:%d:%08x" % (len(data), binascii.crc32(data)), 20)
        self._send(self._h_data, data, 50)
        self.status("FILE:SENT", 20)


# ---------- module interface ----------

def status(text):
    # Used by evbot_run: RUNNING, DONE, STOPPED, ERROR.
    if _service is not None:
        _service.status(text, 20)


def _set(on):
    global _service
    if on and _service is None:
        _service = _Service()
        print('[EVBot] Bluetooth on - this car is "%s"' % _service.name)
        if _service._pin == _DEFAULT_PIN:
            print("[EVBot] This car still has the default PIN 0000 - run teacher_car_setup.py to set one")
    elif not on and _service is not None:
        _service.close()
        _service = None
        print("[EVBot] Bluetooth off (wireless switch)")


def _switch_changed(pin):
    _set(pin.value() == 0)


def start():
    # Never raises and never blocks: the student's program runs next.
    try:
        sw = machine.Pin(_WL_SW_PIN, machine.Pin.IN, machine.Pin.PULL_UP)
        sw.irq(_switch_changed, machine.Pin.IRQ_FALLING | machine.Pin.IRQ_RISING)
        if sw.value() == 0:
            _set(True)
        else:
            print("[EVBot] Wireless switch is off - no Bluetooth (flip WL_SW on to use it)")
    except Exception as e:
        print("[EVBot] Bluetooth could not start:", e)


def set_identity(name, pin):
    # For teacher_car_setup.py: the same rules as the C++ Teacher_CarSetup.
    # Takes effect at the next restart.
    if not 1 <= len(name) <= 20 or not all(c.isalpha() or c.isdigit() or c in "-_" for c in name):
        raise ValueError("name: 1-20 letters, digits, - or _")
    if not 4 <= len(pin) <= 8 or not pin.isdigit():
        raise ValueError("PIN: 4-8 digits")
    evbot._nvs_set_str(_NVS_NS, "car_name", name)
    evbot._nvs_set_str(_NVS_NS, "pin", pin)
