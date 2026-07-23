#pragma once

#define RBFSAFE_VERSION_MAJOR 3
#define RBFSAFE_VERSION_MINOR 4
#define RBFSAFE_VERSION_PATCH 0
#define RBFSAFE_VERSION_STRING "3.4.0"

namespace rbfsafe {
inline constexpr const char* kVersion = RBFSAFE_VERSION_STRING;
inline constexpr unsigned kAtlasSchemaVersion = 2;
inline constexpr unsigned kLectSchemaVersion = 1;
} // namespace rbfsafe
