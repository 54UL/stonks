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
        Info    = 0,
        Warning = 1,
        Alert   = 2
    };

    // A single market insight (recommendation or warning)
    struct MarketInsight
    {
        std::string      symbol;
        std::string      title;
        std::string      body;
        InsightSeverity  severity  = InsightSeverity::Info;
        int64_t          timestamp = 0;
    };

    struct AnalysisResult
    {
        std::string                symbol;
        std::vector<MarketInsight> recommendations;
        std::vector<MarketInsight> warnings;
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
