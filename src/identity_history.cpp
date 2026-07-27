#include <rbfsafe/identity.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/identity.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kHistorySchema = 1;
constexpr std::size_t kMaximumIdentifierBytes = 256;
constexpr std::size_t kMaximumStringBytes = 4096;

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

Result<internal::Json> read_bounded_json(const std::filesystem::path& path, std::uintmax_t maximum_bytes) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) {
        return Result<internal::Json>::failure(
            StatusCode::IoError, "failed to inspect service trust-history metadata", path.string());
    }
    if (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
        return Result<internal::Json>::failure(
            StatusCode::CorruptData, "service trust-history metadata is not a regular file", path.string());
    }
    const auto bytes = std::filesystem::file_size(path, error);
    if (error) {
        return Result<internal::Json>::failure(
            StatusCode::IoError, "failed to inspect service trust-history metadata", path.string());
    }
    if (bytes > maximum_bytes ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return Result<internal::Json>::failure(StatusCode::ResourceLimit,
                                               "service trust-history metadata exceeds configured limit",
                                               path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<internal::Json>::failure(
            StatusCode::IoError, "failed to open service trust-history metadata", path.string());
    }
    std::string text(static_cast<std::size_t>(bytes), '\0');
    if (!text.empty()) {
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (input.gcount() != static_cast<std::streamsize>(text.size())) {
            return Result<internal::Json>::failure(StatusCode::CorruptData,
                                                   "service trust-history metadata changed while reading",
                                                   path.string());
        }
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        return Result<internal::Json>::failure(
            StatusCode::CorruptData, "service trust-history metadata changed while reading", path.string());
    }
    return internal::Json::parse(text);
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 std::size_t maximum_bytes = kMaximumStringBytes, bool allow_empty = false) {
    if (!object.is_object())
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "service trust-history record is not an object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > maximum_bytes ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(
            StatusCode::CorruptData, "service trust-history string field is invalid", std::string(key));
    }
    return value->as_string();
}

Result<std::size_t> integer_field(const internal::Json& object, std::string_view key, std::size_t maximum) {
    if (!object.is_object())
        return Result<std::size_t>::failure(StatusCode::CorruptData,
                                            "service trust-history record is not an object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number()) ||
        value->as_number() < 0.0 || std::floor(value->as_number()) != value->as_number() ||
        value->as_number() > static_cast<double>(maximum)) {
        return Result<std::size_t>::failure(
            StatusCode::CorruptData, "service trust-history integer field is invalid", std::string(key));
    }
    return static_cast<std::size_t>(value->as_number());
}

Result<std::uint64_t> decimal_field(const internal::Json& object, std::string_view key) {
    auto text = string_field(object, key, 32);
    if (!text)
        return text.error();
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(text.value().data(), text.value().data() + text.value().size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.value().data() + text.value().size()) {
        return Result<std::uint64_t>::failure(
            StatusCode::CorruptData, "service trust-history decimal field is invalid", std::string(key));
    }
    return result;
}

Result<void> require_schema(const internal::Json& object, std::string_view expected_format) {
    auto format = string_field(object, "format", 128);
    auto schema = integer_field(object, "schema", 1000);
    auto library_version = string_field(object, "library_version", 128);
    if (!format || !schema || !library_version)
        return Result<void>::failure(StatusCode::CorruptData, "service trust-history document is incomplete");
    if (format.value() != expected_format || schema.value() != kHistorySchema) {
        return Result<void>::failure(StatusCode::IncompatibleFormat,
                                     "unsupported service trust-history schema");
    }
    return Result<void>::success();
}

