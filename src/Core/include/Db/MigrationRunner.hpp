#pragma once

#include <string>
#include <vector>

struct sqlite3;

namespace stnks::db
{
    // Tracks which migration version has been applied.
    struct MigrationEntry
    {
        int         version;
        std::string name;
        int64_t     appliedAt;
    };

    // Versioned SQL migration runner.
    //
    // Manages a `schema_migrations` table inside the target database.
    // On Run(), scans a directory of numbered .sql files (e.g. 001_create_foo.sql),
    // compares against already-applied versions, and executes pending ones in order.
    //
    // Migration files must be named: NNN_description.sql  (NNN = zero-padded version).
    // Each file may contain multiple statements separated by semicolons.
    //
    // Seeds are .sql files in a separate directory, executed only when a specified
    // table is empty (useful for first-run sample data).
    class MigrationRunner
    {
    public:
        explicit MigrationRunner(sqlite3* db);

        // Run all pending migrations from the given directory.
        // Returns the number of migrations applied (0 if already up to date).
        int Run(const std::string& migrationsDir);

        // Run seed files only if `tableName` has zero rows.
        // Returns number of seed files applied.
        int Seed(const std::string& seedsDir, const std::string& tableName);

        // Get the current schema version (highest applied migration).
        int CurrentVersion() const;

        // Get history of applied migrations.
        std::vector<MigrationEntry> History() const;

    private:
        void EnsureMigrationsTable();

        bool IsApplied(int version) const;
        void RecordMigration(int version, const std::string& name);

        bool ExecFileStatements(const std::string& filePath);
        bool IsTableEmpty(const std::string& tableName) const;

        // Parse "NNN" from a filename like "001_create_strategies.sql"
        static int ParseVersion(const std::string& filename);

        sqlite3* db_;
    };

} // namespace stnks::db
