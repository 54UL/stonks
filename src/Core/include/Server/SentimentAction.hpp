#pragma once

#include <Server/IStrategyAction.hpp>
#include <News/NewsService.hpp>
#include <AI/ClaudeAnalyzer.hpp>
#include <Http/HttpClient.hpp>
#include <Threading/ThreadRegistry.hpp>
#include <string>

namespace stnks
{
    // Action that fetches news and runs AI sentiment analysis when a strategy triggers.
    // Can also be used standalone by the StrategyServer to periodically analyze
    // active positions and generate alerts/recommendations.
    //
    // Flow:
    //   1. On trigger (TP/SL hit), fetch recent news for the symbol
    //   2. Run ClaudeAnalyzer to get sentiment + recommendations
    //   3. Log the AI insights (and optionally forward to other actions)
    //
    // For Position-type strategies, the server calls AnalyzeSymbol() periodically
    // instead of waiting for a price trigger.
    class SentimentAction : public IStrategyAction
    {
    public:
        struct Config
        {
            std::string gnewsApiKey;
            std::string claudeApiKey;
            std::string claudeModel = "claude-sonnet-4-20250514";
            int         maxArticles = 5;
        };

        SentimentAction(HttpClient& http, ThreadRegistry& threads, const Config& config);

        std::string Name() const override { return "SentimentAction (AI)"; }
        bool Execute(const StrategyTriggerEvent& event) override;
        bool Validate() override;

        // Standalone analysis for a symbol (used for Position tracking).
        // Returns the analysis result synchronously.
        AnalysisResult AnalyzeSymbol(const std::string& symbol,
                                     const std::vector<Strategy>& activeStrategies);

    private:
        NewsService    news_;
        ClaudeAnalyzer analyzer_;
        Config         config_;
    };

} // namespace stnks
