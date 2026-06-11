-- Create the core strategies table
CREATE TABLE IF NOT EXISTS strategies (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    symbol       TEXT    NOT NULL,
    direction    INTEGER NOT NULL DEFAULT 0,
    entry_price  REAL    NOT NULL,
    take_profit  REAL    NOT NULL,
    stop_loss    REAL    NOT NULL,
    status       INTEGER NOT NULL DEFAULT 0,
    created_at   INTEGER NOT NULL,
    triggered_at INTEGER NOT NULL DEFAULT 0,
    notes        TEXT    NOT NULL DEFAULT ''
);
