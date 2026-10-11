# Mister Mosquito: findings

What the binary revealed, as it is learned - the compiler (the ELF's
`.comment` section), middleware (RCS `$Id:` strings), the SDK version, the VU
regions, and then each subsystem the port had to understand (its menus, its
camera, its streaming, ...), with the addresses and how they were found.

## The disc (2026-10-11)

- A CD, kept as BIN/CUE: one MODE2/2352 track, 285,729 sectors (558 MiB);
  ISO 9660 data is Mode 2 Form 1, the 2048 bytes at offset 24 of each raw
  sector. Volume `MISTER_MOSQUITO`, publisher `EIDOS_INTERACTIVE`.
  Engine v0.2.0 read only .iso images: raw CD and .cue support was added to
  the engine for this game (`ps2decomp/discimage.py`, host `Disc`).
- `SYSTEM.CNF`: `BOOT2 = cdrom0:\SLUS_203.75;1`, `VER = 1.00`, `VMODE = NTSC`.
- `IRX/`: `IOPRP234.IMG` (an IOP replacement image, SDK 2.3.4), `SIO2MAN`,
  `PADMAN`, `MCMAN`, `MCSERV`, `LIBSD`, `SDRDRV`, and the game's own sound
  driver `SNDN2DRV.IRX`.
- `ADPCM/`, `ADPCM2/`: streamed voice and music (`.INT`, interleaved), the
  bulk of the disc. `DUMMY.BIN` (14 MB at LSN 68) is padding to put the
  streams on the outer, faster part of the CD.

## The executable

- `SLUS_203.75`: 1.9 MB loaded at 0x00100000, entry 0x00100008; BSS to
  0x0069A000 (memsz 5.4 MB). No symbols.
- Compiler: **Metrowerks CodeWarrior** (`.comment`: "MW MIPS C Compiler
  (2.4.1.01)") - not GCC, unlike Burnout 3. The SDK libraries (linked as
  built by Sony) are 2.3.x: `PsIIlibgraph2300`, `libkernl 2340`,
  `libpad 2300`, `libcdvd 2340`, `libmc 2320`, `libscf 2300`. Only 3
  functions matched the cross-game signature library (Burnout 3's SDK is a
  later one).
- analyze.py: 1,287 functions + 174 recovered from gaps; 42% of the image
  proven code at first (43% of the image is zero-filled).
- **VU1 microcode** at 0x001C5E00-0x001CD6E0 (in `vu_regions`): two blocks,
  each a DMA chain of VIF MPG uploads of 2 KB chunks to VU1 micro memory
  0x000-0x7FF, ~1,800 bundles, 21 microprograms (E flags), all decoded.
  The code at 0x00101BA0-0x00102800 uses VU0 macro mode (`VuMadd`, `VuMul`
  in the derived C++): the vector maths library.

## CodeWarrior's code, as the tools first saw it

