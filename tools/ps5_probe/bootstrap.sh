#!/usr/bin/env bash
# Fetch/build public dependencies only. Never connects to a console.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$here/dependencies.env"
deps=${LO_PS5_DEPS:-"$here/../../build/ps5-deps"}
mkdir -p "$deps"
deps=$(cd "$deps" && pwd)
checkout() {
    local name=$1 revision=$2 dir="$deps/$1"
    if [[ ! -d $dir ]]; then
        git clone --no-checkout "https://github.com/mihawk-99/$name.git" "$dir"
        git -C "$dir" checkout --detach "$revision"
    fi
    [[ $(git -C "$dir" rev-parse HEAD) == "$revision" ]] || {
        echo "$dir must be at $revision; existing checkout left untouched" >&2; exit 2;
    }
    [[ -z $(git -C "$dir" status --porcelain --untracked-files=no) ]] || {
        echo "$dir has tracked changes; existing work left untouched" >&2; exit 2;
    }
}
checkout PS5_Vulkan "$PS5_VULKAN_REV"
checkout PS5_Mesa "$PS5_MESA_REV"
checkout PS5_PayloadSDK "$PS5_SDK_REV"
export PS5_MESA_FORK="$deps/PS5_Mesa"
export PS5_PAYLOAD_SDK_FORK="$deps/PS5_PayloadSDK"
cd "$deps/PS5_Vulkan"
bash tools/setup-native-dependencies.sh
bash tools/rebuild-libc.sh
bash tools/build-radv.sh release
printf 'PS5_PAYLOAD_SDK=%s\nPS5_VULKAN_DIR=%s\n' "$PWD/.deps/native/ps5-payload-sdk" "$PWD"
