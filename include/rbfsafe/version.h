#pragma once

#define RBFSAFE_VERSION_MAJOR 0
#define RBFSAFE_VERSION_MINOR 2
#define RBFSAFE_VERSION_PATCH 0
#define RBFSAFE_VERSION_STRING "0.2.0"

namespace rbfsafe {
inline constexpr const char* kVersion = RBFSAFE_VERSION_STRING;
inline constexpr unsigned kAtlasSchemaVersion = 1;
inline constexpr unsigned kLectSchemaVersion = 1;
} // namespace rbfsafe
