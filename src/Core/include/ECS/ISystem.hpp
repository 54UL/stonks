#pragma once
#include <ECS/Entity.hpp>

namespace stnks {
    class Engine; // forward

    enum class ProcessingChannel { MAIN, RENDERING, AUDIO };

    class ISystem
    {
    public:
        virtual ~ISystem() = default;
        virtual ProcessingChannel Channel() const = 0;
        virtual void OnStart(Engine& engine) = 0;
        virtual void OnUpdate(float dt) = 0;
    };

} // namespace stnks
