#include <rbfsafe/modules/assurance.h>
#include <rbfsafe/modules/core.h>

#include "internal/binary.h"
#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <charconv>
#include <chrono>
#include <cmath>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace rbfsafe {
namespace {

using internal::Json;

constexpr std::uint32_t kStorageSchema = 1;
constexpr std::size_t kParticipantFields = 18;
constexpr std::size_t kAgreementFields = 16;

bool exact_object(const Json& value, std::size_t fields) {
    return value.is_object() && value.as_object().size() == fields;
}

bool negative_zero(double value) { return value == 0.0 && std::signbit(value); }

Result<std::string> string_field(const Json& object, std::string_view key, std::size_t maximum_bytes,
                                 bool allow_empty = false) {
    const auto* value = object.is_object() ? object.find(key) : nullptr;
    if (value == nullptr || !value->is_string() || (!allow_empty && value->as_string().empty()) ||
        value->as_string().size() > maximum_bytes) {
        return Result<std::string>::failure(
            StatusCode::CorruptData, "coordinated reservation string field is invalid", std::string(key));
    }
    return value->as_string();
}

Result<std::uint64_t> decimal_field(const Json& object, std::string_view key) {
    auto text = string_field(object, key, 32);
    if (!text)
        return text.error();
    if (text.value().size() > 1 && text.value().front() == '0') {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData,
                                              "coordinated reservation decimal field is not canonical",
                                              std::string(key));
    }
    std::uint64_t result = 0;
    const auto* begin = text.value().data();
    const auto* end = begin + text.value().size();
    const auto parsed = std::from_chars(begin, end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return Result<std::uint64_t>::failure(
            StatusCode::CorruptData, "coordinated reservation decimal field is invalid", std::string(key));
    }
    return result;
}

Result<std::uint32_t> integer_field(const Json& object, std::string_view key, std::uint32_t maximum) {
    const auto* value = object.is_object() ? object.find(key) : nullptr;
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number()) ||
        value->as_number() < 0.0 || value->as_number() > static_cast<double>(maximum) ||
        std::floor(value->as_number()) != value->as_number()) {
        return Result<std::uint32_t>::failure(
            StatusCode::CorruptData, "coordinated reservation integer field is invalid", std::string(key));
    }
    return static_cast<std::uint32_t>(value->as_number());
}

Result<double> double_field(const Json& object, std::string_view key) {
    const auto* value = object.is_object() ? object.find(key) : nullptr;
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number()) ||
        negative_zero(value->as_number())) {
        return Result<double>::failure(StatusCode::CorruptData,
                                       "coordinated reservation numeric field is invalid", std::string(key));
    }
    return value->as_number();
}

Json participant_json(const CoordinatedReservationParticipant& participant) {
    return Json::Object{
        {"deployment_id", participant.deployment_id},
        {"id", participant.id},
        {"occupancy_id", participant.occupancy_id},
        {"payload_bytes", std::to_string(participant.payload_bytes)},
        {"payload_digest", participant.payload_digest},
        {"publication_head_id", participant.publication_head_id},
        {"publication_root_id", participant.publication_root_id},
        {"publication_trust_bundle_id", participant.publication_trust_bundle_id},
        {"publisher_key_id", participant.publisher_key_id},
        {"publisher_sequence", std::to_string(participant.publisher_sequence)},
        {"publisher_service_id", participant.publisher_service_id},
        {"storage_schema", static_cast<int>(participant.storage_schema)},
        {"stream_id", participant.stream_id},
        {"trust_head_bundle_id", participant.trust_head_bundle_id},
        {"trust_root_bundle_id", participant.trust_root_bundle_id},
        {"valid_from_tick", std::to_string(participant.valid_from_tick)},
        {"valid_through_tick", std::to_string(participant.valid_through_tick)},
        {"verified_publication_id", participant.verified_publication_id},
    };
}

