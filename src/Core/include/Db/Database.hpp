#pragma once

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

        std::unordered_map<std::string, std::string>
        LoadQueries(const std::string& tableName) const;

    private:
        void RunMigrations();

        static std::string ResolveDbPath(const std::string& dbName);
        static std::string ResolveDbRoot();

        sqlite3*    db_     = nullptr;
        std::string dbPath_;
        std::string dbRoot_;
        std::string dbDir_;
    };

} // namespace stnks::db
