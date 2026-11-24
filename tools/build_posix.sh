#!/usr/bin/env bash
set -eo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
mode="${1:?usage: build_posix.sh setup|firmware|apps [arguments]}"
shift
sdk="$root/SiFli-SDK"
export UV_EXCLUDE_NEWER=false

if [[ "$mode" == setup ]]; then
    git submodule update --init --recursive
    bash "$sdk/install.sh"
fi

expected="$(git rev-parse :SiFli-SDK)"
actual="$(git -C "$sdk" rev-parse HEAD)"
if [[ "$actual" != "$expected" ]]; then
    echo "SiFli SDK at $sdk is $actual, expected $expected. Run just setup." >&2
    exit 1
fi

activate_sdk() {
    # shellcheck source=/dev/null
    source "$sdk/export.sh"
}
activate_sdk

case "$mode" in
    setup) uv run --no-project --python "$(command -v python)" python tools/prepare_build.py ;;
    firmware) scons -C project --board=sf32lb57-epd-gyd7n6_hcpu --board_search_path=../boards -j8 "$@" ;;
    apps) selection="${1:?usage: just apps ID[,ID...]}"
          shift
          scons -C modules "APPS=$selection" -j8 "$@" ;;
    *) echo "Unknown build mode: $mode" >&2; exit 1 ;;
esac
