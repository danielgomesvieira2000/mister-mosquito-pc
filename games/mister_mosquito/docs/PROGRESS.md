# Decompilation progress

_Generated 2026-10-11 07:06 from `SLUS_203.75`._

## Headline number

**6.9490%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `verified` there is no evidence the source is right.

**37.5692%** is machine-checked: verified, or `lifted` - translated instruction by instruction and differential-tested, so correct and complete enough to run, but not yet readable source.

## Static analysis

| metric | value |
| --- | ---: |
| image size | 1,909,632 bytes |
| proven code | 806,040 bytes (42.2%) |
| non-code (rodata/data/padding) | 1,103,592 bytes |
| functions discovered | 1,288 |
| - found via direct calls | 783 |
| - found via prologue scan | 174 |
| functions using VU0 macro mode | 26 |
| strings recovered | 2,868 |
| address cross-references | 1,642 |
| pointer tables (vtables/jump tables) | 7 |

## Progress by tier

| tier | functions | share | code bytes | share |
| --- | ---: | ---: | ---: | ---: |
| `undecompiled` | 0 | 0.00% | 0 | 0.00% |
| `analyzed` | 0 | 0.00% | 0 | 0.00% |
| `reconstructed` | 232 | 18.01% | 505,984 | 62.43% |
| `lifted` | 697 | 54.11% | 248,168 | 30.62% |
| `verified` | 359 | 27.87% | 56,320 | 6.95% |

Tier meanings are defined in [METHODOLOGY.md](../../../engine/docs/METHODOLOGY.md).
