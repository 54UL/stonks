-- Add strategy type, composition, and position tracking columns
ALTER TABLE strategies ADD COLUMN type       INTEGER NOT NULL DEFAULT 0;
ALTER TABLE strategies ADD COLUMN parent_id  INTEGER NOT NULL DEFAULT 0;
ALTER TABLE strategies ADD COLUMN priority   INTEGER NOT NULL DEFAULT 0;
ALTER TABLE strategies ADD COLUMN quantity   REAL    NOT NULL DEFAULT 0;
ALTER TABLE strategies ADD COLUMN entry_date INTEGER NOT NULL DEFAULT 0;
