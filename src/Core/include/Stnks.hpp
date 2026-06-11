#ifndef STNKS_HPP
#define STNKS_HPP

#include <Dependency.hpp>
#include <Engine.hpp>

#ifndef STNKS_HEADLESS
#include <App/App.hpp>
#include <App/SDL2App.hpp>
#include <Graphics/Rendering.hpp>
#include <Graphics/Rendering/Entities/Camera.hpp>
#include <Transform.hpp>

CEREAL_REGISTER_TYPE(stnks::Camera);
CEREAL_REGISTER_TYPE(stnks::Renderable);
CEREAL_REGISTER_POLYMORPHIC_RELATION(stnks::Renderable, stnks::Camera)
#endif

#endif