internal::Json authorization_json(const ServiceTrustBundleAuthorization& authorization) {
    return internal::Json::Object{
        {"algorithm", static_cast<int>(authorization.algorithm)},
        {"authentication_tag", authorization.authentication_tag},
        {"format", "rbfsafe-service-trust-bundle-authorization"},
        {"id", authorization.id},
        {"predecessor_bundle_id", authorization.predecessor_bundle_id},
        {"predecessor_sequence", std::to_string(authorization.predecessor_sequence)},
        {"schema", 1},
        {"signer_key_id", authorization.signer_key_id},
        {"signer_service_id", authorization.signer_service_id},
        {"successor_bundle_id", authorization.successor_bundle_id},
        {"successor_sequence", std::to_string(authorization.successor_sequence)},
    };
}

Result<ServiceTrustBundleAuthorization> decode_authorization(const internal::Json& object) {
    auto format = string_field(object, "format", 128);
    auto schema = integer_field(object, "schema", 1000);
    auto id = string_field(object, "id", 64);
    auto predecessor_bundle_id = string_field(object, "predecessor_bundle_id", 64);
    auto successor_bundle_id = string_field(object, "successor_bundle_id", 64);
    auto predecessor_sequence = decimal_field(object, "predecessor_sequence");
    auto successor_sequence = decimal_field(object, "successor_sequence");
    auto signer_service_id = string_field(object, "signer_service_id", kMaximumIdentifierBytes);
    auto signer_key_id = string_field(object, "signer_key_id", 64);
    auto algorithm = integer_field(object, "algorithm",
                                   static_cast<std::size_t>(ArtifactAuthenticationAlgorithm::Ed25519));
    auto authentication_tag = string_field(object, "authentication_tag", kEd25519SignatureBytes * 2);
    if (!format || !schema || !id || !predecessor_bundle_id || !successor_bundle_id ||
        !predecessor_sequence || !successor_sequence || !signer_service_id || !signer_key_id || !algorithm ||
        !authentication_tag || format.value() != "rbfsafe-service-trust-bundle-authorization" ||
        schema.value() != 1) {
        return Result<ServiceTrustBundleAuthorization>::failure(
            StatusCode::CorruptData, "service trust-bundle authorization is incomplete");
    }
    ServiceTrustBundleAuthorization result;
    result.id = std::move(id).value();
    result.predecessor_bundle_id = std::move(predecessor_bundle_id).value();
    result.successor_bundle_id = std::move(successor_bundle_id).value();
    result.predecessor_sequence = predecessor_sequence.value();
    result.successor_sequence = successor_sequence.value();
    result.signer_service_id = std::move(signer_service_id).value();
    result.signer_key_id = std::move(signer_key_id).value();
    result.algorithm = static_cast<ArtifactAuthenticationAlgorithm>(algorithm.value());
    result.authentication_tag = std::move(authentication_tag).value();
    if (!valid_service_trust_bundle_authorization(result)) {
        return Result<ServiceTrustBundleAuthorization>::failure(
            StatusCode::CorruptData, "service trust-bundle authorization identity is invalid", result.id);
    }
    return result;
}

internal::Json record_json(const ServiceTrustRotationRecord& record) {
    return internal::Json::Object{
        {"authorization",
         record.authorization ? authorization_json(*record.authorization) : internal::Json(nullptr)},
        {"bundle_id", record.bundle_id},
        {"format", "rbfsafe-service-trust-rotation-record"},
        {"id", record.id},
        {"library_version", kVersion},
        {"parent_id", record.parent_id},
        {"schema", static_cast<double>(kHistorySchema)},
        {"sequence", std::to_string(record.sequence)},
        {"type", static_cast<int>(record.type)},
    };
}

bool valid_record_structure(const ServiceTrustRotationRecord& record) {
    const bool root = record.sequence == 1 && record.type == ServiceTrustRotationEventType::RootPinned &&
                      record.parent_id.empty() && !record.authorization;
    const bool successor = record.sequence > 1 &&
                           record.type == ServiceTrustRotationEventType::SuccessorAuthorized &&
                           internal::valid_sha256(record.parent_id) && record.authorization.has_value();
    return (root || successor) && internal::valid_sha256(record.id) &&
           internal::valid_sha256(record.bundle_id) &&
           internal::service_trust_rotation_record_identity(record) == record.id;
}

