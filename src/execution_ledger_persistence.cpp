#include <rbfsafe/execution_ledger.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/execution_ledger.h"
#include "internal/identity.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kSchema = 1;
constexpr std::size_t kMaximumRecognizableSchema = 1000;
constexpr std::size_t kMaximumStringBytes = 4096;

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

internal::Json authorization_json(const ExecutionCommandAuthorization& authorization) {
    return internal::Json::Object{
        {"command_digest", authorization.command_digest},
        {"command_index", std::to_string(authorization.command_index)},
        {"command_sequence_id", authorization.command_sequence_id},
        {"dispatch_monotonic_ns", std::to_string(authorization.dispatch_monotonic_ns)},
        {"evidence", static_cast<int>(authorization.evidence)},
        {"id", authorization.id},
        {"session_id", authorization.session_id},
        {"valid_from_monotonic_ns", std::to_string(authorization.valid_from_monotonic_ns)},
        {"valid_through_monotonic_ns", std::to_string(authorization.valid_through_monotonic_ns)},
    };
}

internal::Json completion_json(const ExecutionControllerCompletion& completion) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(completion.algorithm)},
        {"authentication_tag", completion.authentication_tag},
        {"authorization_id", completion.authorization_id},
        {"command_digest", completion.command_digest},
        {"command_index", std::to_string(completion.command_index)},
        {"command_sequence_id", completion.command_sequence_id},
        {"completed_monotonic_ns", std::to_string(completion.completed_monotonic_ns)},
        {"controller_key_id", completion.controller_key_id},
        {"controller_service_id", completion.controller_service_id},
        {"id", completion.id},
        {"outcome", static_cast<int>(completion.outcome)},
        {"result_digest", completion.result_digest},
        {"session_id", completion.session_id},
        {"storage_schema", static_cast<double>(completion.storage_schema)},
    };
}

internal::Json checkpoint_json(const ServiceTrustCheckpoint& checkpoint) {
    internal::Json::Array signatures;
    signatures.reserve(checkpoint.signatures.size());
    for (const auto& signature : checkpoint.signatures) {
        signatures.emplace_back(internal::Json::Object{
            {"algorithm", static_cast<int>(signature.algorithm)},
            {"authentication_tag", signature.authentication_tag},
            {"signer_key_id", signature.signer_key_id},
            {"signer_service_id", signature.signer_service_id},
        });
    }
    return internal::Json::Object{
        {"head_bundle_id", checkpoint.head_bundle_id},
        {"head_record_id", checkpoint.head_record_id},
        {"head_sequence", std::to_string(checkpoint.head_sequence)},
        {"id", checkpoint.id},
        {"root_bundle_id", checkpoint.root_bundle_id},
        {"signatures", std::move(signatures)},
        {"storage_schema", static_cast<double>(checkpoint.storage_schema)},
    };
}

internal::Json revocation_json(const ExecutionDependencyRevocation& revocation) {
    return internal::Json::Object{
        {"detail", revocation.detail},
        {"kind", static_cast<int>(revocation.kind)},
        {"subject_id", revocation.subject_id},
    };
}

internal::Json record_payload(const ExecutionLedgerRecord& record) {
    return internal::Json::Object{
        {"authorization",
         record.authorization ? authorization_json(*record.authorization) : internal::Json(nullptr)},
        {"completion", record.completion ? completion_json(*record.completion) : internal::Json(nullptr)},
        {"detail", record.detail},
        {"format", "rbfsafe-execution-ledger-record"},
        {"id", record.id},
        {"ledger_id", record.ledger_id},
        {"observed_monotonic_ns", std::to_string(record.observed_monotonic_ns)},
        {"parent_id", record.parent_id},
        {"revocation", record.revocation ? revocation_json(*record.revocation) : internal::Json(nullptr)},
        {"schema", static_cast<double>(record.storage_schema)},
        {"sequence", std::to_string(record.sequence)},
        {"session_id", record.session_id},
        {"trust_checkpoint",
         record.trust_checkpoint ? checkpoint_json(*record.trust_checkpoint) : internal::Json(nullptr)},
        {"type", static_cast<int>(record.type)},
    };
}

