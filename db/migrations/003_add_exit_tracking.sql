-- Add exit price and closed P/L tracking for closed positions
ALTER TABLE strategies ADD COLUMN exit_price REAL    NOT NULL DEFAULT 0;
ALTER TABLE strategies ADD COLUMN closed_pnl REAL    NOT NULL DEFAULT 0;
