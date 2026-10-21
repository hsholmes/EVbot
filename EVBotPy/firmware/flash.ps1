# Flashes the EVBotPy (MicroPython) firmware onto a car over USB.
# Works whether the car currently runs the C++ or the Python firmware.
#
#   .\flash.ps1 -Port COM12                 flash the images in build-out\
#   .\flash.ps1 -Port COM12 -From dist\v1   flash a released version
#
# Writes the four parts at their own addresses, never the merged
# firmware.bin, so NVS (calibration, car name/PIN) is kept. otadata is reset,
# in case a C++ Bluetooth upload left the car booting from the second slot.
# Plug the car straight into the laptop: through a USB hub, transfers failed.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [string]$From = (Join-Path $PSScriptRoot "build-out"),
    [int]$Baud = 115200
)

$esptool = Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py" -Recurse -Filter esptool.exe -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $esptool) {
    Write-Host "esptool not found. Install the esp32 boards in the Arduino IDE (it includes esptool)."
    exit 1
}

$parts = [ordered]@{
    "0x0"     = "bootloader.bin"
    "0x8000"  = "partition-table.bin"
    "0xe000"  = "ota_data_initial.bin"
    "0x10000" = "micropython.bin"
}
$flashArgs = @()
foreach ($offset in $parts.Keys) {
    $file = Join-Path $From $parts[$offset]
    if (-not (Test-Path $file)) {
        Write-Host "Missing $file - run .\build.ps1 first."
        exit 1
    }
    $flashArgs += $offset, $file
}

& $esptool.FullName --chip esp32s3 --port $Port -b $Baud write-flash @flashArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "Flashing failed (exit code $LASTEXITCODE). Check the cable and port, then try again."
    exit $LASTEXITCODE
}
Write-Host "Done. The car now runs the Python firmware."
