# sonicexe-runner build
CC      ?= gcc
CFLAGS  ?= -O2 -pipe
WARN    := -Wall -Wno-misleading-indentation -Wno-unused-function
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
CORE := src/engine.c src/data.c

all: sonicexe extract_sonic

# the game (needs libsdl2-dev, libsdl2-mixer-dev, zlib1g-dev)
sonicexe: src/main_sdl.c $(CORE) src/gm.h
	$(CC) $(CFLAGS) $(WARN) $(SDL_CFLAGS) -o $@ src/main_sdl.c $(CORE) $(SDL_LIBS) -lSDL2_mixer -lz -lm
# no audio (if SDL2_mixer is unavailable)
sonicexe-noaudio: src/main_sdl.c $(CORE) src/gm.h
	$(CC) $(CFLAGS) $(WARN) -DNO_AUDIO $(SDL_CFLAGS) -o sonicexe src/main_sdl.c $(CORE) $(SDL_LIBS) -lz -lm

# SONIC.EXE unpacker (no dependencies)
extract_sonic: src/extract_sonic.c
	$(CC) $(CFLAGS) -w -o $@ $<

# headless software-rendered test harness (writes PPM frames) - no SDL needed
test_runner: src/main_test.c $(CORE) src/gm.h
	$(CC) $(CFLAGS) $(WARN) -o $@ src/main_test.c $(CORE) -lz -lm

clean:
	rm -f sonicexe extract_sonic test_runner
.PHONY: all clean sonicexe-noaudio
