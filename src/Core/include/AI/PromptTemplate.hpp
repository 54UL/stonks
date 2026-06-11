#pragma once

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <regex>
#include <spdlog/spdlog.h>

namespace stnks
{
    // Loads a text template from disk and substitutes {{variable}} placeholders.
    // Falls back to a provided default string if the file cannot be read.
    class PromptTemplate
    {
    public:
        // Load template from file. Returns false if file not found (content stays empty).
        static bool LoadFile(const std::string& path, std::string& outContent)
        {
            std::ifstream f(path);
            if (!f.is_open())
            {
                spdlog::warn("[PromptTemplate] Could not open '{}' — using fallback", path);
                return false;
            }
            std::ostringstream ss;
            ss << f.rdbuf();
            outContent = ss.str();
            return true;
        }

        // Substitute all {{key}} placeholders in the template with values from the map.
        // Unknown keys are left as-is.
        static std::string Render(const std::string& tmpl,
                                  const std::unordered_map<std::string, std::string>& vars)
        {
            std::string result = tmpl;
            for (auto& [key, value] : vars)
            {
                std::string placeholder = "{{" + key + "}}";
                size_t pos = 0;
                while ((pos = result.find(placeholder, pos)) != std::string::npos)
                {
                    result.replace(pos, placeholder.size(), value);
                    pos += value.size();
                }
            }
            return result;
        }

        // Convenience: load + render in one call. Uses fallback if file not found.
        static std::string LoadAndRender(const std::string& path,
                                         const std::unordered_map<std::string, std::string>& vars,
                                         const std::string& fallback = "")
        {
            std::string tmpl;
            if (!LoadFile(path, tmpl))
                tmpl = fallback;
            return Render(tmpl, vars);
        }
    };

} // namespace stnks
