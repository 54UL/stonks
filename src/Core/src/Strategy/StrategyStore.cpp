#include <Strategy/StrategyStore.hpp>
#include <GlobalKeys.hpp>
#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

namespace stnks
{
    std::string StrategyStore::ResolveDbPath(const std::string& dbName)
    {
        fs::path p(dbName);

        // If an absolute path was given, use it directly
        if (p.is_absolute())
        {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            return dbName;
        }

        // Try ASSETS_STNKS env var first
        const char* root = std::getenv(gk::ENV_ASSETS_ROOT);
        fs::path dir;

        if (root && root[0] != '\0')
            dir = fs::path(root) / "app";
        else
            dir = fs::path("app");

        // Ensure the directory exists
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec)
            spdlog::warn("[StrategyStore] Could not create dir '{}': {}", dir.string(), ec.message());

        fs::path full = dir / dbName;
        return full.string();
    }

    StrategyStore::StrategyStore(const std::string& dbName)
        : dbPath_(ResolveDbPath(dbName))
    {
        int rc = sqlite3_open_v2(dbPath_.c_str(), &db_,
                                  SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                  nullptr);
        if (rc != SQLITE_OK)
        {
            spdlog::error("[StrategyStore] Failed to open DB '{}': {}", dbPath_, sqlite3_errmsg(db_));
            db_ = nullptr;
            return;
        }

        // Enable WAL mode for concurrent read/write from multiple threads
        sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "PRAGMA busy_timeout=5000;", nullptr, nullptr, nullptr);

        CreateTable();
        RunMigrations();
        spdlog::info("[StrategyStore] Opened '{}'", dbPath_);
    }

    StrategyStore::~StrategyStore()
    {
        if (db_)
            sqlite3_close(db_);
    }

    void StrategyStore::CreateTable()
    {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql::kCreateTable, nullptr, nullptr, &err) != SQLITE_OK)
        {
            spdlog::error("[StrategyStore] Create table failed: {}", err);
            sqlite3_free(err);
        }
    }

    void StrategyStore::RunMigrations()
    {
        // Try adding each new column. If it already exists, ALTER TABLE will fail
        // silently — that's expected and fine.
        for (const char* migration : sql::kMigrations)
        {
            char* err = nullptr;
            sqlite3_exec(db_, migration, nullptr, nullptr, &err);
            if (err)
                sqlite3_free(err);  // Column already exists — ignore
        }
    }

    Strategy StrategyStore::RowToStrategy(void* rawStmt)
    {
        auto* stmt = static_cast<sqlite3_stmt*>(rawStmt);
        Strategy s;
        s.id          = sqlite3_column_int64(stmt, 0);
        s.symbol      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        s.direction   = static_cast<StrategyDirection>(sqlite3_column_int(stmt, 2));
        s.entryPrice  = static_cast<float>(sqlite3_column_double(stmt, 3));
        s.takeProfit  = static_cast<float>(sqlite3_column_double(stmt, 4));
        s.stopLoss    = static_cast<float>(sqlite3_column_double(stmt, 5));
        s.status      = static_cast<StrategyStatus>(sqlite3_column_int(stmt, 6));
        s.createdAt   = sqlite3_column_int64(stmt, 7);
        s.triggeredAt = sqlite3_column_int64(stmt, 8);

        const unsigned char* notes = sqlite3_column_text(stmt, 9);
        if (notes) s.notes = reinterpret_cast<const char*>(notes);

        s.type         = static_cast<StrategyType>(sqlite3_column_int(stmt, 10));
        s.parentId     = sqlite3_column_int64(stmt, 11);
        s.priority     = sqlite3_column_int(stmt, 12);
        s.quantity     = static_cast<float>(sqlite3_column_double(stmt, 13));
        s.entryDate    = sqlite3_column_int64(stmt, 14);
        s.exitPrice    = static_cast<float>(sqlite3_column_double(stmt, 15));
        s.closedPnlPct = static_cast<float>(sqlite3_column_double(stmt, 16));
        s.enabled      = sqlite3_column_int(stmt, 17) != 0;

        return s;
    }

    int64_t StrategyStore::Insert(const Strategy& s)
    {
        if (!db_) return -1;

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::kInsert, -1, &stmt, nullptr) != SQLITE_OK)
        {
            spdlog::error("[StrategyStore] Insert prepare failed: {}", sqlite3_errmsg(db_));
            return -1;
        }

        sqlite3_bind_text(stmt, 1, s.symbol.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(s.direction));
        sqlite3_bind_double(stmt, 3, s.entryPrice);
        sqlite3_bind_double(stmt, 4, s.takeProfit);
        sqlite3_bind_double(stmt, 5, s.stopLoss);
        sqlite3_bind_int(stmt, 6, static_cast<int>(s.status));
        sqlite3_bind_int64(stmt, 7, s.createdAt ? s.createdAt : std::time(nullptr));
        sqlite3_bind_int64(stmt, 8, s.triggeredAt);
        sqlite3_bind_text(stmt, 9, s.notes.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 10, static_cast<int>(s.type));
        sqlite3_bind_int64(stmt, 11, s.parentId);
        sqlite3_bind_int(stmt, 12, s.priority);
        sqlite3_bind_double(stmt, 13, s.quantity);
        sqlite3_bind_int64(stmt, 14, s.entryDate ? s.entryDate : s.createdAt);
        sqlite3_bind_double(stmt, 15, s.exitPrice);
        sqlite3_bind_double(stmt, 16, s.closedPnlPct);
        sqlite3_bind_int(stmt, 17, s.enabled ? 1 : 0);

        int64_t id = -1;
        if (sqlite3_step(stmt) == SQLITE_DONE)
            id = sqlite3_last_insert_rowid(db_);
        else
            spdlog::error("[StrategyStore] Insert failed: {}", sqlite3_errmsg(db_));

        sqlite3_finalize(stmt);
        return id;
    }

    bool StrategyStore::Update(const Strategy& s)
    {
        if (!db_) return false;

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::kUpdate, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_text(stmt, 1, s.symbol.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, static_cast<int>(s.direction));
        sqlite3_bind_double(stmt, 3, s.entryPrice);
        sqlite3_bind_double(stmt, 4, s.takeProfit);
        sqlite3_bind_double(stmt, 5, s.stopLoss);
        sqlite3_bind_int(stmt, 6, static_cast<int>(s.status));
        sqlite3_bind_int64(stmt, 7, s.createdAt);
        sqlite3_bind_int64(stmt, 8, s.triggeredAt);
        sqlite3_bind_text(stmt, 9, s.notes.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 10, static_cast<int>(s.type));
        sqlite3_bind_int64(stmt, 11, s.parentId);
        sqlite3_bind_int(stmt, 12, s.priority);
        sqlite3_bind_double(stmt, 13, s.quantity);
        sqlite3_bind_int64(stmt, 14, s.entryDate);
        sqlite3_bind_double(stmt, 15, s.exitPrice);
        sqlite3_bind_double(stmt, 16, s.closedPnlPct);
        sqlite3_bind_int(stmt, 17, s.enabled ? 1 : 0);
        sqlite3_bind_int64(stmt, 18, s.id);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        if (!ok) spdlog::error("[StrategyStore] Update failed: {}", sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        return ok;
    }

    bool StrategyStore::Delete(int64_t id)
    {
        if (!db_) return false;

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::kDelete, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int64(stmt, 1, id);
        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    std::vector<Strategy> StrategyStore::GetAll()
    {
        std::vector<Strategy> result;
        if (!db_) return result;

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::kSelectAll, -1, &stmt, nullptr) != SQLITE_OK)
            return result;

        while (sqlite3_step(stmt) == SQLITE_ROW)
            result.push_back(RowToStrategy(stmt));

        sqlite3_finalize(stmt);
        return result;
    }

    std::vector<Strategy> StrategyStore::GetBySymbol(const std::string& symbol)
    {
        std::vector<Strategy> result;
        if (!db_) return result;

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::kSelectBySymbol, -1, &stmt, nullptr) != SQLITE_OK)
            return result;

        sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);

        while (sqlite3_step(stmt) == SQLITE_ROW)
            result.push_back(RowToStrategy(stmt));

        sqlite3_finalize(stmt);
        return result;
    }

    std::vector<Strategy> StrategyStore::GetActive()
    {
        std::vector<Strategy> result;
        if (!db_) return result;

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::kSelectActive, -1, &stmt, nullptr) != SQLITE_OK)
            return result;

        while (sqlite3_step(stmt) == SQLITE_ROW)
            result.push_back(RowToStrategy(stmt));

        sqlite3_finalize(stmt);
        return result;
    }

    Strategy StrategyStore::GetById(int64_t id)
    {
        Strategy s;
        if (!db_) return s;

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::kSelectById, -1, &stmt, nullptr) != SQLITE_OK)
            return s;

        sqlite3_bind_int64(stmt, 1, id);

        if (sqlite3_step(stmt) == SQLITE_ROW)
            s = RowToStrategy(stmt);

        sqlite3_finalize(stmt);
        return s;
    }

    std::vector<Strategy> StrategyStore::GetChildren(int64_t parentId)
    {
        std::vector<Strategy> result;
        if (!db_) return result;

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::kSelectChildren, -1, &stmt, nullptr) != SQLITE_OK)
            return result;

        sqlite3_bind_int64(stmt, 1, parentId);

        while (sqlite3_step(stmt) == SQLITE_ROW)
            result.push_back(RowToStrategy(stmt));

        sqlite3_finalize(stmt);
        return result;
    }

    bool StrategyStore::MarkTriggered(int64_t id, StrategyStatus status, int64_t triggeredAt,
                                      float exitPrice, float closedPnlPct)
    {
        if (!db_) return false;

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::kMarkTriggered, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int(stmt, 1, static_cast<int>(status));
        sqlite3_bind_int64(stmt, 2, triggeredAt);
        sqlite3_bind_double(stmt, 3, exitPrice);
        sqlite3_bind_double(stmt, 4, closedPnlPct);
        sqlite3_bind_int64(stmt, 5, id);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

} // namespace stnks
