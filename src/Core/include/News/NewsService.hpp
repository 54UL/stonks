#pragma once

#include <Http/HttpClient.hpp>
#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

namespace stnks
{
    struct NewsArticle
    {
        std::string title;
        std::string description;
        std::string url;
        std::string source;
        std::string publishedAt;   // ISO 8601 string from GNews
        int64_t     timestamp = 0; // Parsed unix timestamp (best-effort)
    };

    struct NewsFetchResult
    {
        std::string              symbol;
        std::vector<NewsArticle> articles;
        bool                     ok = true;
        std::string              error;
    };

    // Fetches financial news from GNews API.
    // https://gnews.io/docs/v4
    //
    // Threading model (same as MarketService):
    //   FetchNews()      — synchronous, call from any thread
    //   FetchNewsAsync() — submits to ThreadRegistry pool
    //   DrainResults()   — call from main thread to collect
    class NewsService
    {
    public:
        NewsService(HttpClient& http, ThreadRegistry& threads, const std::string& apiKey);

        // Synchronous fetch — blocks calling thread
        NewsFetchResult FetchNews(const std::string& query, int maxArticles = 5);

        // Async fetch — dispatches to thread pool
        void FetchNewsAsync(const std::string& query, int maxArticles = 5);

        // Drain completed results (call from main/UI thread)
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

        bool HasApiKey() const { return !apiKey_.empty(); }

    private:
        std::vector<NewsArticle> ParseGNewsResponse(const std::string& json);

        HttpClient&     http_;
        ThreadRegistry& threads_;
        std::string     apiKey_;

        std::mutex                    mutex_;
        std::vector<NewsFetchResult>  pending_;
    };

} // namespace stnks
