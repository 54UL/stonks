#include <Db/StrategySchema.hpp>
#include <sqlite3.h>
#include <string>
#include <ctime>

namespace stnks
{
    // ── SQL Builders ────────────────────────────────────────────────────────
    // Each builds the query string once (static local) from kDbFields.

    // Helper: comma-separated column list  "symbol, direction, entry_price, ..."
    static std::string BuildColumnList()
    {
        std::string cols;
        for (int i = 0; i < kDbFieldCount; ++i)
        {
            if (i > 0) cols += ", ";
            cols += kDbFields[i].column;
        }
        return cols;
    }

    // Helper: "id, symbol, direction, ..."
    static std::string BuildSelectColumnList()
    {
        return "id, " + BuildColumnList();
    }

    // Helper: "?, ?, ?, ..."  (kDbFieldCount placeholders)
    static std::string BuildPlaceholders()
    {
        std::string ph;
        for (int i = 0; i < kDbFieldCount; ++i)
        {
            if (i > 0) ph += ", ";
            ph += "?";
        }
        return ph;
    }

    // Helper: "symbol=?, direction=?, entry_price=?, ..."
    static std::string BuildSetClause()
    {
        std::string set;
        for (int i = 0; i < kDbFieldCount; ++i)
        {
            if (i > 0) set += ", ";
            set += kDbFields[i].column;
            set += "=?";
        }
        return set;
    }

    namespace sql
    {
        const char* Insert()
        {
            static std::string s =
                "INSERT INTO strategies (" + BuildColumnList() + ") "
                "VALUES (" + BuildPlaceholders() + ");";
            return s.c_str();
        }

        const char* Update()
        {
            static std::string s =
                "UPDATE strategies SET " + BuildSetClause() + " WHERE id=?;";
            return s.c_str();
        }

        const char* Delete()
        {
            static const char* s = "DELETE FROM strategies WHERE id=?;";
            return s;
        }

        const char* SelectAll()
        {
            static std::string s =
                "SELECT " + BuildSelectColumnList() +
                " FROM strategies ORDER BY priority ASC, created_at DESC;";
            return s.c_str();
        }

        const char* SelectBySymbol()
        {
            static std::string s =
                "SELECT " + BuildSelectColumnList() +
                " FROM strategies WHERE symbol=? ORDER BY priority ASC, created_at DESC;";
            return s.c_str();
        }

        const char* SelectActive()
        {
            static std::string s =
                "SELECT " + BuildSelectColumnList() +
                " FROM strategies WHERE status=0 AND enabled=1"
                " ORDER BY priority ASC, created_at DESC;";
            return s.c_str();
        }

        const char* SelectById()
        {
            static std::string s =
                "SELECT " + BuildSelectColumnList() +
                " FROM strategies WHERE id=?;";
            return s.c_str();
        }

        const char* SelectChildren()
        {
            static std::string s =
                "SELECT " + BuildSelectColumnList() +
                " FROM strategies WHERE parent_id=? ORDER BY priority ASC;";
            return s.c_str();
        }

        const char* MarkTriggered()
        {
            static const char* s =
                "UPDATE strategies SET status=?, triggered_at=?, exit_price=?, closed_pnl=? WHERE id=?;";
            return s;
        }

        const char* CreateTable()
        {
            static std::string s = []() {
                std::string ddl = "CREATE TABLE IF NOT EXISTS strategies (\n"
                                  "    id INTEGER PRIMARY KEY AUTOINCREMENT";
                for (int i = 0; i < kDbFieldCount; ++i)
                {
                    ddl += ",\n    ";
                    ddl += kDbFields[i].column;
                    ddl += " ";
                    ddl += kDbFields[i].sqlType;
                    if (kDbFields[i].constraints[0] != '\0')
                    {
                        ddl += " ";
                        ddl += kDbFields[i].constraints;
                    }
                }
                ddl += "\n);";
                return ddl;
            }();
            return s.c_str();
        }
    }

    // ── Bind / Read ─────────────────────────────────────────────────────────
    // Order MUST match kDbFields[]. A static_assert at the bottom verifies the
    // field count so you can't add a DbField without updating these functions.

