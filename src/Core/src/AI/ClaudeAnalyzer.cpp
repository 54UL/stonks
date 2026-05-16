#include <AI/ClaudeAnalyzer.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <ctime>
#include <sstream>

namespace stnks
{
    ClaudeAnalyzer::ClaudeAnalyzer(HttpClient& http, const Config& config)
        : http_(http), config_(config)
    {
        if (config_.apiKey.empty())
            spdlog::info("[ClaudeAnalyzer] No API key — running in dry-run mode");
        else
            spdlog::info("[ClaudeAnalyzer] Initialized with model '{}'", config_.model);
    }

    AnalysisResult ClaudeAnalyzer::Analyze(
        const std::string& symbol,
        const std::vector<NewsArticle>& news,
        const std::vector<Strategy>& activeStrategies)
    {
        if (config_.apiKey.empty())
            return DryRunAnalysis(symbol, news);

        std::string prompt = BuildPrompt(symbol, news, activeStrategies);

        // Build Claude API request
        nlohmann::json request;
        request["model"] = config_.model;
        request["max_tokens"] = 1024;
        request["messages"] = nlohmann::json::array({
            {{"role", "user"}, {"content", prompt}}
        });

        std::string body = request.dump();

        auto response = http_.Post(
            "https://api.anthropic.com/v1/messages", body,
            {
                {"x-api-key",         config_.apiKey},
                {"anthropic-version", "2023-06-01"}
            });

        if (!response.Ok())
        {
            spdlog::error("[ClaudeAnalyzer] API call failed for '{}': {} {}",
                          symbol, response.statusCode, response.error);
            return DryRunAnalysis(symbol, news);
        }

        return ParseResponse(symbol, response.body);
    }

    void ClaudeAnalyzer::AnalyzeAsync(
        const std::string& symbol,
        const std::vector<NewsArticle>& news,
        const std::vector<Strategy>& activeStrategies,
        ThreadRegistry& threads)
    {
        // Copy data for thread safety
        auto newsCopy = news;
        auto strategiesCopy = activeStrategies;

        threads.Submit([this, symbol, newsCopy = std::move(newsCopy),
                        strategiesCopy = std::move(strategiesCopy)]() {
            auto result = Analyze(symbol, newsCopy, strategiesCopy);
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(std::move(result));
        });
    }

    std::string ClaudeAnalyzer::BuildPrompt(
        const std::string& symbol,
        const std::vector<NewsArticle>& news,
        const std::vector<Strategy>& activeStrategies)
    {
        std::ostringstream ss;
        ss << "You are a financial market analyst. Analyze the following for " << symbol << ".\n\n";

        ss << "## Recent News\n";
        if (news.empty())
            ss << "No recent news available.\n";
        else
        {
            for (size_t i = 0; i < news.size(); ++i)
            {
                ss << (i + 1) << ". " << news[i].title << "\n";
                if (!news[i].description.empty())
                    ss << "   " << news[i].description << "\n";
                ss << "   Source: " << news[i].source << " | " << news[i].publishedAt << "\n\n";
            }
        }

        ss << "## Active Strategies\n";
        if (activeStrategies.empty())
            ss << "No active strategies.\n";
        else
        {
            for (auto& s : activeStrategies)
            {
                if (s.symbol != symbol) continue;
                ss << "- " << StrategyTypeToString(s.type) << " "
                   << DirectionToString(s.direction) << " @ " << s.entryPrice;
                if (s.IsTPSL())
                    ss << " TP:" << s.takeProfit << " SL:" << s.stopLoss;
                ss << "\n";
            }
        }

        ss << "\n## Instructions\n"
           << "Provide your analysis as JSON with two arrays:\n"
           << "1. \"recommendations\" - actionable trade ideas (title + body)\n"
           << "2. \"warnings\" - risk alerts or negative signals (title + body + severity: info/warning/alert)\n"
           << "Keep each item concise (1-2 sentences).\n";

        return ss.str();
    }

