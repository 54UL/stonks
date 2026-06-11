#include <Strategy/StrategyStore.hpp>
#include <Db/StrategySchema.hpp>
#include <Db/MigrationRunner.hpp>
#include <GlobalKeys.hpp>
#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace stnks {
    std::string StrategyStore::ResolveDbRoot() {
        const char *root = std::getenv(gk::ENV_ASSETS_ROOT);
        if (root && root[0] != '\0')
            return std::string(root);

        // Default: current working directory
        return fs::current_path().string();
    }

    std::string StrategyStore::ResolveDbPath(const std::string &dbName) {
        fs::path p(dbName);

        if (p.is_absolute()) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            return dbName;
        }

        const char *root = std::getenv(gk::ENV_ASSETS_ROOT);
        fs::path dir;

        if (root && root[0] != '\0')
            dir = fs::path(root) / "app";
        else
            dir = fs::path("app");

        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec)
            spdlog::warn("[StrategyStore] Could not create dir '{}': {}", dir.string(), ec.message());

        return (dir / dbName).string();
    }

    StrategyStore::StrategyStore(const std::string &dbName)
        : dbPath_(ResolveDbPath(dbName))
          , dbRoot_(ResolveDbRoot()) {
        int rc = sqlite3_open_v2(dbPath_.c_str(), &db_,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                 nullptr);
        if (rc != SQLITE_OK) {
            spdlog::error("[StrategyStore] Failed to open DB '{}': {}", dbPath_, sqlite3_errmsg(db_));
            db_ = nullptr;
            return;
        }

        sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "PRAGMA busy_timeout=5000;", nullptr, nullptr, nullptr);

        RunMigrations();
        spdlog::info("[StrategyStore] Opened '{}'", dbPath_);
    }

    StrategyStore::~StrategyStore() {
        if (db_)
            sqlite3_close(db_);
    }

    // Walk up from a starting directory looking for db/migrations/
    static fs::path FindDbDir(const fs::path& start) {
        fs::path dir = start;
        for (int i = 0; i < 10; ++i) {
            if (fs::exists(dir / "db" / "migrations"))
                return dir / "db";
            auto parent = dir.parent_path();
            if (parent == dir) break;
            dir = parent;
        }
        return {};
    }

    void StrategyStore::RunMigrations() {
        // Bootstrap: ensure the base strategies table exists before any migrations.
        // Generated from kDbFields — always reflects the full current schema.
        char* err = nullptr;
        if (sqlite3_exec(db_, sql::CreateTable(), nullptr, nullptr, &err) != SQLITE_OK) {
            spdlog::error("[StrategyStore] Bootstrap CREATE TABLE failed: {}", err ? err : "?");
            if (err) sqlite3_free(err);
        }

        db::MigrationRunner runner(db_);

        fs::path dbDir;

        // 1. Try ASSETS_STNKS / dbRoot_
        dbDir = FindDbDir(dbRoot_);

        // 2. Try relative to executable (handles running from build dir)
        if (dbDir.empty()) {
#ifdef _WIN32
            char exeBuf[512] = {};
            if (GetModuleFileNameA(nullptr, exeBuf, sizeof(exeBuf)))
                dbDir = FindDbDir(fs::path(exeBuf).parent_path());
#else
            char exeBuf[512] = {};
            ssize_t len = ::readlink("/proc/self/exe", exeBuf, sizeof(exeBuf) - 1);
            if (len > 0) {
                exeBuf[len] = '\0';
                dbDir = FindDbDir(fs::path(exeBuf).parent_path());
            }
#endif
        }

        // 3. Try compile-time source directory (always works in dev builds)
#ifdef STNKS_SOURCE_DIR
        if (dbDir.empty())
            dbDir = FindDbDir(fs::path(STNKS_SOURCE_DIR));
#endif

        // 4. Fallback: CWD-relative
        if (dbDir.empty())
            dbDir = fs::path("db");

        fs::path migrationsDir = dbDir / "migrations";
        fs::path seedsDir      = dbDir / "seeds";

        spdlog::info("[StrategyStore] Migrations dir: {}", migrationsDir.string());
        runner.Run(migrationsDir.string());
        runner.Seed(seedsDir.string(), "strategies");
    }

    // ── CRUD using schema helpers ───────────────────────────────────────────

    int64_t StrategyStore::Insert(const Strategy &s) {
        if (!db_) return -1;

        // Fill in defaults before binding
        Strategy copy = s;
        if (copy.createdAt == 0) copy.createdAt = std::time(nullptr);
        if (copy.entryDate == 0) copy.entryDate = copy.createdAt;

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::Insert(), -1, &stmt, nullptr) != SQLITE_OK) {
            spdlog::error("[StrategyStore] Insert prepare failed: {}", sqlite3_errmsg(db_));
            return -1;
        }

        schema::BindStrategy(stmt, copy);

        int64_t id = -1;
        if (sqlite3_step(stmt) == SQLITE_DONE)
            id = sqlite3_last_insert_rowid(db_);
        else
            spdlog::error("[StrategyStore] Insert failed: {}", sqlite3_errmsg(db_));

        sqlite3_finalize(stmt);
        return id;
    }

    bool StrategyStore::Update(const Strategy &s) {
        if (!db_) return false;

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::Update(), -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        schema::BindStrategyForUpdate(stmt, s);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        if (!ok) spdlog::error("[StrategyStore] Update failed: {}", sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        return ok;
    }

    bool StrategyStore::Delete(int64_t id) {
        if (!db_) return false;

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::Delete(), -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int64(stmt, 1, id);
        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    // ── Query helpers ───────────────────────────────────────────────────────

    static std::vector<Strategy> RunSelect(sqlite3* db, const char* query) {
        std::vector<Strategy> result;
        if (!db) return result;

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK)
            return result;

        while (sqlite3_step(stmt) == SQLITE_ROW)
            result.push_back(stnks::schema::ReadRow(stmt));

        sqlite3_finalize(stmt);
        return result;
    }

    std::vector<Strategy> StrategyStore::GetAll() {
        return RunSelect(db_, sql::SelectAll());
    }

    std::vector<Strategy> StrategyStore::GetBySymbol(const std::string &symbol) {
        if (!db_) return {};

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::SelectBySymbol(), -1, &stmt, nullptr) != SQLITE_OK)
            return {};

        sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);

        std::vector<Strategy> result;
        while (sqlite3_step(stmt) == SQLITE_ROW)
            result.push_back(schema::ReadRow(stmt));

        sqlite3_finalize(stmt);
        return result;
    }

    std::vector<Strategy> StrategyStore::GetActive() {
        return RunSelect(db_, sql::SelectActive());
    }

    Strategy StrategyStore::GetById(int64_t id) {
        Strategy s;
        if (!db_) return s;

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::SelectById(), -1, &stmt, nullptr) != SQLITE_OK)
            return s;

        sqlite3_bind_int64(stmt, 1, id);

        if (sqlite3_step(stmt) == SQLITE_ROW)
            s = schema::ReadRow(stmt);

        sqlite3_finalize(stmt);
        return s;
    }

    std::vector<Strategy> StrategyStore::GetChildren(int64_t parentId) {
        if (!db_) return {};

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::SelectChildren(), -1, &stmt, nullptr) != SQLITE_OK)
            return {};

        sqlite3_bind_int64(stmt, 1, parentId);

        std::vector<Strategy> result;
        while (sqlite3_step(stmt) == SQLITE_ROW)
            result.push_back(schema::ReadRow(stmt));

        sqlite3_finalize(stmt);
        return result;
    }

    bool StrategyStore::MarkTriggered(int64_t id, StrategyStatus status, int64_t triggeredAt,
                                      float exitPrice, float closedPnlPct) {
        if (!db_) return false;

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql::MarkTriggered(), -1, &stmt, nullptr) != SQLITE_OK)
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