Result<ServiceTrustRotationRecord> decode_record(const internal::Json& document) {
    auto schema = require_schema(document, "rbfsafe-service-trust-rotation-record");
    if (!schema)
        return schema.error();
    auto sequence = decimal_field(document, "sequence");
    auto id = string_field(document, "id", 64);
    auto parent_id = string_field(document, "parent_id", 64, true);
    auto type = integer_field(document, "type",
                              static_cast<std::size_t>(ServiceTrustRotationEventType::SuccessorAuthorized));
    auto bundle_id = string_field(document, "bundle_id", 64);
    const auto* authorization = document.is_object() ? document.find("authorization") : nullptr;
    if (!sequence || !id || !parent_id || !type || !bundle_id || authorization == nullptr) {
        return Result<ServiceTrustRotationRecord>::failure(StatusCode::CorruptData,
                                                           "service trust-rotation record is incomplete");
    }
    ServiceTrustRotationRecord result;
    result.sequence = sequence.value();
    result.id = std::move(id).value();
    result.parent_id = std::move(parent_id).value();
    result.type = static_cast<ServiceTrustRotationEventType>(type.value());
    result.bundle_id = std::move(bundle_id).value();
    if (!authorization->is_null()) {
        auto decoded = decode_authorization(*authorization);
        if (!decoded)
            return decoded.error();
        result.authorization = std::move(decoded).value();
    }
    if (!valid_record_structure(result)) {
        return Result<ServiceTrustRotationRecord>::failure(
            StatusCode::CorruptData, "service trust-rotation record identity is invalid", result.id);
    }
    return result;
}

std::string record_filename(const ServiceTrustRotationRecord& record) {
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(20) << record.sequence << '-' << record.id << ".json";
    return stream.str();
}

std::string bundle_filename(const ServiceTrustBundle& bundle) {
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(20) << bundle.sequence() << '-' << bundle.id() << ".json";
    return stream.str();
}

internal::Json manifest_payload(const std::string& root_bundle_id) {
    return internal::Json::Object{
        {"format", "rbfsafe-service-trust-history"},
        {"root_bundle_id", root_bundle_id},
        {"schema", static_cast<double>(kHistorySchema)},
    };
}

internal::Json manifest_json(const std::string& root_bundle_id) {
    auto payload = manifest_payload(root_bundle_id);
    auto object = payload.as_object();
    object.emplace("identity", internal::sha256(payload.dump(false)));
    object.emplace("library_version", kVersion);
    return object;
}

Result<std::string> decode_manifest(const internal::Json& document) {
    auto schema = require_schema(document, "rbfsafe-service-trust-history");
    if (!schema)
        return schema.error();
    auto root_bundle_id = string_field(document, "root_bundle_id", 64);
    auto identity = string_field(document, "identity", 64);
    if (!root_bundle_id || !identity || !internal::valid_sha256(root_bundle_id.value()) ||
        !internal::valid_sha256(identity.value()) ||
        identity.value() != internal::sha256(manifest_payload(root_bundle_id.value()).dump(false))) {
        return Result<std::string>::failure(StatusCode::CorruptData,
                                            "service trust-history manifest identity is invalid");
    }
    return root_bundle_id.value();
}

Result<void> write_immutable_file(const std::filesystem::path& destination, const std::string& content) {
    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        if (error) {
            return Result<void>::failure(StatusCode::IoError,
                                         "failed to inspect immutable trust-history record",
                                         destination.string());
        }
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "immutable trust-history record already exists", destination.string());
    }
    const auto temporary = unique_sibling(destination, ".tmp-");
    auto written = internal::write_text_file(temporary, content);
    if (!written) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return written;
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Result<void>::failure(StatusCode::IoError, "failed to publish immutable trust-history record",
                                     destination.string());
    }
    return Result<void>::success();
}

