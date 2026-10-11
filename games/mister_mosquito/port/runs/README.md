# Scripted runs

Each `.args` file is a set of port options that drives the game to a known
point. With `--virtual-time` the run is the same on any machine, under any
load: the same presses land on the same frames and the pictures come out
byte-identical - a regression test, and a way to get somewhere in the game
without playing.

```sh
<exe> <executable> --disc <iso> $(cat games/mister_mosquito/port/runs/<run>.args) --frames out/ --frames-every 200
python engine/tools/regress.py --game games/mister_mosquito <run> --gpu            # check against the recorded CRCs
python engine/tools/regress.py --game games/mister_mosquito <run> --gpu --update   # record them (an intended change only)
```

Record runs that reach the game's main places - boot, the menus, gameplay -
and say here what each reaches and at which frames:

| file | reaches | frames |
| --- | --- | --- |
| `boot.args` | no memory card: the boot (IOP reboot, sound driver, banks), the no-memory-card notice (its CSM2 font) | 600 |
| `title.args` | an empty card (`{memcard}`): the card scan, the title's 3D fly-through (CT24-CLUT textures, field-mode display) | 1200 |

Under `--virtual-time` the title does not yet take START (its start-up
handshake between title tasks never completes - FINDINGS.md); runs into the
menus wait for that.

`python engine/tools/regress_all.py` runs every one of them (and every other
game's, given their repositories) - before an engine version is tagged.