Json agreement_payload(const CoordinatedReservationAgreement& agreement) {
    Json::Array participants;
    participants.reserve(agreement.participants.size());
    for (const auto& participant : agreement.participants)
        participants.emplace_back(participant_json(participant));
    return Json::Object{
        {"evaluation_tick", std::to_string(agreement.evaluation_tick)},
        {"id", agreement.id},
        {"minimum_separation", agreement.minimum_separation},
        {"occupancy_bundle_id", agreement.occupancy_bundle_id},
        {"occupancy_report_id", agreement.occupancy_report_id},
        {"parent_agreement_id", agreement.parent_agreement_id},
        {"participants", std::move(participants)},
        {"payload_bytes", std::to_string(agreement.payload_bytes)},
        {"payload_digest", agreement.payload_digest},
        {"protocol_id", agreement.protocol_id},
        {"round", std::to_string(agreement.round)},
        {"storage_schema", static_cast<int>(agreement.storage_schema)},
        {"timeline_id", agreement.timeline_id},
        {"valid_from_tick", std::to_string(agreement.valid_from_tick)},
        {"valid_through_tick", std::to_string(agreement.valid_through_tick)},
        {"workspace_frame_id", agreement.workspace_frame_id},
    };
}

Json agreement_document(const CoordinatedReservationAgreement& agreement) {
    const auto payload = agreement_payload(agreement);
    return Json::Object{
        {"checksum", internal::sha256(payload.dump(false))},
        {"format", "rbfsafe-coordinated-reservation-agreement"},
        {"library_version", kVersion},
        {"payload", payload},
        {"schema", static_cast<int>(kStorageSchema)},
    };
}

Result<CoordinatedReservationParticipant> decode_participant(const Json& value) {
    if (!exact_object(value, kParticipantFields)) {
        return Result<CoordinatedReservationParticipant>::failure(
            StatusCode::CorruptData, "coordinated reservation participant fields are invalid");
    }
    auto storage_schema = integer_field(value, "storage_schema", std::numeric_limits<std::uint32_t>::max());
    auto id = string_field(value, "id", 64);
    auto deployment = string_field(value, "deployment_id", 256);
    auto occupancy = string_field(value, "occupancy_id", 64);
    auto stream = string_field(value, "stream_id", 256);
    auto service = string_field(value, "publisher_service_id", 256);
    auto key = string_field(value, "publisher_key_id", 64);
    auto sequence = decimal_field(value, "publisher_sequence");
    auto trust_root = string_field(value, "trust_root_bundle_id", 64);
    auto trust_head = string_field(value, "trust_head_bundle_id", 64);
    auto publication_trust = string_field(value, "publication_trust_bundle_id", 64);
    auto publication_root = string_field(value, "publication_root_id", 64);
    auto publication_head = string_field(value, "publication_head_id", 64);
    auto verified = string_field(value, "verified_publication_id", 64);
    auto digest = string_field(value, "payload_digest", 64);
    auto bytes = decimal_field(value, "payload_bytes");
    auto valid_from = decimal_field(value, "valid_from_tick");
    auto valid_through = decimal_field(value, "valid_through_tick");
    if (!storage_schema || !id || !deployment || !occupancy || !stream || !service || !key || !sequence ||
        !trust_root || !trust_head || !publication_trust || !publication_root || !publication_head ||
        !verified || !digest || !bytes || !valid_from || !valid_through) {
        return Result<CoordinatedReservationParticipant>::failure(
            StatusCode::CorruptData, "coordinated reservation participant is incomplete");
    }
    if (storage_schema.value() != kStorageSchema) {
        return Result<CoordinatedReservationParticipant>::failure(
            StatusCode::IncompatibleFormat, "coordinated reservation participant schema is unsupported");
    }
    CoordinatedReservationParticipant result;
    result.storage_schema = storage_schema.value();
    result.id = std::move(id).value();
    result.deployment_id = std::move(deployment).value();
    result.occupancy_id = std::move(occupancy).value();
    result.stream_id = std::move(stream).value();
    result.publisher_service_id = std::move(service).value();
    result.publisher_key_id = std::move(key).value();
    result.publisher_sequence = sequence.value();
    result.trust_root_bundle_id = std::move(trust_root).value();
    result.trust_head_bundle_id = std::move(trust_head).value();
    result.publication_trust_bundle_id = std::move(publication_trust).value();
    result.publication_root_id = std::move(publication_root).value();
    result.publication_head_id = std::move(publication_head).value();
    result.verified_publication_id = std::move(verified).value();
    result.payload_digest = std::move(digest).value();
    result.payload_bytes = bytes.value();
    result.valid_from_tick = valid_from.value();
    result.valid_through_tick = valid_through.value();
    if (!result.valid()) {
        return Result<CoordinatedReservationParticipant>::failure(
            StatusCode::IdentityMismatch, "coordinated reservation participant identity is invalid",
            result.id);
    }
    return result;
}