class DirectoryLock {
  public:
    DirectoryLock(const DirectoryLock&) = delete;
    DirectoryLock& operator=(const DirectoryLock&) = delete;
    DirectoryLock(DirectoryLock&& other) noexcept : path_(std::move(other.path_)), held_(other.held_) {
        other.held_ = false;
    }
    DirectoryLock& operator=(DirectoryLock&&) = delete;

    ~DirectoryLock() {
        if (!held_)
            return;
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    static Result<DirectoryLock> acquire(const std::filesystem::path& history) {
        const auto path = history / ".writer-lock";
        std::error_code error;
        const bool created = std::filesystem::create_directory(path, error);
        if (error) {
            return Result<DirectoryLock>::failure(
                StatusCode::IoError, "failed to acquire service trust-history writer lock", path.string());
        }
        if (!created) {
            return Result<DirectoryLock>::failure(StatusCode::ResourceLimit,
                                                  "service trust-history writer lock is already held",
                                                  path.string());
        }
        return DirectoryLock(path);
    }

  private:
    explicit DirectoryLock(std::filesystem::path path) : path_(std::move(path)) {}

    std::filesystem::path path_;
    bool held_ = true;
};

bool valid_history_options(const ServiceTrustHistoryLoadOptions& options) {
    return options.maximum_bundles > 0 && options.maximum_keys_per_bundle > 0 &&
           options.maximum_total_keys > 0 && options.maximum_metadata_bytes > 0 &&
           options.maximum_bundle_bytes > 0;
}

bool real_directory(const std::filesystem::path& path, std::error_code& error) {
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status);
}

} // namespace

bool ServiceTrustHistory::valid() const {
    if (bundles_.empty() || bundles_.size() != records_.size() || !internal::valid_sha256(root_bundle_id_) ||
        current_bundle_id_ != bundles_.back().id() || root_bundle_id_ != bundles_.front().id()) {
        return false;
    }
    for (std::size_t index = 0; index < bundles_.size(); ++index) {
        const auto& bundle_value = bundles_[index];
        const auto& record = records_[index];
        if (!bundle_value.valid() || bundle_value.storage_schema() < 2 ||
            bundle_value.sequence() != index + 1 || record.sequence != index + 1 ||
            record.bundle_id != bundle_value.id() || !valid_record_structure(record)) {
            return false;
        }
        if (index == 0) {
            if (record.type != ServiceTrustRotationEventType::RootPinned || !record.parent_id.empty() ||
                record.authorization)
                return false;
        } else {
            if (record.type != ServiceTrustRotationEventType::SuccessorAuthorized ||
                record.parent_id != records_[index - 1].id || !record.authorization ||
                !verify_service_trust_bundle_successor(bundles_[index - 1], bundle_value,
                                                       *record.authorization)) {
                return false;
            }
        }
    }
    return true;
}

