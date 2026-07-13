# Database Layer

## Architecture

```
Database (connection + migrations)
    |
    +-- MigrationRunner (versioned .sql migrations)
    |
    +-- DbStore<T, Schema> (template CRUD for any struct)
    |
    +-- StrategyStore (domain facade wrapping DbStore<Strategy>)
```

## DbStore — Template ORM

`DbStore<T, SchemaT>` provides schema-driven CRUD for any struct `T`. Define the mapping once via `MakeSchema()` and member pointers — INSERT, UPDATE, DELETE, SELECT, CREATE TABLE, bind, and read are all generated automatically.

### Defining a Schema

```cpp
#include <Db/DbStore.hpp>

struct MyRecord {
    int64_t     id = 0;
    std::string name;
    float       value = 0.f;
    bool        active = true;
};

inline const auto kMySchema = db::MakeSchema<MyRecord>(
    "my_table", &MyRecord::id,
    db::Col("name",   &MyRecord::name,   "TEXT",    "NOT NULL"),
    db::Col("value",  &MyRecord::value,  "REAL",    "DEFAULT 0"),
    db::Col("active", &MyRecord::active, "INTEGER", "DEFAULT 1")
);
```

### CRUD Operations

```cpp
db::Database database("myapp.db");
db::DbStore store(database.Handle(), kMySchema);
store.CreateTable();

// Insert
MyRecord r{.name = "test", .value = 3.14f};
int64_t id = store.Insert(r);

// Read
auto all = store.GetAll("ORDER BY name ASC");
auto one = store.GetById(id);
auto filtered = store.Where("active=? AND value>?", true, 1.0f);
auto first = store.FindOne("name=?", "test");

// Aggregate
int64_t count = store.Count("active=?", true);
bool exists = store.Exists("name=?", "test");

// Update
r.value = 2.71f;
store.Update(r);

// Delete
store.Delete(id);

// Raw SQL
store.Exec("UPDATE my_table SET active=0 WHERE value<?", 1.0f);
```

### Named Queries

Load `.sql` files from `db/queries/{table}/` for complex operations:

```cpp
auto queries = database.LoadQueries("my_table");
store.SetQueries(queries);

// db/queries/my_table/deactivate_old.sql:
//   UPDATE my_table SET active=0 WHERE value < ?
store.ExecQuery("deactivate_old", 1.0f);

// SELECT queries
auto results = store.RunQuery("find_by_range", minVal, maxVal);
```

### Supported Types

| C++ Type | SQLite Type | Bind | Read |
|----------|------------|------|------|
| `std::string` | TEXT | `sqlite3_bind_text` | `sqlite3_column_text` |
| `float` | REAL | `sqlite3_bind_double` | `sqlite3_column_double` |
| `double` | REAL | `sqlite3_bind_double` | `sqlite3_column_double` |
| `int` | INTEGER | `sqlite3_bind_int` | `sqlite3_column_int` |
| `int64_t` | INTEGER | `sqlite3_bind_int64` | `sqlite3_column_int64` |
| `bool` | INTEGER | `sqlite3_bind_int` (0/1) | `sqlite3_column_int` (!=0) |
| Any `enum` | INTEGER | `sqlite3_bind_int` (cast) | `sqlite3_column_int` (cast) |

### Adding a New Table

1. Define the struct
2. Define the schema with `MakeSchema()` + `Col()` entries
3. Create a migration: `db/migrations/NNN_description.sql`
4. (Optional) Create a domain facade class wrapping `DbStore<YourType>`
5. (Optional) Add named queries in `db/queries/your_table/`

## Migrations

Versioned SQL files in `db/migrations/`, applied in order by `MigrationRunner` at startup.

```
db/migrations/
  001_create_strategies.sql
  002_add_strategy_type.sql
  003_add_exit_tracking.sql
  ...
```

Tracking table `schema_migrations` records which versions have been applied.

The `seed_db.sh` script can apply migrations and seed data from the CLI:
```bash
./dev_env/seed_db.sh                     # default db
./dev_env/seed_db.sh path/to/my.db       # custom path
./dev_env/seed_db.sh my.db --no-seed     # migrations only
```

## Database Connection

`Database` handles connection lifecycle with WAL mode and busy timeout:

```cpp
db::Database database("myapp.db");
sqlite3* handle = database.Handle();
```

Path resolution: uses `ASSETS_STNKS` env var as root, falls back to current directory.
