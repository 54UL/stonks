-- Add enabled flag: disabled strategies are not monitored but still shown
ALTER TABLE strategies ADD COLUMN enabled INTEGER NOT NULL DEFAULT 1;