Result<ServiceTrustHistory> ServiceTrustHistory::create(const std::filesystem::path& directory,
                                                        const ServiceTrustBundle& root_bundle,
                                                        const std::string& expected_root_bundle_id) {
    const ServiceTrustHistoryLoadOptions default_options;
    if (directory.empty() || directory == directory.root_path() || !root_bundle.valid() ||
        root_bundle.storage_schema() < 2 || root_bundle.sequence() != 1 ||
        !internal::valid_sha256(expected_root_bundle_id)) {
        return Result<ServiceTrustHistory>::failure(StatusCode::InvalidArgument,
                                                    "service trust-history creation input is invalid");
    }
    if (root_bundle.id() != expected_root_bundle_id) {
        return Result<ServiceTrustHistory>::failure(
            StatusCode::IdentityMismatch, "service trust-history root does not match the caller pin",
            root_bundle.id());
    }
    if (root_bundle.keys().size() > default_options.maximum_keys_per_bundle ||
        root_bundle.keys().size() > default_options.maximum_total_keys ||
        internal::service_trust_bundle_storage_document(root_bundle).size() >
            default_options.maximum_bundle_bytes) {
        return Result<ServiceTrustHistory>::failure(
            StatusCode::ResourceLimit, "service trust-history root exceeds default resource limits");
    }
    std::error_code error;
    if (std::filesystem::exists(directory, error)) {
        return Result<ServiceTrustHistory>::failure(
            StatusCode::IoError, "service trust-history destination already exists", directory.string());
    }
    if (error) {
        return Result<ServiceTrustHistory>::failure(
            StatusCode::IoError, "failed to inspect service trust-history destination", directory.string());
    }
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error) {
            return Result<ServiceTrustHistory>::failure(StatusCode::IoError,
                                                        "failed to create service trust-history parent",
                                                        directory.parent_path().string());
        }
    }

    ServiceTrustRotationRecord root_record;
    root_record.sequence = 1;
    root_record.type = ServiceTrustRotationEventType::RootPinned;
    root_record.bundle_id = root_bundle.id();
    root_record.id = internal::service_trust_rotation_record_identity(root_record);

    const auto temporary = unique_sibling(directory, ".tmp-");
    const bool temporary_created = std::filesystem::create_directory(temporary, error);
    if (error || !temporary_created) {
        return Result<ServiceTrustHistory>::failure(StatusCode::IoError,
                                                    "failed to create temporary service trust history");
    }
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };
    const bool bundles_created = std::filesystem::create_directory(temporary / "bundles", error);
    const bool records_created = !error && std::filesystem::create_directory(temporary / "records", error);
    if (error || !bundles_created || !records_created) {
        cleanup();
        return Result<ServiceTrustHistory>::failure(
            StatusCode::IoError, "failed to create service trust-history data directories");
    }
    auto saved = root_bundle.save(temporary / "bundles" / bundle_filename(root_bundle));
    if (!saved) {
        cleanup();
        return saved.error();
    }
    auto written = internal::write_text_file(temporary / "records" / record_filename(root_record),
                                             record_json(root_record).dump(true) + "\n");
    if (written) {
        written = internal::write_text_file(temporary / "manifest.json",
                                            manifest_json(root_bundle.id()).dump(true) + "\n");
    }
    if (!written) {
        cleanup();
        return written.error();
    }
    std::filesystem::rename(temporary, directory, error);
    if (error) {
        cleanup();
        return Result<ServiceTrustHistory>::failure(
            StatusCode::IoError, "failed to publish service trust history", directory.string());
    }

    ServiceTrustHistory result;
    result.directory_ = directory;
    result.root_bundle_id_ = root_bundle.id();
    result.current_bundle_id_ = root_bundle.id();
    result.bundles_.push_back(root_bundle);
    result.records_.push_back(std::move(root_record));
    result.options_ = default_options;
    return result;
}

