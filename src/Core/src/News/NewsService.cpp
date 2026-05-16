#include <News/NewsService.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

namespace stnks
{
    NewsService::NewsService(HttpClient& http, ThreadRegistry& threads, const std::string& apiKey)
        : http_(http), threads_(threads), apiKey_(apiKey)
    {
        if (apiKey_.empty())
            spdlog::warn("[NewsService] No GNews API key set — news fetching disabled");
    }

    NewsFetchResult NewsService::FetchNews(const std::string& query, int maxArticles)
    {
        NewsFetchResult result;
        result.symbol = query;

        if (apiKey_.empty())
        {
            result.ok = false;
            result.error = "No API key";
            return result;
        }

        // URL-encode query (minimal: spaces → +)
        std::string encodedQuery;
        for (char c : query)
        {
            if (c == ' ') encodedQuery += '+';
            else          encodedQuery += c;
        }

        std::string url = "https://gnews.io/api/v4/search"
                          "?q=" + encodedQuery +
                          "&lang=en"
                          "&max=" + std::to_string(maxArticles) +
                          "&sortby=publishedAt"
                          "&topic=business"
                          "&apikey=" + apiKey_;

        auto response = http_.Get(url);
        if (!response.Ok())
        {
            result.ok = false;
            result.error = response.error.empty()
                ? ("HTTP " + std::to_string(response.statusCode))
                : response.error;
            spdlog::error("[NewsService] Fetch failed for '{}': {}", query, result.error);
            return result;
        }

        result.articles = ParseGNewsResponse(response.body);
        return result;
    }

    void NewsService::FetchNewsAsync(const std::string& query, int maxArticles)
    {
        threads_.Submit([this, query, maxArticles]() {
            auto result = FetchNews(query, maxArticles);
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(std::move(result));
        });
    }

    std::vector<NewsArticle> NewsService::ParseGNewsResponse(const std::string& json)
    {
        std::vector<NewsArticle> articles;

        try
        {
            auto doc = nlohmann::json::parse(json);

            if (!doc.contains("articles") || !doc["articles"].is_array())
                return articles;

            for (auto& item : doc["articles"])
            {
                NewsArticle a;
                a.title       = item.value("title", "");
                a.description = item.value("description", "");
                a.url         = item.value("url", "");
                a.publishedAt = item.value("publishedAt", "");

                if (item.contains("source") && item["source"].is_object())
                    a.source = item["source"].value("name", "");

                articles.push_back(std::move(a));
            }
        }
        catch (const std::exception& e)
        {
            spdlog::error("[NewsService] JSON parse error: {}", e.what());
        }

        return articles;
    }

} // namespace stnks
