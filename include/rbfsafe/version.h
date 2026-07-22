#pragma once

#define RBFSAFE_VERSION_MAJOR 2
#define RBFSAFE_VERSION_MINOR 0
#define RBFSAFE_VERSION_PATCH 0
#define RBFSAFE_VERSION_STRING "2.0.0"

namespace rbfsafe {
inline constexpr const char* kVersion = RBFSAFE_VERSION_STRING;
inline constexpr unsigned kAtlasSchemaVersion = 2;
inline constexpr unsigned kLectSchemaVersion = 1;
} // namespace rbfsafe