internal::Json record_document(const ExecutionLedgerRecord& record) {
    auto payload = record_payload(record);
    auto object = payload.as_object();
    object.emplace("identity", internal::sha256(payload.dump(false)));
    object.emplace("library_version", kVersion);
    return object;
}

internal::Json manifest_payload(const std::string& ledger_id, const BoundedExecutionSession& session,
                                const std::string& root_record_id) {
    return internal::Json::Object{
        {"command_count", std::to_string(session.command_sequence().commands.size())},
        {"format", "rbfsafe-execution-ledger"},
        {"id", ledger_id},
        {"root_record_id", root_record_id},
        {"schema", static_cast<double>(kSchema)},
        {"session_id", session.id()},
        {"valid_from_monotonic_ns", std::to_string(session.valid_from_monotonic_ns())},
        {"valid_through_monotonic_ns", std::to_string(session.valid_through_monotonic_ns())},
    };
}

internal::Json manifest_document(const std::string& ledger_id, const BoundedExecutionSession& session,
                                 const std::string& root_record_id) {
    auto payload = manifest_payload(ledger_id, session, root_record_id);
    auto object = payload.as_object();
    object.emplace("identity", internal::sha256(payload.dump(false)));
    object.emplace("library_version", kVersion);
    return object;
}

Result<void> inspect_regular_file(const std::filesystem::path& path, std::uintmax_t maximum_bytes) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return Result<void>::failure(StatusCode::IoError,
                                     "execution-ledger file is missing or is not a direct regular file",
                                     path.string());
    }
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        return Result<void>::failure(StatusCode::IoError, "failed to inspect execution-ledger file size",
                                     path.string());
    }
    if (bytes > maximum_bytes) {
        return Result<void>::failure(StatusCode::ResourceLimit,
                                     "execution-ledger file exceeds configured byte limit", path.string());
    }
    return Result<void>::success();
}

Result<internal::Json> read_bounded_json(const std::filesystem::path& path, std::uintmax_t maximum_bytes) {
    auto inspected = inspect_regular_file(path, maximum_bytes);
    if (!inspected)
        return inspected.error();
    return internal::read_json_file(path);
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 bool allow_empty = false) {
    if (!object.is_object()) {
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "execution-ledger record is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > kMaximumStringBytes ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "execution-ledger string field is invalid", std::string(key));
    }
    return value->as_string();
}

Result<double> number_field(const internal::Json& object, std::string_view key) {
    if (!object.is_object()) {
        return Result<double>::failure(StatusCode::CorruptData, "execution-ledger record is not an object");
    }
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number())) {
        return Result<double>::failure(StatusCode::CorruptData, "execution-ledger number field is invalid",
                                       std::string(key));
    }
    return value->as_number();
}

Result<std::uint64_t> decimal_field(const internal::Json& object, std::string_view key) {
    auto text = string_field(object, key);
    if (!text)
        return text.error();
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(text.value().data(), text.value().data() + text.value().size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.value().data() + text.value().size()) {
        return Result<std::uint64_t>::failure(StatusCode::CorruptData,
                                              "execution-ledger decimal field is invalid", std::string(key));
    }
    return result;
}

Result<std::size_t> enum_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    auto value = number_field(object, key);
    if (!value || value.value() < 0.0 || std::floor(value.value()) != value.value() ||
        value.value() > static_cast<double>(maximum)) {
        return Result<std::size_t>::failure(StatusCode::CorruptData, "execution-ledger enum field is invalid",
                                            std::string(key));
    }
    return static_cast<std::size_t>(value.value());
}

