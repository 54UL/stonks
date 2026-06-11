#pragma once

// StrategySchema — single source of truth for the Strategy ↔ DB mapping.
//
// Adding a new persisted field:
//   1. Add member to Strategy struct (Strategy.hpp)
//   2. Add entry to kDbFields[] below (column name, SQL type, constraints)
//   3. Add bind/read lines in StrategySchema.cpp (BindStrategy / ReadRow)
//   4. Create a new migration:  db/migrations/NNN_description.sql
//
// Everything else (SQL queries, column counts, column lists) is derived
// automatically from kDbFields — no manual SQL strings to maintain.

#include <Strategy/Strategy.hpp>
#include <string>

struct sqlite3_stmt;

namespace stnks
{
    // ── DB Field Descriptor ─────────────────────────────────────────────────

    struct DbField
    {
        const char* column;       // SQL column name
        const char* sqlType;      // "TEXT", "INTEGER", "REAL"
        const char* constraints;  // "NOT NULL DEFAULT 0", etc.
    };

    // Fields in bind/read order (excludes `id` which is auto-managed).
    // Order MUST match BindStrategy() and ReadRow() in StrategySchema.cpp.
    inline constexpr DbField kDbFields[] = {
        {"symbol",       "TEXT",    "NOT NULL"},
        {"direction",    "INTEGER", "NOT NULL DEFAULT 0"},
        {"entry_price",  "REAL",    "NOT NULL"},
        {"take_profit",  "REAL",    "NOT NULL"},
        {"stop_loss",    "REAL",    "NOT NULL"},
        {"status",       "INTEGER", "NOT NULL DEFAULT 0"},
        {"created_at",   "INTEGER", "NOT NULL"},
        {"triggered_at", "INTEGER", "NOT NULL DEFAULT 0"},
        {"notes",        "TEXT",    "NOT NULL DEFAULT ''"},
        {"type",         "INTEGER", "NOT NULL DEFAULT 0"},
        {"parent_id",    "INTEGER", "NOT NULL DEFAULT 0"},
        {"priority",     "INTEGER", "NOT NULL DEFAULT 0"},
        {"quantity",     "REAL",    "NOT NULL DEFAULT 0"},
        {"entry_date",   "INTEGER", "NOT NULL DEFAULT 0"},
        {"exit_price",   "REAL",    "NOT NULL DEFAULT 0"},
        {"closed_pnl",   "REAL",    "NOT NULL DEFAULT 0"},
        {"enabled",      "INTEGER", "NOT NULL DEFAULT 1"},
        {"entry_fee",    "REAL",    "DEFAULT 0"},
        {"exit_fee",     "REAL",    "DEFAULT 0"},
        {"broker",       "INTEGER", "DEFAULT 0"},
    };

    inline constexpr int kDbFieldCount = sizeof(kDbFields) / sizeof(kDbFields[0]);

    // ── Generated SQL ───────────────────────────────────────────────────────
    // Built once lazily from kDbFields. Thread-safe (C++11 magic statics).

    namespace sql
    {
        const char* Insert();
        const char* Update();
        const char* Delete();
        const char* SelectAll();
        const char* SelectBySymbol();
        const char* SelectActive();
        const char* SelectById();
        const char* SelectChildren();
        const char* MarkTriggered();
        const char* CreateTable();   // Full CREATE TABLE IF NOT EXISTS
    }

    // ── Bind / Read Helpers ─────────────────────────────────────────────────
    // These follow the same field order as kDbFields.

    namespace schema
    {
        // Bind all fields (params 1..N) for INSERT. Does NOT bind id.
        void BindStrategy(sqlite3_stmt* stmt, const Strategy& s);

        // Bind all fields (params 1..N) + id as param N+1 for UPDATE.
        void BindStrategyForUpdate(sqlite3_stmt* stmt, const Strategy& s);

        // Read a full row (column 0 = id, columns 1..N = fields).
        Strategy ReadRow(sqlite3_stmt* stmt);
    }

} // namespace stnks
