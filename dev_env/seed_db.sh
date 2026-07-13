#!/usr/bin/env bash
# Apply migrations and optionally seed a SQLite database.
# Usage: ./dev_env/seed_db.sh [db_path] [--no-seed]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MIGRATIONS_DIR="$PROJECT_ROOT/db/migrations"
SEEDS_DIR="$PROJECT_ROOT/db/seeds"

DB_PATH="${1:-app/strategies.db}"
SKIP_SEED="${2:-}"

# Resolve relative paths from project root
if [[ "$DB_PATH" != /* ]]; then
    DB_PATH="$PROJECT_ROOT/$DB_PATH"
fi

# Ensure parent directory exists
mkdir -p "$(dirname "$DB_PATH")"

echo "[seed_db] Database: $DB_PATH"
echo "[seed_db] Migrations: $MIGRATIONS_DIR"

# Check sqlite3 is available
if ! command -v sqlite3 &>/dev/null; then
    echo "ERROR: sqlite3 not found. Install it first."
    echo "  Ubuntu/Debian: sudo apt install sqlite3"
    echo "  macOS:         brew install sqlite3"
    echo "  Windows:       choco install sqlite"
    exit 1
fi

# Create schema_migrations tracking table
sqlite3 "$DB_PATH" <<'SQL'
CREATE TABLE IF NOT EXISTS schema_migrations (
    version    INTEGER PRIMARY KEY,
    name       TEXT    NOT NULL,
    applied_at INTEGER NOT NULL
);
SQL

# Apply each migration in order
applied=0
for f in "$MIGRATIONS_DIR"/*.sql; do
    [ -f "$f" ] || continue
    fname="$(basename "$f")"

    # Extract version number from filename (e.g. 001_create_strategies.sql -> 1)
    ver=$(echo "$fname" | grep -oE '^[0-9]+' | sed 's/^0*//')
    [ -z "$ver" ] && continue

    # Check if already applied
    existing=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM schema_migrations WHERE version=$ver;")
    if [ "$existing" -gt 0 ]; then
        echo "[seed_db] Skip $fname (already applied)"
        continue
    fi

    echo "[seed_db] Applying $fname ..."
    if sqlite3 "$DB_PATH" < "$f"; then
        sqlite3 "$DB_PATH" "INSERT INTO schema_migrations (version, name, applied_at) VALUES ($ver, '$fname', $(date +%s));"
        applied=$((applied + 1))
        echo "[seed_db] OK"
    else
        echo "[seed_db] FAILED: $fname"
        exit 1
    fi
done

echo "[seed_db] $applied migration(s) applied."

# Apply seeds (only if strategies table is empty)
if [ "$SKIP_SEED" != "--no-seed" ]; then
    count=$(sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM strategies;" 2>/dev/null || echo "0")
    if [ "$count" -eq 0 ]; then
        for f in "$SEEDS_DIR"/*.sql; do
            [ -f "$f" ] || continue
            fname="$(basename "$f")"
            echo "[seed_db] Seeding $fname ..."
            sqlite3 "$DB_PATH" < "$f"
            echo "[seed_db] Seeded $fname"
        done
    else
        echo "[seed_db] Strategies table not empty ($count rows), skipping seeds."
    fi
fi

echo "[seed_db] Done. Current schema version:"
sqlite3 "$DB_PATH" "SELECT MAX(version) FROM schema_migrations;"