Result<CoordinatedReservationAgreement>
decode_agreement(const Json& payload, const CoordinatedReservationAgreementLoadOptions& options) {
    if (!exact_object(payload, kAgreementFields)) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::CorruptData, "coordinated reservation agreement fields are invalid");
    }
    auto storage_schema = integer_field(payload, "storage_schema", std::numeric_limits<std::uint32_t>::max());
    auto id = string_field(payload, "id", 64);
    auto protocol = string_field(payload, "protocol_id", 256);
    auto round = decimal_field(payload, "round");
    auto parent = string_field(payload, "parent_agreement_id", 64, true);
    auto evaluation = decimal_field(payload, "evaluation_tick");
    auto valid_from = decimal_field(payload, "valid_from_tick");
    auto valid_through = decimal_field(payload, "valid_through_tick");
    auto bundle = string_field(payload, "occupancy_bundle_id", 64);
    auto report = string_field(payload, "occupancy_report_id", 64);
    auto timeline = string_field(payload, "timeline_id", 256);
    auto frame = string_field(payload, "workspace_frame_id", 256);
    auto separation = double_field(payload, "minimum_separation");
    auto digest = string_field(payload, "payload_digest", 64);
    auto bytes = decimal_field(payload, "payload_bytes");
    const auto* participants = payload.is_object() ? payload.find("participants") : nullptr;
    if (!storage_schema || !id || !protocol || !round || !parent || !evaluation || !valid_from ||
        !valid_through || !bundle || !report || !timeline || !frame || !separation || !digest || !bytes ||
        participants == nullptr || !participants->is_array()) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::CorruptData, "coordinated reservation agreement is incomplete");
    }
    if (storage_schema.value() != kStorageSchema) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::IncompatibleFormat, "coordinated reservation agreement schema is unsupported");
    }
    if (participants->as_array().size() > options.maximum_participants) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::ResourceLimit, "coordinated reservation participant count exceeds limit");
    }
    CoordinatedReservationAgreement result;
    result.storage_schema = storage_schema.value();
    result.id = std::move(id).value();
    result.protocol_id = std::move(protocol).value();
    result.round = round.value();
    result.parent_agreement_id = std::move(parent).value();
    result.evaluation_tick = evaluation.value();
    result.valid_from_tick = valid_from.value();
    result.valid_through_tick = valid_through.value();
    result.occupancy_bundle_id = std::move(bundle).value();
    result.occupancy_report_id = std::move(report).value();
    result.timeline_id = std::move(timeline).value();
    result.workspace_frame_id = std::move(frame).value();
    result.minimum_separation = separation.value();
    result.payload_digest = std::move(digest).value();
    result.payload_bytes = bytes.value();
    result.participants.reserve(participants->as_array().size());
    for (const auto& item : participants->as_array()) {
        if (options.cancellation.cancelled()) {
            return Result<CoordinatedReservationAgreement>::failure(
                StatusCode::Cancelled, "coordinated reservation load was cancelled");
        }
        auto participant = decode_participant(item);
        if (!participant)
            return participant.error();
        result.participants.push_back(std::move(participant).value());
    }
    if (!result.valid()) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::IdentityMismatch, "coordinated reservation agreement identity is invalid", result.id);
    }
    return result;
}

std::filesystem::path compact_temporary(const std::filesystem::path& path, std::string_view purpose) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return path.parent_path() / ("." + std::string(purpose) + "-" + std::to_string(nonce) + ".json");
}

Result<void> publish_file(const std::filesystem::path& temporary, const std::filesystem::path& destination,
                          bool destination_exists) {
    std::error_code error;
    std::filesystem::path backup;
    if (destination_exists) {
        backup = compact_temporary(destination, "reservation-backup");
        std::filesystem::rename(destination, backup, error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to stage existing coordinated reservation",
                                         destination.string());
        }
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        if (destination_exists) {
            std::error_code ignored;
            std::filesystem::rename(backup, destination, ignored);
        }
        return Result<void>::failure(StatusCode::IoError, "failed to publish coordinated reservation",
                                     destination.string());
    }
    if (destination_exists) {
        std::filesystem::remove(backup, error);
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to remove staged coordinated reservation", backup.string());
        }
    }
    return Result<void>::success();
}

} // namespace

