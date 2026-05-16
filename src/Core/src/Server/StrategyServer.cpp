#include <Server/StrategyServer.hpp>
#include <Server/SentimentAction.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <set>
#include <thread>
#include <ctime>

namespace stnks
{
    StrategyServer::StrategyServer(const Config& config)
        : config_(config)
        , threads_(2)  // Lightweight pool for headless mode
    {
        ownedHttp_   = std::make_unique<HttpClient>(threads_);
        ownedMarket_ = std::make_unique<MarketService>(*ownedHttp_, threads_);
        ownedStore_  = std::make_unique<StrategyStore>(config_.dbName);
        store_  = ownedStore_.get();
        market_ = ownedMarket_.get();

        spdlog::info("[StrategyServer] Initialized (poll interval: {}s, DB: {})",
                     config_.pollIntervalSec, store_->GetDbPath());
    }

    StrategyServer::StrategyServer(const Config& config, StrategyStore& store,
                                     MarketService& market)
        : config_(config)
        , threads_(0)
        , store_(&store)
        , market_(&market)
    {
        spdlog::info("[StrategyServer] Initialized (shared mode, poll interval: {}s)",
                     config_.pollIntervalSec);
    }

    StrategyServer::~StrategyServer()
    {
        Stop();
    }

    void StrategyServer::AddAction(std::unique_ptr<IStrategyAction> action)
    {
        spdlog::info("[StrategyServer] Registered action: {}", action->Name());

        if (!action->Validate())
            spdlog::warn("[StrategyServer] Action '{}' validation failed — will still be called", action->Name());

        actions_.push_back(std::move(action));
    }

    void StrategyServer::Run()
    {
        if (actions_.empty())
        {
            spdlog::warn("[StrategyServer] No actions registered — monitoring only (no side effects)");
        }

        running_ = true;

        spdlog::info("[StrategyServer] Starting monitoring loop...");

        lastSentimentCheck_ = std::chrono::steady_clock::now();

        while (running_)
        {
            PollAndCheck();

            // Run sentiment analysis periodically
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastSentimentCheck_).count();
            if (elapsed >= config_.sentimentIntervalSec)
            {
                RunSentimentAnalysis();
                lastSentimentCheck_ = std::chrono::steady_clock::now();
            }

            // Sleep in small increments so Stop() is responsive
            auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::seconds(config_.pollIntervalSec);

            while (running_ && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }

        spdlog::info("[StrategyServer] Stopped");
    }

    void StrategyServer::Stop()
    {
        running_ = false;
    }

    // ── Core logic ──────────────────────────────────────────────────────────────

