# Mister Mosquito PC port: plan and log

## Goal

A native PC port of Mister Mosquito (USA, SLUS-20375) on ps2-port-engine,
the second game: the engine's first proof that it is game-independent.

Measured by: how far scripted runs get (boot, logos, title, menus, a stage),
pictures checked against the software GS / PCSX2, the progress report's
VERIFIED and MACHINE-CHECKED numbers.

## Steps

1. Repository and disc - done 2026-10-11: `mister-mosquito-pc` (local only),
   engine submodule; the engine reads raw CD images (.bin/.cue).
2. Analyse, derive, lift (skill ps2-decomp).
3. Bring-up loop (skill ps2-port): build, run, read why it stopped, provide
   it - in the engine when any game needs it, in `port/` when only this one.
   Expected first gaps from what Burnout 3 never used: SDK 2.3-era IOP
   modules from IOPRP234.IMG, EE-side libcdvd RPCs (sceCdRead, streaming
   sceCdSt*), the game's own sound driver SNDN2DRV.
4. Pictures: the first frame, then compare GPU vs software GS.
5. Scripted runs to the title screen and into a stage; regressions.
6. Every engine change: Burnout 3's runs too (regress_all), before a tag.

## Log

- 2026-10-11: engine branch `cd-images` (55f88c5): raw CD + .cue images,
  sceCdRead data patterns, disc type PS2CD. Host tests and engine tests pass.
- 2026-10-11 night (engine, all local on `cd-images`): lift dispatch to
  jr targets (a 5.2 MB function -> 632 KB, 12 min -> 3 s at /O2);
  CodeWarrior jump tables (71 -> 1,849); SIF reset/IOPRP version; libcdvd
  N-commands (read, read-to-IOP, disk ready), sceCdGetError; padman;
  sceSifLoadIopHeap; libsd's older callbacks; guest coroutines (the task
  system); SMODE2 field mode; CSM2 CLUTs; CT24 CLUTs; a stuck-run
  diagnostic in the host watchdog.
- Reached: notice -> title fly-through -> main menu -> stage select, text
  and textures right in both renderers; 34 VU1 programs compiled.
- Open: the stage after START at the stage select (black - investigating);
  memory cards (libmc over mcserv 0x80000400, protocol read but not
  implemented: the game is played without saves); OP.PSS (the opening
  movie, IPU); Burnout 3's runs against the candidate engine (re-lift in
  progress) before any tag; pushing (the user's call).
