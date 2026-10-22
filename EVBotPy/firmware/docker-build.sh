#!/usr/bin/env bash
# Runs inside the espressif/idf container. Start it with build.ps1, not by hand.
#
#   /evbot  this repo (read-only)
#   /build  Docker volume "evbotpy-build": the MicroPython checkout and the
#           build folder. It lives on Linux's own disk, which builds much
#           faster than a Windows folder shared into the container.
#   /out    EVBotPy/firmware/build-out on Windows: the finished images
#
# Arguments: <MicroPython tag> <1 = also compile the evbot C module>
set -euo pipefail

MPY_VERSION="$1"
WITH_ENGINE="$2"
BOARD_DIR=/evbot/EVBotPy/firmware/boards/EVBOT_S3
# The port's default build folder. Don't pass BUILD= to make: the mpy-cross
# sub-make inherits it and leaks its own module list into the firmware
# build ("undefined reference to mp_module_string", micropython#19667).
BUILD=/build/micropython/ports/esp32/build-EVBOT_S3

# Left over from an earlier version of this script, which did pass BUILD=.
rm -rf /build/out

cd /build
if [ ! -d micropython/.git ]; then
  git clone --depth 1 --branch "$MPY_VERSION" https://github.com/micropython/micropython.git
fi
cd micropython
have=$(git describe --tags --exact-match 2>/dev/null || echo unknown)
if [ "$have" != "$MPY_VERSION" ]; then
  echo "The MicroPython checkout is $have, not $MPY_VERSION. Run: .\\build.ps1 -Clean"
  exit 1
fi

make -C mpy-cross -j"$(nproc)"
make -C ports/esp32 BOARD_DIR="$BOARD_DIR" submodules

# frozen_content.c lists the names (qstrs) the frozen Python needs that the
# C code doesn't already have. MicroPython doesn't regenerate it when C code
# adds a name, e.g. the evbot module's wait() vs asyncio's "wait"
# ("redeclaration of enumerator MP_QSTR_wait"). It takes seconds to remake,
# so delete it every time.
rm -f "$BUILD/frozen_content.c"

args=(BOARD_DIR="$BOARD_DIR")
if [ "$WITH_ENGINE" = 1 ]; then
  args+=(USER_C_MODULES=/evbot/EVBotPy/firmware/evbot_module/micropython.cmake)
fi
make -C ports/esp32 -j"$(nproc)" "${args[@]}"

# The separate parts, NOT the merged firmware.bin: makeimg.py fills
# 0x9000-0x10000 with 0xFF, which would wipe NVS (calibration, car name/PIN).
# ota_data_initial.bin resets otadata, in case a C++ Bluetooth upload left
# the car booting from the second app slot.
cp "$BUILD/bootloader/bootloader.bin" \
   "$BUILD/partition_table/partition-table.bin" \
   "$BUILD/ota_data_initial.bin" \
   "$BUILD/micropython.bin" \
   /out/
cat > /out/flash-offsets.txt <<'EOF'
0x0      bootloader.bin
0x8000   partition-table.bin
0xe000   ota_data_initial.bin
0x10000  micropython.bin
EOF
echo
echo "Done. Images are in EVBotPy/firmware/build-out ($MPY_VERSION, engine=$WITH_ENGINE)."
