# Sonic.EXE - PortMaster

### Port by AnubisWithCoffee

A handheld Linux port of Sonic.EXE.

## INSTALLATION

1. Install Sonic.EXE through PortMaster.
2. Obtain your own copy of the original `SONIC.EXE` game file.
3. Copy `SONIC.EXE` into:

   `ports/sonicexe/`

4. Launch Sonic.EXE from the Ports menu.
5. On first launch, the extraction utility will automatically extract the game data.
6. Once extraction is complete, the game will start.

## FIRST LAUNCH

The first launch requires extraction of the original Windows game data.

This process only needs to happen once.

Please do not interrupt the extraction process.

After successful extraction, unnecessary Windows files will be removed automatically.

## CONFIGURATION

The configuration file is:

`ports/sonicexe/sonicexe.cfg`

Available settings include:

- Fullscreen mode
- VSync
- Texture memory budget
- Controller button mapping
- Analog stick dead zone
- Player name
- FPS logging

## TROUBLESHOOTING

### Game data not found

Make sure `SONIC.EXE` is located inside:

`ports/sonicexe/`

### Extraction failed

Check `extract_log.txt` for details.

Make sure your original game file is valid.

### Game crashes

Check `log.txt` for error messages.

### Controller problems

Check the controller settings in `sonicexe.cfg`.

## CREDITS

Port packaging and launcher adaptation:
AnubisWithCoffee

Original game and extraction utilities:
Respective original authors.

## DISCLAIMER

This package does not distribute the original
SONIC.EXE game data.

Users must provide their own legally obtained
copy of the original game.