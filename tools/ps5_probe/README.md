# PS5 first experiment — Vulkan only

This branch builds a **no-game-data native PS5 diagnostic title**, not a playable
Lost Odyssey port. It ran once on a retail PS5 on firmware 10.01 on 2026-10-17
and both checks passed (see "Hardware result" at the end).

The probe cross-compiles for `x86_64-sie-ps5` with the public payload SDK,
links Mesa/RADV statically via `vk_icdGetInstanceProcAddr`, and uses
`VK_KHR_display` / VideoOut. It does not link a Linux Vulkan loader, DXC, SDL,
D3D12, upscalers, frame generation, HDR, updater or mod tools. The full runtime
is deliberately rejected at configure time on PS5 until its platform work is done.

## Build

Linux host prerequisites: Clang/LLVM/lld 19 (including compiler-rt builtins),
CMake >= 3.28, Ninja, Meson, Python with Mako and PyYAML, pkg-config, rsync,
Bison, Flex, git, make, wget, unzip. Mesa's host tools also need LLVM/Clang
19 development files, SPIRV-LLVM-Translator 19, SPIRV-Tools >= 2024.1 and libclc.
These are host tools; none is a proprietary console SDK.

```sh
# This fetches exact public revisions into build/ps5-deps by default.
# LO_PS5_DEPS may instead point outside this repository.
bash tools/ps5_probe/bootstrap.sh
export PS5_VULKAN_DIR="$PWD/build/ps5-deps/PS5_Vulkan"
export PS5_PAYLOAD_SDK="$PWD/build/ps5-deps/lo-sdk"
cmake -S . -B build/ps5 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ps5.cmake \
  -DLO_BUILD_PS5_PROBE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/ps5 --target LoPS5Probe
```

Output: `build/ps5/ps5-probe/title/` (`eboot.bin`, `sce_sys/param.json`, `sce_sys/icon0.png`,
`sce_module/libc.prx`). The intermediate ELF and SELF inspection report stay
beside it. Local experimental title ID: `PPSA99171`; check for a collision on
your console before installation. Build scripts never deploy or contact a console.

Dependencies are pinned in `dependencies.env`:

| Component | Revision / version | License |
| --- | --- | --- |
| [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) | `f410e67a0241b3e8edcdbcd2a9b285dfc4a412ed` | GPL-3.0-or-later; see its NOTICE.md |
| [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) | `1ba9874f478657ccf6da72ce55f0260164cc0bd0` (26.2.0 fork) | Mesa component licenses, predominantly MIT; retain docs/license.rst and source notices |
| [PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) title | `b5efad528e0ac1b8289f39d72a0e890ff68ff0a6` | GPL-3.0-or-later; bundled LLVM runtimes Apache-2.0 WITH LLVM-exception; dlmalloc MIT |
| Driver SDK platform layer | `b83202be73e930050e00de0f1b4d9ec46c0391bf` | Same SDK licenses |
| [Public SDK release](https://github.com/ps5-payload-dev/sdk/releases/tag/v0.42) | v0.42, SHA-256 `8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da` | See SDK and runtime notices |
| zlib (host packaging tool) | 1.3.2, SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16` | zlib |
| [VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate) reference | `91179df760df1e6c6f2d4d172bc435319d79ff3c` | MIT foundation; not linked |

The pinned Mesa cross build uses zlib 1.3.1, wrapdb `1.3.1-1` (source SHA-256
`9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23`,
patch SHA-256 `e79b98eb24a75392009cec6f99ca5cdca9881ff20bfa174e8b8926d5c7a47095`).
The driver and title have separate SDK installs, as in VulkanTemplate. The
newer title SDK provides native-safe `mkstemp` and `readlink`; linking the title
against the driver's older platform layer failed the unavailable-import check. `PROVENANCE.txt`
records the resulting RADV archive hash and SDK revision. The link follows the
GPL PS5_Vulkan recipe (including whole-archive dispatch, direct-memory heap,
pthread wrappers and libc substitutes). `param.json` and shell-exit behavior
were informed by the MIT VulkanTemplate. Its UI/assets are not included.
The clean-room libc is built from PS5_Vulkan sources, not copied from a console.
The repository itself is GPL-3.0. Only source changes are pushed; before any
binary redistribution, stage complete corresponding source and notices for all
linked components, including SDK runtime and Mesa subprojects. The local folder
is an experimental build artifact, not a prepared public binary release.

## What to observe on hardware

Launch the local title using your existing homebrew workflow and capture klog
messages prefixed `[LO PS5]`. It should reserve a 4 GiB guest VA window, map one
64 KiB direct-memory allocation at A and C, and check bidirectional writes. It
reports the actual base and cleanup errors. It does not overwrite unowned VA.

**The E alias stays unmapped, as on macOS.** The production layout puts E at a
4 KiB offset into A/C's physical backing, which 16 KiB pages cannot express.
Apple Silicon has the same 16 KiB pages, and the macOS backend
(`kernel/guest_address_space_macos.cpp`) leaves E reserved but unmapped: the
runtime never hands out E addresses and `MmGetPhysicalAddress` applies the
shift arithmetically, so only a stray guest access to E would fault. The game
has shipped and been played that way on macOS since v0.8.0. This probe reports
`alias_E=UNMAPPED (as on macOS)`, and the memory result is PASS when the A/C
alias works and cleanup succeeds. Full backing allocation, null-page protection
and large heaps still need platform work, without changing PPC semantics.

The Vulkan half independently requests Vulkan 1.2 (portable shader pack target),
reports geometry/blending/descriptor-indexing/scalar-layout/BDA support and GPU
heaps, creates a display surface and an SDR FIFO swapchain, and clears/presents
90 blue/green frames. It prefers a reported 1280x720 mode; if unavailable it
reports and uses the display's mode. Delays limit this diagnostic to at most
30 FPS; this is neither a performance measurement nor a 720p game renderer.
It then requests shell exit. Shader compilation and game rendering are not
exercised.

## Remaining game-port work

- Preserve the shared `kernel/guest_address_space_layout.h` contract, leave E
  unmapped as the macOS backend does, then implement production PS5
  reservation/direct-memory and large-heap management. No PPC instruction
  semantics changed.
- Adapt plume's Vulkan procedure initialization and display surface creation
  from desktop loader/SDL WSI to this static ICD/VideoOut path. The probe uses
  raw Vulkan, **not plume**, and merely reports some of plume's queried features.
- Reuse the existing portable SPIR-V pack for guest shaders. That pack alone is
  insufficient: `renderer.cpp` generates transfer/blit/scene-copy/rect-list,
  specialization and movie-YUV shaders; `presentation.cpp` generates present,
  UI and overlay shaders; temporal AA and ambient occlusion also use DXC.
  Export/version these public utility variants on a host and audit remaining
  dynamic variants/misses before enabling a no-DXC runtime. Do not silently
  skip required shaders; desktop `libdxcompiler.so` is not a PS5 library.
- Implement a small pad/audio/display platform layer or a verified SDL3 port;
  basic input, audio, save/load and clean shutdown are not yet integrated.
  Keep original assets, SDR and 720p/30 as the initial gameplay target.
- Plan read-only assets under `/app0`, and explicit writable save/cache locations
  under a title-specific `/data` directory only after verifying loader access.
  The probe neither requests privilege changes nor writes to those paths.
- Audit remaining POSIX/libc, pthread, filesystem and timing assumptions. The
  driver recipe handles its known gaps, not all of the game runtime's.
- The saved workspace has no private XEX or generated PPC sources (only
  `.gitkeep` in `LostOdysseyRecompLib/private` and `ppc`). Full game linking is
  therefore also blocked on locally supplied private inputs. Never commit them.

## Build evidence

Validated in the saved Linux workspace on 2026-10-17, starting from main
`aa114c1d4513a4f21762f02bab11ef3a7a39f4fa`:

- `LO_PS5_DEPS=/workspace/ps5-deps bash tools/ps5_probe/bootstrap.sh`: PASS.
  Public SDK/platform, clean-room libc and Mesa/RADV release archive built.
- `cmake -S . -B build/ps5 -G Ninja
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ps5.cmake
  -DLO_BUILD_PS5_PROBE=ON -DCMAKE_BUILD_TYPE=Release`: PASS, `ps5-x86_64`.
  Set `PS5_VULKAN_DIR=/workspace/ps5-deps/PS5_Vulkan` and
  `PS5_PAYLOAD_SDK=/workspace/ps5-deps/lo-sdk` in this saved workspace.
- `cmake --build build/ps5 --target LoPS5Probe`: PASS, full target compile,
  static RADV link, import check, native ELF conversion, fake SELF generation.
  C++ module scanning is disabled for these non-module sources because the
  SDK wrapper has no `clang-scan-deps` companion.
- `ps5-native-tool self --inspect`: `integrity: valid`, 12 segments.
- ELF dynamic imports: AGC, AGCDriver, LibcInternal, SystemService, VideoOut,
  kernel only. No Linux loader, SDL, DXC or desktop Vulkan dependency.
- Negative configure checks: host toolchain + PS5 probe rejected; PS5
  toolchain without probe opt-in rejected. Both PASS.
- `bash -n tools/ps5_probe/*.sh` and `git diff --check`: PASS.

Host versions: Clang/LLVM/lld 19.1.7, CMake 3.31.6, Ninja 1.12.1, Meson 1.7.0,
Mako 1.3.9, SPIRV-LLVM-Translator 19.1.0, SPIRV-Tools 2025.1~rc1. System
installation was unavailable; Debian packages were downloaded/extracted under
`/workspace/ps5-host`, with environment setup in `/workspace/ps5-host/env.sh`.
Source that file before rebuilding in this saved workspace.

Local build checksums (not a promise of reproducibility across host toolchains):

| Artifact | SHA-256 |
| --- | --- |
| `eboot.bin` (24,406,871 bytes) | `46d6c8a1485d648dd311e77663ec9145bb97badd062e9ae84fcbb89cf38a9cdd` |
| clean-room `libc.prx` | `e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036` |
| RADV archive | `b342a324d29563da3fde3db15c8664312b88cb19b623eb7a142d5880db4ec48b` |

## Hardware result (2026-10-17)

One run of `c99e32b4` (same `eboot.bin` as `dd940208`, SHA-256 `530660220c56…`,
built in WSL2 Manjaro with Arch's LLVM 19 packages) on a retail PS5, firmware
10.01, etaHEN with kstuff. The title folder went to `/mnt/usb0/homebrew/` over
FTP; ShadowMount+ v1.6beta16 registered it (it needs `sce_sys/icon0.png`) and it
was launched from the home screen. klog came from ps5debug on TCP 3232:

```text
memory.page=16384 direct_alignment=65536 E_shift=4096
memory.reserve4GiB=0x0 address=100000000 preferred=1
memory.A_map=0x0 C_map=0x0 alias_AC=PASS
memory.alias_E=UNMAPPED (as on macOS)
memory.cleanup unmap=0x0 release=0x0
vulkan.device=PlayStation 5 GPU (RADV NAVI21) api=1.4.354
vulkan.features geometry=1 independentBlend=1 scalarLayout=1 BDA=1 runtimeArray=1 partiallyBound=1 variableDescriptors=1
vulkan.heap[0]=4294967296 flags=0x0
vulkan.heap[1]=8589934592 flags=0x1
vulkan.mode=3840x2160@59940_mHz
vulkan.mode=2560x1440@59940_mHz
vulkan.mode=1920x1080@59940_mHz
vulkan.present=PASS frames=90 SDR=3840x2160; no shader/game rendering tested
probe.memory=PASS probe.vulkan=PASS gameplay=NOT_IMPLEMENTED
exit.status=0
```

The 4 GiB guest window landed at the preferred `0x100000000`. The display
offers no 1280x720 mode, so the probe presented at 3840x2160. Mesa's only
warning was `ac_compute_device_uuid ... invalid pci bus info`. Shader
compilation and game rendering remain unexercised.