Result<ExecutionCommandAuthorization> decode_authorization(const internal::Json& object) {
    auto id = string_field(object, "id");
    auto session = string_field(object, "session_id");
    auto sequence = string_field(object, "command_sequence_id");
    auto index = decimal_field(object, "command_index");
    auto digest = string_field(object, "command_digest");
    auto dispatch = decimal_field(object, "dispatch_monotonic_ns");
    auto valid_from = decimal_field(object, "valid_from_monotonic_ns");
    auto valid_through = decimal_field(object, "valid_through_monotonic_ns");
    auto evidence =
        enum_field(object, "evidence", static_cast<std::size_t>(EvidenceLevel::RuntimeExecutable));
    if (!id || !session || !sequence || !index || !digest || !dispatch || !valid_from || !valid_through ||
        !evidence) {
        return Result<ExecutionCommandAuthorization>::failure(StatusCode::CorruptData,
                                                              "execution-ledger authorization is incomplete");
    }
    ExecutionCommandAuthorization result;
    result.id = std::move(id).value();
    result.session_id = std::move(session).value();
    result.command_sequence_id = std::move(sequence).value();
    result.command_index = index.value();
    result.command_digest = std::move(digest).value();
    result.dispatch_monotonic_ns = dispatch.value();
    result.valid_from_monotonic_ns = valid_from.value();
    result.valid_through_monotonic_ns = valid_through.value();
    result.evidence = static_cast<EvidenceLevel>(evidence.value());
    if (!result.valid()) {
        return Result<ExecutionCommandAuthorization>::failure(
            StatusCode::CorruptData, "execution-ledger authorization identity is invalid", result.id);
    }
    return result;
}

Result<ExecutionControllerCompletion> decode_completion(const internal::Json& object) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto session = string_field(object, "session_id");
    auto authorization = string_field(object, "authorization_id");
    auto sequence = string_field(object, "command_sequence_id");
    auto index = decimal_field(object, "command_index");
    auto digest = string_field(object, "command_digest");
    auto service = string_field(object, "controller_service_id");
    auto key = string_field(object, "controller_key_id");
    auto outcome =
        enum_field(object, "outcome", static_cast<std::size_t>(ExecutionCompletionOutcome::Rejected));
    auto completed = decimal_field(object, "completed_monotonic_ns");
    auto result_digest = string_field(object, "result_digest");
    auto algorithm =
        enum_field(object, "algorithm", static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto tag = string_field(object, "authentication_tag");
    if (!storage || !id || !session || !authorization || !sequence || !index || !digest || !service || !key ||
        !outcome || !completed || !result_digest || !algorithm || !tag) {
        return Result<ExecutionControllerCompletion>::failure(
            StatusCode::CorruptData, "execution-ledger controller completion is incomplete");
    }
    ExecutionControllerCompletion result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.session_id = std::move(session).value();
    result.authorization_id = std::move(authorization).value();
    result.command_sequence_id = std::move(sequence).value();
    result.command_index = index.value();
    result.command_digest = std::move(digest).value();
    result.controller_service_id = std::move(service).value();
    result.controller_key_id = std::move(key).value();
    result.outcome = static_cast<ExecutionCompletionOutcome>(outcome.value());
    result.completed_monotonic_ns = completed.value();
    result.result_digest = std::move(result_digest).value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(tag).value();
    if (!result.valid()) {
        return Result<ExecutionControllerCompletion>::failure(
            StatusCode::CorruptData, "execution-ledger controller-completion identity is invalid", result.id);
    }
    return result;
}