    namespace schema
    {
        void BindStrategy(sqlite3_stmt* stmt, const Strategy& s)
        {
            int i = 1;   // sqlite3 params are 1-indexed
            sqlite3_bind_text  (stmt, i++, s.symbol.c_str(), -1, SQLITE_TRANSIENT); // symbol
            sqlite3_bind_int   (stmt, i++, static_cast<int>(s.direction));           // direction
            sqlite3_bind_double(stmt, i++, s.entryPrice);                            // entry_price
            sqlite3_bind_double(stmt, i++, s.takeProfit);                            // take_profit
            sqlite3_bind_double(stmt, i++, s.stopLoss);                              // stop_loss
            sqlite3_bind_int   (stmt, i++, static_cast<int>(s.status));              // status
            sqlite3_bind_int64 (stmt, i++, s.createdAt);                             // created_at
            sqlite3_bind_int64 (stmt, i++, s.triggeredAt);                           // triggered_at
            sqlite3_bind_text  (stmt, i++, s.notes.c_str(), -1, SQLITE_TRANSIENT);   // notes
            sqlite3_bind_int   (stmt, i++, static_cast<int>(s.type));                // type
            sqlite3_bind_int64 (stmt, i++, s.parentId);                              // parent_id
            sqlite3_bind_int   (stmt, i++, s.priority);                              // priority
            sqlite3_bind_double(stmt, i++, s.quantity);                              // quantity
            sqlite3_bind_int64 (stmt, i++, s.entryDate);                             // entry_date
            sqlite3_bind_double(stmt, i++, s.exitPrice);                             // exit_price
            sqlite3_bind_double(stmt, i++, s.closedPnlPct);                         // closed_pnl
            sqlite3_bind_int   (stmt, i++, s.enabled ? 1 : 0);                      // enabled
            sqlite3_bind_double(stmt, i++, s.entryFee);                              // entry_fee
            sqlite3_bind_double(stmt, i++, s.exitFee);                               // exit_fee
            sqlite3_bind_int   (stmt, i++, static_cast<int>(s.broker));              // broker

            // Compile-time safety: if you added a field to kDbFields but forgot
            // a bind line above, this will fail.
            static_assert(kDbFieldCount == 20,
                "kDbFields count changed — update BindStrategy to match");
        }

        void BindStrategyForUpdate(sqlite3_stmt* stmt, const Strategy& s)
        {
            BindStrategy(stmt, s);
            sqlite3_bind_int64(stmt, kDbFieldCount + 1, s.id);  // WHERE id=?
        }

        Strategy ReadRow(sqlite3_stmt* stmt)
        {
            Strategy s;
            int c = 0;  // sqlite3 columns are 0-indexed
            s.id          = sqlite3_column_int64(stmt, c++);                                    // id

            s.symbol      = reinterpret_cast<const char*>(sqlite3_column_text(stmt, c++));      // symbol
            s.direction   = static_cast<StrategyDirection>(sqlite3_column_int(stmt, c++));      // direction
            s.entryPrice  = static_cast<float>(sqlite3_column_double(stmt, c++));               // entry_price
            s.takeProfit  = static_cast<float>(sqlite3_column_double(stmt, c++));               // take_profit
            s.stopLoss    = static_cast<float>(sqlite3_column_double(stmt, c++));               // stop_loss
            s.status      = static_cast<StrategyStatus>(sqlite3_column_int(stmt, c++));         // status
            s.createdAt   = sqlite3_column_int64(stmt, c++);                                    // created_at
            s.triggeredAt = sqlite3_column_int64(stmt, c++);                                    // triggered_at

            const unsigned char* notes = sqlite3_column_text(stmt, c++);                        // notes
            if (notes) s.notes = reinterpret_cast<const char*>(notes);

            s.type         = static_cast<StrategyType>(sqlite3_column_int(stmt, c++));          // type
            s.parentId     = sqlite3_column_int64(stmt, c++);                                   // parent_id
            s.priority     = sqlite3_column_int(stmt, c++);                                     // priority
            s.quantity     = static_cast<float>(sqlite3_column_double(stmt, c++));               // quantity
            s.entryDate    = sqlite3_column_int64(stmt, c++);                                   // entry_date
            s.exitPrice    = static_cast<float>(sqlite3_column_double(stmt, c++));               // exit_price
            s.closedPnlPct = static_cast<float>(sqlite3_column_double(stmt, c++));              // closed_pnl
            s.enabled      = sqlite3_column_int(stmt, c++) != 0;                                // enabled
            s.entryFee     = static_cast<float>(sqlite3_column_double(stmt, c++));               // entry_fee
            s.exitFee      = static_cast<float>(sqlite3_column_double(stmt, c++));               // exit_fee
            s.broker       = static_cast<BrokerSource>(sqlite3_column_int(stmt, c++));           // broker

            // c should be 1 (id) + kDbFieldCount
            static_assert(kDbFieldCount == 20,
                "kDbFields count changed — update ReadRow to match");

            return s;
        }
    }

} // namespace stnks
