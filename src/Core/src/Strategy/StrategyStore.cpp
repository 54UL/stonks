#include <Strategy/StrategyStore.hpp>
#include <spdlog/spdlog.h>
#include <ctime>

namespace stnks
{
    StrategyStore::StrategyStore(const std::string& dbName)
        : db_(std::make_unique<db::Database>(dbName))
    {
        if (!db_->IsOpen()) return;

        store_ = std::make_unique<StrategyDbStore>(db_->Handle(), kStrategySchema);
        store_->CreateTable();
        store_->SetQueries(db_->LoadQueries("strategies"));
    }

    StrategyStore::~StrategyStore()
    {
        store_.reset();  // Release store before closing DB
        db_.reset();
    }

    const std::string& StrategyStore::GetDbPath() const
    {
        return db_->GetPath();
    }

    // ── CRUD ────────────────────────────────────────────────────────────────

    int64_t StrategyStore::Insert(const Strategy& s)
    {
        if (!store_) return -1;

        Strategy copy = s;
        if (copy.createdAt == 0) copy.createdAt = std::time(nullptr);
        if (copy.entryDate == 0) copy.entryDate = copy.createdAt;

        int64_t id = store_->Insert(copy);
        if (id < 0)
            spdlog::error("[StrategyStore] Insert failed");
        return id;
    }

    bool StrategyStore::Update(const Strategy& s)
    {
        if (!store_) return false;
        bool ok = store_->Update(s);
        if (!ok) spdlog::error("[StrategyStore] Update failed");
        return ok;
    }

    bool StrategyStore::Delete(int64_t id)
    {
        if (!store_) return false;
        return store_->Delete(id);
    }

    // ── Queries ─────────────────────────────────────────────────────────────

    std::vector<Strategy> StrategyStore::GetAll()
    {
        if (!store_) return {};
        return store_->GetAll("ORDER BY priority ASC, created_at DESC");
    }

    std::vector<Strategy> StrategyStore::GetBySymbol(const std::string& symbol)
    {
        if (!store_) return {};
        return store_->Where("symbol=? ORDER BY priority ASC, created_at DESC", symbol);
    }

    std::vector<Strategy> StrategyStore::GetActive()
    {
        if (!store_) return {};
        return store_->Where("status=0 AND enabled=1 ORDER BY priority ASC, created_at DESC");
    }

    Strategy StrategyStore::GetById(int64_t id)
    {
        if (!store_) return {};
        return store_->GetById(id);
    }

    std::vector<Strategy> StrategyStore::GetChildren(int64_t parentId)
    {
        if (!store_) return {};
        return store_->Where("parent_id=? ORDER BY priority ASC", parentId);
    }

    bool StrategyStore::MarkTriggered(int64_t id, StrategyStatus status, int64_t triggeredAt,
                                      float exitPrice, float closedPnlPct)
    {
        if (!store_) return false;

        // Try named query from db/queries/strategies/mark_triggered.sql first
        if (store_->HasQuery("mark_triggered"))
            return store_->ExecQuery("mark_triggered",
                                     static_cast<int>(status), triggeredAt,
                                     static_cast<double>(exitPrice),
                                     static_cast<double>(closedPnlPct), id);

        // Inline fallback
        return store_->Exec(
            "UPDATE strategies SET status=?, triggered_at=?, exit_price=?, closed_pnl=? WHERE id=?;",
            static_cast<int>(status), triggeredAt,
            static_cast<double>(exitPrice),
            static_cast<double>(closedPnlPct), id);
    }

} // namespace stnks
