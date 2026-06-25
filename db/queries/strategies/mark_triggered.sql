-- Mark a strategy as triggered (TP hit, SL hit, or cancelled).
-- Params: ?1=status, ?2=triggered_at, ?3=exit_price, ?4=closed_pnl, ?5=id
UPDATE strategies
SET status=?, triggered_at=?, exit_price=?, closed_pnl=?
WHERE id=?;
