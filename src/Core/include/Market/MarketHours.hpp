#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <cstdint>
#include <algorithm>

namespace stnks
{
    // Market classification
    enum class MarketType
    {
        US,       // NYSE / NASDAQ — ET timezone
        Mexico,   // BMV (Bolsa Mexicana) — CT timezone
        Crypto,   // 24/7
        Unknown   // Treat as US hours
    };

    // Market session state
    enum class MarketState
    {
        Open,       // Regular trading hours
        PreMarket,  // Extended hours (US 4:00-9:30 ET)
        AfterHours, // Extended hours (US 16:00-20:00 ET)
        Closed      // Outside all trading (nights, weekends, holidays)
    };

    // Market schedule definitions
    struct MarketSchedule
    {
        int openHour;     // Local time hour (24h) market opens
        int openMin;
        int closeHour;    // Local time hour (24h) market closes
        int closeMin;
        int utcOffset;    // Hours from UTC (standard time, not DST-adjusted for simplicity)
        bool weekendsOff; // True for traditional markets
    };

    // Static utility — no state needed
    class MarketHours
    {
    public:
        // Classify a symbol into its market type
        static MarketType ClassifySymbol(const std::string& symbol)
        {
            // Mexican market: ends with .MX
            if (symbol.size() > 3 && symbol.substr(symbol.size() - 3) == ".MX")
                return MarketType::Mexico;

            // Crypto: common suffixes and pairs
            // Yahoo uses -USD (BTC-USD), some use USDT pairs
            if (symbol.find("-USD") != std::string::npos ||
                symbol.find("-BTC") != std::string::npos ||
                symbol.find("-ETH") != std::string::npos ||
                symbol.find("USDT") != std::string::npos ||
                // Common crypto tickers on Yahoo
                symbol == "BTC-USD" || symbol == "ETH-USD" ||
                symbol == "SOL-USD" || symbol == "DOGE-USD" ||
                symbol == "XRP-USD" || symbol == "ADA-USD" ||
                symbol == "AVAX-USD" || symbol == "DOT-USD")
                return MarketType::Crypto;

            // Default: US market
            return MarketType::US;
        }

        // Get the schedule for a market type
        static MarketSchedule GetSchedule(MarketType type)
        {
            switch (type)
            {
            case MarketType::US:
                // NYSE/NASDAQ: 9:30 - 16:00 ET (UTC-5)
                return {9, 30, 16, 0, -5, true};

            case MarketType::Mexico:
                // BMV: 8:30 - 15:00 CT (UTC-6)
                return {8, 30, 15, 0, -6, true};

            case MarketType::Crypto:
                // 24/7
                return {0, 0, 23, 59, 0, false};

            case MarketType::Unknown:
            default:
                return {9, 30, 16, 0, -5, true};
            }
        }

        // Get current market state for a symbol
        static MarketState GetState(const std::string& symbol)
        {
            MarketType type = ClassifySymbol(symbol);
            return GetStateForMarket(type);
        }

        static MarketState GetStateForMarket(MarketType type)
        {
            if (type == MarketType::Crypto)
                return MarketState::Open; // Always open

            auto schedule = GetSchedule(type);
            auto now = GetMarketLocalTime(schedule.utcOffset);

            // Check weekend
            if (schedule.weekendsOff && (now.tm_wday == 0 || now.tm_wday == 6))
                return MarketState::Closed;

            int currentMin = now.tm_hour * 60 + now.tm_min;
            int openMin    = schedule.openHour * 60 + schedule.openMin;
            int closeMin   = schedule.closeHour * 60 + schedule.closeMin;

            if (currentMin >= openMin && currentMin < closeMin)
                return MarketState::Open;

            // Pre-market: 4:00 - open (US only concept, but apply generically 90min before)
            int preMarketStart = openMin - 90; // 90 min before open
            if (preMarketStart < 0) preMarketStart = 0;
            if (currentMin >= preMarketStart && currentMin < openMin)
                return MarketState::PreMarket;

            // After hours: close to close+240min (4 hours after)
            int afterEnd = closeMin + 240;
            if (currentMin >= closeMin && currentMin < afterEnd)
                return MarketState::AfterHours;

            return MarketState::Closed;
        }

        // Check if market is currently active (open or extended hours)
        static bool IsActive(const std::string& symbol)
        {
            auto state = GetState(symbol);
            return state != MarketState::Closed;
        }

