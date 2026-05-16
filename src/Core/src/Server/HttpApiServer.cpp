#include <Server/HttpApiServer.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <ctime>

using json = nlohmann::json;

namespace stnks
{
    HttpApiServer::HttpApiServer(StrategyStore& store, MarketService& market,
                                  StrategyServer* server, const Config& config)
        : config_(config), store_(store), market_(market), server_(server)
    {
        httpServer_ = std::make_unique<httplib::Server>();
        SetupRoutes();
    }

    HttpApiServer::~HttpApiServer()
    {
        Stop();
    }

    std::string HttpApiServer::GetUrl() const
    {
        return "http://" + config_.host + ":" + std::to_string(config_.port);
    }

    void HttpApiServer::Start()
    {
        if (running_) return;
        running_ = true;

        thread_ = std::thread([this]() {
            spdlog::info("[HttpApi] Listening on {}:{}", config_.host, config_.port);
            httpServer_->listen(config_.host, config_.port);
            running_ = false;
            spdlog::info("[HttpApi] Stopped");
        });
    }

    void HttpApiServer::Stop()
    {
        if (httpServer_)
            httpServer_->stop();
        if (thread_.joinable())
            thread_.join();
        running_ = false;
    }

    // ── Routes ───────────────────────────────────────────────────────────────────

