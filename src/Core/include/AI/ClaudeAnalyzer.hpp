#pragma once

#include <AI/IMarketAnalyzer.hpp>
#include <Http/HttpClient.hpp>
#include <Threading/ThreadRegistry.hpp>
#include <mutex>

namespace stnks
{
    // Claude API-powered market analyzer.
    // Loads prompt templates from disk (config/prompts/) and substitutes {{variables}}.
    // When no API key is set, operates in dry-run mode with placeholder insights.
    class ClaudeAnalyzer : public IMarketAnalyzer
    {
    public:
        struct Config
        {
            std::string apiKey;
            std::string model      = "claude-sonnet-4-20250514";
            std::string promptsDir = "config/prompts/"; // Directory containing .txt templates
        };

        ClaudeAnalyzer(HttpClient& http, const Config& config);

        std::string Name() const override { return "Claude AI"; }
        bool IsAvailable() const override { return !config_.apiKey.empty(); }

        AnalysisResult Analyze(
            const std::string& symbol,
            const std::vector<NewsArticle>& news,
            const std::vector<Strategy>& activeStrategies) override;

        AnalysisResult AnalyzeWithContext(
            const std::string& symbol,
            const std::vector<NewsArticle>& news,
            const std::vector<Strategy>& activeStrategies,
            const ChartContext& chartCtx) override;

        // Async analysis — dispatches to thread pool
        void AnalyzeAsync(
            const std::string& symbol,
            const std::vector<NewsArticle>& news,
            const std::vector<Strategy>& activeStrategies,
            ThreadRegistry& threads);

        void AnalyzeAsyncWithContext(
            const std::string& symbol,
            const std::vector<NewsArticle>& news,
            const std::vector<Strategy>& activeStrategies,
            const ChartContext& chartCtx,
            ThreadRegistry& threads);

        // Drain completed analysis results (call from main thread)
        template <typename Fn>
        size_t DrainResults(Fn&& fn)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            size_t count = pending_.size();
            for (auto& r : pending_)
                fn(std::move(r));
            pending_.clear();
            return count;
        }

    private:
        // Build template variable map from inputs
        std::unordered_map<std::string, std::string> BuildVars(
            const std::string& symbol,
            const std::vector<NewsArticle>& news,
            const std::vector<Strategy>& activeStrategies) const;

        std::unordered_map<std::string, std::string> BuildVarsWithContext(
            const std::string& symbol,
            const std::vector<NewsArticle>& news,
            const std::vector<Strategy>& activeStrategies,
            const ChartContext& chartCtx) const;

        // Render a prompt from template file + variables
        std::string RenderPrompt(const std::string& templateFile,
                                 const std::unordered_map<std::string, std::string>& vars,
                                 const std::string& fallback) const;

        // Load instructions from instructions.txt (or default)
        std::string LoadInstructions() const;

        // Format helpers for building variable values
        static std::string FormatNews(const std::vector<NewsArticle>& news);
        static std::string FormatStrategies(const std::string& symbol,
                                             const std::vector<Strategy>& strategies);
        static std::string FormatChartSummary(const ChartContext& ctx);
        static std::string FormatEvents(const ChartContext& ctx);
        static std::string FormatPatterns(const ChartContext& ctx);

        AnalysisResult ParseResponse(const std::string& symbol, const std::string& responseBody);
        AnalysisResult DryRunAnalysis(const std::string& symbol, const std::vector<NewsArticle>& news);

        HttpClient& http_;
        Config      config_;

        std::mutex                    mutex_;
        std::vector<AnalysisResult>   pending_;
    };

} // namespace stnks
