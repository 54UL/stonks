#include <Server/SentimentAction.hpp>
#include <spdlog/spdlog.h>

namespace stnks
{
    SentimentAction::SentimentAction(HttpClient& http, ThreadRegistry& threads, const Config& config)
        : news_(http, threads, config.gnewsApiKey)
        , analyzer_(http, ClaudeAnalyzer::Config{config.claudeApiKey, config.claudeModel})
        , config_(config)
    {
    }

    bool SentimentAction::Validate()
    {
        bool hasNews = news_.HasApiKey();
        bool hasAI   = analyzer_.IsAvailable();

        if (!hasNews)
            spdlog::warn("[SentimentAction] No GNews API key — news fetching disabled");
        if (!hasAI)
            spdlog::info("[SentimentAction] No Claude API key — using dry-run analysis");

        return true; // Always valid, degrades gracefully
    }

    bool SentimentAction::Execute(const StrategyTriggerEvent& event)
    {
        bool isTakeProfit = (event.triggerType == StrategyStatus::TPHit);
        spdlog::info("[SentimentAction] {} triggered for {} @ {:.2f} — fetching sentiment...",
                     isTakeProfit ? "TP" : "SL", event.symbol, event.currentPrice);

        // Fetch news synchronously (we're already on a background thread in the server)
        auto newsResult = news_.FetchNews(event.symbol, config_.maxArticles);

        if (!newsResult.ok)
        {
            spdlog::warn("[SentimentAction] News fetch failed for '{}': {}", event.symbol, newsResult.error);
            // Continue with empty news — analyzer will still produce basic insights
        }

        // Run analysis
        std::vector<Strategy> context = {event.strategy};
        auto analysis = analyzer_.Analyze(event.symbol, newsResult.articles, context);

        if (!analysis.ok)
        {
            spdlog::error("[SentimentAction] Analysis failed for '{}': {}", event.symbol, analysis.error);
            return false;
        }

        // Log insights
        for (auto& rec : analysis.recommendations)
        {
            spdlog::info("[SentimentAction] REC [{}] {}: {}",
                         rec.symbol, rec.title, rec.body);
        }
        for (auto& warn : analysis.warnings)
        {
            const char* sevStr = warn.severity == InsightSeverity::Alert   ? "ALERT"
                               : warn.severity == InsightSeverity::Warning ? "WARN"
                               : "INFO";
            spdlog::info("[SentimentAction] {} [{}] {}: {}",
                         sevStr, warn.symbol, warn.title, warn.body);
        }

        return true;
    }

    AnalysisResult SentimentAction::AnalyzeSymbol(const std::string& symbol,
                                                   const std::vector<Strategy>& activeStrategies)
    {
        spdlog::info("[SentimentAction] Periodic analysis for '{}'", symbol);

        auto newsResult = news_.FetchNews(symbol, config_.maxArticles);

        if (!newsResult.ok)
            spdlog::warn("[SentimentAction] News fetch failed for '{}': {}", symbol, newsResult.error);

        return analyzer_.Analyze(symbol, newsResult.articles, activeStrategies);
    }

} // namespace stnks
