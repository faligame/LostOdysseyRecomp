#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Native link architecture follows PS5_Vulkan/tools/build-radv-title.sh
# (Mihawk-99, GPL-3.0-or-later); use its pinned recipe rather than Linux ld flags.
set -euo pipefail
work=$1 vulkan=$2 sdk=$3
shift 3
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$here/dependencies.env"
[[ $(git -C "$vulkan" rev-parse HEAD) == "$PS5_VULKAN_REV" ]]
[[ $(cat "$sdk/.ps5-sdk-revision") == "$PS5_SDK_REV" ]]
archive="$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
grep -qx "sdk: $PS5_DRIVER_SDK_REV" "$vulkan/.deps/native/radv-release/PROVENANCE.txt"
grep -qx "revision: $PS5_MESA_REV" "$vulkan/.deps/native/radv-release/PROVENANCE.txt"
tool="$vulkan/build/runtime-shim/ps5-native-tool"
native="$vulkan/tooling/native"
mkdir -p "$work/obj" "$work/stubs" "$work/title/sce_sys" "$work/title/sce_module"
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" "$@"; }
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -c "$native/app_crt.cpp" -o "$work/obj/crt.o"
for pair in 'libSceAgc agc_canary_link_stub.c' 'libSceAgcDriver agc_driver_canary_link_stub.c'; do
    read -r name source <<< "$pair"
    cc -std=c11 -O2 -fPIC -c "$vulkan/vendor/ps5/sdk/stubs/$source" -o "$work/obj/$name.o"
    "$sdk/bin/prospero-lld" --shared -soname "$name.prx" -o "$work/stubs/$name.so" "$work/obj/$name.o"
done
source "$vulkan/tools/radv-link.sh"
radv_link_recipe "$vulkan" "$sdk" "$archive"
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL -e _start \
    -o "$work/llvm-pie.elf" "$work/obj/crt.o" "$@" \
    "$work/stubs/libSceAgc.so" "$work/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" --as-needed "$sdk"/target/lib/*.so
# Detect imports only satisfied by modules native titles cannot load.
missing=$(comm -23 \
    <("$sdk/bin/llvm-nm" -D --undefined-only "$work/llvm-pie.elf" | awk '$1 == "U" {sub(/@.*/, "", $2); print $2}' | sort -u) \
    <(for lib in "$sdk"/target/lib/*.so "$work"/stubs/*.so; do
        case ${lib##*/} in libkernel_sys.so|libScePosixForWebKit.so) continue;; esac
        "$sdk/bin/llvm-nm" -D --defined-only "$lib" | awk '{print $NF}'
    done | sort -u))
[[ -z $missing ]] || { echo "Unavailable native-title imports: $missing" >&2; exit 1; }
"$tool" link --in "$work/llvm-pie.elf" --out "$work/eboot.elf.new" \
    --stub-dir "$sdk/target/lib" --stub "$work/stubs/libSceAgc.so" --stub "$work/stubs/libSceAgcDriver.so" \
    --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
"$tool" self --sign --in "$work/eboot.elf.new" --out "$work/title/eboot.bin" --magic 0x1D3D154F
"$tool" self --inspect --file "$work/title/eboot.bin" > "$work/self-inspect.txt"
(cd "$vulkan/runtime" && sha256sum --check --strict libc.prx.sha256)
cp "$vulkan/runtime/libc.prx" "$work/title/sce_module/libc.prx"
cp "$here/param.json" "$work/title/sce_sys/param.json"
# Home-screen installers (e.g. ShadowMount) refuse a title without its icon.
cp "$here/icon0.png" "$work/title/sce_sys/icon0.png"
cp "$here/dependencies.env" "$vulkan/.deps/native/radv-release/PROVENANCE.txt" "$work/title/"
cp "$here/README.md" "$work/title/README.txt"
mkdir -p "$work/title/licenses"
cp "$here/../../LICENSE" "$work/title/licenses/LostOdysseyRecomp-GPL-3.0.txt"
cp "$vulkan/LICENSE" "$vulkan/NOTICE.md" "$work/title/licenses/"
# No binary release is published by this target; see redistribution requirements in README.
mv "$work/eboot.elf.new" "$work/eboot.elf"
