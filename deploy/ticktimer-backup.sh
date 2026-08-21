#!/bin/sh
# TickTimer data backup -- one dated tarball of the server's data directory.
#
# WHAT THIS PROTECTS, AND WHAT IT DOES NOT: this writes to the SAME MACHINE.
# It protects against the mistakes that actually happen -- a bad edit to
# accounts.json, a wrong rm, a store that wrote something broken -- and it is
# useless against losing the machine. The off-box copy is the second half and
# it is a separate step; see docs/ROLLOUT.md 2g. A backup that only exists on
# the thing it is backing up is a rehearsal, not insurance.
#
# It is also the migration. Moving to a Raspberry Pi is "restore last night's
# tarball onto the new box", which means the path gets walked every night for
# months before it is ever walked in anger.
#
# Install:
#   install -m 755 deploy/ticktimer-backup.sh /usr/local/bin/ticktimer-backup
#
# Run by ticktimer-backup.timer; see ticktimer-backup.service.example.

set -eu

DATA_DIR="${TICKTIMER_DATA:-/var/lib/ticktimer}"
DEST_DIR="${TICKTIMER_BACKUP_DIR:-/var/backups/ticktimer}"
KEEP_DAYS="${TICKTIMER_BACKUP_KEEP:-30}"

if [ ! -d "$DATA_DIR" ]; then
    echo "ticktimer-backup: no data directory at $DATA_DIR" >&2
    exit 1
fi

mkdir -p "$DEST_DIR"

STAMP="$(date -u +%Y-%m-%dT%H%M%SZ)"
OUT="$DEST_DIR/ticktimer-$STAMP.tar.gz"

# WRITE THEN RENAME, the same discipline QSaveFile uses in the C++ stores and
# for the same reason. A backup killed halfway must not be sitting there under
# a name that says it finished -- the worst possible outcome is a file that
# looks like a backup and is half a file. Only a completed tar earns the name.
tar -czf "$OUT.partial" -C "$(dirname "$DATA_DIR")" "$(basename "$DATA_DIR")"

# Prove it is readable BEFORE it is allowed to count as a backup. tar happily
# writes an archive it cannot later read if the disk filled mid-write.
tar -tzf "$OUT.partial" >/dev/null

mv "$OUT.partial" "$OUT"

# The server writes accounts.json and devices.json non-atomically (plain
# QFile, truncate-on-open), so a tarball taken during one of those writes can
# capture a truncated file. Rare -- those writes happen on registration and
# login, and take microseconds -- but the consequence is a backup that
# restores an empty account list, which is exactly the failure a backup is
# supposed to prevent. Recorded here so the next person knows the risk is
# understood rather than unnoticed; the real fix is QSaveFile in the stores.
if [ ! -s "$DATA_DIR/accounts.json" ]; then
    echo "ticktimer-backup: WARNING accounts.json is empty or missing" >&2
fi

# Prune old dailies. Keeping a month of 80 KB files costs nothing; the point
# of a limit is that an unbounded backup directory eventually fills the disk
# and takes the SERVER down, turning a safety net into an outage.
find "$DEST_DIR" -name 'ticktimer-*.tar.gz' -type f -mtime "+$KEEP_DAYS" -delete

# Sweep aborted runs so a crash loop cannot fill the disk with .partial files.
find "$DEST_DIR" -name 'ticktimer-*.tar.gz.partial' -type f -mtime +1 -delete

COUNT="$(find "$DEST_DIR" -name 'ticktimer-*.tar.gz' -type f | wc -l)"
SIZE="$(du -h "$OUT" | cut -f1)"
echo "ticktimer-backup: wrote $OUT ($SIZE); $COUNT kept in $DEST_DIR"
