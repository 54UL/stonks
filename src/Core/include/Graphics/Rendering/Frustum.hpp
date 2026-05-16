#pragma once

#include <glm/glm.hpp>
#include <array>

namespace stnks
{
    struct Frustum
    {
        bool enabled = false;
        std::array<glm::vec4, 6> planes{};

        bool IsVisible(const glm::vec3& pos, float w, float h) const
        {
            if (!enabled) return true;
            float radius = glm::max(w, h) * 0.5f;
            for (const auto& p : planes)
            {
                if (glm::dot(glm::vec3(p), pos) + p.w + radius < 0.f)
                    return false;
            }
            return true;
        }

        static Frustum FromPV(const glm::mat4& pv)
        {
            Frustum f;
            f.enabled = true;
            // Extract planes from combined projection-view matrix
            for (int i = 0; i < 3; ++i)
            {
                f.planes[i * 2 + 0] = glm::vec4(
                    pv[0][3] + pv[0][i],
                    pv[1][3] + pv[1][i],
                    pv[2][3] + pv[2][i],
                    pv[3][3] + pv[3][i]);
                f.planes[i * 2 + 1] = glm::vec4(
                    pv[0][3] - pv[0][i],
                    pv[1][3] - pv[1][i],
                    pv[2][3] - pv[2][i],
                    pv[3][3] - pv[3][i]);
            }
            // Normalize
            for (auto& p : f.planes)
            {
                float len = glm::length(glm::vec3(p));
                if (len > 0.f) p /= len;
            }
            return f;
        }
    };

} // namespace stnks
