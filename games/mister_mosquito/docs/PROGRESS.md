# Decompilation progress

_Generated 2026-10-11 02:16 from `SLUS_203.75`._

## Headline number

**7.3443%** of the executable's code bytes are verified - reconstructed source that was differential-tested against the original machine code and agreed.

This is the only number that means anything on its own. The tiers below track work in progress, but until a function reaches `verified` there is no evidence the source is right.

**39.7007%** is machine-checked: verified, or `lifted` - translated instruction by instruction and differential-tested, so correct and complete enough to run, but not yet readable source.

## Static analysis

| metric | value |
| --- | ---: |
| image size | 1,909,632 bytes |
| proven code | 806,040 bytes (42.2%) |
| non-code (rodata/data/padding) | 1,103,592 bytes |
| functions discovered | 1,280 |
| - found via direct calls | 783 |
| - found via prologue scan | 235 |
| functions using VU0 macro mode | 26 |
| strings recovered | 2,868 |
| address cross-references | 1,456 |
| pointer tables (vtables/jump tables) | 7 |

## Progress by tier

| tier | functions | share | code bytes | share |
| --- | ---: | ---: | ---: | ---: |
| `undecompiled` | 0 | 0.00% | 0 | 0.00% |
| `analyzed` | 0 | 0.00% | 0 | 0.00% |
| `reconstructed` | 228 | 17.81% | 462,404 | 60.30% |
| `lifted` | 693 | 54.14% | 248,124 | 32.36% |
| `verified` | 359 | 28.05% | 56,320 | 7.34% |

Tier meanings are defined in [METHODOLOGY.md](../../../engine/docs/METHODOLOGY.md).
