#include <Db/Database.hpp>
#include <Db/MigrationRunner.hpp>
#include <GlobalKeys.hpp>
#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace stnks::db
{
    // ── Path resolution ─────────────────────────────────────────────────────

    std::string Database::ResolveDbRoot()
    {
        const char* root = std::getenv(gk::ENV_ASSETS_ROOT);
        if (root && root[0] != '\0')
            return std::string(root);
        return fs::current_path().string();
    }

    std::string Database::ResolveDbPath(const std::string& dbName)
    {
        fs::path p(dbName);

        if (p.is_absolute()) {
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            return dbName;
        }

        const char* root = std::getenv(gk::ENV_ASSETS_ROOT);
        fs::path dir;

        if (root && root[0] != '\0')
            dir = fs::path(root) / "app";
        else
            dir = fs::path("app");

        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec)
            spdlog::warn("[Database] Could not create dir '{}': {}", dir.string(), ec.message());

        return (dir / dbName).string();
    }

    // Walk up from a starting directory looking for db/migrations/
    static fs::path FindDbDir(const fs::path& start)
    {
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

    // ── Construction ────────────────────────────────────────────────────────

    Database::Database(const std::string& dbName)
        : dbPath_(ResolveDbPath(dbName))
        , dbRoot_(ResolveDbRoot())
    {
        int rc = sqlite3_open_v2(dbPath_.c_str(), &db_,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                                 nullptr);
        if (rc != SQLITE_OK) {
            spdlog::error("[Database] Failed to open '{}': {}", dbPath_, sqlite3_errmsg(db_));
            db_ = nullptr;
            return;
        }

        sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "PRAGMA busy_timeout=5000;", nullptr, nullptr, nullptr);

        RunMigrations();
        spdlog::info("[Database] Opened '{}'", dbPath_);
    }

    Database::~Database()
    {
        if (db_)
            sqlite3_close(db_);
    }

    // ── Migrations ──────────────────────────────────────────────────────────

    void Database::RunMigrations()
    {
        MigrationRunner runner(db_);

        // 1. Try ASSETS_STNKS / dbRoot_
        dbDir_ = FindDbDir(dbRoot_).string();

        // 2. Try relative to executable
        if (dbDir_.empty()) {
#ifdef _WIN32
            char exeBuf[512] = {};
            if (GetModuleFileNameA(nullptr, exeBuf, sizeof(exeBuf)))
                dbDir_ = FindDbDir(fs::path(exeBuf).parent_path()).string();
#else
            char exeBuf[512] = {};
            ssize_t len = ::readlink("/proc/self/exe", exeBuf, sizeof(exeBuf) - 1);
            if (len > 0) {
                exeBuf[len] = '\0';
                dbDir_ = FindDbDir(fs::path(exeBuf).parent_path()).string();
            }
#endif
        }

        // 3. Try compile-time source directory
#ifdef STNKS_SOURCE_DIR
        if (dbDir_.empty())
            dbDir_ = FindDbDir(fs::path(STNKS_SOURCE_DIR)).string();
#endif

        // 4. Fallback: CWD-relative
        if (dbDir_.empty())
            dbDir_ = "db";

        fs::path migrationsDir = fs::path(dbDir_) / "migrations";
        fs::path seedsDir      = fs::path(dbDir_) / "seeds";

        spdlog::info("[Database] Migrations dir: {}", migrationsDir.string());
        runner.Run(migrationsDir.string());
        runner.Seed(seedsDir.string(), "strategies");
    }

    // ── Query file loading ──────────────────────────────────────────────────

    // Validate query name: only alphanumeric + underscore (prevent path traversal)
    static bool IsValidQueryName(const std::string& name)
    {
        if (name.empty()) return false;
        for (char c : name)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                return false;
        return true;
    }

    std::unordered_map<std::string, std::string>
    Database::LoadQueries(const std::string& tableName) const
    {
        std::unordered_map<std::string, std::string> result;

        if (!IsValidQueryName(tableName)) {
            spdlog::warn("[Database] Invalid table name for queries: '{}'", tableName);
            return result;
        }

        fs::path queriesDir = fs::path(dbDir_) / "queries" / tableName;
        if (!fs::exists(queriesDir) || !fs::is_directory(queriesDir)) {
            spdlog::debug("[Database] No queries dir: {}", queriesDir.string());
            return result;
        }

        for (const auto& entry : fs::directory_iterator(queriesDir))
        {
            if (!entry.is_regular_file()) continue;

            auto fname = entry.path().filename().string();
            if (fname.size() < 5) continue;
            auto ext = fname.substr(fname.size() - 4);
            if (ext != ".sql") continue;

            // Extract name (filename without .sql extension)
            std::string queryName = fname.substr(0, fname.size() - 4);
            if (!IsValidQueryName(queryName)) {
                spdlog::warn("[Database] Skipping query with invalid name: '{}'", fname);
                continue;
            }

            // Read file content
            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            std::stringstream ss;
            ss << file.rdbuf();
            std::string sql = ss.str();

            // Strip leading/trailing whitespace
            auto start = sql.find_first_not_of(" \t\n\r");
            if (start == std::string::npos) continue;
            auto end = sql.find_last_not_of(" \t\n\r");
            sql = sql.substr(start, end - start + 1);

            if (sql.empty()) continue;

            result[queryName] = std::move(sql);
            spdlog::debug("[Database] Loaded query '{}/{}' ({} bytes)",
                          tableName, queryName, result[queryName].size());
        }

        if (!result.empty())
            spdlog::info("[Database] Loaded {} queries for '{}'", result.size(), tableName);

        return result;
    }

} // namespace stnks::db
