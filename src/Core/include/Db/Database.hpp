#pragma once

// Database — owns a SQLite connection and manages schema lifecycle.
//
// Handles:
//   - Connection open/close with WAL mode and busy timeout
//   - Path resolution (ASSETS_STNKS env var, relative paths)
//   - Migration running (db/migrations/*.sql)
//   - Seed data (db/seeds/*.sql)
//   - Loading named queries from db/queries/{table}/*.sql
//
// Usage:
//   Database db("strategies.db");
//   auto store = db.MakeStore(kStrategySchema);  // schema-driven store
//   store.SetQueries(db.LoadQueries("strategies"));

#include <sqlite3.h>
#include <string>
#include <unordered_map>

namespace stnks::db
{
    class Database
    {
    public:
        explicit Database(const std::string& dbName = "strategies.db");
        ~Database();

        Database(const Database&) = delete;
        Database& operator=(const Database&) = delete;

        sqlite3*           Handle() const { return db_; }
        bool               IsOpen() const { return db_ != nullptr; }
        const std::string& GetPath() const { return dbPath_; }

        // Load all .sql files from db/queries/{tableName}/.
        // Returns map: filename (no extension) -> SQL content.
        // Only loads files with alphanumeric/underscore names ending in .sql.
        // Returns empty map if directory doesn't exist (queries are optional).
        std::unordered_map<std::string, std::string>
        LoadQueries(const std::string& tableName) const;

    private:
        void RunMigrations();

        static std::string ResolveDbPath(const std::string& dbName);
        static std::string ResolveDbRoot();

        sqlite3*    db_     = nullptr;
        std::string dbPath_;
        std::string dbRoot_;
        std::string dbDir_;   // Resolved path to db/ directory (for queries)
    };

} // namespace stnks::db
