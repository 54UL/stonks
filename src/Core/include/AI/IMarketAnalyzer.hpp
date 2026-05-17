#pragma once

#include <Strategy/Strategy.hpp>
#include <News/NewsService.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace stnks
{
    enum class InsightSeverity : int
    {
        Info    = 0,   // Green — recommendation, nice to know
        Warning = 1,   // Yellow — should act (e.g. "should sell due to analysis")
        Alert   = 2    // Red — EMERGENCY, important call to make NOW
    };

    inline const char* SeverityToString(InsightSeverity s)
    {
        switch (s)
        {
        case InsightSeverity::Info:    return "RECOMMENDATION";
        case InsightSeverity::Warning: return "WARNING";
        case InsightSeverity::Alert:   return "EMERGENCY";
        }
        return "UNKNOWN";
    }

    // A single market insight (recommendation or warning)
    struct MarketInsight
    {
        std::string      symbol;
        std::string      title;
        std::string      body;
        InsightSeverity  severity  = InsightSeverity::Info;
        int64_t          timestamp = 0;
    };

    // An AI-suggested operation on an existing strategy
    enum class OperationType : int
    {
        Hold        = 0,  // Do nothing, keep position
        Buy         = 1,  // Open/add to position
        Sell        = 2,  // Close/reduce position
        AdjustTP    = 3,  // Move take-profit level
        AdjustSL    = 4,  // Move stop-loss level
    };

    inline const char* OperationTypeToString(OperationType t)
    {
        switch (t)
        {
        case OperationType::Hold:     return "HOLD";
        case OperationType::Buy:      return "BUY";
        case OperationType::Sell:     return "SELL";
        case OperationType::AdjustTP: return "Adjust TP";
        case OperationType::AdjustSL: return "Adjust SL";
        }
        return "UNKNOWN";
    }

    // AI-generated operation: a suggested action on a strategy
    struct AIOperation
    {
        int64_t          strategyId = 0;   // Which strategy this relates to (0 = new)
        std::string      symbol;
        OperationType    type       = OperationType::Hold;
        InsightSeverity  urgency    = InsightSeverity::Info;
        std::string      reason;           // Why the AI suggests this
        float            suggestedPrice = 0.f;  // Suggested price for the operation
        float            confidence = 0.f;      // 0-1 confidence level
        int64_t          timestamp  = 0;
        bool             executed   = false;    // Whether auto-trade acted on this
    };

    struct AnalysisResult
    {
        std::string                symbol;
        std::vector<MarketInsight> recommendations;
        std::vector<MarketInsight> warnings;
        std::vector<AIOperation>   operations;   // Actionable trade suggestions
        bool                       ok = true;
        std::string                error;
    };

    // Interface for AI-powered market analysis.
    // Implementations can use Claude API, local models, or rule-based logic.
    class IMarketAnalyzer
    {
    public:
        virtual ~IMarketAnalyzer() = default;

        virtual std::string Name() const = 0;

        // Analyze a symbol given its recent news and current strategy context.
        // Returns recommendations and warnings.
        virtual AnalysisResult Analyze(
            const std::string& symbol,
            const std::vector<NewsArticle>& news,
            const std::vector<Strategy>& activeStrategies) = 0;

        // Check if the analyzer is configured and ready
        virtual bool IsAvailable() const = 0;
    };

} // namespace stnks
