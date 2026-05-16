#include <Service/RemoteStrategyService.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace stnks
{
    RemoteStrategyService::RemoteStrategyService(HttpClient& http, const std::string& serverUrl)
        : http_(http), serverUrl_(serverUrl)
    {
        spdlog::info("[RemoteService] Connecting to {}", serverUrl_);
    }

    // ── Strategy CRUD ────────────────────────────────────────────────────────────

    int64_t RemoteStrategyService::InsertStrategy(const Strategy& s)
    {
        auto resp = http_.Post(serverUrl_ + "/api/strategies", StrategyToJson(s));
        if (!resp.Ok()) return -1;
        auto parsed = ParseStrategy(resp.body);
        return parsed.id;
    }

    bool RemoteStrategyService::UpdateStrategy(const Strategy& s)
    {
        auto resp = http_.Put(serverUrl_ + "/api/strategies/" + std::to_string(s.id),
                              StrategyToJson(s));
        return resp.Ok();
    }

    bool RemoteStrategyService::DeleteStrategy(int64_t id)
    {
        auto resp = http_.Delete(serverUrl_ + "/api/strategies/" + std::to_string(id));
        return resp.Ok();
    }

    bool RemoteStrategyService::CancelStrategy(int64_t id)
    {
        auto resp = http_.Post(serverUrl_ + "/api/strategies/" + std::to_string(id) + "/cancel", "{}");
        return resp.Ok();
    }

    Strategy RemoteStrategyService::GetStrategy(int64_t id)
    {
        auto resp = http_.Get(serverUrl_ + "/api/strategies/" + std::to_string(id));
        if (!resp.Ok()) return {};
        return ParseStrategy(resp.body);
    }

    std::vector<Strategy> RemoteStrategyService::GetAllStrategies()
    {
        auto resp = http_.Get(serverUrl_ + "/api/strategies");
        if (!resp.Ok()) return {};
        return ParseStrategies(resp.body);
    }

    std::vector<Strategy> RemoteStrategyService::GetActiveStrategies()
    {
        auto resp = http_.Get(serverUrl_ + "/api/strategies/active");
        if (!resp.Ok()) return {};
        return ParseStrategies(resp.body);
    }

    std::vector<Strategy> RemoteStrategyService::GetStrategiesBySymbol(const std::string& symbol)
    {
        auto resp = http_.Get(serverUrl_ + "/api/strategies/symbol/" + symbol);
        if (!resp.Ok()) return {};
        return ParseStrategies(resp.body);
    }

    // ── Market data ──────────────────────────────────────────────────────────────

    StockQuote RemoteStrategyService::FetchQuote(const std::string& symbol,
                                                  const std::string& interval,
                                                  const std::string& range)
    {
        std::string url = serverUrl_ + "/api/market/quote?symbol=" + symbol
                        + "&interval=" + interval + "&range=" + range;
        auto resp = http_.Get(url);
        if (!resp.Ok())
        {
            StockQuote empty;
            empty.symbol = symbol;
            return empty;
        }
        return ParseQuote(resp.body);
    }

    std::vector<SymbolMatch> RemoteStrategyService::SearchSymbols(const std::string& query)
    {
        auto resp = http_.Get(serverUrl_ + "/api/market/search?q=" + query);
        if (!resp.Ok()) return {};
        return ParseSymbolMatches(resp.body);
    }

    // ── Status ───────────────────────────────────────────────────────────────────

    bool RemoteStrategyService::IsConnected() const
    {
        auto resp = const_cast<HttpClient&>(http_).Get(serverUrl_ + "/api/status");
        lastConnected_ = resp.Ok();
        return lastConnected_;
    }

    bool RemoteStrategyService::IsMonitoring() const
    {
        auto resp = const_cast<HttpClient&>(http_).Get(serverUrl_ + "/api/status");
        if (!resp.Ok()) return false;
        try
        {
            auto j = json::parse(resp.body);
            return j.value("monitoring", false);
        }
        catch (...) { return false; }
    }

    // ── JSON helpers ─────────────────────────────────────────────────────────────

    std::string RemoteStrategyService::StrategyToJson(const Strategy& s)
    {
        json j;
        j["id"]          = s.id;
        j["symbol"]      = s.symbol;
        j["direction"]   = (int)s.direction;
        j["type"]        = (int)s.type;
        j["entryPrice"]  = s.entryPrice;
        j["takeProfit"]  = s.takeProfit;
        j["stopLoss"]    = s.stopLoss;
        j["status"]      = (int)s.status;
        j["createdAt"]   = s.createdAt;
        j["triggeredAt"] = s.triggeredAt;
        j["notes"]       = s.notes;
        j["parentId"]    = s.parentId;
        j["priority"]    = s.priority;
        j["quantity"]    = s.quantity;
        j["entryDate"]   = s.entryDate;
        return j.dump();
    }

    Strategy RemoteStrategyService::ParseStrategy(const std::string& jsonStr)
    {
        Strategy s;
        try
        {
            auto j = json::parse(jsonStr);
            s.id          = j.value("id", (int64_t)0);
            s.symbol      = j.value("symbol", "");
            s.direction   = (StrategyDirection)j.value("direction", 0);
            s.type        = (StrategyType)j.value("type", 0);
            s.entryPrice  = j.value("entryPrice", 0.f);
            s.takeProfit  = j.value("takeProfit", 0.f);
            s.stopLoss    = j.value("stopLoss", 0.f);
            s.status      = (StrategyStatus)j.value("status", 0);
            s.createdAt   = j.value("createdAt", (int64_t)0);
            s.triggeredAt = j.value("triggeredAt", (int64_t)0);
            s.notes       = j.value("notes", "");
            s.parentId    = j.value("parentId", (int64_t)0);
            s.priority    = j.value("priority", 0);
            s.quantity    = j.value("quantity", 0.f);
            s.entryDate   = j.value("entryDate", (int64_t)0);
        }
        catch (const std::exception& e)
        {
            spdlog::error("[RemoteService] Strategy parse error: {}", e.what());
        }
        return s;
    }

    std::vector<Strategy> RemoteStrategyService::ParseStrategies(const std::string& jsonStr)
    {
        std::vector<Strategy> result;
        try
        {
            auto arr = json::parse(jsonStr);
            for (auto& j : arr)
                result.push_back(ParseStrategy(j.dump()));
        }
        catch (const std::exception& e)
        {
            spdlog::error("[RemoteService] Strategies parse error: {}", e.what());
        }
        return result;
    }

    StockQuote RemoteStrategyService::ParseQuote(const std::string& jsonStr)
    {
        StockQuote q;
        try
        {
            auto j = json::parse(jsonStr);
            q.symbol    = j.value("symbol", "");
            q.name      = j.value("name", "");
            q.exchange  = j.value("exchange", "");
            q.currency  = j.value("currency", "");
            q.interval  = j.value("interval", "");
            q.source    = j.value("source", "");
            q.fetchedAt = j.value("fetchedAt", (int64_t)0);

            if (j.contains("candles"))
            {
                for (auto& c : j["candles"])
                {
                    Candle candle;
                    candle.timestamp = c.value("t", (int64_t)0);
                    candle.open      = c.value("o", 0.f);
                    candle.high      = c.value("h", 0.f);
                    candle.low       = c.value("l", 0.f);
                    candle.close     = c.value("c", 0.f);
                    candle.volume    = c.value("v", 0.f);
                    q.candles.push_back(candle);
                }
            }
        }
        catch (const std::exception& e)
        {
            spdlog::error("[RemoteService] Quote parse error: {}", e.what());
        }
        return q;
    }

    std::vector<SymbolMatch> RemoteStrategyService::ParseSymbolMatches(const std::string& jsonStr)
    {
        std::vector<SymbolMatch> result;
        try
        {
            auto arr = json::parse(jsonStr);
            for (auto& j : arr)
            {
                SymbolMatch m;
                m.symbol   = j.value("symbol", "");
                m.name     = j.value("name", "");
                m.exchange = j.value("exchange", "");
                m.type     = j.value("type", "");
                if (!m.symbol.empty())
                    result.push_back(std::move(m));
            }
        }
        catch (const std::exception& e)
        {
            spdlog::error("[RemoteService] Symbol matches parse error: {}", e.what());
        }
        return result;
    }

} // namespace stnks
