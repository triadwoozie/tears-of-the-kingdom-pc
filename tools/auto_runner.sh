#!/usr/bin/env bash

set -u

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root" || exit 1

log() {
    printf '[%s] %s\n' "$(date --iso-8601=seconds)" "$*"
}

while true; do
    if ! git fetch origin; then
        log "git fetch failed; retrying in 30 seconds"
        sleep 30
        continue
    fi

    remote_head="$(git rev-parse --verify refs/remotes/origin/main 2>/dev/null)" || {
        log "origin/main is unavailable; retrying in 30 seconds"
        sleep 30
        continue
    }
    local_head="$(git rev-parse HEAD)"

    if [[ "$remote_head" != "$local_head" ]]; then
        log "Remote main changed ($local_head -> $remote_head); rebasing"
        if ! git pull --rebase origin main; then
            log "rebase failed; stopping runner for manual resolution"
            exit 1
        fi

        if ninja -C build -j 2 && ctest --test-dir build -j 2 --output-on-failure; then
            boot_status=0
            timeout 25s ./build/totk_recomp --boot-real > work/boot_trace_latest.log 2>&1 || boot_status=$?

            {
                printf 'Remote commit: %s\n' "$remote_head"
                printf 'Boot exit status: %s\n' "$boot_status"
                grep -E 'Result: 0x|VAbortImpl|cmd=|nvn|MainThread' work/boot_trace_latest.log \
                    | tail -n 20 || true
            } > docs/agent_sync/LATEST_BOOT_STATUS.txt

            if git add docs/agent_sync/LATEST_BOOT_STATUS.txt &&
                git commit -m "telemetry: automated boot check for remote commit"; then
                if ! git push origin main; then
                    log "telemetry push failed; it will be retried after the next fetch"
                fi
            else
                log "telemetry commit failed"
            fi
        else
            log "build or tests failed; skipping boot and telemetry push"
        fi
    fi

    sleep 30
done
