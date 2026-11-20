#!/bin/bash

# Sonic.EXE - PortMaster launcher
# Ported by AnubisWithCoffee

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

# Locate PortMaster
if [ -d "/opt/system/Tools/PortMaster/" ]; then
    controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
    controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
    controlfolder="$XDG_DATA_HOME/PortMaster"
else
    controlfolder="/roms/ports/PortMaster"
fi

# Initialise PortMaster
source "$controlfolder/control.txt"
source "$controlfolder/tasksetter"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

get_controls

# Game directory
export GAMEDIR="/$directory/ports/sonicexe"
cd "$GAMEDIR" || exit 1

# Display output
printf "\033c" > /dev/tty0

say() {
    printf "%b\n" "$1" > /dev/tty0
}

# Verify extracted game data
valid_data() {
    [ -s data.win ] && [ "$(head -c4 data.win 2>/dev/null)" = "FORM" ]
}

# First launch: extract game data
if ! valid_data; then

    if [ -f SONIC.EXE ]; then

        say "Sonic.EXE - First time setup"
        say "Extracting game data. Please wait..."

        ./extract_sonic --game-only SONIC.EXE . 2>&1 | tee extract_log.txt > /dev/tty0

        rc=${PIPESTATUS[0]}

        if [ "$rc" -ne 0 ] || ! valid_data; then

            say "Extraction FAILED!"
            say "Check extract_log.txt for details."
            say "SONIC.EXE has been preserved."

            rm -f data.win
            sleep 10
            pm_finish
            exit 1

        fi

        say "Extraction complete."

    else

        say "Sonic.EXE: Game files not found."
        say "Copy SONIC.EXE into:"
        say "$GAMEDIR"

        sleep 8
        pm_finish
        exit 1

    fi
fi

# Cleanup after successful extraction
if valid_data && [ -f snd_bgmTitle.ogg ]; then

    freed=0

    for f in SONIC.EXE Sonic7.exe D3DX9_43.dll; do

        if [ -f "$f" ]; then
            freed=$((freed + $(stat -c%s "$f")))
            rm -f "$f"
        fi

    done

    if [ "$freed" -gt 0 ]; then
        say "Cleanup complete!"
        sleep 2
    fi

fi

# SDL configuration
export SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1
export SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-alsa}

# Start game
say "Starting Sonic.EXE..."

$TASKSET "$GAMEDIR/sonicexe" "$GAMEDIR" > log.txt 2>&1

# Return to PortMaster
printf "\033c" > /dev/tty0

pm_finish