    void StrategyServer::PollAndCheck()
    {
        auto symbols = GetActiveSymbols();
        if (symbols.empty())
        {
            spdlog::debug("[StrategyServer] No active strategies to monitor");
            return;
        }

        spdlog::info("[StrategyServer] Checking {} symbol(s): ", symbols.size());
        for (auto& sym : symbols)
            spdlog::info("  - {}", sym);

        // Fetch quotes synchronously (headless, no UI to block)
        for (auto& symbol : symbols)
        {
            market_->FetchQuoteAsync(symbol);
        }

        // Wait for results, drain them as they come
        // Give up to 30 seconds for all fetches to complete
        auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        size_t received = 0;

        while (received < symbols.size() &&
               std::chrono::steady_clock::now() < timeout &&
               running_)
        {
            market_->DrainQuoteResults([&](QuoteFetchResult&& result) {
                if (result.quote.candles.empty())
                {
                    spdlog::warn("[StrategyServer] No data for {}", result.symbol);
                }
                else
                {
                    spdlog::info("[StrategyServer] Got {} candles for {} (last close: {:.2f})",
                                 result.quote.candles.size(), result.symbol,
                                 result.quote.candles.back().close);

                    CheckStrategies(result.symbol, result.quote);
                }
                ++received;
            });

            if (received < symbols.size())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (received < symbols.size())
            spdlog::warn("[StrategyServer] Timed out waiting for {} symbol(s)",
                         symbols.size() - received);
    }

    void StrategyServer::CheckStrategies(const std::string& symbol, const StockQuote& quote)
    {
        if (quote.candles.empty()) return;

        const auto& last = quote.candles.back();
        float high  = last.high;
        float low   = last.low;
        float close = last.close;

        auto strategies = store_->GetBySymbol(symbol);

        for (auto& strat : strategies)
        {
            if (!strat.IsActive()) continue;

            bool tpHit = false;
            bool slHit = false;

            if (strat.direction == StrategyDirection::Long)
            {
                tpHit = high >= strat.takeProfit;
                slHit = low  <= strat.stopLoss;
            }
            else
            {
                tpHit = low  <= strat.takeProfit;
                slHit = high >= strat.stopLoss;
            }

            if (tpHit)
            {
                spdlog::info("[StrategyServer] TP HIT for {} #{} @ {:.2f} (target: {:.2f})",
                             symbol, strat.id, close, strat.takeProfit);

                int64_t now = std::time(nullptr);
                store_->MarkTriggered(strat.id, StrategyStatus::TPHit, now);
                FireActions(strat, StrategyStatus::TPHit, close);
            }
            else if (slHit)
            {
                spdlog::info("[StrategyServer] SL HIT for {} #{} @ {:.2f} (stop: {:.2f})",
                             symbol, strat.id, close, strat.stopLoss);

                int64_t now = std::time(nullptr);
                store_->MarkTriggered(strat.id, StrategyStatus::SLHit, now);
                FireActions(strat, StrategyStatus::SLHit, close);
            }
        }
    }

    void StrategyServer::FireActions(const Strategy& strategy, StrategyStatus triggerType, float currentPrice)
    {
        StrategyTriggerEvent event{
            strategy,
            triggerType,
            currentPrice,
            strategy.symbol,
            std::time(nullptr)
        };

        for (auto& action : actions_)
        {
            try
            {
                bool ok = action->Execute(event);
                if (!ok)
                    spdlog::error("[StrategyServer] Action '{}' failed for strategy #{}",
                                  action->Name(), strategy.id);
            }
            catch (const std::exception& e)
            {
                spdlog::error("[StrategyServer] Action '{}' threw: {}",
                              action->Name(), e.what());
            }
        }
    }

    void StrategyServer::RunSentimentAnalysis()
    {
        // Find SentimentAction among registered actions
        SentimentAction* sentiment = nullptr;
        for (auto& action : actions_)
        {
            sentiment = dynamic_cast<SentimentAction*>(action.get());
            if (sentiment) break;
        }

        if (!sentiment) return;

        auto symbols = GetActiveSymbols();
        if (symbols.empty()) return;

        spdlog::info("[StrategyServer] Running sentiment analysis for {} symbol(s)...", symbols.size());

        for (auto& symbol : symbols)
        {
            if (!running_) break;

            auto strategies = store_->GetBySymbol(symbol);
            std::vector<Strategy> active;
            for (auto& s : strategies)
                if (s.IsActive()) active.push_back(s);

            auto result = sentiment->AnalyzeSymbol(symbol, active);

            // Log results
            for (auto& rec : result.recommendations)
                spdlog::info("[StrategyServer] AI REC [{}] {}: {}", rec.symbol, rec.title, rec.body);
            for (auto& warn : result.warnings)
            {
                const char* sev = warn.severity == InsightSeverity::Alert   ? "ALERT"
                                : warn.severity == InsightSeverity::Warning ? "WARN"
                                : "INFO";
                spdlog::info("[StrategyServer] AI {} [{}] {}: {}", sev, warn.symbol, warn.title, warn.body);
            }

            // Forward to callback if set
            if (sentimentCallback_)
                sentimentCallback_(result);
        }
    }

    std::vector<std::string> StrategyServer::GetActiveSymbols()
    {
        auto active = store_->GetActive();

        std::set<std::string> unique;
        for (auto& s : active)
            unique.insert(s.symbol);

        return {unique.begin(), unique.end()};
    }

} // namespace stnks