    AnalysisResult ClaudeAnalyzer::ParseResponse(const std::string& symbol, const std::string& responseBody)
    {
        AnalysisResult result;
        result.symbol = symbol;

        try
        {
            auto doc = nlohmann::json::parse(responseBody);

            // Extract from Claude response format
            std::string content;
            if (doc.contains("content") && doc["content"].is_array() && !doc["content"].empty())
                content = doc["content"][0].value("text", "");

            if (content.empty())
            {
                result.ok = false;
                result.error = "Empty response from Claude";
                return result;
            }

            // Try to parse the content as JSON
            auto analysis = nlohmann::json::parse(content);
            int64_t now = std::time(nullptr);

            if (analysis.contains("recommendations"))
            {
                for (auto& r : analysis["recommendations"])
                {
                    MarketInsight insight;
                    insight.symbol    = symbol;
                    insight.title     = r.value("title", "");
                    insight.body      = r.value("body", "");
                    insight.severity  = InsightSeverity::Info;
                    insight.timestamp = now;
                    result.recommendations.push_back(std::move(insight));
                }
            }

            if (analysis.contains("warnings"))
            {
                for (auto& w : analysis["warnings"])
                {
                    MarketInsight insight;
                    insight.symbol    = symbol;
                    insight.title     = w.value("title", "");
                    insight.body      = w.value("body", "");
                    insight.timestamp = now;

                    std::string sev = w.value("severity", "info");
                    if (sev == "alert")        insight.severity = InsightSeverity::Alert;
                    else if (sev == "warning")  insight.severity = InsightSeverity::Warning;
                    else                        insight.severity = InsightSeverity::Info;

                    result.warnings.push_back(std::move(insight));
                }
            }
        }
        catch (const std::exception& e)
        {
            result.ok = false;
            result.error = std::string("Parse error: ") + e.what();
            spdlog::error("[ClaudeAnalyzer] {}", result.error);
        }

        return result;
    }

    AnalysisResult ClaudeAnalyzer::DryRunAnalysis(const std::string& symbol, const std::vector<NewsArticle>& news)
    {
        AnalysisResult result;
        result.symbol = symbol;
        int64_t now = std::time(nullptr);

        // Generate placeholder insights from news headlines
        if (!news.empty())
        {
            MarketInsight rec;
            rec.symbol    = symbol;
            rec.title     = "News Activity Detected";
            rec.body      = std::to_string(news.size()) + " recent articles found for " + symbol +
                            ". Review headlines for sentiment signals.";
            rec.severity  = InsightSeverity::Info;
            rec.timestamp = now;
            result.recommendations.push_back(std::move(rec));

            // Check for negative keywords in headlines
            for (auto& article : news)
            {
                std::string lower = article.title;
                for (auto& c : lower) c = static_cast<char>(std::tolower(c));

                bool negative = lower.find("crash") != std::string::npos
                             || lower.find("plunge") != std::string::npos
                             || lower.find("decline") != std::string::npos
                             || lower.find("lawsuit") != std::string::npos
                             || lower.find("investigation") != std::string::npos
                             || lower.find("warning") != std::string::npos
                             || lower.find("downgrade") != std::string::npos;

                if (negative)
                {
                    MarketInsight warn;
                    warn.symbol    = symbol;
                    warn.title     = "Negative Headline";
                    warn.body      = article.title + " (" + article.source + ")";
                    warn.severity  = InsightSeverity::Warning;
                    warn.timestamp = now;
                    result.warnings.push_back(std::move(warn));
                }
            }
        }
        else
        {
            MarketInsight rec;
            rec.symbol    = symbol;
            rec.title     = "No News Data";
            rec.body      = "No recent news found. Configure GNews API key for live data.";
            rec.severity  = InsightSeverity::Info;
            rec.timestamp = now;
            result.recommendations.push_back(std::move(rec));
        }

        spdlog::info("[ClaudeAnalyzer] Dry-run analysis for '{}': {} recs, {} warnings",
                     symbol, result.recommendations.size(), result.warnings.size());

        return result;
    }

} // namespace stnks
