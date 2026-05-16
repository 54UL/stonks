#include <Service/LocalStrategyService.hpp>
#include <spdlog/spdlog.h>
#include <ctime>

namespace stnks
{
    LocalStrategyService::LocalStrategyService(HttpClient& http, ThreadRegistry& threads,
                                                const Config& config)
    {
        store_  = std::make_unique<StrategyStore>(config.server.dbName);
        market_ = std::make_unique<MarketService>(http, threads);

        // Create server in shared mode — it borrows our store and market
        server_ = std::make_unique<StrategyServer>(config.server, *store_, *market_);

        if (config.startMonitoring)
        {
            serverThread_ = std::thread([this]() {
                spdlog::info("[LocalService] Embedded server started");
                server_->Run();
                spdlog::info("[LocalService] Embedded server stopped");
            });
        }

        spdlog::info("[LocalService] Initialized (monolith mode, DB: {})", store_->GetDbPath());
    }

    LocalStrategyService::~LocalStrategyService()
    {
        if (server_)
            server_->Stop();
        if (serverThread_.joinable())
            serverThread_.join();
    }

    bool LocalStrategyService::IsMonitoring() const
    {
        return server_ && server_->IsRunning();
    }

    // ── Strategy CRUD (direct to SQLite) ─────────────────────────────────────────

    int64_t LocalStrategyService::InsertStrategy(const Strategy& s)
    {
        return store_->Insert(s);
    }

    bool LocalStrategyService::UpdateStrategy(const Strategy& s)
    {
        return store_->Update(s);
    }

    bool LocalStrategyService::DeleteStrategy(int64_t id)
    {
        return store_->Delete(id);
    }

    bool LocalStrategyService::CancelStrategy(int64_t id)
    {
        return store_->MarkTriggered(id, StrategyStatus::Cancelled, std::time(nullptr));
    }

    Strategy LocalStrategyService::GetStrategy(int64_t id)
    {
        return store_->GetById(id);
    }

    std::vector<Strategy> LocalStrategyService::GetAllStrategies()
    {
        return store_->GetAll();
    }

    std::vector<Strategy> LocalStrategyService::GetActiveStrategies()
    {
        return store_->GetActive();
    }

    std::vector<Strategy> LocalStrategyService::GetStrategiesBySymbol(const std::string& symbol)
    {
        return store_->GetBySymbol(symbol);
    }

    // ── Market data (direct) ─────────────────────────────────────────────────────

    StockQuote LocalStrategyService::FetchQuote(const std::string& symbol,
                                                 const std::string& interval,
                                                 const std::string& range)
    {
        return market_->FetchQuote(symbol, interval, range);
    }

    std::vector<SymbolMatch> LocalStrategyService::SearchSymbols(const std::string& query)
    {
        return market_->SearchSymbols(query);
    }

} // namespace stnks
