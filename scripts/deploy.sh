#!/usr/bin/env bash
#
# Deploy the latest commit and prove the service still works, rolling back
# automatically if it does not.
#
#   ./scripts/deploy.sh
#
# Everything is overridable from the environment:
#   REPO_DIR    checkout to deploy from   (default: the repo this script is in)
#   SERVICE     systemd unit to restart   (default: zathas)
#   HEALTH_URL  endpoint that must answer (default: http://127.0.0.1:8080/api/health)
#   JOBS        build parallelism         (default: 1, for small boxes)
#   KEEP        binary backups to retain  (default: 5)
#
# Rolling back restores the previous binary and restarts, so a bad build costs
# one restart rather than an outage.

set -euo pipefail

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="${REPO_DIR:-$(cd "$SELF_DIR/.." && pwd)}"
SERVICE="${SERVICE:-zathas}"
HEALTH_URL="${HEALTH_URL:-http://127.0.0.1:8080/api/health}"
JOBS="${JOBS:-1}"
KEEP="${KEEP:-5}"

BIN="$REPO_DIR/build/zathas_ai"
BACKUP_DIR="$REPO_DIR/build/backups"
STAMP="$(date -u +%Y%m%d-%H%M%S)"
BACKUP="$BACKUP_DIR/zathas_ai.$STAMP"

say()  { printf '[deploy] %s\n' "$*"; }
die()  { printf '[deploy] ERROR: %s\n' "$*" >&2; exit 1; }

# Waits for the health endpoint to answer 200. Returns non-zero if it never does.
wait_healthy() {
    for _ in $(seq 1 30); do
        if curl -fsS --max-time 3 "$HEALTH_URL" >/dev/null 2>&1; then return 0; fi
        sleep 1
    done
    return 1
}

cd "$REPO_DIR"

# ── 1. Update ─────────────────────────────────────────────────────────────────
BEFORE="$(git rev-parse HEAD)"
say "Updating $REPO_DIR"
git pull --ff-only
AFTER="$(git rev-parse HEAD)"
if [ "$BEFORE" = "$AFTER" ]; then
    say "Already at $(git rev-parse --short HEAD); redeploying anyway"
else
    say "$(git rev-parse --short "$BEFORE") -> $(git rev-parse --short "$AFTER")"
fi

# ── 2. Back up the running binary ─────────────────────────────────────────────
# Moved rather than copied: the running process holds this inode, and writing
# over it during the build fails with ETXTBSY. Moving lets the old process keep
# its inode while the link name is freed for the new build.
mkdir -p "$BACKUP_DIR"
if [ -f "$BIN" ]; then
    mv "$BIN" "$BACKUP"
    say "Previous binary saved as $(basename "$BACKUP")"
else
    BACKUP=""
    say "No existing binary to back up"
fi

# ── 3. Build ──────────────────────────────────────────────────────────────────
say "Building (JOBS=$JOBS)"
if ! cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null; then
    die "cmake configure failed; service still running the previous binary"
fi
if ! cmake --build build --parallel "$JOBS"; then
    say "Build failed - restoring previous binary"
    [ -n "$BACKUP" ] && mv "$BACKUP" "$BIN"
    die "build failed; nothing was restarted"
fi

# ── 4. Restart and verify ─────────────────────────────────────────────────────
say "Restarting $SERVICE"
sudo systemctl restart "$SERVICE"

if wait_healthy; then
    say "Health check passed: $(curl -fsS --max-time 3 "$HEALTH_URL")"
else
    say "Health check FAILED - rolling back"
    if [ -n "$BACKUP" ] && [ -f "$BACKUP" ]; then
        mv "$BIN" "$BIN.failed-$STAMP" 2>/dev/null || true
        cp "$BACKUP" "$BIN"
        sudo systemctl restart "$SERVICE"
        if wait_healthy; then
            die "rolled back to $(basename "$BACKUP"); the failed build is kept as zathas_ai.failed-$STAMP"
        fi
        die "rollback ALSO failed health check - service is down, investigate now"
    fi
    die "no backup available to roll back to - service is down"
fi

# ── 5. Prune old backups ──────────────────────────────────────────────────────
# Newest first, drop everything past KEEP.
if [ -d "$BACKUP_DIR" ]; then
    ls -1t "$BACKUP_DIR"/zathas_ai.* 2>/dev/null | tail -n "+$((KEEP + 1))" | while read -r old; do
        rm -f "$old"
        say "Pruned $(basename "$old")"
    done
fi

say "Deployed $(git rev-parse --short HEAD) successfully"