Result<ServiceTrustCheckpoint> decode_checkpoint(const internal::Json& object,
                                                 const ExecutionLedgerLoadOptions& options,
                                                 std::size_t& total_signatures) {
    auto storage = enum_field(object, "storage_schema", 1);
    auto id = string_field(object, "id");
    auto root = string_field(object, "root_bundle_id");
    auto head = string_field(object, "head_bundle_id");
    auto sequence = decimal_field(object, "head_sequence");
    auto record = string_field(object, "head_record_id");
    const auto* signatures = object.is_object() ? object.find("signatures") : nullptr;
    if (!storage || !id || !root || !head || !sequence || !record || signatures == nullptr ||
        !signatures->is_array() || signatures->as_array().empty()) {
        return Result<ServiceTrustCheckpoint>::failure(StatusCode::CorruptData,
                                                       "execution-ledger checkpoint is incomplete");
    }
    if (signatures->as_array().size() > options.maximum_signatures_per_checkpoint ||
        signatures->as_array().size() > options.maximum_total_checkpoint_signatures - total_signatures) {
        return Result<ServiceTrustCheckpoint>::failure(
            StatusCode::ResourceLimit,
            "execution-ledger checkpoint signature count exceeds configured limits");
    }
    ServiceTrustCheckpoint result;
    result.storage_schema = static_cast<std::uint32_t>(storage.value());
    result.id = std::move(id).value();
    result.root_bundle_id = std::move(root).value();
    result.head_bundle_id = std::move(head).value();
    result.head_sequence = sequence.value();
    result.head_record_id = std::move(record).value();
    result.signatures.reserve(signatures->as_array().size());
    for (const auto& value : signatures->as_array()) {
        auto service = string_field(value, "signer_service_id");
        auto key = string_field(value, "signer_key_id");
        auto algorithm = enum_field(value, "algorithm",
                                    static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
        auto tag = string_field(value, "authentication_tag");
        if (!service || !key || !algorithm || !tag) {
            return Result<ServiceTrustCheckpoint>::failure(
                StatusCode::CorruptData, "execution-ledger checkpoint signature is incomplete");
        }
        ServiceTrustCheckpointSignature signature;
        signature.signer_service_id = std::move(service).value();
        signature.signer_key_id = std::move(key).value();
        signature.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
        signature.authentication_tag = std::move(tag).value();
        result.signatures.push_back(std::move(signature));
    }
    total_signatures += result.signatures.size();
    if (!result.valid()) {
        return Result<ServiceTrustCheckpoint>::failure(
            StatusCode::CorruptData, "execution-ledger checkpoint identity is invalid", result.id);
    }
    return result;
}

Result<ExecutionDependencyRevocation> decode_revocation(const internal::Json& object) {
    auto kind =
        enum_field(object, "kind", static_cast<std::size_t>(ExecutionDependencyKind::TrustCheckpoint));
    auto subject = string_field(object, "subject_id");
    auto detail = string_field(object, "detail");
    if (!kind || !subject || !detail) {
        return Result<ExecutionDependencyRevocation>::failure(
            StatusCode::CorruptData, "execution-ledger dependency revocation is incomplete");
    }
    ExecutionDependencyRevocation result;
    result.kind = static_cast<ExecutionDependencyKind>(kind.value());
    result.subject_id = std::move(subject).value();
    result.detail = std::move(detail).value();
    if (!valid_execution_dependency_revocation(result)) {
        return Result<ExecutionDependencyRevocation>::failure(
            StatusCode::CorruptData, "execution-ledger dependency revocation is invalid");
    }
    return result;
}

Result<ExecutionLedgerRecord> decode_record(const internal::Json& document,
                                            const ExecutionLedgerLoadOptions& options,
                                            std::size_t& total_signatures) {
    auto format = string_field(document, "format");
    auto schema = enum_field(document, "schema", kMaximumRecognizableSchema);
    auto library_version = string_field(document, "library_version");
    auto identity = string_field(document, "identity");
    auto id = string_field(document, "id");
    auto sequence = decimal_field(document, "sequence");
    auto parent = string_field(document, "parent_id", true);
    auto ledger = string_field(document, "ledger_id");
    auto session = string_field(document, "session_id");
    auto type =
        enum_field(document, "type", static_cast<std::size_t>(ExecutionLedgerRecordType::DependencyRevoked));
    auto observed = decimal_field(document, "observed_monotonic_ns");
    auto detail = string_field(document, "detail", true);
    const auto* authorization = document.is_object() ? document.find("authorization") : nullptr;
    const auto* completion = document.is_object() ? document.find("completion") : nullptr;
    const auto* checkpoint = document.is_object() ? document.find("trust_checkpoint") : nullptr;
    const auto* revocation = document.is_object() ? document.find("revocation") : nullptr;
    if (!format || !schema || !library_version || !identity || !id || !sequence || !parent || !ledger ||
        !session || !type || !observed || !detail || authorization == nullptr || completion == nullptr ||
        checkpoint == nullptr || revocation == nullptr) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::CorruptData,
                                                      "execution-ledger record document is incomplete");
    }
    if (format.value() != "rbfsafe-execution-ledger-record" || schema.value() != kSchema) {
        return Result<ExecutionLedgerRecord>::failure(StatusCode::IncompatibleFormat,
                                                      "unsupported execution-ledger record schema");
    }
    ExecutionLedgerRecord result;
    result.storage_schema = static_cast<std::uint32_t>(schema.value());
    result.id = std::move(id).value();
    result.sequence = sequence.value();
    result.parent_id = std::move(parent).value();
    result.ledger_id = std::move(ledger).value();
    result.session_id = std::move(session).value();
    result.type = static_cast<ExecutionLedgerRecordType>(type.value());
    result.observed_monotonic_ns = observed.value();
    result.detail = std::move(detail).value();
    if (!authorization->is_null()) {
        auto decoded = decode_authorization(*authorization);
        if (!decoded)
            return decoded.error();
        result.authorization = std::move(decoded).value();
    }
    if (!completion->is_null()) {
        auto decoded = decode_completion(*completion);
        if (!decoded)
            return decoded.error();
        result.completion = std::move(decoded).value();
    }
    if (!checkpoint->is_null()) {
        auto decoded = decode_checkpoint(*checkpoint, options, total_signatures);
        if (!decoded)
            return decoded.error();
        result.trust_checkpoint = std::move(decoded).value();
    }
    if (!revocation->is_null()) {
        auto decoded = decode_revocation(*revocation);
        if (!decoded)
            return decoded.error();
        result.revocation = std::move(decoded).value();
    }
    if (!result.valid() || !internal::valid_sha256(identity.value()) ||
        identity.value() != internal::sha256(record_payload(result).dump(false))) {
        return Result<ExecutionLedgerRecord>::failure(
            StatusCode::CorruptData, "execution-ledger record identity or checksum is invalid", result.id);
    }
    return result;
}

