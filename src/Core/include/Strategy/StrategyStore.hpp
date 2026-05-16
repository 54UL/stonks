#pragma once

#include <Strategy/Strategy.hpp>
#include <string>
#include <vector>
#include <functional>

struct sqlite3;

namespace stnks
{
    // SQL query constants
    namespace sql
    {
        inline constexpr const char* kCreateTable = R"(
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
                notes        TEXT    NOT NULL DEFAULT '',
                type         INTEGER NOT NULL DEFAULT 0,
                parent_id    INTEGER NOT NULL DEFAULT 0,
                priority     INTEGER NOT NULL DEFAULT 0,
                quantity     REAL    NOT NULL DEFAULT 0,
                entry_date   INTEGER NOT NULL DEFAULT 0
            );
        )";

        // Migration: add new columns to existing tables
        inline constexpr const char* kMigrations[] = {
            "ALTER TABLE strategies ADD COLUMN type       INTEGER NOT NULL DEFAULT 0;",
            "ALTER TABLE strategies ADD COLUMN parent_id  INTEGER NOT NULL DEFAULT 0;",
            "ALTER TABLE strategies ADD COLUMN priority   INTEGER NOT NULL DEFAULT 0;",
            "ALTER TABLE strategies ADD COLUMN quantity   REAL    NOT NULL DEFAULT 0;",
            "ALTER TABLE strategies ADD COLUMN entry_date INTEGER NOT NULL DEFAULT 0;"
        };

        inline constexpr const char* kInsert = R"(
            INSERT INTO strategies
                (symbol, direction, entry_price, take_profit, stop_loss, status,
                 created_at, triggered_at, notes, type, parent_id, priority, quantity, entry_date)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )";

        inline constexpr const char* kUpdate = R"(
            UPDATE strategies
            SET symbol=?, direction=?, entry_price=?, take_profit=?, stop_loss=?,
                status=?, created_at=?, triggered_at=?, notes=?,
                type=?, parent_id=?, priority=?, quantity=?, entry_date=?
            WHERE id=?;
        )";

        inline constexpr const char* kDelete =
            "DELETE FROM strategies WHERE id=?;";

        inline constexpr const char* kSelectAll =
            "SELECT id, symbol, direction, entry_price, take_profit, stop_loss, "
            "status, created_at, triggered_at, notes, type, parent_id, priority, quantity, entry_date "
            "FROM strategies ORDER BY priority ASC, created_at DESC;";

        inline constexpr const char* kSelectBySymbol =
            "SELECT id, symbol, direction, entry_price, take_profit, stop_loss, "
            "status, created_at, triggered_at, notes, type, parent_id, priority, quantity, entry_date "
            "FROM strategies WHERE symbol=? ORDER BY priority ASC, created_at DESC;";

        inline constexpr const char* kSelectActive =
            "SELECT id, symbol, direction, entry_price, take_profit, stop_loss, "
            "status, created_at, triggered_at, notes, type, parent_id, priority, quantity, entry_date "
            "FROM strategies WHERE status=0 ORDER BY priority ASC, created_at DESC;";

        inline constexpr const char* kSelectById =
            "SELECT id, symbol, direction, entry_price, take_profit, stop_loss, "
            "status, created_at, triggered_at, notes, type, parent_id, priority, quantity, entry_date "
            "FROM strategies WHERE id=?;";

        inline constexpr const char* kSelectChildren =
            "SELECT id, symbol, direction, entry_price, take_profit, stop_loss, "
            "status, created_at, triggered_at, notes, type, parent_id, priority, quantity, entry_date "
            "FROM strategies WHERE parent_id=? ORDER BY priority ASC;";

        inline constexpr const char* kMarkTriggered =
            "UPDATE strategies SET status=?, triggered_at=? WHERE id=?;";
    }

    // SQLite-backed persistent storage for strategies.
    // DB file is stored under the app data directory.
    class StrategyStore
    {
    public:
        // Resolves an absolute path for the DB file.
        // Uses ASSETS_STNKS env var if set, otherwise "app/" relative to CWD.
        explicit StrategyStore(const std::string& dbName = "strategies.db");
        ~StrategyStore();

        StrategyStore(const StrategyStore&) = delete;
        StrategyStore& operator=(const StrategyStore&) = delete;

        // CRUD
        int64_t Insert(const Strategy& s);
        bool    Update(const Strategy& s);
        bool    Delete(int64_t id);

        // Queries
        std::vector<Strategy> GetAll();
        std::vector<Strategy> GetBySymbol(const std::string& symbol);
        std::vector<Strategy> GetActive();
        Strategy              GetById(int64_t id);
        std::vector<Strategy> GetChildren(int64_t parentId);

        // Mark a strategy as triggered
        bool MarkTriggered(int64_t id, StrategyStatus status, int64_t triggeredAt);

        // Get resolved DB path (for diagnostics)
        const std::string& GetDbPath() const { return dbPath_; }

    private:
        void CreateTable();
        void RunMigrations();
        Strategy RowToStrategy(void* stmt);

        static std::string ResolveDbPath(const std::string& dbName);

        sqlite3*    db_     = nullptr;
        std::string dbPath_;
    };

} // namespace stnks
