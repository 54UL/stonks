#include <Http/HttpClient.hpp>
#include <cpr/cpr.h>
#include <spdlog/spdlog.h>

namespace stnks
{
    //TODO: IMPROVE AND REFACTOR!!!
    HttpClient::HttpClient(ThreadRegistry& threads) : threads_(threads) {}

    HttpResponse HttpClient::Get(const std::string& url)
    {
        spdlog::debug("[HttpClient] GET {}", url);
        auto r = cpr::Get(cpr::Url{url},
                          cpr::Header{{"User-Agent", "STNKS/1.0"}},
                          cpr::Timeout{10000});

        HttpResponse resp;
        resp.statusCode = r.status_code;
        resp.body       = std::move(r.text);
        if (r.error)
            resp.error = r.error.message;

        if (!resp.Ok())
            spdlog::warn("[HttpClient] GET {} -> {} ({})", url, resp.statusCode, resp.error);

        return resp;
    }

    HttpResponse HttpClient::Post(const std::string& url, const std::string& body,
                                     const std::vector<std::pair<std::string,std::string>>& headers)
    {
        spdlog::debug("[HttpClient] POST {} ({} bytes)", url, body.size());

        cpr::Header cprHeaders{{"User-Agent", "STNKS/1.0"}, {"Content-Type", "application/json"}};
        for (auto& [k, v] : headers)
            cprHeaders[k] = v;

        auto r = cpr::Post(cpr::Url{url},
                           cprHeaders,
                           cpr::Body{body},
                           cpr::Timeout{30000});

        HttpResponse resp;
        resp.statusCode = r.status_code;
        resp.body       = std::move(r.text);
        if (r.error)
            resp.error = r.error.message;

        if (!resp.Ok())
            spdlog::warn("[HttpClient] POST {} -> {} ({})", url, resp.statusCode, resp.error);

        return resp;
    }

    HttpResponse HttpClient::Put(const std::string& url, const std::string& body,
                                    const std::vector<std::pair<std::string,std::string>>& headers)
    {
        spdlog::debug("[HttpClient] PUT {} ({} bytes)", url, body.size());

        cpr::Header cprHeaders{{"User-Agent", "STNKS/1.0"}, {"Content-Type", "application/json"}};
        for (auto& [k, v] : headers)
            cprHeaders[k] = v;

        auto r = cpr::Put(cpr::Url{url},
                          cprHeaders,
                          cpr::Body{body},
                          cpr::Timeout{30000});

        HttpResponse resp;
        resp.statusCode = r.status_code;
        resp.body       = std::move(r.text);
        if (r.error)
            resp.error = r.error.message;

        if (!resp.Ok())
            spdlog::warn("[HttpClient] PUT {} -> {} ({})", url, resp.statusCode, resp.error);

        return resp;
    }

    HttpResponse HttpClient::Delete(const std::string& url)
    {
        spdlog::debug("[HttpClient] DELETE {}", url);
        auto r = cpr::Delete(cpr::Url{url},
                              cpr::Header{{"User-Agent", "STNKS/1.0"}},
                              cpr::Timeout{10000});

        HttpResponse resp;
        resp.statusCode = r.status_code;
        resp.body       = std::move(r.text);
        if (r.error)
            resp.error = r.error.message;

        if (!resp.Ok())
            spdlog::warn("[HttpClient] DELETE {} -> {} ({})", url, resp.statusCode, resp.error);

        return resp;
    }

    std::future<HttpResponse> HttpClient::GetAsync(const std::string& url)
    {
        return threads_.Submit([this, url]() {
            return Get(url);
        });
    }

} // namespace stnks
