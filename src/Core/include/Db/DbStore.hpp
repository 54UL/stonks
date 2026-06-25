#pragma once

// DbStore — generic template-based ORM for SQLite.
//
// Define a schema once (column name + member pointer + SQL type), and get
// INSERT, UPDATE, DELETE, SELECT, CREATE TABLE, bind, and read for free.
//
// Features:
//   - Schema-driven CRUD (no manual bind/read code per table)
//   - Variadic parameter binding (no raw sqlite3_bind_* calls)
//   - Named queries from .sql files (loaded via SetQueries)
//   - Type-safe: enums, bools, floats, strings all auto-dispatched
//
// Usage:
//   inline const auto kSchema = db::MakeSchema<MyStruct>("my_table", &MyStruct::id,
//       db::Col("name",  &MyStruct::name,  "TEXT", "NOT NULL"),
//       db::Col("value", &MyStruct::value, "REAL", "DEFAULT 0")
//   );
//   db::DbStore store(db, kSchema);
//   store.Insert(obj);
//   store.Where("value > ?", 10.0f);
//   store.ExecQuery("mark_done", true, id);

#include <sqlite3.h>
#include <string>
#include <vector>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace stnks::db
{
    // ── Field descriptor ──────────────────────────────────────────────────
    // Binds a SQL column to a C++ struct member via pointer-to-member.

    template<typename T, typename M>
    struct Field
    {
        const char* column;
        const char* sqlType;
        const char* constraints;
        M T::* member;
    };

    template<typename T, typename M>
    constexpr Field<T, M> Col(const char* col, M T::* mem,
                              const char* type, const char* constraints = "")
    {
        return {col, type, constraints, mem};
    }

    // ── Schema descriptor ─────────────────────────────────────────────────

    template<typename T, typename... Fs>
    struct Schema
    {
        const char* tableName;
        int64_t T::* idMember;
        std::tuple<Fs...> fields;
        static constexpr std::size_t fieldCount = sizeof...(Fs);
    };

    template<typename T, typename... Ms>
    constexpr auto MakeSchema(const char* table, int64_t T::* id, Field<T, Ms>... fs)
    {
        return Schema<T, Field<T, Ms>...>{table, id, std::make_tuple(fs...)};
    }

    // ── Member-pointer bind/read (schema-driven) ──────────────────────────
    // Used internally by DbStore for INSERT/UPDATE/SELECT on struct fields.

    template<typename T, typename M>
    void BindValue(sqlite3_stmt* stmt, int idx, const T& obj, M T::* member)
    {
        using V = std::decay_t<M>;
        const auto& val = obj.*member;

        if constexpr (std::is_same_v<V, std::string>)
            sqlite3_bind_text(stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);
        else if constexpr (std::is_same_v<V, float>)
            sqlite3_bind_double(stmt, idx, static_cast<double>(val));
        else if constexpr (std::is_same_v<V, double>)
            sqlite3_bind_double(stmt, idx, val);
        else if constexpr (std::is_same_v<V, int64_t>)
            sqlite3_bind_int64(stmt, idx, val);
        else if constexpr (std::is_same_v<V, int>)
            sqlite3_bind_int(stmt, idx, val);
        else if constexpr (std::is_same_v<V, bool>)
            sqlite3_bind_int(stmt, idx, val ? 1 : 0);
        else if constexpr (std::is_enum_v<V>)
            sqlite3_bind_int(stmt, idx, static_cast<int>(val));
    }

    template<typename T, typename M>
    void ReadValue(sqlite3_stmt* stmt, int col, T& obj, M T::* member)
    {
        using V = std::decay_t<M>;

        if constexpr (std::is_same_v<V, std::string>) {
            const unsigned char* t = sqlite3_column_text(stmt, col);
            if (t) obj.*member = reinterpret_cast<const char*>(t);
        }
        else if constexpr (std::is_same_v<V, float>)
            obj.*member = static_cast<float>(sqlite3_column_double(stmt, col));
        else if constexpr (std::is_same_v<V, double>)
            obj.*member = sqlite3_column_double(stmt, col);
        else if constexpr (std::is_same_v<V, int64_t>)
            obj.*member = sqlite3_column_int64(stmt, col);
        else if constexpr (std::is_same_v<V, int>)
            obj.*member = sqlite3_column_int(stmt, col);
        else if constexpr (std::is_same_v<V, bool>)
            obj.*member = sqlite3_column_int(stmt, col) != 0;
        else if constexpr (std::is_enum_v<V>)
            obj.*member = static_cast<V>(sqlite3_column_int(stmt, col));
    }

    // ── Standalone parameter binding (variadic) ───────────────────────────
    // For binding raw values to ? placeholders in custom queries.
    // Supports: string, const char*, int, int64_t, float, double, bool, enums.

    inline void BindOne(sqlite3_stmt* s, int i, const std::string& v)
    { sqlite3_bind_text(s, i, v.c_str(), -1, SQLITE_TRANSIENT); }

    inline void BindOne(sqlite3_stmt* s, int i, const char* v)
    { sqlite3_bind_text(s, i, v, -1, SQLITE_TRANSIENT); }

    inline void BindOne(sqlite3_stmt* s, int i, int v)
    { sqlite3_bind_int(s, i, v); }

    inline void BindOne(sqlite3_stmt* s, int i, int64_t v)
    { sqlite3_bind_int64(s, i, v); }

    inline void BindOne(sqlite3_stmt* s, int i, float v)
    { sqlite3_bind_double(s, i, static_cast<double>(v)); }

    inline void BindOne(sqlite3_stmt* s, int i, double v)
    { sqlite3_bind_double(s, i, v); }

    inline void BindOne(sqlite3_stmt* s, int i, bool v)
    { sqlite3_bind_int(s, i, v ? 1 : 0); }

    // Enum catch-all
    template<typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
    void BindOne(sqlite3_stmt* s, int i, E v)
    { sqlite3_bind_int(s, i, static_cast<int>(v)); }

    // Bind all variadic args to params 1..N (left-to-right, guaranteed by comma fold)
    template<typename... Args>
    void BindParams(sqlite3_stmt* stmt, const Args&... args)
    {
        int idx = 1;
        (BindOne(stmt, idx++, args), ...);
    }

    // ── DbStore ───────────────────────────────────────────────────────────
    // Generic CRUD store for any struct T described by a Schema.
    // Caller owns the sqlite3* handle.

    template<typename T, typename SchemaT>
    class DbStore
    {
    public:
        DbStore(sqlite3* db, const SchemaT& schema)
            : db_(db), schema_(schema)
        {
            sqlInsert_      = BuildInsert();
            sqlUpdate_      = BuildUpdate();
            sqlDelete_      = std::string("DELETE FROM ") + schema_.tableName + " WHERE id=?;";
            sqlSelectBase_  = BuildSelect();
            sqlSelectById_  = sqlSelectBase_ + " WHERE id=?;";
            sqlCreateTable_ = BuildCreateTable();
        }

        // ── DDL ────────────────────────────────────────────────────────────

        const std::string& CreateTableSQL() const { return sqlCreateTable_; }

        bool CreateTable() const
        {
            char* err = nullptr;
            if (sqlite3_exec(db_, sqlCreateTable_.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
                if (err) sqlite3_free(err);
                return false;
            }
            return true;
        }

        // ── Named queries (.sql files) ─────────────────────────────────────
        // Loaded externally (by Database) and injected here.
        // Keys are filenames without extension (e.g. "mark_triggered").

        void SetQueries(std::unordered_map<std::string, std::string> queries)
        {
            queries_ = std::move(queries);
        }

        bool HasQuery(const std::string& name) const
        {
            return queries_.count(name) > 0;
        }

        // Execute a named query (non-SELECT) with variadic param binding.
        template<typename... Args>
        bool ExecQuery(const std::string& name, const Args&... args) const
        {
            auto it = queries_.find(name);
            if (it == queries_.end()) return false;
            return Exec(it->second, args...);
        }

        // Run a named SELECT query, returning rows via schema ReadRow.
        template<typename... Args>
        std::vector<T> RunQuery(const std::string& name, const Args&... args) const
        {
            auto it = queries_.find(name);
            if (it == queries_.end()) return {};
            return RunSelect(it->second, args...);
        }

        // ── CRUD ───────────────────────────────────────────────────────────

        int64_t Insert(const T& obj) const
        {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sqlInsert_.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                return -1;
            BindAll(stmt, obj);
            int64_t id = -1;
            if (sqlite3_step(stmt) == SQLITE_DONE)
                id = sqlite3_last_insert_rowid(db_);
            sqlite3_finalize(stmt);
            return id;
        }

        bool Update(const T& obj) const
        {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sqlUpdate_.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                return false;
            BindAll(stmt, obj);
            sqlite3_bind_int64(stmt, static_cast<int>(SchemaT::fieldCount) + 1,
                               obj.*(schema_.idMember));
            bool ok = sqlite3_step(stmt) == SQLITE_DONE;
            sqlite3_finalize(stmt);
            return ok;
        }

        bool Delete(int64_t id) const
        {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sqlDelete_.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                return false;
            sqlite3_bind_int64(stmt, 1, id);
            bool ok = sqlite3_step(stmt) == SQLITE_DONE;
            sqlite3_finalize(stmt);
            return ok;
        }

        // ── Queries ────────────────────────────────────────────────────────

        T GetById(int64_t id) const
        {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sqlSelectById_.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                return T{};
            sqlite3_bind_int64(stmt, 1, id);
            T obj{};
            if (sqlite3_step(stmt) == SQLITE_ROW)
                obj = ReadRow(stmt);
            sqlite3_finalize(stmt);
            return obj;
        }

        // SELECT all rows with optional suffix (ORDER BY, LIMIT, etc.)
        std::vector<T> GetAll(const std::string& suffix = "") const
        {
            std::string sql = sqlSelectBase_;
            if (!suffix.empty()) { sql += " "; sql += suffix; }
            sql += ";";
            return RunSelect(sql);
        }

        // SELECT with WHERE clause + variadic param binding.
        // The clause can include ORDER BY, LIMIT, etc. after the conditions.
        //   store.Where("status=0 AND enabled=1 ORDER BY priority ASC");
        //   store.Where("symbol=? ORDER BY created_at DESC", symbol);
        //   store.Where("parent_id=? ORDER BY priority ASC", parentId);
        template<typename... Args>
        std::vector<T> Where(const std::string& clause, const Args&... args) const
        {
            std::string sql = sqlSelectBase_ + " WHERE " + clause + ";";
            return RunSelect(sql, args...);
        }

        // ── Raw exec with variadic binding ─────────────────────────────────
        // For custom UPDATE/DELETE/INSERT statements.
        //   store.Exec("UPDATE t SET status=? WHERE id=?", status, id);

        template<typename... Args>
        bool Exec(const std::string& sql, const Args&... args) const
        {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                return false;
            if constexpr (sizeof...(args) > 0)
                BindParams(stmt, args...);
            bool ok = sqlite3_step(stmt) == SQLITE_DONE;
            sqlite3_finalize(stmt);
            return ok;
        }

        const std::string& SelectBase() const { return sqlSelectBase_; }
        const SchemaT& GetSchema() const { return schema_; }

    private:
        // ── SQL builders ───────────────────────────────────────────────────

        std::string ColumnList() const
        {
            std::string s;
            auto add = [&s](const char* c) { if (!s.empty()) s += ", "; s += c; };
            std::apply([&](const auto&... f) { (add(f.column), ...); }, schema_.fields);
            return s;
        }

        std::string Placeholders() const
        {
            std::string s;
            auto add = [&s](const auto&) { if (!s.empty()) s += ", "; s += "?"; };
            std::apply([&](const auto&... f) { (add(f), ...); }, schema_.fields);
            return s;
        }

        std::string SetClause() const
        {
            std::string s;
            auto add = [&s](const char* c) { if (!s.empty()) s += ", "; s += c; s += "=?"; };
            std::apply([&](const auto&... f) { (add(f.column), ...); }, schema_.fields);
            return s;
        }

        std::string BuildInsert() const
        {
            return "INSERT INTO " + std::string(schema_.tableName) +
                   " (" + ColumnList() + ") VALUES (" + Placeholders() + ");";
        }

        std::string BuildUpdate() const
        {
            return "UPDATE " + std::string(schema_.tableName) +
                   " SET " + SetClause() + " WHERE id=?;";
        }

        std::string BuildSelect() const
        {
            return "SELECT id, " + ColumnList() + " FROM " + schema_.tableName;
        }

        std::string BuildCreateTable() const
        {
            std::string ddl = "CREATE TABLE IF NOT EXISTS ";
            ddl += schema_.tableName;
            ddl += " (\n    id INTEGER PRIMARY KEY AUTOINCREMENT";
            auto addCol = [&ddl](const auto& f) {
                ddl += ",\n    ";
                ddl += f.column;
                ddl += " ";
                ddl += f.sqlType;
                if (f.constraints && f.constraints[0] != '\0') {
                    ddl += " ";
                    ddl += f.constraints;
                }
            };
            std::apply([&](const auto&... f) { (addCol(f), ...); }, schema_.fields);
            ddl += "\n);";
            return ddl;
        }

        // ── Schema bind / read ─────────────────────────────────────────────

        template<std::size_t... Is>
        void BindImpl(sqlite3_stmt* stmt, const T& obj, std::index_sequence<Is...>) const
        {
            (BindValue(stmt, static_cast<int>(Is) + 1, obj,
                       std::get<Is>(schema_.fields).member), ...);
        }

        void BindAll(sqlite3_stmt* stmt, const T& obj) const
        {
            BindImpl(stmt, obj, std::make_index_sequence<SchemaT::fieldCount>{});
        }

        template<std::size_t... Is>
        T ReadImpl(sqlite3_stmt* stmt, std::index_sequence<Is...>) const
        {
            T obj{};
            obj.*(schema_.idMember) = sqlite3_column_int64(stmt, 0);
            (ReadValue(stmt, static_cast<int>(Is) + 1, obj,
                       std::get<Is>(schema_.fields).member), ...);
            return obj;
        }

        T ReadRow(sqlite3_stmt* stmt) const
        {
            return ReadImpl(stmt, std::make_index_sequence<SchemaT::fieldCount>{});
        }

        // ── Internal select runner ─────────────────────────────────────────

        template<typename... Args>
        std::vector<T> RunSelect(const std::string& sql, const Args&... args) const
        {
            std::vector<T> result;
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
                return result;
            if constexpr (sizeof...(args) > 0)
                BindParams(stmt, args...);
            while (sqlite3_step(stmt) == SQLITE_ROW)
                result.push_back(ReadRow(stmt));
            sqlite3_finalize(stmt);
            return result;
        }

        sqlite3*    db_;
        SchemaT     schema_;
        std::string sqlInsert_;
        std::string sqlUpdate_;
        std::string sqlDelete_;
        std::string sqlSelectBase_;
        std::string sqlSelectById_;
        std::string sqlCreateTable_;
        std::unordered_map<std::string, std::string> queries_;
    };

} // namespace stnks::db