Result<ServiceTrustHistory> ServiceTrustHistory::open(const std::filesystem::path& directory,
                                                      const std::string& expected_root_bundle_id,
                                                      const std::string& expected_head_bundle_id,
                                                      const ServiceTrustHistoryLoadOptions& options) {
    if (directory.empty() || !internal::valid_sha256(expected_root_bundle_id) ||
        !internal::valid_sha256(expected_head_bundle_id) || !valid_history_options(options)) {
        return Result<ServiceTrustHistory>::failure(StatusCode::InvalidArgument,
                                                    "service trust-history open input is invalid");
    }
    if (options.cancellation.cancelled())
        return Result<ServiceTrustHistory>::failure(StatusCode::Cancelled,
                                                    "service trust-history load was cancelled");
    std::error_code error;
    if (!real_directory(directory, error)) {
        return Result<ServiceTrustHistory>::failure(StatusCode::CorruptData,
                                                    "service trust-history directory is missing or indirect");
    }
    auto manifest = read_bounded_json(directory / "manifest.json", options.maximum_metadata_bytes);
    if (!manifest)
        return manifest.error();
    auto stored_root = decode_manifest(manifest.value());
    if (!stored_root)
        return stored_root.error();
    if (stored_root.value() != expected_root_bundle_id) {
        return Result<ServiceTrustHistory>::failure(
            StatusCode::IdentityMismatch, "service trust-history root does not match the caller pin",
            stored_root.value());
    }

    const auto records_directory = directory / "records";
    const auto bundles_directory = directory / "bundles";
    if (!real_directory(records_directory, error) || !real_directory(bundles_directory, error)) {
        return Result<ServiceTrustHistory>::failure(
            StatusCode::CorruptData, "service trust-history data directories are missing or indirect");
    }
    std::vector<std::pair<std::string, ServiceTrustRotationRecord>> decoded;
    std::size_t directory_entries = 0;
    for (std::filesystem::directory_iterator iterator(records_directory, error), end; iterator != end;
         iterator.increment(error)) {
        if (error) {
            return Result<ServiceTrustHistory>::failure(StatusCode::IoError,
                                                        "failed to enumerate service trust-history records");
        }
        if (options.cancellation.cancelled()) {
            return Result<ServiceTrustHistory>::failure(StatusCode::Cancelled,
                                                        "service trust-history load was cancelled");
        }
        if (directory_entries >= options.maximum_bundles) {
            return Result<ServiceTrustHistory>::failure(
                StatusCode::ResourceLimit, "service trust-history record-entry count exceeds limit");
        }
        ++directory_entries;
        const auto filename = iterator->path().filename().string();
        const auto entry_status = iterator->symlink_status(error);
        if (error) {
            return Result<ServiceTrustHistory>::failure(StatusCode::IoError,
                                                        "failed to inspect service trust-history record");
        }
        const bool regular_file =
            std::filesystem::is_regular_file(entry_status) && !std::filesystem::is_symlink(entry_status);
        if (regular_file && iterator->path().extension() == ".json") {
            if (decoded.size() >= options.maximum_bundles) {
                return Result<ServiceTrustHistory>::failure(
                    StatusCode::ResourceLimit, "service trust-history bundle count exceeds limit");
            }
            auto document = read_bounded_json(iterator->path(), options.maximum_metadata_bytes);
            if (!document)
                return document.error();
            auto record = decode_record(document.value());
            if (!record)
                return record.error();
            decoded.emplace_back(filename, std::move(record).value());
        } else if (filename.find(".tmp-") == std::string::npos) {
            return Result<ServiceTrustHistory>::failure(
                StatusCode::CorruptData, "unexpected service trust-history record entry", filename);
        }
    }
    if (error) {
        return Result<ServiceTrustHistory>::failure(StatusCode::IoError,
                                                    "failed to enumerate service trust-history records");
    }
    if (decoded.empty()) {
        return Result<ServiceTrustHistory>::failure(StatusCode::CorruptData,
                                                    "service trust history contains no rotation records");
    }
    std::sort(decoded.begin(), decoded.end(), [](const auto& left, const auto& right) {
        if (left.second.sequence != right.second.sequence)
            return left.second.sequence < right.second.sequence;
        return left.second.id < right.second.id;
    });

    ServiceTrustHistory result;
    result.directory_ = directory;
    result.root_bundle_id_ = stored_root.value();
    result.options_ = options;
    std::size_t total_keys = 0;
    ServiceTrustBundleLoadOptions bundle_options;
    bundle_options.maximum_keys = options.maximum_keys_per_bundle;
    bundle_options.maximum_payload_bytes = options.maximum_bundle_bytes;
    for (std::size_t index = 0; index < decoded.size(); ++index) {
        if (options.cancellation.cancelled()) {
            return Result<ServiceTrustHistory>::failure(StatusCode::Cancelled,
                                                        "service trust-history load was cancelled");
        }
        const auto& filename = decoded[index].first;
        const auto& record = decoded[index].second;
        if (record.sequence != index + 1 || filename != record_filename(record) ||
            (index == 0 && (record.bundle_id != stored_root.value() ||
                            record.type != ServiceTrustRotationEventType::RootPinned)) ||
            (index > 0 && (record.parent_id != decoded[index - 1].second.id ||
                           record.type != ServiceTrustRotationEventType::SuccessorAuthorized))) {
            return Result<ServiceTrustHistory>::failure(
                StatusCode::CorruptData, "service trust-history record chain is invalid", record.id);
        }
        std::ostringstream bundle_name;
        bundle_name << std::setfill('0') << std::setw(20) << record.sequence << '-' << record.bundle_id
                    << ".json";
        const auto bundle_path = bundles_directory / bundle_name.str();
        const auto bundle_status = std::filesystem::symlink_status(bundle_path, error);
        if (error || !std::filesystem::is_regular_file(bundle_status) ||
            std::filesystem::is_symlink(bundle_status)) {
            return Result<ServiceTrustHistory>::failure(
                StatusCode::CorruptData, "service trust-history bundle file is missing or indirect",
                record.bundle_id);
        }
        auto bundle_value = ServiceTrustBundle::load(bundle_path, bundle_options);
        if (!bundle_value)
            return bundle_value.error();
        if (bundle_value.value().storage_schema() < 2 || bundle_value.value().sequence() != record.sequence ||
            bundle_value.value().id() != record.bundle_id) {
            return Result<ServiceTrustHistory>::failure(StatusCode::CorruptData,
                                                        "service trust-history bundle identity is invalid",
                                                        record.bundle_id);
        }
        if (bundle_value.value().keys().size() > options.maximum_total_keys - total_keys) {
            return Result<ServiceTrustHistory>::failure(
                StatusCode::ResourceLimit, "service trust-history total key count exceeds limit");
        }
        total_keys += bundle_value.value().keys().size();
        if (index > 0) {
            if (!record.authorization) {
                return Result<ServiceTrustHistory>::failure(
                    StatusCode::CorruptData, "service trust-history successor lacks authorization",
                    record.id);
            }
            auto verified = verify_service_trust_bundle_successor(
                result.bundles_.back(), bundle_value.value(), *record.authorization);
            if (!verified)
                return verified.error();
        }
        result.records_.push_back(record);
        result.bundles_.push_back(std::move(bundle_value).value());
    }
    result.current_bundle_id_ = result.bundles_.back().id();
    if (!result.valid()) {
        return Result<ServiceTrustHistory>::failure(StatusCode::CorruptData,
                                                    "service trust-history replay is inconsistent");
    }
    if (result.current_bundle_id_ != expected_head_bundle_id) {
        return Result<ServiceTrustHistory>::failure(
            StatusCode::IdentityMismatch,
            "service trust-history head does not match the caller expected head", result.current_bundle_id_);
    }
    return result;
}

