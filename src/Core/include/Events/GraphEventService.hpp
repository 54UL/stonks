#pragma once

#include <Events/GraphEvent.hpp>
#include <Market/MarketData.hpp>
#include <Strategy/Strategy.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace stnks
{
    // Scans chart data for notable technical events across all indicators.
    // Call Scan() whenever chart data is refreshed; it diffs against previous
    // state and only emits new events.
    //
    // Thread-safe: Scan() may be called from background threads,
    // GetEvents()/Consume() from the UI thread.
    class GraphEventService
    {
    public:
        GraphEventService();

        // Scan candle data for a symbol. Detects new events since last scan.
        void Scan(const std::string& symbol, const StockQuote& quote);

        // Record a strategy trigger event (TP/SL hit, cancel)
        void RecordStrategyEvent(const Strategy& s, StrategyStatus trigger, float exitPrice);

        // Get all events for a symbol (most recent first)
        std::vector<GraphEvent> GetEvents(const std::string& symbol) const;

        // Get all events across all symbols (most recent first)
        std::vector<GraphEvent> GetAllEvents() const;

        // Consume: returns events and marks them as read (clears new flag)
        // Returns only events added since last consume.
        std::vector<GraphEvent> ConsumeNew();

        // Total unread count
        int UnreadCount() const;

        // Push an externally generated event (e.g., from VolumeProfileLayer)
        void PushEvent(const std::string& symbol, GraphEvent event);

        // Clear all events for a symbol (or all if empty)
        void Clear(const std::string& symbol = "");

        // Get latest pattern matches for a symbol
        std::vector<PatternMatch> GetPatternMatches(const std::string& symbol) const;

        // Max events kept per symbol
        int maxEventsPerSymbol = 100;

    private:
        // Per-symbol scan state to avoid duplicate detections
        struct ScanState
        {
            int     lastScannedIdx  = -1; // Last candle index we fully scanned
            int     lastCandleCount = 0;  // Candle count at last scan (detect new candles)
            int64_t firstTimestamp  = 0;  // Timestamp of first candle (detect timeframe/range changes)
        };

        // Individual indicator scanners
        void ScanCandles(const std::string& symbol, const std::vector<Candle>& candles, ScanState& state);
        void ScanVolume(const std::string& symbol, const std::vector<Candle>& candles, ScanState& state);
        void ScanRSI(const std::string& symbol, const std::vector<Candle>& candles, ScanState& state);
        void ScanMACD(const std::string& symbol, const std::vector<Candle>& candles, ScanState& state);
        void ScanEMA(const std::string& symbol, const std::vector<Candle>& candles, ScanState& state);
        void ScanBollinger(const std::string& symbol, const std::vector<Candle>& candles, ScanState& state);
        void ScanVolumeProfile(const std::string& symbol, const std::vector<Candle>& candles, ScanState& state);

        // Pattern recognition: scans recent events for multi-indicator patterns
        void ScanPatterns(const std::string& symbol, const std::vector<Candle>& candles);

        void Push(const std::string& symbol, GraphEvent&& event);

        // EMA computation helpers
        static std::vector<float> ComputeEMA(const std::vector<Candle>& candles, int period);
        static std::vector<float> ComputeSMA(const std::vector<Candle>& candles, int period);

        mutable std::mutex mutex_;
        std::unordered_map<std::string, std::vector<GraphEvent>> events_;
        std::unordered_map<std::string, ScanState> scanStates_;
        std::vector<GraphEvent> newEvents_; // Since last ConsumeNew()

        // Pattern definitions (built-in)
        std::vector<PatternDefinition> patterns_;
        std::unordered_map<std::string, std::vector<PatternMatch>> patternMatches_;

        void InitPatterns();
    };

} // namespace stnks
