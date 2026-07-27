#pragma once

#include <rbfsafe/remote.h>

#include <string>

namespace rbfsafe::internal {

std::string artifact_fetch_request_identity(const ArtifactFetchRequest& request);
std::string artifact_fetch_response_identity(const ArtifactFetchResponse& response);
std::string artifact_publish_request_identity(const ArtifactPublishRequest& request);
std::string artifact_publish_receipt_identity(const ArtifactPublishReceipt& receipt);
std::string verified_artifact_transfer_identity(const VerifiedArtifactTransfer& transfer);
std::string artifact_transfer_record_identity(const ArtifactTransferRecord& record);

} // namespace rbfsafe::internal
