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

staple_bundle()
{
    local bundle_path="$1"

    if [[ -e "$bundle_path" ]]
    then
        log "stapling $(basename "$bundle_path")"
        xcrun stapler staple "$bundle_path"
        xcrun stapler validate "$bundle_path"
    fi
}

main()
{
    require_command xcrun
    require_command ditto
    require_command cp

    local project_root
    project_root="$(script_dir)"

    local build_dir="${BUILD_DIR:-$project_root/build}"
    local stage_dir="${STAGE_DIR:-$build_dir/dist/stage}"
    local dist_dir="${DIST_DIR:-$build_dir/dist}"
    local submit_dir="${SUBMIT_DIR:-$dist_dir/notary-submit}"
    local package_name="${PACKAGE_NAME:-CsoundVST3-macOS}"
    local archive_path="$dist_dir/$package_name.zip"
    local notary_profile="${NOTARY_PROFILE:-csound-vst3-notary-profile}"
    local vst3_stage="$stage_dir/Library/Audio/Plug-Ins/VST3/CsoundVST3.vst3"
    local au_stage="$stage_dir/Library/Audio/Plug-Ins/Components/CsoundVST3.component"
    local app_stage="$stage_dir/Applications/CsoundVST3.app"

    local vst3_submit="$submit_dir/Library/Audio/Plug-Ins/VST3/CsoundVST3.vst3"
    local au_submit="$submit_dir/Library/Audio/Plug-Ins/Components/CsoundVST3.component"
    local app_submit="$submit_dir/Applications/CsoundVST3.app"

    require_path "$stage_dir"

    if ! xcrun notarytool history --keychain-profile "$notary_profile" >/dev/null 2>&1
    then
        fail "notarytool keychain profile not found or not usable: $notary_profile"
    fi

    if [[ ! -e "$vst3_stage" && ! -e "$au_stage" && ! -e "$app_stage" ]]
    then
        fail "no staged artifacts found under $stage_dir"
    fi

    log "preparing clean notarization payload"
    rm -rf "$submit_dir"
    mkdir -p "$(dirname "$vst3_submit")" "$(dirname "$au_submit")" "$(dirname "$app_submit")"

    if [[ -e "$vst3_stage" ]]
    then
        cp -R "$vst3_stage" "$vst3_submit"
    fi

    if [[ -e "$au_stage" ]]
    then
        cp -R "$au_stage" "$au_submit"
    fi

    if [[ -e "$app_stage" ]]
    then
        cp -R "$app_stage" "$app_submit"
    fi

    log "creating notarization archive"
    mkdir -p "$dist_dir"
    rm -f "$archive_path"
    ditto -c -k --keepParent "$submit_dir" "$archive_path"

    log "submitting for notarization"
    xcrun notarytool submit "$archive_path" \
        --keychain-profile "$notary_profile" \
        --wait

    log "stapling accepted ticket"
    staple_bundle "$vst3_stage"
    staple_bundle "$au_stage"
    staple_bundle "$app_stage"

    log "done"
    echo "notarized archive:"
    echo "  $archive_path"
    echo "stapled artifacts remain in:"
    echo "  $stage_dir"
}

main "$@"