std::string record_filename(const ExecutionLedgerRecord& record) {
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(20) << record.sequence << '-' << record.id << ".json";
    return stream.str();
}

Result<void> write_immutable_file(const std::filesystem::path& destination, const std::string& content) {
    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to inspect immutable execution-ledger record",
                                         destination.string());
        }
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "immutable execution-ledger record already exists",
                                     destination.string());
    }
    const auto temporary = unique_sibling(destination, ".tmp-");
    auto written = internal::write_text_file(temporary, content);
    if (!written)
        return written;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Result<void>::failure(
            StatusCode::IoError, "failed to publish immutable execution-ledger record", destination.string());
    }
    return Result<void>::success();
}

} // namespace

namespace internal {

ExecutionLedgerWriteLock::ExecutionLedgerWriteLock(ExecutionLedgerWriteLock&& other) noexcept
    : path_(std::move(other.path_)), held_(other.held_) {
    other.held_ = false;
}

ExecutionLedgerWriteLock::~ExecutionLedgerWriteLock() {
    if (!held_)
        return;
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}

Result<ExecutionLedgerWriteLock> ExecutionLedgerWriteLock::acquire(const std::filesystem::path& directory) {
    std::error_code directory_error;
    const auto directory_status = std::filesystem::symlink_status(directory, directory_error);
    if (directory_error || std::filesystem::is_symlink(directory_status) ||
        !std::filesystem::is_directory(directory_status)) {
        return Result<ExecutionLedgerWriteLock>::failure(
            StatusCode::CorruptData, "execution-ledger directory is missing or indirect", directory.string());
    }
    const auto path = directory / ".writer-lock";
    std::error_code error;
    const bool created = std::filesystem::create_directory(path, error);
    if (error) {
        return Result<ExecutionLedgerWriteLock>::failure(
            StatusCode::IoError, "failed to acquire execution-ledger writer lock", path.string());
    }
    if (!created) {
        return Result<ExecutionLedgerWriteLock>::failure(
            StatusCode::ResourceLimit, "execution-ledger writer lock is already held", path.string());
    }
    return ExecutionLedgerWriteLock(path);
}

ExecutionLedgerWriteLock::ExecutionLedgerWriteLock(std::filesystem::path path) : path_(std::move(path)) {}

Result<void> append_execution_ledger_record_file(const std::filesystem::path& directory,
                                                 const ExecutionLedgerRecord& record) {
    if (directory.empty() || !record.valid()) {
        return Result<void>::failure(StatusCode::InvalidArgument,
                                     "execution-ledger append-file input is invalid");
    }
    return write_immutable_file(directory / "records" / record_filename(record),
                                record_document(record).dump(true) + "\n");
}

} // namespace internal

