#include <Server/StrategyServer.hpp>
#include <Server/SentimentAction.hpp>
#include <Market/MarketHours.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <set>
#include <thread>
#include <ctime>

namespace stnks
{
    StrategyServer::StrategyServer() : StrategyServer(Config{}) {}

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

            // Adaptive poll interval based on market hours
            // Crypto: 5s, Market open: config interval, Closed: 5min
            auto symbols = GetActiveSymbols();
            int adaptiveInterval = symbols.empty()
                ? config_.pollIntervalSec
                : MarketHours::GetServerPollInterval(symbols);

            // Never go below configured minimum, but allow faster for crypto
            int effectiveInterval = std::max(adaptiveInterval, 5);

            // Log when interval changes significantly
            if (effectiveInterval != lastEffectiveInterval_)
            {
                spdlog::info("[StrategyServer] Poll interval adjusted to {}s (symbols: {})",
                             effectiveInterval, symbols.size());
                lastEffectiveInterval_ = effectiveInterval;
            }

            // Sleep in small increments so Stop() is responsive
            auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::seconds(effectiveInterval);

            while (running_ && std::chrono::steady_clock::now() < deadline)
            {
                if (idleCallback_)
                    idleCallback_();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        spdlog::info("[StrategyServer] Stopped");
    }

    void StrategyServer::Stop()
    {
        running_ = false;
    }


    void StrategyServer::PollAndCheck()
    {
        auto symbols = GetActiveSymbols();
        if (symbols.empty())
        {
            spdlog::debug("[StrategyServer] No active strategies to monitor");
            return;
        }

        // Filter: only process symbols whose market is active or near open/close
        std::vector<std::string> processable;
        processable.reserve(symbols.size());

        for (auto& sym : symbols)
        {
            auto state = MarketHours::GetState(sym);
            if (MarketHours::ShouldProcess(sym))
            {
                processable.push_back(sym);
                spdlog::info("  - {} [{}]", sym, MarketHours::StateToString(state));
            }
            else
            {
                spdlog::debug("  - {} [{}] SKIPPED (market closed)", sym, MarketHours::StateToString(state));
            }
        }

        if (processable.empty())
        {
            spdlog::info("[StrategyServer] All {} symbol(s) skipped (markets closed)", symbols.size());
            return;
        }

        spdlog::info("[StrategyServer] Processing {}/{} symbol(s)", processable.size(), symbols.size());

        // Fetch quotes for processable symbols only
        for (auto& symbol : processable)
        {
            market_->FetchQuoteAsync(symbol);
        }

        // Wait for results, drain them as they come
        // Give up to 30 seconds for all fetches to complete
        auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        size_t received = 0;

        while (received < processable.size() &&
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

                    // Broadcast tick to connected clients
                    if (tickCallback_)
                        tickCallback_(result.symbol, result.quote);
                }
                ++received;
            });

            if (received < processable.size())
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (received < processable.size())
            spdlog::warn("[StrategyServer] Timed out waiting for {} symbol(s)",
                         processable.size() - received);
    }

    void StrategyServer::CheckStrategies(const std::string& symbol, const StockQuote& quote)
    {
        if (quote.candles.empty()) return;

        const auto& last = quote.candles.back();
        float price = last.close;  // Use close price — the latest known price

        // Guard against invalid/uninitialized candle data
        if (price <= 0.f) return;

        auto strategies = store_->GetBySymbol(symbol);

        // Log the full strategy table for this symbol
        for (auto& strat : strategies)
        {
            const char* statusStr = StatusToString(strat.status);
            spdlog::info("[Monitor] {} #{} {} {} [{}] entry={:.4f} tp={:.4f} sl={:.4f} exit={:.4f} price={:.4f} enabled={}",
                         symbol, strat.id,
                         DirectionToString(strat.direction),
                         strat.IsEnabled() ? "ON" : "OFF",
                         statusStr,
                         strat.entryPrice, strat.takeProfit, strat.stopLoss,
                         strat.exitPrice, price, strat.enabled);
        }

        for (auto& strat : strategies)
        {
            if (!strat.IsActive() || !strat.IsEnabled()) continue;
            if (strat.takeProfit <= 0.f && strat.stopLoss <= 0.f) continue;

            bool tpHit = false;
            bool slHit = false;

            // Check against close price (latest known), not high/low.
            // Using high/low of daily candles would trigger on the full day's range,
            // causing false triggers for tight TP/SL levels.
            if (strat.direction == StrategyDirection::Long)
            {
                if (strat.takeProfit > 0.f) tpHit = price >= strat.takeProfit;
                if (strat.stopLoss > 0.f)   slHit = price <= strat.stopLoss;
            }
            else
            {
                if (strat.takeProfit > 0.f) tpHit = price <= strat.takeProfit;
                if (strat.stopLoss > 0.f)   slHit = price >= strat.stopLoss;
            }

            if (tpHit)
            {
                spdlog::info("[StrategyServer] TP HIT #{} {} {} price={:.4f} >= tp={:.4f} (entry={:.4f})",
                             strat.id, symbol, DirectionToString(strat.direction),
                             price, strat.takeProfit, strat.entryPrice);

                int64_t now = std::time(nullptr);
                float pnlPct = strat.UnrealizedPnLPercent(price);
                store_->MarkTriggered(strat.id, StrategyStatus::TPHit, now, price, pnlPct);
                FireActions(strat, StrategyStatus::TPHit, price);
            }
            else if (slHit)
            {
                spdlog::info("[StrategyServer] SL HIT #{} {} {} price={:.4f} <= sl={:.4f} (entry={:.4f})",
                             strat.id, symbol, DirectionToString(strat.direction),
                             price, strat.stopLoss, strat.entryPrice);

                int64_t now = std::time(nullptr);
                float pnlPct = strat.UnrealizedPnLPercent(price);
                store_->MarkTriggered(strat.id, StrategyStatus::SLHit, now, price, pnlPct);
                FireActions(strat, StrategyStatus::SLHit, price);
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

        // Only run sentiment for symbols whose market is active
        std::vector<std::string> processable;
        for (auto& sym : symbols)
            if (MarketHours::ShouldProcess(sym))
                processable.push_back(sym);

        if (processable.empty())
        {
            spdlog::info("[StrategyServer] Sentiment analysis skipped (all markets closed)");
            return;
        }

        spdlog::info("[StrategyServer] Running sentiment analysis for {} symbol(s)...", processable.size());

        for (auto& symbol : processable)
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
