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

- The title (task F_0019C790) takes START only while a counter in its
  frame (`0x180($sp)`) is below 300 (0x001A26E4); in real time a press
  every couple of seconds lands. Under `--virtual-time` the presses tried
  so far missed it - the scripted runs for the menus are real-time for now.
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
