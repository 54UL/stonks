#pragma once

#include <Events/GraphEvent.hpp>
#include <Strategy/Strategy.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace stnks
{
    // What the user can DO when they click/accept a signal

    enum class SignalAction : int
    {
        None       = 0,  // Informational only — no action available
        Buy        = 1,  // Open long position / buy asset
        Sell       = 2,  // Open short or close position / sell asset
        CreateTPSL = 3,  // Create a TP/SL strategy at suggested levels
        AdjustTP   = 4,  // Move take-profit on an existing strategy
        AdjustSL   = 5,  // Move stop-loss on an existing strategy
        ClosePos   = 6,  // Close/cancel an existing position
    };

    inline const char* SignalActionToString(SignalAction a)
    {
        switch (a)
        {
        case SignalAction::None:       return "Info";
        case SignalAction::Buy:        return "BUY";
        case SignalAction::Sell:       return "SELL";
        case SignalAction::CreateTPSL: return "Create TP/SL";
        case SignalAction::AdjustTP:   return "Adjust TP";
        case SignalAction::AdjustSL:   return "Adjust SL";
        case SignalAction::ClosePos:   return "Close Position";
        }
        return "?";
    }

    // Where the signal was generated from

    enum class SignalOrigin : int
    {
        GraphEvent   = 0,  // Technical indicator event (RSI, MACD, candle pattern, etc.)
        Pattern      = 1,  // Multi-indicator pattern match
        AI           = 2,  // Claude / AI analyzer suggestion
        Broker       = 3,  // Broker event (order fill, margin call, etc.)
        Strategy     = 4,  // Strategy trigger (TP/SL hit)
        Manual       = 5,  // User-created signal
    };

    inline const char* SignalOriginToString(SignalOrigin o)
    {
        switch (o)
        {
        case SignalOrigin::GraphEvent: return "Technical";
        case SignalOrigin::Pattern:    return "Pattern";
        case SignalOrigin::AI:         return "AI";
        case SignalOrigin::Broker:     return "Broker";
        case SignalOrigin::Strategy:   return "Strategy";
        case SignalOrigin::Manual:     return "Manual";
        }
        return "?";
    }

    // A single piece of evidence/context that supports the signal.
    // Rendered as a tree node in the collapsible detail view.

    enum class ResourceType : int
    {
        GraphEvent     = 0,  // A single chart event (RSI cross, candle pattern, etc.)
        PatternMatch   = 1,  // A pattern condition that matched (with matched/missed list)
        AIPrompt       = 2,  // The input prompt sent to the AI
        AIResponse     = 3,  // The AI's response/reasoning
        BrokerOrder    = 4,  // A broker order reference
        PriceLevel     = 5,  // A specific price level (entry, TP, SL suggestion)
        NewsArticle    = 6,  // A news article that influenced the signal
        Note           = 7,  // Freeform text note / explanation
    };

    struct SignalResource
    {
        ResourceType type;
        std::string  label;       // Short label: "RSI Oversold Exit", "AI Prompt", "TP Level"
        std::string  content;     // Full content: event detail, prompt text, price, etc.
        float        score = 0.f; // Confidence/relevance (0-1), shown as bar for patterns
        int64_t      timestamp = 0;
        int          candleIdx = -1; // For chart events — navigate to this candle on click

        // For price levels
        float        price = 0.f;

        // Source event info (for coloring/icons)
        EventSource  eventSource = EventSource::Strategy;
    };


    enum class SignalSeverity : int
    {
        Info    = 0,  // Blue/gray — informational, no urgency
        Warning = 1,  // Yellow — should review, moderate urgency
        Alert   = 2,  // Red — act now, high urgency
    };

    inline const char* SignalSeverityToString(SignalSeverity s)
    {
        switch (s)
        {
        case SignalSeverity::Info:    return "INFO";
        case SignalSeverity::Warning: return "WARNING";
        case SignalSeverity::Alert:   return "ALERT";
        }
        return "?";
    }

    // The unified signal struct that combines all sources into one actionable item.

    struct MarketSignal
    {
        int64_t         id         = 0;      // Unique ID (auto-increment)
        std::string     symbol;              // Asset symbol (AAPL, BTC-USD, etc.)
        std::string     title;               // "Bullish Reversal Setup", "AI: Buy AAPL"
        std::string     description;         // Longer explanation (shown in collapsed state)
        SignalSeverity  severity   = SignalSeverity::Info;
        SignalOrigin    origin     = SignalOrigin::GraphEvent;
        SignalAction    action     = SignalAction::None;
        int64_t         timestamp  = 0;      // When the signal was generated
        float           confidence = 0.f;    // 0-1 overall confidence/score

        // Suggested trade parameters (pre-fill wizard when user accepts)
        StrategyDirection direction = StrategyDirection::Long;
        float           suggestedEntry = 0.f;
        float           suggestedTP    = 0.f;
        float           suggestedSL    = 0.f;
        float           suggestedQty   = 0.f;
        BrokerSource    broker         = BrokerSource::Auto;

        // Links to existing strategy (for adjust/close actions)
        int64_t         strategyId = 0;

        // Resources: the evidence tree that backs this signal
        std::vector<SignalResource> resources;

        // State
        bool            dismissed  = false;  // User clicked "dismiss" / not interested
        bool            accepted   = false;  // User clicked "accept" and created strategy/order
        bool            read       = false;  // User has seen this signal (expanded it)

        bool IsActionable() const { return action != SignalAction::None && !dismissed && !accepted; }
        bool IsBuyish() const { return action == SignalAction::Buy || action == SignalAction::CreateTPSL; }
        bool IsSellish() const { return action == SignalAction::Sell || action == SignalAction::ClosePos; }
    };

} // namespace stnks
