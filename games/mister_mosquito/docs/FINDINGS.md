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
