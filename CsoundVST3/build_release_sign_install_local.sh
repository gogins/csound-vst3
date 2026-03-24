#!/usr/bin/env bash

set -euo pipefail

script_dir()
{
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd
}

fail()
{
    echo "error: $*" >&2
    exit 1
}

require_command()
{
    command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"
}

require_path()
{
    local path="$1"
    [[ -e "$path" ]] || fail "required path not found: $path"
}

log()
{
    printf '\n==> %s\n' "$*"
}

sign_bundle()
{
    local bundle_path="$1"

    if [[ -e "$bundle_path" ]]
    then
        log "signing $(basename "$bundle_path")"
        codesign \
            --force \
            --deep \
            --strict \
            --options runtime \
            --timestamp \
            --sign "$sign_identity" \
            "$bundle_path"

        codesign \
            --verify \
            --deep \
            --strict \
            --verbose=2 \
            "$bundle_path"
    fi
}

copy_if_present()
{
    local src="$1"
    local dst_dir="$2"

    if [[ -e "$src" ]]
    then
        mkdir -p "$dst_dir"
        rm -rf "$dst_dir/$(basename "$src")"
        cp -R "$src" "$dst_dir/"
    fi
}

main()
{
    require_command cmake
    require_command codesign
    require_command security

    local project_root
    project_root="$(script_dir)"

    local build_dir="${BUILD_DIR:-$project_root/build}"
    local config="${CONFIG:-Release}"
    sign_identity="${SIGN_IDENTITY:-Developer ID Application: Michael Gogins (9UX792D3V9)}"
    local csound_framework="${CSOUND_FRAMEWORK_PATH:-/Library/Frameworks/CsoundLib64.framework}"
    local stage_dir="${STAGE_DIR:-$build_dir/dist/stage}"

    local vst3_stage="$stage_dir/Library/Audio/Plug-Ins/VST3/CsoundVST3.vst3"
    local au_stage="$stage_dir/Library/Audio/Plug-Ins/Components/CsoundVST3.component"
    local app_stage="$stage_dir/Applications/CsoundVST3.app"

    local user_vst3_dir="$HOME/Library/Audio/Plug-Ins/VST3"
    local user_au_dir="$HOME/Library/Audio/Plug-Ins/Components"
    local user_app_dir="$HOME/Applications"

    require_path "$project_root/CMakeLists.txt"
    require_path "$csound_framework"

    if ! security find-identity -v -p codesigning | grep -F "$sign_identity" >/dev/null 2>&1
    then
        fail "codesigning identity not found in keychain: $sign_identity"
    fi

    log "configuring"
    cmake -S "$project_root" -B "$build_dir" -DCMAKE_BUILD_TYPE="$config"

    log "building"
    cmake --build "$build_dir" --config "$config"

    log "staging install"
    rm -rf "$stage_dir"
    cmake --install "$build_dir" --config "$config" --prefix "$stage_dir"

    log "signing staged bundles"
    sign_bundle "$vst3_stage"
    sign_bundle "$au_stage"
    sign_bundle "$app_stage"

    log "installing locally"
    copy_if_present "$vst3_stage" "$user_vst3_dir"
    copy_if_present "$au_stage" "$user_au_dir"
    copy_if_present "$app_stage" "$user_app_dir"

    log "done"
    echo "staged artifacts:"
    echo "  $stage_dir"
    echo "installed locally to:"
    echo "  $user_vst3_dir"
    echo "  $user_au_dir"
    echo "  $user_app_dir"
}

main "$@"
