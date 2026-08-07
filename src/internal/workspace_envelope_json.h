#pragma once

#include "json.h"

#include <rbfsafe/modules/core.h>
#include <rbfsafe/modules/envelope.h>

namespace rbfsafe::internal {

Json workspace_envelope_json(const WorkspaceEnvelope& envelope, bool include_type = true);
Result<WorkspaceEnvelope> decode_workspace_envelope(const Json& value, bool typed);

} // namespace rbfsafe::internal