Result<ExecutionLedger> ExecutionLedger::create(const std::filesystem::path& directory,
                                                const BoundedExecutionSession& session) {
    if (directory.empty() || directory == directory.root_path() || !session.valid()) {
        return Result<ExecutionLedger>::failure(StatusCode::InvalidArgument,
                                                "execution-ledger creation input is invalid");
    }
    std::error_code error;
    if (std::filesystem::exists(directory, error)) {
        return Result<ExecutionLedger>::failure(
            StatusCode::IoError, "execution-ledger destination already exists", directory.string());
    }
    if (error) {
        return Result<ExecutionLedger>::failure(
            StatusCode::IoError, "failed to inspect execution-ledger destination", directory.string());
    }
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error) {
            return Result<ExecutionLedger>::failure(StatusCode::IoError,
                                                    "failed to create execution-ledger parent",
                                                    directory.parent_path().string());
        }
    }
    const auto ledger_id = internal::execution_ledger_identity(session.id());
    ExecutionLedgerRecord root;
    root.ledger_id = ledger_id;
    root.session_id = session.id();
    root.type = ExecutionLedgerRecordType::SessionOpened;
    root.observed_monotonic_ns = session.valid_from_monotonic_ns();
    root.id = internal::execution_ledger_record_identity(root);

    const auto temporary = unique_sibling(directory, ".tmp-");
    const bool records_created = std::filesystem::create_directories(temporary / "records", error);
    if (error || !records_created) {
        return Result<ExecutionLedger>::failure(StatusCode::IoError,
                                                "failed to create temporary execution-ledger directory");
    }
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };
    auto manifest_written = internal::write_text_file(
        temporary / "manifest.json", manifest_document(ledger_id, session, root.id).dump(true) + "\n");
    auto root_written = manifest_written
                            ? internal::write_text_file(temporary / "records" / record_filename(root),
                                                        record_document(root).dump(true) + "\n")
                            : manifest_written;
    if (!manifest_written || !root_written) {
        cleanup();
        return !manifest_written ? manifest_written.error() : root_written.error();
    }
    std::filesystem::rename(temporary, directory, error);
    if (error) {
        cleanup();
        return Result<ExecutionLedger>::failure(StatusCode::IoError, "failed to publish execution ledger",
                                                directory.string());
    }
    ExecutionLedger result;
    result.directory_ = directory;
    result.id_ = ledger_id;
    result.session_id_ = session.id();
    result.current_record_id_ = root.id;
    result.command_count_ = session.command_sequence().commands.size();
    result.valid_from_monotonic_ns_ = session.valid_from_monotonic_ns();
    result.valid_through_monotonic_ns_ = session.valid_through_monotonic_ns();
    result.records_.push_back(std::move(root));
    result.options_ = ExecutionLedgerLoadOptions{};
    if (!result.valid()) {
        return Result<ExecutionLedger>::failure(StatusCode::InternalError,
                                                "constructed execution ledger is invalid");
    }
    return result;
}

