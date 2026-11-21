# Sonic.EXE - PortMaster

### Port by AnubisWithCoffee

A handheld Linux port of Sonic.EXE.

Here is a port I just finished of the classic Creepypasta fan game Sonic.exe! its been Tested with a Original R36S on both ArkOS4Clone and DarkOSen! It SHOULD work on any device on these OS's! also some may ask "Why?" eh, why not, it has a place in cultural history and I wanted a small project to work on to get my feet wet as it where. I have more ports in the works!
Just some things to note right off the bat! 

## WARNING: 
Jump scares and may trigger epilepsy!
also, I can find no bugs that did not exist in the original, I left those in the game to keep it as faithful as I could aside from a word wrapping toggle (Default off) though that has a chance to crush the words here and there but is still readable.## INSTALLATION

1. Install Sonic.EXE through PortMaster/PortMaster auto install (Tools/Portmaster/autoinstall) or just unpack the data yourself to your ports folder.
2. Obtain your own copy of the original `SONIC.EXE` game file. (Available from the creator, MY5TCrimson on Gamejolt)
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

## CONTROLS
A - Attack (Knuckles stage)
B - Jump
Start - Select data
Start & Select - Hold for 1 second to quit
D-Pad/Left Analog - Move

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

Original game: MY5TCrimson 

Extraction utilities: Respective original authors.

## DISCLAIMER

This package does not distribute the original
SONIC.EXE game data.

Users must provide their own legally obtained
copy of the original game.