#pragma once

#include <AI/IMarketAnalyzer.hpp>
#include <Http/HttpClient.hpp>
#include <Threading/ThreadRegistry.hpp>
#include <mutex>

namespace stnks
{
    // Claude API-powered market analyzer.
    // Sends news + strategy context to Claude and parses structured recommendations/warnings.
    // When no API key is set, operates in dry-run mode with placeholder insights.
    class ClaudeAnalyzer : public IMarketAnalyzer
    {
    public:
        struct Config
        {
            std::string apiKey;
            std::string model = "claude-sonnet-4-20250514";
        };

        ClaudeAnalyzer(HttpClient& http, const Config& config);

        std::string Name() const override { return "Claude AI"; }
        bool IsAvailable() const override { return !config_.apiKey.empty(); }

        AnalysisResult Analyze(
            const std::string& symbol,
            const std::vector<NewsArticle>& news,
            const std::vector<Strategy>& activeStrategies) override;

        // Async analysis — dispatches to thread pool
        void AnalyzeAsync(
            const std::string& symbol,
            const std::vector<NewsArticle>& news,
            const std::vector<Strategy>& activeStrategies,
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
        std::string BuildPrompt(
            const std::string& symbol,
            const std::vector<NewsArticle>& news,
            const std::vector<Strategy>& activeStrategies);

        AnalysisResult ParseResponse(const std::string& symbol, const std::string& responseBody);
        AnalysisResult DryRunAnalysis(const std::string& symbol, const std::vector<NewsArticle>& news);

        HttpClient& http_;
        Config      config_;

        std::mutex                    mutex_;
        std::vector<AnalysisResult>   pending_;
    };

} // namespace stnks
