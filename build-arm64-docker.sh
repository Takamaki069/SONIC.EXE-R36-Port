#!/bin/bash
# Build aarch64 binaries on a PC using an Ubuntu 20.04 arm64 container (glibc matches ArkOS).
# Needs docker + qemu-user-static (e.g. `sudo apt install qemu-user-static docker.io`).
set -e
cd "$(dirname "$0")"
docker run --rm --platform linux/arm64 -v "$PWD":/work -w /work ubuntu:20.04 bash -c '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq && apt-get install -y -qq build-essential libsdl2-dev libsdl2-mixer-dev zlib1g-dev >/dev/null
  make clean && make'
cp sonicexe extract_sonic ports/sonicexe/
echo "Built. Copy the contents of ports/ to /roms/ports/ on the device."