Result<ServiceTrustBundle> ServiceTrustHistory::current_bundle() const {
    if (!valid()) {
        return Result<ServiceTrustBundle>::failure(StatusCode::InvalidArgument,
                                                   "service trust-history object is not initialized");
    }
    return bundles_.back();
}

Result<ServiceTrustBundle> ServiceTrustHistory::bundle(const std::string& bundle_id) const {
    if (!valid() || !internal::valid_sha256(bundle_id)) {
        return Result<ServiceTrustBundle>::failure(StatusCode::InvalidArgument,
                                                   "service trust-history bundle lookup is invalid");
    }
    const auto found = std::find_if(bundles_.begin(), bundles_.end(),
                                    [&](const auto& candidate) { return candidate.id() == bundle_id; });
    if (found == bundles_.end()) {
        return Result<ServiceTrustBundle>::failure(
            StatusCode::InvalidArgument, "service trust-history bundle is not registered", bundle_id);
    }
    return *found;
}

Result<ServiceTrustRotationRecord>
ServiceTrustHistory::publish(const ServiceTrustBundle& successor,
                             const ServiceTrustBundleAuthorization& authorization,
                             const std::string& expected_head_bundle_id, std::size_t maximum_bundles) {
    if (!valid() || !successor.valid() || !valid_service_trust_bundle_authorization(authorization) ||
        !internal::valid_sha256(expected_head_bundle_id) || maximum_bundles == 0) {
        return Result<ServiceTrustRotationRecord>::failure(
            StatusCode::InvalidArgument, "service trust-history publication input is invalid");
    }
    if (options_.cancellation.cancelled()) {
        return Result<ServiceTrustRotationRecord>::failure(StatusCode::Cancelled,
                                                           "service trust-history publication was cancelled");
    }
    auto lock = DirectoryLock::acquire(directory_);
    if (!lock)
        return lock.error();
    auto held_lock = std::move(lock).value();
    (void)held_lock;
    auto fresh = ServiceTrustHistory::open(directory_, root_bundle_id_, expected_head_bundle_id, options_);
    if (!fresh)
        return fresh.error();
    const auto effective_limit = std::min(maximum_bundles, options_.maximum_bundles);
    if (fresh.value().bundles_.size() >= effective_limit) {
        return Result<ServiceTrustRotationRecord>::failure(StatusCode::ResourceLimit,
                                                           "service trust-history bundle limit reached");
    }
    if (successor.keys().size() > options_.maximum_keys_per_bundle ||
        internal::service_trust_bundle_storage_document(successor).size() > options_.maximum_bundle_bytes) {
        return Result<ServiceTrustRotationRecord>::failure(
            StatusCode::ResourceLimit, "service trust-history successor exceeds configured bundle limits");
    }
    std::size_t total_keys = 0;
    for (const auto& item : fresh.value().bundles_) {
        if (item.keys().size() > options_.maximum_total_keys - total_keys) {
            return Result<ServiceTrustRotationRecord>::failure(
                StatusCode::ResourceLimit, "service trust-history total key count exceeds limit");
        }
        total_keys += item.keys().size();
    }
    if (successor.keys().size() > options_.maximum_total_keys - total_keys) {
        return Result<ServiceTrustRotationRecord>::failure(
            StatusCode::ResourceLimit, "service trust-history total key count exceeds limit");
    }
    auto verified =
        verify_service_trust_bundle_successor(fresh.value().bundles_.back(), successor, authorization);
    if (!verified)
        return verified.error();

    ServiceTrustRotationRecord record;
    record.sequence = successor.sequence();
    record.parent_id = fresh.value().records_.back().id;
    record.type = ServiceTrustRotationEventType::SuccessorAuthorized;
    record.bundle_id = successor.id();
    record.authorization = authorization;
    record.id = internal::service_trust_rotation_record_identity(record);
    if (!valid_record_structure(record)) {
        return Result<ServiceTrustRotationRecord>::failure(
            StatusCode::InternalError, "generated service trust-rotation record is invalid");
    }

    const auto bundle_path = directory_ / "bundles" / bundle_filename(successor);
    std::error_code error;
    if (std::filesystem::exists(bundle_path, error)) {
        if (error) {
            return Result<ServiceTrustRotationRecord>::failure(
                StatusCode::IoError, "failed to inspect service trust-history bundle");
        }
        ServiceTrustBundleLoadOptions load_options;
        load_options.maximum_keys = options_.maximum_keys_per_bundle;
        load_options.maximum_payload_bytes = options_.maximum_bundle_bytes;
        auto orphan = ServiceTrustBundle::load(bundle_path, load_options);
        if (!orphan || orphan.value().id() != successor.id()) {
            return Result<ServiceTrustRotationRecord>::failure(
                StatusCode::CorruptData, "orphan service trust-history bundle is inconsistent",
                successor.id());
        }
    } else {
        auto saved = successor.save(bundle_path);
        if (!saved)
            return saved.error();
    }
    auto committed = write_immutable_file(directory_ / "records" / record_filename(record),
                                          record_json(record).dump(true) + "\n");
    if (!committed)
        return committed.error();
    fresh.value().bundles_.push_back(successor);
    fresh.value().records_.push_back(record);
    fresh.value().current_bundle_id_ = successor.id();
    *this = std::move(fresh).value();
    return record;
}

} // namespace rbfsafe
