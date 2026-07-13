#include <Signals/MarketSignalService.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <ctime>
#include <cmath>

namespace stnks
{
    MarketSignalService::MarketSignalService() = default;


    void MarketSignalService::IngestGraphEvents(const std::vector<GraphEvent>& newEvents)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto& ev : newEvents)
        {
            // Only promote Pattern-origin events into signals.
            // Individual technical indicators (RSI, MACD, EMA, etc.) stay in Graph Events panel only.
            if (ev.source != EventSource::Pattern) continue;

            // Only promote events at or above the configured severity threshold
            if ((int)ev.severity < (int)minAutoSignalSeverity) continue;

            // Skip if we already have a recent signal for the same event
            if (HasRecentSignal(ev.symbol, ev.title)) continue;

            MarketSignal sig;
            sig.id        = NextId();
            sig.symbol    = ev.symbol;
            sig.title     = ev.title;
            sig.description = ev.detail;
            sig.timestamp = ev.timestamp > 0 ? ev.timestamp : std::time(nullptr);
            sig.confidence = ev.score;
            sig.origin    = (ev.source == EventSource::Pattern)
                          ? SignalOrigin::Pattern
                          : (ev.source == EventSource::Strategy)
                              ? SignalOrigin::Strategy
                              : SignalOrigin::GraphEvent;

            // Map event severity
            switch (ev.severity)
            {
            case EventSeverity::Alert:   sig.severity = SignalSeverity::Alert; break;
            case EventSeverity::Warning: sig.severity = SignalSeverity::Warning; break;
            default:                     sig.severity = SignalSeverity::Info; break;
            }

            // Determine if this event suggests an action
            // Alert-level RSI/MACD/EMA events often imply a direction
            if (ev.severity == EventSeverity::Alert)
            {
                std::string titleLower = ev.title;
                for (auto& c : titleLower) c = (char)std::tolower((unsigned char)c);

                bool bullish = titleLower.find("bullish") != std::string::npos ||
                               titleLower.find("oversold exit") != std::string::npos ||
                               titleLower.find("golden") != std::string::npos ||
                               titleLower.find("morning star") != std::string::npos;
                bool bearish = titleLower.find("bearish") != std::string::npos ||
                               titleLower.find("overbought exit") != std::string::npos ||
                               titleLower.find("death") != std::string::npos ||
                               titleLower.find("evening star") != std::string::npos;

                if (bullish)
                {
                    sig.action    = SignalAction::Buy;
                    sig.direction = StrategyDirection::Long;
                }
                else if (bearish)
                {
                    sig.action    = SignalAction::Sell;
                    sig.direction = StrategyDirection::Short;
                }
            }

            // Add the source event as a resource
            SignalResource res;
            res.type        = ResourceType::GraphEvent;
            res.label       = std::string(EventSourceName(ev.source)) + ": " + ev.title;
            res.content     = ev.detail;
            res.score       = ev.score;
            res.timestamp   = ev.timestamp;
            res.candleIdx   = ev.candleIdx;
            res.eventSource = ev.source;
            sig.resources.push_back(std::move(res));

            signals_.push_back(std::move(sig));
        }

        Trim();
    }

    void MarketSignalService::IngestPatternMatches(const std::string& symbol,
                                                    const std::vector<PatternMatch>& matches,
                                                    float currentPrice)
    {
        if (!patternSignalsEnabled) return;

        std::lock_guard<std::mutex> lock(mutex_);

        for (auto& pm : matches)
        {
            if (pm.score < minPatternScore) continue;
            if (HasRecentSignal(symbol, pm.patternName)) continue;

            MarketSignal sig;
            sig.id          = NextId();
            sig.symbol      = symbol;
            sig.title       = pm.patternName;
            sig.description = pm.description;
            sig.timestamp   = pm.timestamp > 0 ? pm.timestamp : std::time(nullptr);
            sig.confidence  = pm.score;
            sig.origin      = SignalOrigin::Pattern;
            sig.severity    = pm.score >= 0.7f ? SignalSeverity::Alert : SignalSeverity::Warning;
            sig.action      = ActionFromPattern(pm.patternName);
            sig.direction   = DirectionFromPattern(pm.patternName);
            sig.suggestedEntry = currentPrice;

            // Add matched conditions as resources
            for (auto& m : pm.matched)
            {
                SignalResource res;
                res.type    = ResourceType::PatternMatch;
                res.label   = m;
                res.content = "Condition matched within pattern window";
                res.score   = 1.f;
                sig.resources.push_back(std::move(res));
            }

            // Add missed conditions (dimmed in UI)
            for (auto& m : pm.missed)
            {
                SignalResource res;
                res.type    = ResourceType::PatternMatch;
                res.label   = m;
                res.content = "Condition not matched";
                res.score   = 0.f;
                sig.resources.push_back(std::move(res));
            }

            signals_.push_back(std::move(sig));
        }

        Trim();
    }

    void MarketSignalService::IngestAIResult(const AnalysisResult& result, float currentPrice)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Convert AI recommendations into signals
        for (auto& rec : result.recommendations)
        {
            if (HasRecentSignal(rec.symbol, rec.title)) continue;

            MarketSignal sig;
            sig.id          = NextId();
            sig.symbol      = rec.symbol;
            sig.title       = "AI: " + rec.title;
            sig.description = rec.body;
            sig.timestamp   = rec.timestamp > 0 ? rec.timestamp : std::time(nullptr);
            sig.origin      = SignalOrigin::AI;
            sig.severity    = SignalSeverity::Info;

            // The AI response body is itself a resource
            SignalResource res;
            res.type    = ResourceType::AIResponse;
            res.label   = "AI Analysis";
            res.content = rec.body;
            sig.resources.push_back(std::move(res));

            signals_.push_back(std::move(sig));
        }

        // Convert AI warnings into signals
        for (auto& warn : result.warnings)
        {
            if (HasRecentSignal(warn.symbol, warn.title)) continue;

            MarketSignal sig;
            sig.id          = NextId();
            sig.symbol      = warn.symbol;
            sig.title       = "AI: " + warn.title;
            sig.description = warn.body;
            sig.timestamp   = warn.timestamp > 0 ? warn.timestamp : std::time(nullptr);
            sig.origin      = SignalOrigin::AI;

            switch (warn.severity)
            {
            case InsightSeverity::Alert:   sig.severity = SignalSeverity::Alert; break;
            case InsightSeverity::Warning: sig.severity = SignalSeverity::Warning; break;
            default:                       sig.severity = SignalSeverity::Info; break;
            }

            SignalResource res;
            res.type    = ResourceType::AIResponse;
            res.label   = "AI Warning";
            res.content = warn.body;
            sig.resources.push_back(std::move(res));

            signals_.push_back(std::move(sig));
        }

        // Convert AI operations into actionable signals
        for (auto& op : result.operations)
        {
            if (HasRecentSignal(op.symbol, OperationTypeToString(op.type))) continue;

            MarketSignal sig;
            sig.id          = NextId();
            sig.symbol      = op.symbol;
            sig.timestamp   = op.timestamp > 0 ? op.timestamp : std::time(nullptr);
            sig.origin      = SignalOrigin::AI;
            sig.confidence  = op.confidence;
            sig.strategyId  = op.strategyId;
            sig.suggestedEntry = op.suggestedPrice > 0.f ? op.suggestedPrice : currentPrice;

            switch (op.urgency)
            {
            case InsightSeverity::Alert:   sig.severity = SignalSeverity::Alert; break;
            case InsightSeverity::Warning: sig.severity = SignalSeverity::Warning; break;
            default:                       sig.severity = SignalSeverity::Info; break;
            }

            // Map operation type to signal action
            switch (op.type)
            {
            case OperationType::Buy:
                sig.action    = SignalAction::Buy;
                sig.direction = StrategyDirection::Long;
                sig.title     = "AI: Buy " + op.symbol;
                break;
            case OperationType::Sell:
                sig.action    = SignalAction::Sell;
                sig.direction = StrategyDirection::Short;
                sig.title     = "AI: Sell " + op.symbol;
                break;
            case OperationType::AdjustTP:
                sig.action      = SignalAction::AdjustTP;
                sig.suggestedTP = op.suggestedPrice;
                sig.title       = "AI: Adjust TP " + op.symbol;
                break;
            case OperationType::AdjustSL:
                sig.action      = SignalAction::AdjustSL;
                sig.suggestedSL = op.suggestedPrice;
                sig.title       = "AI: Adjust SL " + op.symbol;
                break;
            case OperationType::Hold:
                sig.action = SignalAction::None;
                sig.title  = "AI: Hold " + op.symbol;
                break;
            }

            // Add reason as resource
            if (!op.reason.empty())
            {
                SignalResource res;
                res.type    = ResourceType::AIResponse;
                res.label   = "AI Reasoning";
                res.content = op.reason;
                sig.resources.push_back(std::move(res));
            }

            // Add suggested price as resource
            if (op.suggestedPrice > 0.f)
            {
                SignalResource res;
                res.type  = ResourceType::PriceLevel;
                res.label = std::string("Suggested ") + OperationTypeToString(op.type) + " price";
                res.price = op.suggestedPrice;
                char buf[64];
                snprintf(buf, sizeof(buf), "%.4f", op.suggestedPrice);
                res.content = buf;
                sig.resources.push_back(std::move(res));
            }

            if (op.executed)
            {
                sig.accepted = true;
                SignalResource res;
                res.type    = ResourceType::Note;
                res.label   = "Auto-executed";
                res.content = "This operation was auto-executed by AI auto-trade.";
                sig.resources.push_back(std::move(res));
            }

            signals_.push_back(std::move(sig));
        }

        Trim();
    }

    void MarketSignalService::IngestStrategyTrigger(const Strategy& strategy,
                                                     StrategyStatus trigger,
                                                     float exitPrice)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        MarketSignal sig;
        sig.id        = NextId();
        sig.symbol    = strategy.symbol;
        sig.timestamp = std::time(nullptr);
        sig.origin    = SignalOrigin::Strategy;
        sig.severity  = SignalSeverity::Alert;
        sig.strategyId = strategy.id;

        float pnlPct = strategy.UnrealizedPnLPercent(exitPrice);

        if (trigger == StrategyStatus::TPHit)
        {
            sig.title = "TP Hit: " + strategy.symbol;
            sig.action = SignalAction::None; // Already handled
            char buf[128];
            snprintf(buf, sizeof(buf), "%s %s take-profit hit at %.4f (%+.2f%%)",
                     strategy.symbol.c_str(), DirectionToString(strategy.direction),
                     exitPrice, pnlPct);
            sig.description = buf;
        }
        else if (trigger == StrategyStatus::SLHit)
        {
            sig.title = "SL Hit: " + strategy.symbol;
            sig.action = SignalAction::None;
            char buf[128];
            snprintf(buf, sizeof(buf), "%s %s stop-loss hit at %.4f (%+.2f%%)",
                     strategy.symbol.c_str(), DirectionToString(strategy.direction),
                     exitPrice, pnlPct);
            sig.description = buf;
        }
        else
        {
            sig.title = "Cancelled: " + strategy.symbol;
            sig.severity = SignalSeverity::Info;
            sig.action = SignalAction::None;
            char buf[128];
            snprintf(buf, sizeof(buf), "%s strategy cancelled at %.4f",
                     strategy.symbol.c_str(), exitPrice);
            sig.description = buf;
        }

        // Add strategy details as resources
        {
            SignalResource res;
            res.type  = ResourceType::PriceLevel;
            res.label = "Entry Price";
            res.price = strategy.entryPrice;
            char buf[32]; snprintf(buf, sizeof(buf), "%.4f", strategy.entryPrice);
            res.content = buf;
            sig.resources.push_back(std::move(res));
        }
        {
            SignalResource res;
            res.type  = ResourceType::PriceLevel;
            res.label = "Exit Price";
            res.price = exitPrice;
            char buf[64]; snprintf(buf, sizeof(buf), "%.4f (%+.2f%%)", exitPrice, pnlPct);
            res.content = buf;
            sig.resources.push_back(std::move(res));
        }
        if (!strategy.notes.empty())
        {
            SignalResource res;
            res.type    = ResourceType::Note;
            res.label   = "Strategy Notes";
            res.content = strategy.notes;
            sig.resources.push_back(std::move(res));
        }

        signals_.push_back(std::move(sig));
        Trim();
    }

    void MarketSignalService::Push(MarketSignal&& signal)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (signal.id == 0) signal.id = NextId();
        if (signal.timestamp == 0) signal.timestamp = std::time(nullptr);
        signals_.push_back(std::move(signal));
        Trim();
    }


    std::vector<MarketSignal> MarketSignalService::GetAll() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto copy = signals_;
        std::sort(copy.begin(), copy.end(),
                  [](const MarketSignal& a, const MarketSignal& b) { return a.timestamp > b.timestamp; });
        return copy;
    }

    std::vector<MarketSignal> MarketSignalService::GetForSymbol(const std::string& symbol) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MarketSignal> result;
        for (auto& s : signals_)
            if (s.symbol == symbol) result.push_back(s);
        std::sort(result.begin(), result.end(),
                  [](const MarketSignal& a, const MarketSignal& b) { return a.timestamp > b.timestamp; });
        return result;
    }

    std::vector<MarketSignal> MarketSignalService::GetActionable() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MarketSignal> result;
        for (auto& s : signals_)
            if (s.IsActionable()) result.push_back(s);
        std::sort(result.begin(), result.end(),
                  [](const MarketSignal& a, const MarketSignal& b) {
                      if ((int)a.severity != (int)b.severity) return (int)a.severity > (int)b.severity;
                      return a.timestamp > b.timestamp;
                  });
        return result;
    }

    std::vector<MarketSignal> MarketSignalService::GetUnread() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<MarketSignal> result;
        for (auto& s : signals_)
            if (!s.read) result.push_back(s);
        return result;
    }

    int MarketSignalService::UnreadCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int count = 0;
        for (auto& s : signals_)
            if (!s.read && !s.dismissed) ++count;
        return count;
    }

    int MarketSignalService::ActionableCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int count = 0;
        for (auto& s : signals_)
            if (s.IsActionable()) ++count;
        return count;
    }


    void MarketSignalService::MarkRead(int64_t signalId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& s : signals_)
            if (s.id == signalId) { s.read = true; return; }
    }

    void MarketSignalService::MarkDismissed(int64_t signalId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& s : signals_)
            if (s.id == signalId) { s.dismissed = true; s.read = true; return; }
    }

    void MarketSignalService::MarkAccepted(int64_t signalId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& s : signals_)
            if (s.id == signalId) { s.accepted = true; s.read = true; return; }
    }

    void MarketSignalService::DismissAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& s : signals_)
        {
            s.dismissed = true;
            s.read = true;
        }
    }

    void MarketSignalService::Clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        signals_.clear();
    }


    int64_t MarketSignalService::NextId()
    {
        return nextId_++;
    }

    void MarketSignalService::Trim()
    {
        // Remove dismissed signals over capacity, keep recent ones
        if ((int)signals_.size() <= maxSignals) return;

        // First remove old dismissed signals
        signals_.erase(
            std::remove_if(signals_.begin(), signals_.end(),
                [](const MarketSignal& s) { return s.dismissed; }),
            signals_.end());

        // If still over limit, remove oldest
        if ((int)signals_.size() > maxSignals)
        {
            std::sort(signals_.begin(), signals_.end(),
                [](const MarketSignal& a, const MarketSignal& b) { return a.timestamp > b.timestamp; });
            signals_.resize(maxSignals);
        }
    }

    bool MarketSignalService::HasRecentSignal(const std::string& symbol,
                                               const std::string& title,
                                               int64_t withinSeconds) const
    {
        // Caller must hold mutex_
        int64_t now = std::time(nullptr);
        for (auto& s : signals_)
        {
            if (s.symbol == symbol && s.title == title &&
                (now - s.timestamp) < withinSeconds)
                return true;
        }
        return false;
    }

    SignalAction MarketSignalService::ActionFromPattern(const std::string& patternName)
    {
        std::string lower = patternName;
        for (auto& c : lower) c = (char)std::tolower((unsigned char)c);

        if (lower.find("bullish") != std::string::npos ||
            lower.find("golden") != std::string::npos ||
            lower.find("accumulation") != std::string::npos ||
            lower.find("uptrend") != std::string::npos)
            return SignalAction::Buy;

        if (lower.find("bearish") != std::string::npos ||
            lower.find("death") != std::string::npos ||
            lower.find("divergence") != std::string::npos)
            return SignalAction::Sell;

        if (lower.find("squeeze") != std::string::npos ||
            lower.find("breakout") != std::string::npos)
            return SignalAction::CreateTPSL;

        return SignalAction::None;
    }

    StrategyDirection MarketSignalService::DirectionFromPattern(const std::string& patternName)
    {
        std::string lower = patternName;
        for (auto& c : lower) c = (char)std::tolower((unsigned char)c);

        if (lower.find("bearish") != std::string::npos ||
            lower.find("death") != std::string::npos)
            return StrategyDirection::Short;

        return StrategyDirection::Long;
    }

} // namespace stnks
