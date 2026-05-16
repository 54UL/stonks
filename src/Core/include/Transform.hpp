#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cereal/archives/json.hpp>

namespace stnks
{
    class Transform
    {
    public:
        glm::vec3 position{0.f};
        glm::vec3 rotation{0.f};
        glm::vec3 scale{1.f};

        void setGlobalPosition(const glm::vec3& pos) { position = pos; }
        glm::vec3 getGlobalPosition() const { return position; }

        void setGlobalScale(const glm::vec3& s) { scale = s; }
        glm::vec3 getGlobalScale() const { return scale; }

        void setGlobalRotation(const glm::vec3& rot) { rotation = rot; }
        glm::vec3 getGlobalRotation() const { return rotation; }

        glm::mat4 GetMatrix() const
        {
            glm::mat4 m(1.0f);
            m = glm::translate(m, position);
            m = glm::rotate(m, rotation.x, glm::vec3(1, 0, 0));
            m = glm::rotate(m, rotation.y, glm::vec3(0, 1, 0));
            m = glm::rotate(m, rotation.z, glm::vec3(0, 0, 1));
            m = glm::scale(m, scale);
            return m;
        }

        template <class Archive>
        void serialize(Archive& ar)
        {
            ar(cereal::make_nvp("px", position.x),
               cereal::make_nvp("py", position.y),
               cereal::make_nvp("pz", position.z),
               cereal::make_nvp("rx", rotation.x),
               cereal::make_nvp("ry", rotation.y),
               cereal::make_nvp("rz", rotation.z),
               cereal::make_nvp("sx", scale.x),
               cereal::make_nvp("sy", scale.y),
               cereal::make_nvp("sz", scale.z));
        }
    };

} // namespace stnks
