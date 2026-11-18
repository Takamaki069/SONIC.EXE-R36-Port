#!/bin/bash
# Sonic.EXE - ArkOS "Ports" launcher
if   [ -d "/roms2/ports/sonicexe" ]; then GAMEDIR="/roms2/ports/sonicexe"
else GAMEDIR="/roms/ports/sonicexe"; fi
cd "$GAMEDIR" || exit 1

# first run: unpack SONIC.EXE (the original installer) next to the runner
if [ ! -f data.win ]; then
  if [ -f SONIC.EXE ]; then ./extract_sonic SONIC.EXE . > extract_log.txt 2>&1 || exit 1
  else echo "Put SONIC.EXE or the unpacked game files in $GAMEDIR" > /dev/tty1; sleep 5; exit 1; fi
fi

sudo chmod 666 /dev/tty1 2>/dev/null
printf "\033c" > /dev/tty1
export SDL_GAMECONTROLLERCONFIG_FILE="$GAMEDIR/gamecontrollerdb.txt"   # optional; ignored if missing
./sonicexe "$GAMEDIR" > log.txt 2>&1
printf "\033c" > /dev/tty1
