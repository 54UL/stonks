#ifndef STNKS_HPP
#define STNKS_HPP

#include <App/App.hpp>
#include <Dependency.hpp>
#include <App/SDL2App.hpp>
#include <Graphics/Rendering.hpp>
#include <Graphics/Rendering/Entities/Camera.hpp>
#include <Transform.hpp>
#include <Engine.hpp>

CEREAL_REGISTER_TYPE(stnks::Camera);
CEREAL_REGISTER_TYPE(stnks::Renderable);
CEREAL_REGISTER_POLYMORPHIC_RELATION(stnks::Renderable, stnks::Camera)

#endif