Result<ExecutionLedger>
ExecutionLedger::open(const std::filesystem::path& directory, const BoundedExecutionSession& session,
                      const ReviewedDeploymentProfile& reviewed, const ServiceTrustHistory& trust_history,
                      const SafeAtlas& atlas, const ExecutionLedgerLoadOptions& options) {
    if (directory.empty() || !session.valid() || !reviewed.valid() || !trust_history.valid() ||
        atlas.dimension() == 0 || options.maximum_records == 0 ||
        options.maximum_signatures_per_checkpoint == 0 || options.maximum_total_checkpoint_signatures == 0 ||
        options.maximum_manifest_bytes == 0 || options.maximum_record_bytes == 0) {
        return Result<ExecutionLedger>::failure(StatusCode::InvalidArgument,
                                                "execution-ledger open input is invalid");
    }
    if (options.cancellation.cancelled()) {
        return Result<ExecutionLedger>::failure(StatusCode::Cancelled, "execution-ledger open was cancelled");
    }
    std::error_code root_error;
    const auto root_status = std::filesystem::symlink_status(directory, root_error);
    if (root_error || std::filesystem::is_symlink(root_status) ||
        !std::filesystem::is_directory(root_status)) {
        return Result<ExecutionLedger>::failure(StatusCode::CorruptData,
                                                "execution-ledger directory is missing or indirect");
    }
    auto manifest = read_bounded_json(directory / "manifest.json", options.maximum_manifest_bytes);
    if (!manifest)
        return manifest.error();
    auto format = string_field(manifest.value(), "format");
    auto schema = enum_field(manifest.value(), "schema", kMaximumRecognizableSchema);
    auto library_version = string_field(manifest.value(), "library_version");
    auto identity = string_field(manifest.value(), "identity");
    auto id = string_field(manifest.value(), "id");
    auto session_id = string_field(manifest.value(), "session_id");
    auto command_count = decimal_field(manifest.value(), "command_count");
    auto valid_from = decimal_field(manifest.value(), "valid_from_monotonic_ns");
    auto valid_through = decimal_field(manifest.value(), "valid_through_monotonic_ns");
    auto root_record = string_field(manifest.value(), "root_record_id");
    if (!format || !schema || !library_version || !identity || !id || !session_id || !command_count ||
        !valid_from || !valid_through || !root_record) {
        return Result<ExecutionLedger>::failure(StatusCode::CorruptData,
                                                "execution-ledger manifest is incomplete");
    }
    if (format.value() != "rbfsafe-execution-ledger" || schema.value() != kSchema) {
        return Result<ExecutionLedger>::failure(StatusCode::IncompatibleFormat,
                                                "unsupported execution-ledger manifest schema");
    }
    if (id.value() != internal::execution_ledger_identity(session.id()) ||
        session_id.value() != session.id() ||
        command_count.value() != session.command_sequence().commands.size() ||
        valid_from.value() != session.valid_from_monotonic_ns() ||
        valid_through.value() != session.valid_through_monotonic_ns() ||
        !internal::valid_sha256(root_record.value()) || !internal::valid_sha256(identity.value()) ||
        identity.value() !=
            internal::sha256(manifest_payload(id.value(), session, root_record.value()).dump(false))) {
        return Result<ExecutionLedger>::failure(
            StatusCode::IdentityMismatch, "execution-ledger manifest does not match the supplied session",
            id.value());
    }
    std::error_code error;
    const auto records_directory = directory / "records";
    const auto records_status = std::filesystem::symlink_status(records_directory, error);
    if (error || std::filesystem::is_symlink(records_status) ||
        !std::filesystem::is_directory(records_status)) {
        return Result<ExecutionLedger>::failure(StatusCode::CorruptData,
                                                "execution-ledger records directory is missing or indirect");
    }
    std::vector<std::pair<std::string, ExecutionLedgerRecord>> decoded;
    std::size_t total_signatures = 0;
    for (std::filesystem::directory_iterator iterator(records_directory, error), end; iterator != end;
         iterator.increment(error)) {
        if (error) {
            return Result<ExecutionLedger>::failure(StatusCode::IoError,
                                                    "failed to enumerate execution-ledger records");
        }
        if (options.cancellation.cancelled()) {
            return Result<ExecutionLedger>::failure(StatusCode::Cancelled,
                                                    "execution-ledger open was cancelled");
        }
        const auto filename = iterator->path().filename().string();
        const auto entry_status = std::filesystem::symlink_status(iterator->path(), error);
        if (error) {
            return Result<ExecutionLedger>::failure(StatusCode::IoError,
                                                    "failed to inspect execution-ledger record entry");
        }
        if (!std::filesystem::is_symlink(entry_status) && std::filesystem::is_regular_file(entry_status) &&
            iterator->path().extension() == ".json") {
            if (decoded.size() >= options.maximum_records) {
                return Result<ExecutionLedger>::failure(
                    StatusCode::ResourceLimit, "execution-ledger record count exceeds configured limit");
            }
            auto document = read_bounded_json(iterator->path(), options.maximum_record_bytes);
            if (!document)
                return document.error();
            auto record = decode_record(document.value(), options, total_signatures);
            if (!record)
                return record.error();
            decoded.emplace_back(filename, std::move(record).value());
        } else if (filename.find(".tmp-") == std::string::npos) {
            return Result<ExecutionLedger>::failure(StatusCode::CorruptData,
                                                    "unexpected execution-ledger record entry", filename);
        }
    }
    if (error) {
        return Result<ExecutionLedger>::failure(StatusCode::IoError,
                                                "failed to enumerate execution-ledger records");
    }
    if (decoded.empty()) {
        return Result<ExecutionLedger>::failure(StatusCode::CorruptData,
                                                "execution ledger contains no records");
    }
    std::sort(decoded.begin(), decoded.end(), [](const auto& left, const auto& right) {
        if (left.second.sequence != right.second.sequence)
            return left.second.sequence < right.second.sequence;
        return left.second.id < right.second.id;
    });
    std::vector<ExecutionLedgerRecord> records;
    records.reserve(decoded.size());
    for (std::size_t index = 0; index < decoded.size(); ++index) {
        const auto& filename = decoded[index].first;
        const auto& record = decoded[index].second;
        if (record.sequence != index || filename != record_filename(record) ||
            record.ledger_id != id.value() || record.session_id != session.id() ||
            (index == 0 &&
             (record.id != root_record.value() || record.type != ExecutionLedgerRecordType::SessionOpened ||
              !record.parent_id.empty())) ||
            (index > 0 && record.parent_id != decoded[index - 1].second.id)) {
            return Result<ExecutionLedger>::failure(StatusCode::CorruptData,
                                                    "execution-ledger record chain is invalid", record.id);
        }
        records.push_back(record);
    }
    ExecutionLedger result;
    result.directory_ = directory;
    result.id_ = std::move(id).value();
    result.session_id_ = std::move(session_id).value();
    result.current_record_id_ = records.back().id;
    result.command_count_ = static_cast<std::size_t>(command_count.value());
    result.valid_from_monotonic_ns_ = valid_from.value();
    result.valid_through_monotonic_ns_ = valid_through.value();
    result.records_ = std::move(records);
    result.options_ = options;
    if (!result.valid()) {
        return Result<ExecutionLedger>::failure(StatusCode::CorruptData,
                                                "loaded execution ledger is structurally invalid");
    }
    auto audited = result.audit(session, reviewed, trust_history, atlas);
    if (!audited)
        return audited.error();
    return result;
}

} // namespace rbfsafe