    void HttpApiServer::SetupRoutes()
    {
        auto& svr = *httpServer_;

        // GET /api/status
        svr.Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
            json j;
            j["ok"] = true;
            j["monitoring"] = server_ ? server_->IsRunning() : false;
            j["timestamp"] = std::time(nullptr);
            res.set_content(j.dump(), "application/json");
        });

        // GET /api/strategies
        svr.Get("/api/strategies", [this](const httplib::Request&, httplib::Response& res) {
            auto all = store_.GetAll();
            res.set_content(StrategiesToJson(all), "application/json");
        });

        // GET /api/strategies/active
        svr.Get("/api/strategies/active", [this](const httplib::Request&, httplib::Response& res) {
            auto active = store_.GetActive();
            res.set_content(StrategiesToJson(active), "application/json");
        });

        // GET /api/strategies/:id
        svr.Get(R"(/api/strategies/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
            int64_t id = std::stoll(req.matches[1]);
            auto s = store_.GetById(id);
            if (s.id == 0)
            {
                res.status = 404;
                res.set_content(R"({"error":"not found"})", "application/json");
                return;
            }
            res.set_content(StrategyToJson(s), "application/json");
        });

        // GET /api/strategies/symbol/:sym
        svr.Get(R"(/api/strategies/symbol/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
            std::string symbol = req.matches[1];
            auto list = store_.GetBySymbol(symbol);
            res.set_content(StrategiesToJson(list), "application/json");
        });

        // POST /api/strategies
        svr.Post("/api/strategies", [this](const httplib::Request& req, httplib::Response& res) {
            auto s = JsonToStrategy(req.body);
            int64_t id = store_.Insert(s);
            if (id > 0)
            {
                s.id = id;
                res.status = 201;
                res.set_content(StrategyToJson(s), "application/json");
            }
            else
            {
                res.status = 500;
                res.set_content(R"({"error":"insert failed"})", "application/json");
            }
        });

        // PUT /api/strategies/:id
        svr.Put(R"(/api/strategies/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
            int64_t id = std::stoll(req.matches[1]);
            auto s = JsonToStrategy(req.body);
            s.id = id;
            bool ok = store_.Update(s);
            if (ok)
                res.set_content(StrategyToJson(s), "application/json");
            else
            {
                res.status = 500;
                res.set_content(R"({"error":"update failed"})", "application/json");
            }
        });

        // DELETE /api/strategies/:id
        svr.Delete(R"(/api/strategies/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
            int64_t id = std::stoll(req.matches[1]);
            bool ok = store_.Delete(id);
            json j;
            j["ok"] = ok;
            j["id"] = id;
            res.set_content(j.dump(), "application/json");
        });

        // POST /api/strategies/:id/cancel
        svr.Post(R"(/api/strategies/(\d+)/cancel)", [this](const httplib::Request& req, httplib::Response& res) {
            int64_t id = std::stoll(req.matches[1]);
            bool ok = store_.MarkTriggered(id, StrategyStatus::Cancelled, std::time(nullptr));
            json j;
            j["ok"] = ok;
            j["id"] = id;
            res.set_content(j.dump(), "application/json");
        });

        // GET /api/market/quote?symbol=X&interval=1d&range=6mo
        svr.Get("/api/market/quote", [this](const httplib::Request& req, httplib::Response& res) {
            auto symbol   = req.get_param_value("symbol");
            auto interval = req.get_param_value("interval");
            auto range    = req.get_param_value("range");

            if (symbol.empty())
            {
                res.status = 400;
                res.set_content(R"({"error":"symbol required"})", "application/json");
                return;
            }
            if (interval.empty()) interval = "1d";
            if (range.empty())    range    = "6mo";

            auto quote = market_.FetchQuote(symbol, interval, range);
            res.set_content(QuoteToJson(quote), "application/json");
        });

        // GET /api/market/search?q=X
        svr.Get("/api/market/search", [this](const httplib::Request& req, httplib::Response& res) {
            auto query = req.get_param_value("q");
            if (query.empty())
            {
                res.status = 400;
                res.set_content(R"({"error":"query required"})", "application/json");
                return;
            }
            auto matches = market_.SearchSymbols(query);
            res.set_content(SymbolMatchesToJson(matches), "application/json");
        });
    }

    // ── JSON Serialization ───────────────────────────────────────────────────────

    std::string HttpApiServer::StrategyToJson(const Strategy& s)
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

    std::string HttpApiServer::StrategiesToJson(const std::vector<Strategy>& list)
    {
        json arr = json::array();
        for (auto& s : list)
            arr.push_back(json::parse(StrategyToJson(s)));
        return arr.dump();
    }

    Strategy HttpApiServer::JsonToStrategy(const std::string& jsonStr)
    {
        Strategy s;
        try
        {
            auto j = json::parse(jsonStr);
            if (j.contains("id"))          s.id          = j["id"].get<int64_t>();
            if (j.contains("symbol"))      s.symbol      = j["symbol"].get<std::string>();
            if (j.contains("direction"))   s.direction   = (StrategyDirection)j["direction"].get<int>();
            if (j.contains("type"))        s.type        = (StrategyType)j["type"].get<int>();
            if (j.contains("entryPrice"))  s.entryPrice  = j["entryPrice"].get<float>();
            if (j.contains("takeProfit"))  s.takeProfit  = j["takeProfit"].get<float>();
            if (j.contains("stopLoss"))    s.stopLoss    = j["stopLoss"].get<float>();
            if (j.contains("status"))      s.status      = (StrategyStatus)j["status"].get<int>();
            if (j.contains("createdAt"))   s.createdAt   = j["createdAt"].get<int64_t>();
            if (j.contains("triggeredAt")) s.triggeredAt = j["triggeredAt"].get<int64_t>();
            if (j.contains("notes"))       s.notes       = j["notes"].get<std::string>();
            if (j.contains("parentId"))    s.parentId    = j["parentId"].get<int64_t>();
            if (j.contains("priority"))    s.priority    = j["priority"].get<int>();
            if (j.contains("quantity"))    s.quantity    = j["quantity"].get<float>();
            if (j.contains("entryDate"))   s.entryDate   = j["entryDate"].get<int64_t>();
        }
        catch (const std::exception& e)
        {
            spdlog::error("[HttpApi] JSON parse error: {}", e.what());
        }
        return s;
    }

    std::string HttpApiServer::QuoteToJson(const StockQuote& q)
    {
        json j;
        j["symbol"]    = q.symbol;
        j["name"]      = q.name;
        j["exchange"]  = q.exchange;
        j["currency"]  = q.currency;
        j["interval"]  = q.interval;
        j["source"]    = q.source;
        j["fetchedAt"] = q.fetchedAt;

        json candles = json::array();
        for (auto& c : q.candles)
        {
            candles.push_back({
                {"t", c.timestamp}, {"o", c.open}, {"h", c.high},
                {"l", c.low}, {"c", c.close}, {"v", c.volume}
            });
        }
        j["candles"] = candles;
        return j.dump();
    }

    std::string HttpApiServer::SymbolMatchesToJson(const std::vector<SymbolMatch>& matches)
    {
        json arr = json::array();
        for (auto& m : matches)
        {
            arr.push_back({
                {"symbol", m.symbol}, {"name", m.name},
                {"exchange", m.exchange}, {"type", m.type}
            });
        }
        return arr.dump();
    }

} // namespace stnks
