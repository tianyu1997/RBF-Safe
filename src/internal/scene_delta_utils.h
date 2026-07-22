#pragma once

#include <rbfsafe/scene_delta.h>

#include "internal/json.h"

namespace rbfsafe::internal {

Json encode_scene_delta(const SceneDelta& delta);
Result<SceneDelta> decode_scene_delta(const Json& document);
Result<void> validate_scene_delta(const SceneDelta& delta);

} // namespace rbfsafe::internal
