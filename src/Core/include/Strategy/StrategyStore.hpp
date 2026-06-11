#pragma once

#include <Strategy/Strategy.hpp>
#include <string>
#include <vector>
#include <functional>

struct sqlite3;

namespace stnks
{
    // SQLite-backed persistent storage for strategies.
    // Uses MigrationRunner for schema management (db/migrations/*.sql)
    // and seed scripts (db/seeds/*.sql) on first run.
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

        // Mark a strategy as triggered (records exit price and frozen P/L%)
        bool MarkTriggered(int64_t id, StrategyStatus status, int64_t triggeredAt,
                           float exitPrice = 0.f, float closedPnlPct = 0.f);

        // Get resolved DB path (for diagnostics)
        const std::string& GetDbPath() const { return dbPath_; }

    private:
        void RunMigrations();

        static std::string ResolveDbPath(const std::string& dbName);
        static std::string ResolveDbRoot();

        sqlite3*    db_     = nullptr;
        std::string dbPath_;
        std::string dbRoot_;  // Project root for finding db/ folder
    };

} // namespace stnks
