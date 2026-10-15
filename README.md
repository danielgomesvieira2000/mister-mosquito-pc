# Mister Mosquito - PC port

**Mister Mosquito, native on Windows - a work in progress.** The PS2 game's
program translated to C++ and compiled for the PC, not emulated, with
[ps2-port-engine](https://github.com/danielgomesvieira2000/ps2-port-engine),
the toolchain and runtime also behind the
[Burnout 3: Takedown PC port](https://github.com/danielgomesvieira2000/burnout3-pc).

> **Unofficial and fan-made.** Not affiliated with, endorsed or sponsored by
> the game's developer or publishers. You need your own copy of the game: a
> disc image made from your own Mister Mosquito disc (NTSC-U, SLUS-20375,
> BIN/CUE). No game content is in this repository.

## Where it stands

Early: there is **no release yet**. It boots, plays the title, the menus and
the first stage with sound, on the Vulkan renderer at up to 4x the PS2's
resolution, widescreen with the HUD kept in place (F10 edits it), PC SETTINGS
and EXIT GAME in the game's main menu, the engine's PC settings panel
(F9) and an FPS counter (F1). What the game taught the port, and the open problems, are in
[games/mister_mosquito/docs/FINDINGS.md](games/mister_mosquito/docs/FINDINGS.md).

## AI usage

**This port is made with AI.** It is built by
[Claude Code](https://claude.com/claude-code) (Anthropic's Claude), directed,
playtested and reviewed by the repository's author. Commits carry a
`Co-Authored-By: Claude` line. The workflow it follows is in the engine's
skills (`engine/.claude/skills`).

## Building it

`engine/` is the ps2-port-engine submodule. With your disc image as
`Mister Mosquito (USA).cue` (and its `.bin`) in the repository's root
(game.json's `"iso"`):

```sh
git clone --recursive https://github.com/danielgomesvieira2000/mister-mosquito-pc
cd mister-mosquito-pc
python engine/tools/isotool.py extract "Mister Mosquito (USA).cue" SLUS_203.75 -o games/mister_mosquito/extracted/
python engine/tools/analyze.py --game games/mister_mosquito
python engine/tools/derive.py  --game games/mister_mosquito --emit --csv
python engine/tools/lift.py    --game games/mister_mosquito --structure --emit --csv
python engine/tools/port.py    --game games/mister_mosquito build
```

The game's code in C++ is generated on your machine from your disc image and
never committed. Building needs Visual Studio 2022 with C++ (its MSVC, CMake
and Ninja) and Python 3.10+. The engine's
[docs/PORTING.md](https://github.com/danielgomesvieira2000/ps2-port-engine/blob/master/docs/PORTING.md)
has every option of the port; `games/mister_mosquito/port/runs/` the scripted
test runs and their pictures' checksums.

## Legal

This repository contains **no game assets, no disc images or BIOS files, and
none of the game's code.** The game's code in C++ - lifted, derived or
reconstructed - is generated on your own machine from your own disc image and
never committed (`.gitignore`). What the repository holds is original: the
port's code, documentation of how the game works (addresses, function names,
findings) and checksums of test pictures. "Mister Mosquito" is a trademark of
its owner.

The project's own code is under the MIT licence ([LICENSE](LICENSE)).
