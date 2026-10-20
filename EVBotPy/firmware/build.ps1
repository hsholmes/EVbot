# Builds the EVBotPy firmware in Docker. Docker Desktop must be running.
#
#   .\build.ps1            MicroPython + the EVBot board (phase 0)
#   .\build.ps1 -Engine    also compile the evbot C module (phase 1 on)
#   .\build.ps1 -Clean     start again from a fresh MicroPython checkout
#
# The first build downloads the ESP-IDF image (~3 GB) and MicroPython, and
# takes a while. Later builds only recompile what changed.
param([switch]$Engine, [switch]$Clean)

$MpyVersion = "v1.29.0"
$IdfImage = "espressif/idf:v5.5.2"  # the ESP-IDF version MicroPython v1.29.0 recommends
$Volume = "evbotpy-build"

$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$Out = Join-Path $PSScriptRoot "build-out"
New-Item -ItemType Directory -Force $Out | Out-Null

if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    Write-Host "Docker isn't installed (or this window was opened before it was). Install Docker Desktop, then open a new PowerShell window."
    exit 1
}
docker info --format "{{.ServerVersion}}" | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "Docker isn't running. Start Docker Desktop, wait for 'Engine running', then try again."
    exit 1
}

if ($Clean) {
    docker volume rm $Volume | Out-Null
}

$withEngine = if ($Engine) { "1" } else { "0" }
docker run --rm `
    -v "${Volume}:/build" `
    -v "${Repo}:/evbot:ro" `
    -v "${Out}:/out" `
    $IdfImage `
    bash /evbot/EVBotPy/firmware/docker-build.sh $MpyVersion $withEngine
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed (exit code $LASTEXITCODE)."
    exit $LASTEXITCODE
}
