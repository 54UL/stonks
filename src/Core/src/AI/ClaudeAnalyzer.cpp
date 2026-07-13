#include <AI/ClaudeAnalyzer.hpp>
#include <AI/PromptTemplate.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <ctime>
#include <sstream>

namespace stnks
{

    static const char* kDefaultAnalysis =
        "You are a financial market analyst. Analyze {{symbol}}.\n\n"
        "## Recent News\n{{news}}\n\n"
        "## Active Strategies\n{{strategies}}\n\n"
        "## Instructions\n{{instructions}}";

    static const char* kDefaultAnalysisContext =
        "You are a financial market analyst. Analyze {{symbol}}.\n\n"
        "## Chart Summary\n{{chart_summary}}\n\n"
        "## Recent Technical Events\n{{events}}\n\n"
        "## Detected Patterns\n{{patterns}}\n\n"
        "## Recent News\n{{news}}\n\n"
        "## Active Strategies\n{{strategies}}\n\n"
        "## Instructions\n{{instructions}}";

    static const char* kDefaultInstructions =
        "Respond with JSON only. Three arrays:\n"
        "1. \"recommendations\" - [{title, body}] actionable trade ideas\n"
        "2. \"warnings\" - [{title, body, severity: info|warning|alert}] risk alerts\n"
        "3. \"operations\" - [{strategy_id, action: hold|buy|sell|adjust_tp|adjust_sl, "
        "urgency: info|warning|alert, reason, suggested_price, confidence: 0-1}]\n"
        "   strategy_id=0 means new position. Keep concise (1-2 sentences each).";


    ClaudeAnalyzer::ClaudeAnalyzer(HttpClient& http, const Config& config)
        : http_(http), config_(config)
    {
        if (config_.apiKey.empty())
            spdlog::info("[ClaudeAnalyzer] No API key — running in dry-run mode");
        else
            spdlog::info("[ClaudeAnalyzer] Initialized with model '{}', prompts dir: '{}'",
                         config_.model, config_.promptsDir);
    }


