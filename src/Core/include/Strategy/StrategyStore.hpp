#pragma once

// StrategyStore — domain facade over Database + DbStore<Strategy>.
//
// Provides named convenience methods (GetActive, GetBySymbol, MarkTriggered)
// on top of the generic DbStore. All CRUD and queries are handled by DbStore;
// this class only adds strategy-specific business logic (e.g., default timestamps).
//
// Adding a new table does NOT require a new Store class — just define a schema
// (like kStrategySchema) and use DbStore directly.

#include <Strategy/Strategy.hpp>
#include <Db/StrategySchema.hpp>
#include <Db/Database.hpp>
#include <string>
#include <vector>
#include <memory>

namespace stnks
{
    class StrategyStore
    {
    public:
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

        // Diagnostics
        const std::string& GetDbPath() const;

        // Direct access to the generic store (for future callers that want
        // full DbStore API — variadic Where, ExecQuery, RunQuery, etc.)
        StrategyDbStore*       Store()       { return store_.get(); }
        const StrategyDbStore* Store() const { return store_.get(); }

    private:
        std::unique_ptr<db::Database>   db_;
        std::unique_ptr<StrategyDbStore> store_;
    };

} // namespace stnks
