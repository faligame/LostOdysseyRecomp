# PS5 first experiment — Vulkan only

This branch builds a **no-game-data native PS5 diagnostic title**, not a playable
Lost Odyssey port. No console execution has been performed. Target hardware for
follow-up is a standard PS5 on firmware 10.01 with an existing homebrew loader.

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
export PS5_PAYLOAD_SDK="$PS5_VULKAN_DIR/.deps/native/ps5-payload-sdk"
cmake -S . -B build/ps5 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ps5.cmake \
  -DLO_BUILD_PS5_PROBE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/ps5 --target LoPS5Probe
```

Output: `build/ps5/ps5-probe/title/` (`eboot.bin`, `sce_sys/param.json`,
`sce_module/libc.prx`). The intermediate ELF and SELF inspection report stay
beside it. Local experimental title ID: `PPSA99171`; check for a collision on
your console before installation. Build scripts never deploy or contact a console.

Dependencies are pinned in `dependencies.env`:

| Component | Revision / version | License |
| --- | --- | --- |
| [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) | `f410e67a0241b3e8edcdbcd2a9b285dfc4a412ed` | GPL-3.0-or-later; see its NOTICE.md |
| [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) | `1ba9874f478657ccf6da72ce55f0260164cc0bd0` (26.2.0 fork) | Mesa component licenses, predominantly MIT; retain docs/license.rst and source notices |
| [PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) | `b83202be73e930050e00de0f1b4d9ec46c0391bf` | GPL-3.0-or-later; bundled LLVM runtimes Apache-2.0 WITH LLVM-exception; dlmalloc MIT |
| [Public SDK release](https://github.com/ps5-payload-dev/sdk/releases/tag/v0.42) | v0.42, SHA-256 `8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da` | See SDK and runtime notices |
| zlib (host packaging tool) | 1.3.2, SHA-256 `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16` | zlib |
| [VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate) reference | `91179df760df1e6c6f2d4d172bc435319d79ff3c` | MIT foundation; not linked |

The pinned Mesa cross build uses its pinned zlib Meson wrap. `PROVENANCE.txt`
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

**The current E alias is a known blocker:** the production layout maps E at a
4 KiB offset into A/C's physical backing. The SDK's PS5 page size is 16 KiB.
This probe reports `alias_E=UNSUPPORTED`, and the memory result remains BLOCKED.
Rounding that offset or copying E into independent storage would break direct
PPC `base + address` reads/writes. No such workaround is implemented. Full
backing allocation, null-page protection, and alias/protection behavior need
further platform work without rewriting PPC semantics.

The Vulkan half independently requests Vulkan 1.2 (portable shader pack target),
reports geometry/blending/descriptor-indexing/scalar-layout/BDA support and GPU
heaps, creates a display surface and an SDR FIFO swapchain, and clears/presents
90 blue/green frames. It prefers a reported 1280x720 mode; if unavailable it
reports and uses the display's mode. Delays limit this diagnostic to at most
30 FPS; this is neither a performance measurement nor a 720p game renderer.
It then requests shell exit. Shader compilation and game rendering are not
exercised. A successful Vulkan result does not override the memory blocker.

## Remaining game-port work

- Preserve the shared `kernel/guest_address_space_layout.h` contract, resolve E
  on 16 KiB pages, then implement production PS5 reservation/direct-memory and
  large-heap management. No PPC instruction semantics changed.
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

First milestone: all three probe translation units cross-compiled with public
SDK v0.42 / fork b83202b and Debian Clang 19.1.7 using `-std=c++20 -Wall
-Wextra -Wpedantic -Wno-missing-field-initializers`. Shell syntax and
`git diff --check` passed. RADV/native title linking is still in progress at
this milestone. No hardware result is implied by a host build or ELF inspection.