Result<void> CoordinatedReservationAgreement::save(const std::filesystem::path& path,
                                                   const SaveOptions& options) const {
    if (!valid() || path.empty() || path == path.root_path()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "coordinated reservation agreement or destination is invalid");
    }
    std::error_code error;
    const bool destination_exists = std::filesystem::exists(path, error);
    if (error) {
        return Result<void>::failure(StatusCode::IoError,
                                     "failed to inspect coordinated reservation destination", path.string());
    }
    if (destination_exists) {
        const auto status = std::filesystem::symlink_status(path, error);
        if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
            return Result<void>::failure(
                StatusCode::IoError, "coordinated reservation destination is indirect or not a regular file",
                path.string());
        }
        if (!options.overwrite) {
            return Result<void>::failure(StatusCode::IoError,
                                         "coordinated reservation destination already exists", path.string());
        }
    }
    const auto parent = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
    if (!std::filesystem::exists(parent, error) || !std::filesystem::is_directory(parent, error) || error) {
        return Result<void>::failure(StatusCode::IoError,
                                     "coordinated reservation destination parent is unavailable",
                                     parent.string());
    }
    const auto temporary = compact_temporary(path, "reservation");
    const auto document = agreement_document(*this).dump(true) + "\n";
    auto written = internal::write_text_file(temporary, document);
    if (!written) {
        std::filesystem::remove(temporary, error);
        return written.error();
    }
    CoordinatedReservationAgreementLoadOptions verification_options;
    verification_options.maximum_participants = participants.size();
    verification_options.maximum_payload_bytes = std::max<std::uintmax_t>(document.size(), 1);
    auto loaded = CoordinatedReservationAgreement::load(temporary, verification_options);
    if (!loaded || loaded.value().id != id) {
        std::filesystem::remove(temporary, error);
        if (!loaded)
            return loaded.error();
        return Result<void>::failure(StatusCode::CorruptData,
                                     "staged coordinated reservation identity changed", id);
    }
    auto published = publish_file(temporary, path, destination_exists);
    if (!published)
        std::filesystem::remove(temporary, error);
    return published;
}

Result<CoordinatedReservationAgreement>
CoordinatedReservationAgreement::load(const std::filesystem::path& path,
                                      const CoordinatedReservationAgreementLoadOptions& options) {
    if (path.empty() || options.maximum_participants == 0 || options.maximum_payload_bytes == 0) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::InvalidArgument, "coordinated reservation path or limits are invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<CoordinatedReservationAgreement>::failure(StatusCode::Cancelled,
                                                                "coordinated reservation load was cancelled");
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || status.type() == std::filesystem::file_type::not_found) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::IoError, "failed to inspect coordinated reservation", path.string());
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::CorruptData, "coordinated reservation is indirect or not a regular file",
            path.string());
    }
    auto bytes = internal::read_binary_file(path, options.maximum_payload_bytes);
    if (!bytes)
        return bytes.error();
    const std::string_view text(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
    auto document = Json::parse(text);
    if (!document)
        return document.error();
    if (!exact_object(document.value(), 5)) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::CorruptData, "coordinated reservation document fields are invalid");
    }
    auto format = string_field(document.value(), "format", 128);
    auto schema = integer_field(document.value(), "schema", std::numeric_limits<std::uint32_t>::max());
    auto version = string_field(document.value(), "library_version", 128);
    auto checksum = string_field(document.value(), "checksum", 64);
    const auto* payload = document.value().find("payload");
    if (!format || !schema || !version || !checksum || payload == nullptr) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::CorruptData, "coordinated reservation document is incomplete");
    }
    if (format.value() != "rbfsafe-coordinated-reservation-agreement" || schema.value() != kStorageSchema) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::IncompatibleFormat, "coordinated reservation format or schema is unsupported");
    }
    if (!internal::valid_sha256(checksum.value()) ||
        checksum.value() != internal::sha256(payload->dump(false))) {
        return Result<CoordinatedReservationAgreement>::failure(
            StatusCode::CorruptData, "coordinated reservation checksum mismatch", path.string());
    }
    return decode_agreement(*payload, options);
}

} // namespace rbfsafe
