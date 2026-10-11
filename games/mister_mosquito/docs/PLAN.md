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
- 2026-10-11 05:00: Burnout 3 against the candidate engine first failed
  memcard_save (a jr target outside a run-time-translated function's
  slice) and race (from frame 17,000: the narrowed jr dispatch missed a
  table built at run time and ran a case as a separate function). Both
  fixed in the engine: tables searched program-wide; every instruction a
  dispatch target again except in functions over 3,000 instructions,
  which also keep every function start inside them. Burnout 3 re-lifting
  to confirm before any tag.
- Analysis now knows CodeWarrior's large-frame prologue and accepts a
  pointer target that opens a frame without a return: the stage's 24 KB
  task 0x0015D920 is a function.
- Stage 1: entered in real time once (stopped at 0x0015D920, now fixed);
  an intermittent stall remains where the EE sound thread's sequencer
  (F_001BEC18) loops on a track whose next event never comes due - sound
  trace: PS2HOST_SOUND_LOG=1 (port/sound_trace.cpp).

## Night of 2026-10-11 - summary

Reached: boot -> no-memory-card notice (or, with a card, straight on) ->
3D title fly-through -> main menu -> stage select, pictures right on both
renderers (text, textures, field-mode display), sound driver running, pad,
memory card. Stage 1 stalls on entry (the sequencer, FINDINGS.md); with
the opt-in PS2HOST_STOP_TRACKS_ON_UNLOAD=1 it loads and plays (bedroom,
mosquito, HUD).
Recorded runs: boot (gpu, soft), title (gpu). VERIFIED 6.9%,
MACHINE-CHECKED 37.6% of code bytes.

Engine (branch cd-images, 15 commits, local only - not pushed, not tagged):
CD images; the lift's jr dispatch (one switch per function; every
instruction a target up to 3,000 instructions, else the knowable targets
and all function starts); jump tables searched program-wide; CodeWarrior
jump tables and large-frame prologues; SDK 2.3 services (IOP reboot image
versions, libcdvd N-commands with a drive's timing, padman, LoadIopHeap,
libsd's older callbacks, libmc's mcserv); guest coroutines; SMODE2 field
mode; CSM2 and CT24 CLUTs; diagnostics (stuck runs, SIF/MC/pad logs).

Burnout 3 against the engine (its lift redone with it, identical numbers):
car_select gpu/soft, memcard_save gpu, race gpu, race_training gpu/soft as
recorded; race soft differs on 2-3 frames that change from run to run - and
so does the v0.2.0 baseline (1 frame, then 2: 17800, 19000). Only frame 9000,
in one candidate run, was not seen at the baseline.

Next:
1. The stage-entry stall, for real: in PCSX2, at stage entry, where
   SYSTEM01.HD lands and whether track 0 (0x00665E40) still plays
   (FINDINGS.md, end). Then replace the opt-in workaround with what the
   console does.
2. Why the title takes no START under --virtual-time (scripted runs into
   the menus need it).
3. Then the stage itself: gameplay, the 3D, the mosquito's controls.

## 2026-10-11 midday - the stall's cause, and stage 1

PCSX2 (PINE socket for EE RAM, keystrokes for the pad) showed the stage's
banks reloaded only after its intro cutscene, the START effect long over.
The port had no fade and no intro because its main loop never waited for the
vertical blank: the host had no INTC_STAT, so sceGsSyncV's poll read back its
own write. The main loop ran five times a frame. Engine: INTC_STAT as a
register (vertical blanks and RaiseIntc set it; writing 1 clears it). Port:
the poll is an idle loop. Now the logos and warnings show, PLAYTIME keeps
time, the title takes START under --virtual-time, and stage 1 is entered and
played with no workaround (the PS2HOST_STOP_TRACKS_ON_UNLOAD diagnostic is
removed). Runs: boot and title re-recorded (the timing changed, intended;
title now 3,000 frames), stage1 added (19,000 frames, into play).

Next:
1. Burnout 3 against the engine: it has the same sceGsSyncV, called from nine
   places, which now waits a frame as on the console. Its runs may change.
2. Stage 1's intro cutscene: a large blurred copy of the mother's face covers
   the top of the picture (PCSX2: sharp, top centre). A render-to-texture or
   blur effect; compare the software GS.
3. Gameplay: the mosquito's controls, the blood meter, sound in play.
