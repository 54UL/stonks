#include <Db/MigrationRunner.hpp>
#include <sqlite3.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>

namespace fs = std::filesystem;

namespace stnks::db
{
    MigrationRunner::MigrationRunner(sqlite3* db)
        : db_(db)
    {
        EnsureMigrationsTable();
    }

    void MigrationRunner::EnsureMigrationsTable()
    {
        const char* sql = R"(
            CREATE TABLE IF NOT EXISTS schema_migrations (
                version    INTEGER PRIMARY KEY,
                name       TEXT    NOT NULL,
                applied_at INTEGER NOT NULL
            );
        )";
        char* err = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK)
        {
            spdlog::error("[Migration] Failed to create schema_migrations: {}", err);
            sqlite3_free(err);
        }
    }

    int MigrationRunner::Run(const std::string& migrationsDir)
    {
        if (!fs::exists(migrationsDir) || !fs::is_directory(migrationsDir))
        {
            spdlog::warn("[Migration] Directory not found: {}", migrationsDir);
            return 0;
        }

        // Collect and sort migration files
        struct MigrationFile { int version; std::string path; std::string name; };
        std::vector<MigrationFile> files;

        for (auto& entry : fs::directory_iterator(migrationsDir))
        {
            if (!entry.is_regular_file()) continue;
            auto fname = entry.path().filename().string();
            if (fname.size() < 5 || fname.substr(fname.size() - 4) != ".sql") continue;

            int ver = ParseVersion(fname);
            if (ver <= 0) continue;

            files.push_back({ver, entry.path().string(), fname});
        }

        std::sort(files.begin(), files.end(),
                  [](const MigrationFile& a, const MigrationFile& b) { return a.version < b.version; });

        int applied = 0;
        for (auto& mf : files)
        {
            if (IsApplied(mf.version))
                continue;

            spdlog::info("[Migration] Applying {} ...", mf.name);

            // Run inside a transaction
            char* err = nullptr;
            sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &err);
            if (err) { sqlite3_free(err); err = nullptr; }

            if (ExecFileStatements(mf.path))
            {
                RecordMigration(mf.version, mf.name);
                sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &err);
                if (err) sqlite3_free(err);
                applied++;
                spdlog::info("[Migration] Applied {}", mf.name);
            }
            else
            {
                sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &err);
                if (err) sqlite3_free(err);
                spdlog::error("[Migration] Failed to apply {}, rolled back", mf.name);
                // Stop on first failure — don't skip migrations
                break;
            }
        }

        if (applied > 0)
            spdlog::info("[Migration] {} migration(s) applied, now at version {}",
                         applied, CurrentVersion());
        else
            spdlog::debug("[Migration] Schema up to date (version {})", CurrentVersion());

        return applied;
    }

    int MigrationRunner::Seed(const std::string& seedsDir, const std::string& tableName)
    {
        if (!IsTableEmpty(tableName))
            return 0;

        if (!fs::exists(seedsDir) || !fs::is_directory(seedsDir))
            return 0;

        // Collect and sort seed files
        std::vector<std::string> files;
        for (auto& entry : fs::directory_iterator(seedsDir))
        {
            if (!entry.is_regular_file()) continue;
            auto fname = entry.path().filename().string();
            if (fname.size() < 5 || fname.substr(fname.size() - 4) != ".sql") continue;
            files.push_back(entry.path().string());
        }
        std::sort(files.begin(), files.end());

        int applied = 0;
        for (auto& path : files)
        {
            auto fname = fs::path(path).filename().string();
            spdlog::info("[Seed] Running {} ...", fname);

            if (ExecFileStatements(path))
            {
                applied++;
                spdlog::info("[Seed] Applied {}", fname);
            }
            else
            {
                spdlog::error("[Seed] Failed: {}", fname);
            }
        }
        return applied;
    }

    int MigrationRunner::CurrentVersion() const
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT MAX(version) FROM schema_migrations;",
                               -1, &stmt, nullptr) != SQLITE_OK)
            return 0;

        int ver = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW)
            ver = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return ver;
    }

    std::vector<MigrationEntry> MigrationRunner::History() const
    {
        std::vector<MigrationEntry> result;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_,
                "SELECT version, name, applied_at FROM schema_migrations ORDER BY version;",
                -1, &stmt, nullptr) != SQLITE_OK)
            return result;

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            MigrationEntry e;
            e.version   = sqlite3_column_int(stmt, 0);
            e.name      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            e.appliedAt = sqlite3_column_int64(stmt, 2);
            result.push_back(std::move(e));
        }
        sqlite3_finalize(stmt);
        return result;
    }

    bool MigrationRunner::IsApplied(int version) const
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_,
                "SELECT 1 FROM schema_migrations WHERE version=?;",
                -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int(stmt, 1, version);
        bool found = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        return found;
    }

    void MigrationRunner::RecordMigration(int version, const std::string& name)
    {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_,
                "INSERT INTO schema_migrations (version, name, applied_at) VALUES (?, ?, ?);",
                -1, &stmt, nullptr) != SQLITE_OK)
            return;

        sqlite3_bind_int(stmt, 1, version);
        sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, std::time(nullptr));
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    bool MigrationRunner::ExecFileStatements(const std::string& filePath)
    {
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            spdlog::error("[Migration] Cannot open file: {}", filePath);
            return false;
        }

        std::stringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();

        // Split on semicolons and execute each statement individually.
        // This handles ALTER TABLE migrations where some columns may already exist:
        // "duplicate column" errors are tolerated, other errors abort.
        std::istringstream stream(content);
        std::string statement;
        bool anyFatalError = false;

        while (std::getline(stream, statement, ';'))
        {
            // Strip leading comment lines (-- ...) and whitespace
            std::istringstream lineStream(statement);
            std::string line;
            std::string cleaned;
            while (std::getline(lineStream, line))
            {
                // Trim leading whitespace from line
                size_t ls = line.find_first_not_of(" \t\r");
                if (ls == std::string::npos) continue;
                std::string trimmed = line.substr(ls);
                // Skip comment lines
                if (trimmed.size() >= 2 && trimmed[0] == '-' && trimmed[1] == '-')
                    continue;
                cleaned += trimmed + "\n";
            }

            // Trim final result
            size_t start = cleaned.find_first_not_of(" \t\n\r");
            if (start == std::string::npos) continue;
            cleaned = cleaned.substr(start);
            size_t end = cleaned.find_last_not_of(" \t\n\r");
            if (end != std::string::npos) cleaned = cleaned.substr(0, end + 1);

            if (cleaned.empty()) continue;

            std::string sql = cleaned + ";";
            char* err = nullptr;
            int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
            if (rc != SQLITE_OK)
            {
                std::string errStr = err ? err : "unknown";
                if (err) sqlite3_free(err);

                // Tolerate "duplicate column name" from ALTER TABLE
                if (errStr.find("duplicate column") != std::string::npos)
                {
                    spdlog::debug("[Migration] Column already exists (tolerated): {}", errStr);
                    continue;
                }

                spdlog::error("[Migration] SQL error in {}: {}", filePath, errStr);
                anyFatalError = true;
                break;
            }
        }
        return !anyFatalError;
    }

    bool MigrationRunner::IsTableEmpty(const std::string& tableName) const
    {
        // Sanitize: only allow alphanumeric and underscore in table name
        for (char c : tableName)
            if (!std::isalnum(c) && c != '_') return true;

        std::string sql = "SELECT COUNT(*) FROM " + tableName + ";";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
            return true; // Table doesn't exist yet — treat as empty

        bool empty = true;
        if (sqlite3_step(stmt) == SQLITE_ROW)
            empty = sqlite3_column_int(stmt, 0) == 0;
        sqlite3_finalize(stmt);
        return empty;
    }

    int MigrationRunner::ParseVersion(const std::string& filename)
    {
        // Extract leading digits: "001_create_strategies.sql" -> 1
        int ver = 0;
        for (char c : filename)
        {
            if (c >= '0' && c <= '9')
                ver = ver * 10 + (c - '0');
            else
                break;
        }
        return ver;
    }

} // namespace stnks::db