        // Check if fully open (regular hours)
        static bool IsOpen(const std::string& symbol)
        {
            return GetState(symbol) == MarketState::Open;
        }

        // Check if the server should process (fetch/check) this symbol right now.
        // Returns false when market is fully closed AND we are more than 15 min
        // past close or more than 15 min before open. This avoids wasting API calls
        // on stale data while still catching the open/close transitions.
        // Crypto: always process.
        static bool ShouldProcess(const std::string& symbol)
        {
            MarketType type = ClassifySymbol(symbol);
            if (type == MarketType::Crypto)
                return true; // 24/7

            MarketState state = GetStateForMarket(type);
            if (state != MarketState::Closed)
                return true; // Open, PreMarket, or AfterHours — always process

            // Market is Closed. Check if we're within 15min of open or close boundary.
            auto schedule = GetSchedule(type);
            auto now = GetMarketLocalTime(schedule.utcOffset);

            // Weekend: never process (no open/close boundary matters)
            if (schedule.weekendsOff && (now.tm_wday == 0 || now.tm_wday == 6))
                return false;

            int currentMin = now.tm_hour * 60 + now.tm_min;
            int openMin    = schedule.openHour * 60 + schedule.openMin;
            int closeMin   = schedule.closeHour * 60 + schedule.closeMin;

            // After-hours window ends at close + 240min.
            // "Closed" state starts after that. Check if within 15min past that boundary.
            int afterEnd = closeMin + 240;
            if (currentMin >= afterEnd && currentMin < afterEnd + 15)
                return true; // Just entered full-closed, still process briefly

            // Pre-market starts at open - 90min.
            // Check if within 15min before pre-market starts.
            int preMarketStart = openMin - 90;
            if (preMarketStart < 0) preMarketStart = 0;
            if (currentMin >= preMarketStart - 15 && currentMin < preMarketStart)
                return true; // About to enter pre-market

            return false; // Fully closed, no point fetching
        }

        // Get recommended poll interval in seconds for a symbol
        //  - Crypto open: fast (5s)
        //  - Market open: normal (10s)
        //  - Pre/After market: slower (30s)
        //  - Market closed: very slow (300s = 5min)
        static int GetPollInterval(const std::string& symbol)
        {
            MarketType type = ClassifySymbol(symbol);
            MarketState state = GetStateForMarket(type);

            if (type == MarketType::Crypto)
                return 5; // Crypto always fast

            switch (state)
            {
            case MarketState::Open:       return 10;
            case MarketState::PreMarket:  return 30;
            case MarketState::AfterHours: return 30;
            case MarketState::Closed:     return 300;
            }
            return 60;
        }

        // Get recommended server poll interval (aggregate across symbols)
        // Returns the minimum interval needed for the most demanding symbol
        static int GetServerPollInterval(const std::vector<std::string>& symbols)
        {
            int minInterval = 300;
            for (auto& sym : symbols)
            {
                int interval = GetPollInterval(sym);
                if (interval < minInterval)
                    minInterval = interval;
            }
            return minInterval;
        }

        // Human-readable state string
        static const char* StateToString(MarketState state)
        {
            switch (state)
            {
            case MarketState::Open:       return "Open";
            case MarketState::PreMarket:  return "Pre-Market";
            case MarketState::AfterHours: return "After Hours";
            case MarketState::Closed:     return "Closed";
            }
            return "Unknown";
        }

        static const char* MarketTypeToString(MarketType type)
        {
            switch (type)
            {
            case MarketType::US:      return "US";
            case MarketType::Mexico:  return "MX";
            case MarketType::Crypto:  return "Crypto";
            case MarketType::Unknown: return "Unknown";
            }
            return "?";
        }

    private:
        // Get current time adjusted to a market's local timezone
        static struct tm GetMarketLocalTime(int utcOffset)
        {
            time_t now = std::time(nullptr);
            struct tm utcTm;
#ifdef _WIN32
            gmtime_s(&utcTm, &now);
#else
            gmtime_r(&now, &utcTm);
#endif
            // Apply UTC offset
            time_t local = now + (utcOffset * 3600);
            struct tm localTm;
#ifdef _WIN32
            gmtime_s(&localTm, &local);
#else
            gmtime_r(&local, &localTm);
#endif
            return localTm;
        }
    };

} // namespace stnks
