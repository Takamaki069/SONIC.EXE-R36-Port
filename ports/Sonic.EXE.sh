#!/bin/bash
# Sonic.EXE - ArkOS "Ports" launcher
if   [ -d "/roms2/ports/sonicexe" ]; then GAMEDIR="/roms2/ports/sonicexe"
else GAMEDIR="/roms/ports/sonicexe"; fi
cd "$GAMEDIR" || exit 1

sudo chmod 666 /dev/tty1 2>/dev/null
printf "\033c" > /dev/tty1
say() { printf "%b\n" "$1" > /dev/tty1; }
valid_data() { [ -s data.win ] && [ "$(head -c4 data.win 2>/dev/null)" = "FORM" ]; }

# ---- first run: unpack SONIC.EXE, then clean up ----
if ! valid_data; then
  if [ -f SONIC.EXE ]; then
    say "Sonic.EXE - first run: unpacking the game data."
    say "This takes about a minute and only happens once. Please wait...\n"
    ./extract_sonic --game-only SONIC.EXE . 2>&1 | tee extract_log.txt > /dev/tty1
    rc=${PIPESTATUS[0]}
    if [ "$rc" -ne 0 ] || ! valid_data; then
      say "\nExtraction FAILED (details in extract_log.txt). SONIC.EXE was kept."
      say "Closing in 10 seconds..."; rm -f data.win; sleep 10; exit 1
    fi
    say "\nExtraction complete."
  else
    say "Sonic.EXE: game files not found."
    say "Copy SONIC.EXE into:\n  $GAMEDIR\nand start the game again."; sleep 8; exit 1
  fi
fi

# ---- cleanup: the installer and the Windows-only files are not needed once data.win exists ----
if valid_data && [ -f snd_bgmTitle.ogg ]; then
  freed=0
  for f in SONIC.EXE Sonic7.exe D3DX9_43.dll; do
    if [ -f "$f" ]; then freed=$((freed + $(stat -c%s "$f"))); rm -f "$f"; fi
  done
  if [ "$freed" -gt 0 ]; then say "Cleaned up: removed installer/Windows-only files (freed $((freed/1048576)) MB)."; sleep 2; fi
fi

say "Starting Sonic.EXE..."
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1      # handhelds have no keyboard focus; SDL would otherwise ignore the pad
export SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-alsa}    # avoids the harmless "pulse" warning; remove this line if you get no sound
./sonicexe "$GAMEDIR" > log.txt 2>&1
printf "\033c" > /dev/tty1
