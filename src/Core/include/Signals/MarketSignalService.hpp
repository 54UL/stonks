#pragma once

#include <Signals/MarketSignal.hpp>
#include <Events/GraphEventService.hpp>
#include <AI/IMarketAnalyzer.hpp>
#include <vector>
#include <string>
#include <mutex>
#include <functional>
#include <cstdint>

namespace stnks
{
    // Unified signal pipeline that collects events from all sources
    // (technical, pattern, AI, broker, strategy) and produces actionable
    // MarketSignal items for the UI.
    //
    // Thread-safe: signals can be pushed from background analysis threads.
    class MarketSignalService
    {
    public:
        MarketSignalService();

        // ── Signal ingestion ──────────────────────────────────────────────────

        // Process new graph events and convert Alert-level ones into signals.
        // Call after GraphEventService::Scan() produces new events.
        void IngestGraphEvents(const std::vector<GraphEvent>& newEvents);

        // Process pattern matches and create actionable signals for high-score patterns.
        void IngestPatternMatches(const std::string& symbol,
                                  const std::vector<PatternMatch>& matches,
                                  float currentPrice);

        // Process AI analysis results (recommendations, warnings, operations).
        void IngestAIResult(const AnalysisResult& result, float currentPrice);

        // Record a strategy trigger (TP/SL hit) as a signal.
        void IngestStrategyTrigger(const Strategy& strategy, StrategyStatus trigger, float exitPrice);

        // Push a custom/manual signal (from broker events, webhooks, etc.)
        void Push(MarketSignal&& signal);

        // ── Signal access ─────────────────────────────────────────────────────

        // Get all signals (most recent first), optionally filtered
        std::vector<MarketSignal> GetAll() const;
        std::vector<MarketSignal> GetForSymbol(const std::string& symbol) const;
        std::vector<MarketSignal> GetActionable() const; // Not dismissed, not accepted
        std::vector<MarketSignal> GetUnread() const;

        int UnreadCount() const;
        int ActionableCount() const;

        // ── Signal state changes ──────────────────────────────────────────────

        void MarkRead(int64_t signalId);
        void MarkDismissed(int64_t signalId);
        void MarkAccepted(int64_t signalId);
        void DismissAll();
        void Clear();

        // ── Configuration ─────────────────────────────────────────────────────

        // Minimum severity to auto-create signals from graph events
        SignalSeverity minAutoSignalSeverity = SignalSeverity::Warning;

        // Whether pattern matches should auto-generate actionable signals
        bool patternSignalsEnabled = true;

        // Minimum pattern score to generate a signal
        float minPatternScore = 0.5f;

        // Max signals kept
        int maxSignals = 200;

    private:
        int64_t NextId();
        void Trim();

        // Dedup: avoid duplicate signals for same event
        bool HasRecentSignal(const std::string& symbol, const std::string& title, int64_t withinSeconds = 300) const;

        // Determine action from pattern name / AI operation
        static SignalAction ActionFromPattern(const std::string& patternName);
        static StrategyDirection DirectionFromPattern(const std::string& patternName);

        mutable std::mutex mutex_;
        std::vector<MarketSignal> signals_;
        int64_t nextId_ = 1;
    };

} // namespace stnks
