#pragma once

#include <rbfsafe/modules/lect.h>

#include "internal/binary.h"

#include <span>

namespace rbfsafe::internal {

BinaryWriter encode_lect_tree(const LectTree& tree);
Result<LectTree> decode_lect_tree(std::span<const std::byte> bytes);

} // namespace rbfsafe::internal
