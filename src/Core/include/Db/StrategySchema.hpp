#pragma once

// StrategySchema — single source of truth for the Strategy <-> DB mapping.
//
// Adding a new persisted field:
//   1. Add member to Strategy struct (Strategy.hpp)
//   2. Add a Col() entry below
//   3. Create a migration file: db/migrations/NNN_description.sql
//
// That's it. INSERT, UPDATE, SELECT, CREATE TABLE, bind, and read are all
// derived automatically from the schema definition + member pointers.

#include <Strategy/Strategy.hpp>
#include <Db/DbStore.hpp>

namespace stnks
{
    inline const auto kStrategySchema = db::MakeSchema<Strategy>(
        "strategies",
        &Strategy::id,
        db::Col("symbol",       &Strategy::symbol,      "TEXT",    "NOT NULL"),
        db::Col("direction",    &Strategy::direction,    "INTEGER", "NOT NULL DEFAULT 0"),
        db::Col("entry_price",  &Strategy::entryPrice,  "REAL",    "NOT NULL"),
        db::Col("take_profit",  &Strategy::takeProfit,  "REAL",    "NOT NULL"),
        db::Col("stop_loss",    &Strategy::stopLoss,    "REAL",    "NOT NULL"),
        db::Col("status",       &Strategy::status,      "INTEGER", "NOT NULL DEFAULT 0"),
        db::Col("created_at",   &Strategy::createdAt,   "INTEGER", "NOT NULL"),
        db::Col("triggered_at", &Strategy::triggeredAt, "INTEGER", "NOT NULL DEFAULT 0"),
        db::Col("notes",        &Strategy::notes,       "TEXT",    "NOT NULL DEFAULT ''"),
        db::Col("type",         &Strategy::type,        "INTEGER", "NOT NULL DEFAULT 0"),
        db::Col("parent_id",    &Strategy::parentId,    "INTEGER", "NOT NULL DEFAULT 0"),
        db::Col("priority",     &Strategy::priority,    "INTEGER", "NOT NULL DEFAULT 0"),
        db::Col("quantity",     &Strategy::quantity,    "REAL",    "NOT NULL DEFAULT 0"),
        db::Col("entry_date",   &Strategy::entryDate,   "INTEGER", "NOT NULL DEFAULT 0"),
        db::Col("exit_price",   &Strategy::exitPrice,   "REAL",    "NOT NULL DEFAULT 0"),
        db::Col("closed_pnl",   &Strategy::closedPnlPct,"REAL",   "NOT NULL DEFAULT 0"),
        db::Col("enabled",      &Strategy::enabled,     "INTEGER", "NOT NULL DEFAULT 1"),
        db::Col("entry_fee",    &Strategy::entryFee,    "REAL",    "DEFAULT 0"),
        db::Col("exit_fee",     &Strategy::exitFee,     "REAL",    "DEFAULT 0"),
        db::Col("broker",       &Strategy::broker,      "INTEGER", "DEFAULT 0")
    );

    using StrategySchemaType = std::decay_t<decltype(kStrategySchema)>;
    using StrategyDbStore    = db::DbStore<Strategy, StrategySchemaType>;

} // namespace stnks
