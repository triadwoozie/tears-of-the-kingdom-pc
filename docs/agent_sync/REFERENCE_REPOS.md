# Reference Repositories & Upstream Sources

This document contains upstream repositories, architectures, and symbol resources for reference during decompilation, HLE implementation, and verification.

## 1. Upstream Repositories

| Subdirectory | Upstream Remote URL | Description / Utility |
|---|---|---|
| `reference_repos/totk-mobile` | `https://github.com/OJZen/totk-mobile.git` | Tears of the Kingdom static recompiler implementation, runtime hooks, SEAD/NVN bridges |
| `reference_repos/SavageWindRecomp` | `https://github.com/Revivedx/SavageWindRecomp.git` | BotW / TotK AArch64 static recompilation framework and runtime patterns |
| `reference_repos/SwitchRecomp` | `https://github.com/bgyss/SwitchRecomp.git` | Nintendo Switch static binary lifter, HLE Horizon kernel, and graphics dispatch |
| `reference_repos/LASRecomp` | `https://github.com/peiikai/LASRecomp.git` | Link's Awakening static recompilation reference, asset pipelines, and audio engine |
| `reference_repos/botw` | `https://github.com/zeldaret/botw.git` | Decompilation of Breath of the Wild (v1.5.0), SEAD engine layout, actor system, havok interfaces |
| `reference_repos/gpu_reference` | `https://github.com/alula/Ryujinx.git` | Maxwell 3D / NVN command buffer parsing, ASTC decoding, Vulkan 1.3 pipeline state emulation |

---

## 2. Symbol Resolution Reference
TotK module symbols (demangled function names, SEAD class symbols, and Horizon SDK entry points) are indexed in `work/symbols.tsv` on PC-1.
A tracking branch `symbol-table` has been created or symbol metadata can be cross-referenced via `reference_repos/totk-mobile` and `reference_repos/botw`.
