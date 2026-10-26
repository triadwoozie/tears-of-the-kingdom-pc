#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git -C "$(dirname "${BASH_SOURCE[0]}")/.." rev-parse --show-toplevel)"
ULTRA="$ROOT/refs/ultratears"
OUTBOX="$ROOT/work/outbox_prs"
MANIFEST="$OUTBOX/pr_manifest.tsv"

if [[ "$(git -C "$ROOT" branch --show-current)" != main ]]; then
    printf 'error: bridge repository must be on main\n' >&2
    exit 1
fi
if [[ -n "$(git -C "$ROOT" status --porcelain)" ]]; then
    printf 'error: bridge repository must be clean before pulling\n' >&2
    exit 1
fi
git -C "$ROOT" pull --ff-only origin main

if [[ ! -d "$ULTRA/.git" ]]; then
    printf 'error: ultratears checkout not found at %s\n' "$ULTRA" >&2
    exit 1
fi
if [[ -n "$(git -C "$ULTRA" status --porcelain --untracked-files=all)" ]]; then
    printf 'error: ultratears worktree must be clean before ingestion\n' >&2
    exit 1
fi
if [[ ! -d "$OUTBOX" || ! -f "$MANIFEST" ]]; then
    printf 'error: expected patch outbox and manifest at %s\n' "$OUTBOX" >&2
    exit 1
fi

shopt -s nullglob
patches=("$OUTBOX"/*.patch)
if (( ${#patches[@]} == 0 )); then
    printf 'error: no .patch files found in %s\n' "$OUTBOX" >&2
    exit 1
fi

git -C "$ULTRA" fetch upstream dev
git -C "$ULTRA" fetch origin dev
start_branch="$(git -C "$ULTRA" branch --show-current)"
entries=0

trim() {
    local value="$1"
    value="${value#"${value%%[![:space:]]*}"}"
    value="${value%"${value##*[![:space:]]}"}"
    printf '%s' "$value"
}

while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line%$'\r'}"
    [[ -z "$(trim "$line")" || "$line" == \#* ]] && continue

    if [[ "$line" == *$'\t'* ]]; then
        IFS=$'\t' read -r branch patch_file title body_file extra <<< "$line"
    elif [[ "$line" == *'|'* ]]; then
        IFS='|' read -r branch patch_file title body_file extra <<< "$line"
        branch="$(trim "$branch")"
        patch_file="$(trim "$patch_file")"
        title="$(trim "$title")"
        body_file="$(trim "$body_file")"
    else
        printf 'error: malformed manifest row: %s\n' "$line" >&2
        exit 1
    fi

    [[ "$branch" == branch_name ]] && continue
    if [[ -n "${extra:-}" || -z "${branch:-}" || -z "${patch_file:-}" || -z "${title:-}" || -z "${body_file:-}" ]]; then
        printf 'error: expected four non-empty manifest columns: %s\n' "$line" >&2
        exit 1
    fi

    base=upstream/dev
    if [[ "$branch" == stack:* ]]; then
        branch="${branch#stack:}"
        base=origin/dev
    fi
    if ! git -C "$ULTRA" check-ref-format --branch "$branch" >/dev/null; then
        printf 'error: invalid branch name: %s\n' "$branch" >&2
        exit 1
    fi

    patch_path="$(realpath -e "$OUTBOX/$patch_file")"
    body_path="$(realpath -e "$OUTBOX/$body_file")"
    case "$patch_path" in "$OUTBOX"/*) ;; *) printf 'error: patch path escapes outbox: %s\n' "$patch_file" >&2; exit 1 ;; esac
    case "$body_path" in "$OUTBOX"/*) ;; *) printf 'error: PR body path escapes outbox: %s\n' "$body_file" >&2; exit 1 ;; esac
    [[ "$patch_path" == *.patch && -f "$body_path" ]] || {
        printf 'error: patch must end in .patch and PR body must be a file\n' >&2
        exit 1
    }
    if git -C "$ULTRA" show-ref --verify --quiet "refs/heads/$branch" ||
       git -C "$ULTRA" show-ref --verify --quiet "refs/remotes/origin/$branch"; then
        printf 'error: branch already exists locally or on origin: %s\n' "$branch" >&2
        exit 1
    fi

    git -C "$ULTRA" checkout -b "$branch" "$base"
    git -C "$ULTRA" apply --index --whitespace=error-all "$patch_path"
    git -C "$ULTRA" commit -m "$title"
    git -C "$ULTRA" push -u origin "$branch"
    gh pr create --repo kuraeow/ultratears --base dev --head "triadwoozie:$branch" \
        --title "$title" --body-file "$body_path"
    entries=$((entries + 1))
done < "$MANIFEST"

if (( entries == 0 )); then
    printf 'error: manifest contains no PR entries\n' >&2
    exit 1
fi

git -C "$ULTRA" checkout "$start_branch"
printf 'Ingested %d PC-2 patch(es).\n' "$entries"