    AnalysisResult ClaudeAnalyzer::Analyze(
        const std::string& symbol,
        const std::vector<NewsArticle>& news,
        const std::vector<Strategy>& activeStrategies)
    {
        if (config_.apiKey.empty())
            return DryRunAnalysis(symbol, news);

        auto vars = BuildVars(symbol, news, activeStrategies);
        std::string prompt = RenderPrompt("analysis.txt", vars, kDefaultAnalysis);

        nlohmann::json request;
        request["model"] = config_.model;
        request["max_tokens"] = 1024;
        request["messages"] = nlohmann::json::array({
            {{"role", "user"}, {"content", prompt}}
        });

        auto response = http_.Post(
            "https://api.anthropic.com/v1/messages", request.dump(),
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

    AnalysisResult ClaudeAnalyzer::AnalyzeWithContext(
        const std::string& symbol,
        const std::vector<NewsArticle>& news,
        const std::vector<Strategy>& activeStrategies,
        const ChartContext& chartCtx)
    {
        if (config_.apiKey.empty())
            return DryRunAnalysis(symbol, news);

        auto vars = BuildVarsWithContext(symbol, news, activeStrategies, chartCtx);
        std::string prompt = RenderPrompt("analysis_context.txt", vars, kDefaultAnalysisContext);

        nlohmann::json request;
        request["model"] = config_.model;
        request["max_tokens"] = 1024;
        request["messages"] = nlohmann::json::array({
            {{"role", "user"}, {"content", prompt}}
        });

        auto response = http_.Post(
            "https://api.anthropic.com/v1/messages", request.dump(),
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
        auto newsCopy = news;
        auto strategiesCopy = activeStrategies;

        threads.Submit([this, symbol, newsCopy = std::move(newsCopy),
                        strategiesCopy = std::move(strategiesCopy)]() {
            auto result = Analyze(symbol, newsCopy, strategiesCopy);
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(std::move(result));
        });
    }

    void ClaudeAnalyzer::AnalyzeAsyncWithContext(
        const std::string& symbol,
        const std::vector<NewsArticle>& news,
        const std::vector<Strategy>& activeStrategies,
        const ChartContext& chartCtx,
        ThreadRegistry& threads)
    {
        auto newsCopy = news;
        auto strategiesCopy = activeStrategies;
        auto ctxCopy = chartCtx;

        threads.Submit([this, symbol, newsCopy = std::move(newsCopy),
                        strategiesCopy = std::move(strategiesCopy),
                        ctxCopy = std::move(ctxCopy)]() {
            auto result = AnalyzeWithContext(symbol, newsCopy, strategiesCopy, ctxCopy);
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(std::move(result));
        });
    }


    std::string ClaudeAnalyzer::RenderPrompt(
        const std::string& templateFile,
        const std::unordered_map<std::string, std::string>& vars,
        const std::string& fallback) const
    {
        std::string path = config_.promptsDir + templateFile;
        return PromptTemplate::LoadAndRender(path, vars, fallback);
    }

    std::string ClaudeAnalyzer::LoadInstructions() const
    {
        std::string content;
        std::string path = config_.promptsDir + "instructions.txt";
        if (PromptTemplate::LoadFile(path, content))
            return content;
        return kDefaultInstructions;
    }


    std::unordered_map<std::string, std::string> ClaudeAnalyzer::BuildVars(
        const std::string& symbol,
        const std::vector<NewsArticle>& news,
        const std::vector<Strategy>& activeStrategies) const
    {
        return {
            {"symbol",       symbol},
            {"news",         FormatNews(news)},
            {"strategies",   FormatStrategies(symbol, activeStrategies)},
            {"instructions", LoadInstructions()},
        };
    }

    std::unordered_map<std::string, std::string> ClaudeAnalyzer::BuildVarsWithContext(
        const std::string& symbol,
        const std::vector<NewsArticle>& news,
        const std::vector<Strategy>& activeStrategies,
        const ChartContext& chartCtx) const
    {
        return {
            {"symbol",        symbol},
            {"news",          FormatNews(news)},
            {"strategies",    FormatStrategies(symbol, activeStrategies)},
            {"chart_summary", FormatChartSummary(chartCtx)},
            {"events",        FormatEvents(chartCtx)},
            {"patterns",      FormatPatterns(chartCtx)},
            {"instructions",  LoadInstructions()},
        };
    }


    std::string ClaudeAnalyzer::FormatNews(const std::vector<NewsArticle>& news)
    {
        if (news.empty()) return "No recent news available.";
        std::ostringstream ss;
        for (size_t i = 0; i < std::min(news.size(), (size_t)5); ++i)
        {
            ss << (i + 1) << ". " << news[i].title;
            if (!news[i].source.empty()) ss << " (" << news[i].source << ")";
            ss << "\n";
        }
        return ss.str();
    }

    std::string ClaudeAnalyzer::FormatStrategies(
        const std::string& symbol,
        const std::vector<Strategy>& strategies)
    {
        std::ostringstream ss;
        bool any = false;
        for (auto& s : strategies)
        {
            if (s.symbol != symbol) continue;
            any = true;
            ss << "- id:" << s.id << " " << StrategyTypeToString(s.type) << " "
               << DirectionToString(s.direction) << " @ " << s.entryPrice;
            if (s.IsTPSL()) ss << " TP:" << s.takeProfit << " SL:" << s.stopLoss;
            if (s.quantity > 0.f) ss << " qty:" << s.quantity;
            ss << "\n";
        }
        if (!any) ss << "None.";
        return ss.str();
    }

    std::string ClaudeAnalyzer::FormatChartSummary(const ChartContext& ctx)
    {
        std::ostringstream ss;
        ss << "- Current Price: " << ctx.currentPrice << "\n";
        if (ctx.open24h > 0.f)
            ss << "- Open: " << ctx.open24h
               << " | High: " << ctx.high24h
               << " | Low: " << ctx.low24h << "\n";
        if (ctx.changePct24h != 0.f)
            ss << "- Change: " << (ctx.changePct24h > 0 ? "+" : "") << ctx.changePct24h << "%\n";
        if (ctx.volume24h > 0.f)
            ss << "- Volume: " << ctx.volume24h << "\n";
        if (ctx.rsi14 > 0.f)
            ss << "- RSI(14): " << ctx.rsi14 << "\n";
        if (ctx.ema20 > 0.f)
            ss << "- EMA(20): " << ctx.ema20 << " | EMA(50): " << ctx.ema50 << "\n";
        return ss.str();
    }

    std::string ClaudeAnalyzer::FormatEvents(const ChartContext& ctx)
    {
        if (ctx.recentEvents.empty()) return "No recent events.";
        std::ostringstream ss;
        int count = 0;
        for (auto& ev : ctx.recentEvents)
        {
            if (++count > 15)
            {
                ss << "... and " << ((int)ctx.recentEvents.size() - 15) << " more\n";
                break;
            }
            ss << "- [" << EventSourceName(ev.source) << "] " << ev.title;
            if (!ev.detail.empty()) ss << " (" << ev.detail << ")";
            ss << "\n";
        }
        return ss.str();
    }

    std::string ClaudeAnalyzer::FormatPatterns(const ChartContext& ctx)
    {
        if (ctx.recentPatterns.empty()) return "No patterns detected.";
        std::ostringstream ss;
        for (auto& pm : ctx.recentPatterns)
        {
            ss << "- " << pm.patternName << " (score: " << (int)(pm.score * 100) << "%";
            if (!pm.description.empty()) ss << ", " << pm.description;
            ss << ")\n";
        }
        return ss.str();
    }


    AnalysisResult ClaudeAnalyzer::ParseResponse(const std::string& symbol, const std::string& responseBody)
    {
        AnalysisResult result;
        result.symbol = symbol;

        try
        {
            auto doc = nlohmann::json::parse(responseBody);

            std::string content;
            if (doc.contains("content") && doc["content"].is_array() && !doc["content"].empty())
                content = doc["content"][0].value("text", "");

            if (content.empty())
            {
                result.ok = false;
                result.error = "Empty response from Claude";
                return result;
            }

            // Find JSON block in content (may be wrapped in ```json ... ```)
            size_t jsonStart = content.find('{');
            size_t jsonEnd = content.rfind('}');
            if (jsonStart != std::string::npos && jsonEnd != std::string::npos && jsonEnd > jsonStart)
                content = content.substr(jsonStart, jsonEnd - jsonStart + 1);

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
                    else if (sev == "warning") insight.severity = InsightSeverity::Warning;
                    else                       insight.severity = InsightSeverity::Info;

                    result.warnings.push_back(std::move(insight));
                }
            }

            if (analysis.contains("operations"))
            {
                for (auto& o : analysis["operations"])
                {
                    AIOperation op;
                    op.symbol     = symbol;
                    op.strategyId = o.value("strategy_id", (int64_t)0);
                    op.reason     = o.value("reason", "");
                    op.suggestedPrice = o.value("suggested_price", 0.f);
                    op.confidence = o.value("confidence", 0.5f);
                    op.timestamp  = now;

                    std::string action = o.value("action", "hold");
                    if (action == "buy")            op.type = OperationType::Buy;
                    else if (action == "sell")       op.type = OperationType::Sell;
                    else if (action == "adjust_tp")  op.type = OperationType::AdjustTP;
                    else if (action == "adjust_sl")  op.type = OperationType::AdjustSL;
                    else                            op.type = OperationType::Hold;

                    std::string urg = o.value("urgency", "info");
                    if (urg == "alert")          op.urgency = InsightSeverity::Alert;
                    else if (urg == "warning")   op.urgency = InsightSeverity::Warning;
                    else                         op.urgency = InsightSeverity::Info;

                    result.operations.push_back(std::move(op));
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
