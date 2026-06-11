-- Sample strategies for testing
-- Only applied when the strategies table is empty
INSERT INTO strategies (symbol, direction, entry_price, take_profit, stop_loss, status, created_at, triggered_at, notes, type, parent_id, priority, quantity, entry_date, exit_price, closed_pnl, enabled) VALUES
('AAPL',  0, 300.23, 306.23, 294.23, 0, strftime('%s','now'), 0, 'Long 2% bands', 0, 0, 0, 10, strftime('%s','now'), 0, 0, 1),
('MSFT',  0, 421.92, 430.36, 413.48, 0, strftime('%s','now'), 0, 'Long 2% bands', 0, 0, 0, 8,  strftime('%s','now'), 0, 0, 1),
('GOOGL', 0, 396.78, 404.72, 388.84, 0, strftime('%s','now'), 0, 'Long 2% bands', 0, 0, 0, 12, strftime('%s','now'), 0, 0, 1),
('AMZN',  0, 264.14, 269.42, 258.86, 0, strftime('%s','now'), 0, 'Long 2% bands', 0, 0, 0, 10, strftime('%s','now'), 0, 0, 1),
('NVDA',  0, 225.32, 229.83, 220.81, 0, strftime('%s','now'), 0, 'Long 2% bands', 0, 0, 0, 20, strftime('%s','now'), 0, 0, 1),
('TSLA',  1, 422.24, 413.79, 430.68, 0, strftime('%s','now'), 0, 'Short 2% bands', 0, 0, 0, 8,  strftime('%s','now'), 0, 0, 1),
('META',  0, 614.23, 626.51, 601.95, 0, strftime('%s','now'), 0, 'Long 2% bands', 0, 0, 0, 5,  strftime('%s','now'), 0, 0, 1),
('SPY',   0, 739.17, 753.95, 724.39, 0, strftime('%s','now'), 0, 'Long 2% bands', 0, 0, 0, 15, strftime('%s','now'), 0, 0, 1),
('QQQ',   0, 708.93, 723.11, 694.75, 0, strftime('%s','now'), 0, 'Long 2% bands', 0, 0, 0, 12, strftime('%s','now'), 0, 0, 1),
('AMD',   0, 424.10, 432.58, 415.62, 0, strftime('%s','now'), 0, 'Long 2% bands', 0, 0, 0, 10, strftime('%s','now'), 0, 0, 1);