- **Switch tables.** CodeWarrior forms a table's address in another
  register than the `lui`'s (`lui $v0, hi; addiu $v1, $v0, lo; sll $v0,
  $i, 2; addu $v0, $v0, $v1; lw $v0, 0($v0); jr $v0`) and places the cases
  after the `jr`. analysis.py only knew GCC's in-place form: 71 tables were
  found instead of 1,849, and 181 case labels were taken for functions of
  their own (e.g. forty "functions" 0x158 apart at 0x0014CB5C..0x0014FECC,
  the cases of one state machine) while the switches' functions ended at
  their `jr`. Fixed in the engine (analysis.py, `jump_table`).
- **Huge functions.** Some functions are 15-25 KB of code (stage logic,
  state machines). Each `jr` through a register was lifted as a switch over
  every instruction of its function - 5.2 MB of C++ for 0x0016D550 - which
  MSVC took twelve minutes and 10 GB over. The lifter now emits one
  dispatch per function, to the addresses a jr can reach (engine lift.py).

## The boot (2026-10-11)

1. `0x00100008` entry; a kernel patch is copied to 0x80076000 (the
   discovered code at 0x001CD788).
2. `0x00168D60` (main): `sceSifRebootIop("cdrom0:\IRX\IOPRP234.IMG")`
   (0x0010AD70) and `sceSifSyncIop` (0x0010AD38), then
   `sceSifLoadModule` (0x0010A870) of SIO2MAN, PADMAN, LIBSD, SNDN2DRV,
   MCMAN, MCSERV, SDRDRV, each retried until it loads. The SDK 2.3
   loadfile library refuses to load anything until server 0x80000006's
   procedure 0xFF answers "2340" - the release of the IOPRP image's
   LOADFILE module (`loadfile2340` in its library tag). The host now takes
   that release from the reboot image (engine loadfile.cpp, `UseIopImage`).
3. CD reads through libcdvd's N-command server (0x80000595): procedure 1
   sceCdRead { lsn, sectors, buffer, mode, unaligned-ends record }, 14
   sceCdDiskReady; sceCdGetError is S-command 4 with a one-word answer
   (a nonzero error makes the game's read wrapper 0x0011DD20 retry
   forever). The game's CD callback (0x0011DEA0, registered at 0x0011DEF4)
   reads sceCdGetError into `$gp-0x7BE8`.

## The task system

Most of the game runs as cooperative tasks with stacks of their own.
Globals at 0x0039B090: +0x00 the run list's head, +0x08 the task running,
+0x14 the scheduler's loop (0x0011ABEC) that switch routines jump to, +0x18
the scheduler's `$sp`, +0x1C the address it resumed. A TCB is 0x210 bytes:
+0x00 u16 flags (0x8000 sleeping, 0x4000 held), +0x04 u16 state, +0x10 next,
+0x20 the state table, +0x24 frames to sleep, +0x28 the stack's top, +0x2C
saved `$sp`, +0x30 resume address, +0xF0 `$fp $gp $s0-$s7` (128 bits each),
+0x190 `$f0-$f31`.

- 0x0011AB40 init (each TCB's `$gp`), 0x0011AB9C the scheduler (once a
  frame from main, 0x00168D60): for each runnable task, load its registers
  and `jr` to its resume address.
- 0x0011AF7C yield, 0x0011AE98 sleep(frames), 0x0011AD74 kill(tcb): save
  the registers and jump back into the scheduler's loop through +0x14.
- 0x0011AE04 / 0x0011AE50 set a task's state (from its table); nothing
  calls them directly.

Translated, a switch routine's `jr $ra` is a native return, so the task
simply went on and the scheduler never ran the others: the first task
(0x00141650) spun forever. `port/tasks.cpp` runs every task on a host
coroutine (engine `CreateCoroutine`/`SwitchCoroutine`): the scheduler
switches into a task's coroutine where the game jumped into the task, and
yield, sleep and kill switch back. Memory is kept as the game keeps it.

## Sound

- `SNDN2DRV.IRX` (RPC server "sndn", 0x736E646E) runs in the IOP
  interpreter. Procedure 101 initialises (sceSdInit, the transfer callback
  through libsd's older `sceSdSetTransCallback`), procedure 100 is called
  once a frame by the EE's sound thread (0x00198560, woken from the
  vertical-blank-end handler 0x00198520) with the commands queued since
  (0x001BF0D0), and answers 512 bytes of status. The EE numbers its
  commands (0x00666E48) and waits for the driver's count at status +0x1C0
  (0x001C2690); a bank upload (`sceSdVoiceTrans`) only counts once the
  transfer's callback has run - the host's libsd never called the older
  callback, and the game waited forever at its first bank.
- Banks: `SOUND/ENG/*.BD` loaded into the IOP heap with sceSifLoadIopHeap
  (iopheap procedure 3, EE 0x00109D80), then uploaded to SPU2 RAM by the
  driver. Music streams from `ADPCM/*.INT` through sceCdReadIOPMem
  (N-command 13) into a 256 KB IOP buffer.

## Controller

libpad 2.3 / padman 4.x (EE 0x0010AF50-0x0010C100): engine `dbc.cpp`,
padman. Two pad areas, ports 0 and 1 (0x0038DE40, 0x00391E40).

## Graphics

- Display: PMODE circuit 2, SMODE2 = 3 (interlaced, field mode), two
  640x224 frame buffers (DISPFB2 FBP 0 / 0x46), DISPLAY2 DH 447 - a field
  shows all 224 lines.
- Text: `MENU/FONTDATA.BIN` holds the font as TIM2 images, a 32x32 4-bit
  glyph each with a 16-entry CT16 CLUT (TIM2 CLUT type 0x81). The glyph is
  uploaded to 0x1A40 per character; the CLUT once, as a 16x1 row at
  0x1A60, read with CSM2 (TEX0 bit 55).
- Most world textures are 8-bit with CT24 CLUTs (CPSM 1).

## Getting around (scripted presses, virtual time)

`--press cross@500-510` answers the no-memory-card notice (YES: no saves);
START at the title (frame ~1500 under virtual time) opens the main menu
(the mosquito and NEW GAME / ...), START again the stage select (STAGE 1,
the Yamadas' room).

## Memory card

libmc 2.3 over mcserv (engine `mcserv.cpp`). With a card (`--memcard DIR`)
the game skips the notice, scans `/BASLUS-20375KA1/BASLUS-20375KA1` to
`...KA7` (getdir, then open read-write) for its seven save slots, and
polls getinfo now and then; without one it polls getinfo every frame.

## Where the run stands (2026-10-11 morning)

- The title (task F_0019C790, TCB 0x0066A620): after loading
  `menu/ttitleus.bin` it waits for its TCB's byte +0x39, which the task
  F_001A4600 sets (in real and virtual time alike), then loops yielding at
  0x001A2F10 with a frame counter at `0x180($sp)`. Its own START test at
  0x001A26FC needs `0x200($sp)` = 1, which stays 0 in both modes - so START
  is taken elsewhere (candidates: 0x0013F574 in F_0013E5A0, 0x00151E28 in
  F_001516F0). In real time presses a couple of seconds apart land; under
  `--virtual-time` none has yet (PS2HOST_TITLE_LOG traces the title's
  state) - the scripted runs into the menus are real-time for now.
- Entering stage 1 (START at the stage select) stalls: the stage loader
  F_001516F0 waits for the sound driver (0x001C2690) for a bank upload,
  but the EE sound thread (0x00198560, the highest priority) loops in its
  sequencer F_001BEC18 (at 0x001BECA8) and never sends its queue - 23
  commands pending at 0x00666E80, request number 8. Next: why that loop
  never ends (the sequence data it walks, 0x001C0318's events).

## The stage-entry stall, as far as it is understood (2026-10-11 06:30)

The EE runs a sequencer of its own (F_001BEC18, the sound thread, each
frame): 48 tracks of 0x54 bytes at 0x00665E40 (+0 flags, low nibble the
state: 3 and 0xC play; +4 offset, +8 data, +0x10 tempo step, +0x14 time
to the next event, +0x18 the bank), banks described at 0x006647C0 (12
bytes each: the bank's .HD - "SShd", relocated in place - its size, ...).
An event with no delay is taken at once; a track whose data are zeros never
ends a frame.

What happened in the stalled runs: at the stage select (sound frame ~741)
SYSTEM01.HD (bank 1) was read to 0x00BCF800 and track 0 started on it
(data 0x00BCF9AE, state 0xC - looping); entering stage 1 (~1050) the game
read SYSTEM01.HD again, to 0x00BD0400, with track 0 still playing - its
old data had meanwhile been reused and were zeros. The sequencer then
never finishes its frame, the queued commands (23, among them the stage's
bank upload) are never sent, and the stage loader waits forever in
0x001C2690. In other runs the same moment passed (the track had been
stopped first). On the console the game must stop that track before the
bank moves; what decides whether it does here - thread interleaving around
the stage loader, CD reads finishing at once instead of taking time, or a
translation fault - is the next thing to find (watch track 0's flags with
--watch-write 0x00665E40 across the stage select -> stage transition).

Later the same morning, with CD reads taking a drive's time (engine
579da46): every attempt stalls the same way (3 of 3), the bank's header
always reloaded at 0x00BD0400. In the stalling frame (sound frame ~1356)
the stage loader queues twenty `0x40` driver commands (voices off) and two
`0x43` (stream) - and between them the menu task F_0017DA40 starts a new
looping sound on track 0 (0x001C3560, flags 0x1C0C) on bank 1, which the
loader then moves. So the menu task is still alive, and running after the
loader's stop-all, in the frame the stage takes over. Next: when the menu
task should end (who kills it - 0x0011AD74's callers around the stage
select's START), compared with PCSX2 (task order and lifetimes in that
frame); the coroutine scheduler (port/tasks.cpp) runs the tasks in the
run list's order, as the original does, but a difference in when a task is
created, killed or put to sleep would show exactly so.

### What starts the track, and what clears its data (2026-10-11 08:00-09:00)

With PS2HOST_TASK_LOG (port/tasks.cpp), --watch-write and a RAM dump of the
stall (--dump-ram):

- Track 0 is started by the sound-effect task F_0017DA40 (TCB 0x00669BD0,
  task table 0x002D2198; it loads SOUND/<lang>/MML.BIN, then loops F_0017D010
  and a yield forever). It is never killed: it is the game's MML player, and
  the 0x1C0C sound is the stage select's START "confirm" effect. It starts in
  the same frame as the kill-others (0x001938C4, run by TCB 0x00669DE0) that
  ends the stage select and starts the loader (TCB 0x00669FF0 at 0x001516F0).
- In that frame the main thread (0x001429F0) resets the heap cursor
  (gp-0x7B94 = 0x002D23DC, a bump allocator; F_001994D0 aligns it) to
  0x00BCF540 and clears the memory after it (F_00116100). That zeroes the old
  SYSTEM01.HD copy at 0x00BCF800 that track 0 reads through its absolute data
  pointer (+8 = 0x00BCF9AE).
- The loader then allocates 0x658, 0x420 and 6 x 0x40 bytes, aligns to 0x400,
  and reads SYSTEM01.HD to 0x00BD0400. Unloading the bank (F_001C2880, from
  F_00199290) stops the *voices* that use bank 1 (handle table 0x00664DC0) but
  not the sequencer's *tracks* (0x00665E40).
- The sequencer (F_001BEC18 -> F_001C0318) checks a track's bank (entry
  non-zero, "SShd" at +0xC). With the bank missing it skips the track for that
  frame. It does not stop it. Once the bank is back, the zero bytes are read
  as a running status that never advances, which is the spin.

Tried and ruled out: a distance-dependent seek time (30 ms near, 100 ms far)
for libcdvd reads. Even with several frames between unload and reload, the
sequencer only skips the track then, so the stall stays (checked with a
rebuilt port, 1 of 1; the change was reverted).

Past the stall: with PS2HOST_STOP_TRACKS_ON_UNLOAD=1 (port/sound_trace.cpp,
a diagnostic, off by default) unloading a bank also stops the tracks that
play from it. Stage 1 then loads and plays (real time, --gpu, START every
150 frames): the bedroom, the player's mosquito, the HUD (hearts, gauge,
radar), sound requests going on (sound frame 8000+). The stall is the one
blocker on the way into the game. The workaround is not the console's
behaviour, so it stays opt-in until PCSX2 shows what really happens.

Open: on the console either the track is not playing at this point (the START
effect ends, or is never started, before the heap reset), or SYSTEM01.HD
lands at 0x00BCF800 again (the loader's allocations before it would then total
at most 0x2C0 bytes, against 0xC00 here). Next: in PCSX2, break on the
SYSTEM01.HD read at stage entry (LSN 284402) and compare its buffer address
and track 0's flags (0x00665E40) with these.

### The cause: the main loop never waited for the vertical blank (2026-10-11 midday)

PCSX2 (2.6.3, the PINE socket reading EE RAM while keystrokes drove the pad)
at the same moments:

- START at the stage select: track 0's confirm effect (0x1C0C, then 0x44 the
  next sequencer frame) ends on its own about 1.5 s later. The heap is reset
  about 25 frames after the press (0x0188FF60 -> 0x00BD0140). The stage's
  intro cutscene (the Yamadas' faces) then loads and plays. SYSTEM01.HD is
  read again to 0x00BD0400 (bank table 0x006647CC) only when the intro is over,
  long after the effect has stopped. Until then the bank entry still points at
  the zeroed 0x00BCF800, which harms nothing because no track plays from it.
- In the port the same reload came in the same frame as the press. There was
  no fade and no intro: the START, still held, also skipped the intro.

Why: the main loop (F_00168D60) paces itself with libgraph's sceGsSyncV
(0x00100B40 -> 0x00103160). That routine writes 4 to INTC_STAT (0x1000F000)
to clear VBLANK_START, then polls the register until bit 2 is set again. The
host had no INTC_STAT, so the register was plain memory: the poll read back
the 4 just written and never waited. The main loop ran about five times per
displayed frame (its counter 0x0038B958 and the frame count 0x002D2538:
7,653 by frame 1,500; PCSX2 advances both 60 per second). Everything counted
in main-loop passes ran five times too fast: PLAYTIME, fades, the logos and
warning screens (Fresh Games, ZOOM, "explicit itchiness", the insecticide
CAUTION, all skipped before) and the stage select's hand-over.

Fixed in the engine: INTC_STAT is now a register. A frame's vertical blank sets
bits 2 and 3, RaiseIntc sets its cause, and writing 1 clears a bit. The port
names the poll loop (0x00103170) an idle loop, so the wait sleeps. Now: 59 fps
in real time, the intro screens shown, PLAYTIME at one second per second, the
title taking START under --virtual-time, and stage 1 entered through its
intro cutscene with no stall and no workaround (the PS2HOST_STOP_TRACKS_ON_UNLOAD
diagnostic is removed). Past it: Rena's cutscene, "STAGE 1 - Suck Rena's
blood", START!, and play with the HUD (frame ~18,500 under virtual time).

The title, for scripted runs: a single START (frame 2800 under virtual time)
lets the fly-through run on to dusk, the mosquito flies in, and the menu
opens on its own about two minutes later (frame ~10,400; on PCSX2 too). START
at 11,000 selects STAGE, START at 11,600 the stage.

## Stage 1's intro: a blur that must lose (2026-10-11 afternoon)

The intro cutscene draws each frame, copies it at half size into a 320x112
buffer (FBP 0x14C, read as TBP 0x2980), and blends the copy back twice: once
depth-tested at alpha 0x60 (a depth-of-field blur of the background), once
untested at 0x80 from the *previous* frame (a motion blur). Both blends are
weighted by the alpha in the frame buffer, and they write that alpha back, so
the frame's alpha feeds itself. Loading leaves both buffers at alpha 0x80.
On the console the old picture fades from the edges over a few seconds,
because each bilinear pass truncates. In the port it held for good: a huge
blurred copy of the mother's face from the cutscene's first frames.

How it was found: PCSX2 GS dumps taken with its software renderer (the
hardware ones leave stale frame buffers in the dump), replayed on the host
(--gs-replay); the port's own stream captured with PS2HOST_CAPTURE_FRAME.
The port's commands, given the console's GS memory, drew right. The console's
commands, given the port's memory, kept the ghost. A 300-frame loop of the
console's stream left the host with the ghost too. Fixed in the engine:
bilinear weights in sixteenths with each step truncated, in the software GS
and the GPU shader. The intro now matches PCSX2.

## Streamed audio: narration and music (2026-10-11 evening)

The narration and music (`ADPCM/**/*.INT`, SPU ADPCM, every block flagged
2) are streamed by the EE's sound code. It reads 64 KB chunks into a 256 KB
IOP ring at 0x1BA40 with sceCdReadIOPMem and queues driver op 0x43. SNDN2DRV
copies 2 KB halves (left at 0x15140, right at 0x15940) and uploads them with
sceSdVoiceTrans into an 8 KB SPU RAM ring at 0x5400-0x7400. Core 0 voice 0
loops over that ring at pitch 0xEB3 (44.1 kHz), and the driver polls its NAX
(sceSdGetAddr 0x2240) to refill the half just played. The driver's
BlockTrans (PCM input) path, ops 0x46/0x47, is dead code: nothing in the EE
calls it.

The voice played (envelope 0x7FFF, NAX advancing) but was silent: the
driver clears VMIXEL/VMIXER and never sets VMIXL/VMIXR, relying on
sceSdInit leaving every voice mixed. The host had them at 0. Fixed in the
engine (libsd's sceSdInit). The opening narration (N_OPN.INT, after START at
the title) now plays at a normal level.

## Interlaced shake in the menus (2026-10-11 evening)

Field mode (SMODE2 INT and FFMD) shows one 224-line field at a time. The
game draws odd fields half a pixel lower (XYOFFSET Y +8 sixteenths,
sceGsSetHalfOffset); a television scans that field a line lower and the two
meet. Progressive, consecutive frames moved by exactly 0.5 lines, the shake in
the menus and loading screens. A shift at presentation can't undo it exactly:
the window's scale makes half a line a fraction of a pixel (1.6 px in a
1280x896 window; the blit moves whole pixels). The engine now drops the half
pixel when drawing in field mode: 0.0 lines frame to frame, rendered and in
the window. The runs were re-recorded (title, stage1: the odd fields' frames
moved).
