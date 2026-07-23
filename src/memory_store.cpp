#include <rbfsafe/memory.h>
#include <rbfsafe/version.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/memory.h"
#include "internal/sha256.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace rbfsafe {
namespace {

constexpr std::size_t kStoreSchema = 1;
constexpr std::size_t kMaximumMetadataStringBytes = 4096;

std::filesystem::path unique_sibling(const std::filesystem::path& destination, std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return destination.parent_path() /
           (destination.filename().string() + std::string(suffix) + std::to_string(nonce));
}

Result<internal::Json> read_bounded_json(const std::filesystem::path& path, std::uintmax_t maximum_bytes) {
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error)
        return Result<internal::Json>::failure(StatusCode::IoError, "failed to inspect memory-store metadata",
                                               path.string());
    if (bytes > maximum_bytes) {
        return Result<internal::Json>::failure(
            StatusCode::ResourceLimit, "memory-store metadata exceeds configured limit", path.string());
    }
    return internal::read_json_file(path);
}

Result<std::string> string_field(const internal::Json& object, std::string_view key,
                                 bool allow_empty = false) {
    if (!object.is_object())
        return Result<std::string>::failure(StatusCode::CorruptData, "memory-store record is not an object");
    const auto* value = object.find(key);
    if (value == nullptr || !value->is_string() || value->as_string().size() > kMaximumMetadataStringBytes ||
        (!allow_empty && value->as_string().empty())) {
        return Result<std::string>::failure(StatusCode::CorruptData, "memory-store string field is invalid",
                                            std::string(key));
    }
    return value->as_string();
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
                                              "memory-store decimal field is invalid", std::string(key));
    }
    return result;
}

Result<void> require_schema(const internal::Json& object, std::string_view expected_format) {
    auto format = string_field(object, "format");
    const auto* schema = object.is_object() ? object.find("schema") : nullptr;
    if (!format || format.value() != expected_format || schema == nullptr || !schema->is_number() ||
        schema->as_number() != static_cast<double>(kStoreSchema)) {
        return Result<void>::failure(StatusCode::IncompatibleFormat,
                                     "unsupported safety-memory store format");
    }
    auto library_version = string_field(object, "library_version");
    if (!library_version)
        return library_version.error();
    return Result<void>::success();
}

internal::Json root_payload(const std::string& root_revision_id) {
    return internal::Json::Object{
        {"format", "rbfsafe-safety-memory-store"},
        {"root_revision_id", root_revision_id},
        {"schema", static_cast<double>(kStoreSchema)},
    };
}

internal::Json root_document(const std::string& root_revision_id) {
    auto payload = root_payload(root_revision_id);
    auto object = payload.as_object();
    object.emplace("identity", internal::sha256(payload.dump(false)));
    object.emplace("library_version", kVersion);
    return object;
}

internal::Json commit_payload(const SafetyMemoryRevisionInfo& revision) {
    return internal::Json::Object{
        {"format", "rbfsafe-safety-memory-commit"},
        {"id", revision.id},
        {"memory_id", revision.memory_id},
        {"parent_id", revision.parent_id},
        {"schema", static_cast<double>(kStoreSchema)},
        {"sequence", std::to_string(revision.sequence)},
    };
}

internal::Json commit_document(const SafetyMemoryRevisionInfo& revision) {
    auto payload = commit_payload(revision);
    auto object = payload.as_object();
    object.emplace("identity", internal::sha256(payload.dump(false)));
    object.emplace("library_version", kVersion);
    return object;
}

Result<void> validate_revision(const SafetyMemoryRevisionInfo& revision) {
    const bool root = revision.sequence == 0 && revision.parent_id.empty();
    const bool derived = revision.sequence > 0 && internal::valid_sha256(revision.parent_id);
    if ((!root && !derived) || !internal::valid_sha256(revision.id) ||
        !internal::valid_sha256(revision.memory_id) ||
        internal::safety_memory_revision_identity(revision) != revision.id) {
        return Result<void>::failure(StatusCode::CorruptData, "safety-memory revision identity is invalid",
                                     revision.id);
    }
    return Result<void>::success();
}

Result<SafetyMemoryRevisionInfo> decode_commit(const internal::Json& document) {
    auto schema = require_schema(document, "rbfsafe-safety-memory-commit");
    if (!schema)
        return schema.error();
    auto sequence = decimal_field(document, "sequence");
    auto id = string_field(document, "id");
    auto parent_id = string_field(document, "parent_id", true);
    auto memory_id = string_field(document, "memory_id");
    auto identity = string_field(document, "identity");
    if (!sequence || !id || !parent_id || !memory_id || !identity) {
        return Result<SafetyMemoryRevisionInfo>::failure(StatusCode::CorruptData,
                                                         "safety-memory commit is incomplete");
    }
    SafetyMemoryRevisionInfo result;
    result.sequence = sequence.value();
    result.id = std::move(id).value();
    result.parent_id = std::move(parent_id).value();
    result.memory_id = std::move(memory_id).value();
    auto valid = validate_revision(result);
    if (!valid)
        return valid.error();
    if (!internal::valid_sha256(identity.value()) ||
        identity.value() != internal::sha256(commit_payload(result).dump(false))) {
        return Result<SafetyMemoryRevisionInfo>::failure(
            StatusCode::CorruptData, "safety-memory commit checksum is invalid", result.id);
    }
    return result;
}

std::string commit_filename(const SafetyMemoryRevisionInfo& revision) {
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(20) << revision.sequence << '-' << revision.id << ".json";
    return stream.str();
}

Result<void> write_immutable_file(const std::filesystem::path& destination, const std::string& content) {
    std::error_code error;
    if (std::filesystem::exists(destination, error)) {
        if (error) {
            return Result<void>::failure(
                StatusCode::IoError, "failed to inspect immutable memory-store record", destination.string());
        }
        return Result<void>::failure(StatusCode::IdentityMismatch,
                                     "immutable memory-store record already exists", destination.string());
    }
    const auto temporary = unique_sibling(destination, ".tmp-");
    auto written = internal::write_text_file(temporary, content);
    if (!written)
        return written;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Result<void>::failure(StatusCode::IoError, "failed to publish immutable memory-store record",
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
        std::filesystem::remove_all(path_, ignored);
    }

    static Result<DirectoryLock> acquire(const std::filesystem::path& store) {
        const auto path = store / ".writer-lock";
        std::error_code error;
        const bool created = std::filesystem::create_directory(path, error);
        if (error) {
            return Result<DirectoryLock>::failure(
                StatusCode::IoError, "failed to acquire safety-memory store writer lock", path.string());
        }
        if (!created) {
            return Result<DirectoryLock>::failure(
                StatusCode::ResourceLimit, "safety-memory store writer lock is already held", path.string());
        }
        return DirectoryLock(path);
    }

  private:
    explicit DirectoryLock(std::filesystem::path path) : path_(std::move(path)) {}

    std::filesystem::path path_;
    bool held_ = true;
};

Result<void> validate_store_root(const internal::Json& document, std::string& root_revision_id) {
    auto schema = require_schema(document, "rbfsafe-safety-memory-store");
    if (!schema)
        return schema;
    auto root = string_field(document, "root_revision_id");
    auto identity = string_field(document, "identity");
    if (!root || !identity || !internal::valid_sha256(root.value()) ||
        !internal::valid_sha256(identity.value()) ||
        identity.value() != internal::sha256(root_payload(root.value()).dump(false))) {
        return Result<void>::failure(StatusCode::CorruptData, "safety-memory store root manifest is invalid");
    }
    root_revision_id = std::move(root).value();
    return Result<void>::success();
}

} // namespace

Result<SafetyMemoryStore> SafetyMemoryStore::create(const std::filesystem::path& directory,
                                                    const SafetyMemory& initial_memory) {
    if (directory.empty() || directory == directory.root_path() || !initial_memory.valid()) {
        return Result<SafetyMemoryStore>::failure(StatusCode::InvalidArgument,
                                                  "memory-store creation input is invalid");
    }
    std::error_code error;
    if (std::filesystem::exists(directory, error)) {
        return Result<SafetyMemoryStore>::failure(
            StatusCode::IoError, "memory-store destination already exists", directory.string());
    }
    if (error) {
        return Result<SafetyMemoryStore>::failure(
            StatusCode::IoError, "failed to inspect memory-store destination", directory.string());
    }
    if (!directory.parent_path().empty()) {
        std::filesystem::create_directories(directory.parent_path(), error);
        if (error) {
            return Result<SafetyMemoryStore>::failure(StatusCode::IoError,
                                                      "failed to create memory-store parent",
                                                      directory.parent_path().string());
        }
    }

    SafetyMemoryRevisionInfo root;
    root.memory_id = initial_memory.identity();
    root.id = internal::safety_memory_revision_identity(root);
    const auto temporary = unique_sibling(directory, ".tmp-");
    const bool commits_created = std::filesystem::create_directories(temporary / "commits", error);
    const bool revisions_created =
        !error && std::filesystem::create_directories(temporary / "revisions", error);
    if (error || !commits_created || !revisions_created) {
        return Result<SafetyMemoryStore>::failure(StatusCode::IoError,
                                                  "failed to create temporary memory store");
    }
    auto cleanup = [&]() {
        std::error_code ignored;
        std::filesystem::remove_all(temporary, ignored);
    };
    auto saved = initial_memory.save(temporary / "revisions" / root.id);
    if (!saved) {
        cleanup();
        return saved.error();
    }
    auto written =
        internal::write_text_file(temporary / "manifest.json", root_document(root.id).dump(true) + "\n");
    if (written) {
        written = internal::write_text_file(temporary / "commits" / commit_filename(root),
                                            commit_document(root).dump(true) + "\n");
    }
    if (!written) {
        cleanup();
        return written.error();
    }
    std::filesystem::rename(temporary, directory, error);
    if (error) {
        cleanup();
        return Result<SafetyMemoryStore>::failure(StatusCode::IoError,
                                                  "failed to publish safety-memory store");
    }
    SafetyMemoryStore result;
    result.directory_ = directory;
    result.current_revision_id_ = root.id;
    result.revisions_.push_back(root);
    return result;
}

Result<SafetyMemoryStore> SafetyMemoryStore::open(const std::filesystem::path& directory,
                                                  const SafetyMemoryStoreOpenOptions& options) {
    if (directory.empty() || options.maximum_revisions == 0 || options.maximum_metadata_bytes == 0 ||
        options.memory_load.maximum_artifacts == 0 || options.memory_load.maximum_events == 0 ||
        options.memory_load.maximum_payload_bytes == 0) {
        return Result<SafetyMemoryStore>::failure(StatusCode::InvalidArgument,
                                                  "memory-store open options are invalid");
    }
    auto root_document_value = read_bounded_json(directory / "manifest.json", options.maximum_metadata_bytes);
    if (!root_document_value)
        return root_document_value.error();
    std::string root_revision_id;
    auto root_status = validate_store_root(root_document_value.value(), root_revision_id);
    if (!root_status)
        return root_status.error();

    std::error_code error;
    const auto commits_directory = directory / "commits";
    if (!std::filesystem::is_directory(commits_directory, error) || error) {
        return Result<SafetyMemoryStore>::failure(StatusCode::CorruptData,
                                                  "memory-store commits directory is missing");
    }
    std::vector<std::pair<std::string, SafetyMemoryRevisionInfo>> decoded;
    for (std::filesystem::directory_iterator iterator(commits_directory, error), end; iterator != end;
         iterator.increment(error)) {
        if (error) {
            return Result<SafetyMemoryStore>::failure(StatusCode::IoError,
                                                      "failed to enumerate memory-store commits");
        }
        const auto filename = iterator->path().filename().string();
        const bool regular_file = iterator->is_regular_file(error);
        if (error) {
            return Result<SafetyMemoryStore>::failure(StatusCode::IoError,
                                                      "failed to inspect memory-store commit entry");
        }
        if (regular_file && iterator->path().extension() == ".json") {
            if (decoded.size() >= options.maximum_revisions) {
                return Result<SafetyMemoryStore>::failure(StatusCode::ResourceLimit,
                                                          "memory-store revision count exceeds limit");
            }
            auto document = read_bounded_json(iterator->path(), options.maximum_metadata_bytes);
            if (!document)
                return document.error();
            auto revision = decode_commit(document.value());
            if (!revision)
                return revision.error();
            decoded.emplace_back(filename, std::move(revision).value());
        } else if (filename.find(".tmp-") == std::string::npos) {
            return Result<SafetyMemoryStore>::failure(StatusCode::CorruptData,
                                                      "unexpected memory-store commit entry", filename);
        }
    }
    if (error) {
        return Result<SafetyMemoryStore>::failure(StatusCode::IoError,
                                                  "failed to enumerate memory-store commits");
    }
    if (decoded.empty()) {
        return Result<SafetyMemoryStore>::failure(StatusCode::CorruptData,
                                                  "memory store contains no commits");
    }
    std::sort(decoded.begin(), decoded.end(), [](const auto& first, const auto& second) {
        if (first.second.sequence != second.second.sequence)
            return first.second.sequence < second.second.sequence;
        return first.second.id < second.second.id;
    });
    std::vector<SafetyMemoryRevisionInfo> revisions;
    revisions.reserve(decoded.size());
    for (std::size_t index = 0; index < decoded.size(); ++index) {
        const auto& filename = decoded[index].first;
        const auto& revision = decoded[index].second;
        if (revision.sequence != static_cast<std::uint64_t>(index) || filename != commit_filename(revision) ||
            (index == 0 && (revision.id != root_revision_id || !revision.parent_id.empty())) ||
            (index != 0 && revision.parent_id != decoded[index - 1].second.id)) {
            return Result<SafetyMemoryStore>::failure(StatusCode::CorruptData,
                                                      "memory-store commit chain is invalid", revision.id);
        }
        const auto revision_directory = directory / "revisions" / revision.id;
        if (!std::filesystem::is_regular_file(revision_directory / "manifest.json", error) || error ||
            !std::filesystem::is_regular_file(revision_directory / "memory.json", error) || error) {
            return Result<SafetyMemoryStore>::failure(
                StatusCode::CorruptData, "memory-store revision payload is missing", revision.id);
        }
        revisions.push_back(revision);
    }
    SafetyMemoryStore result;
    result.directory_ = directory;
    result.current_revision_id_ = revisions.back().id;
    result.revisions_ = std::move(revisions);
    result.options_ = options;
    auto current = result.load_current();
    if (!current)
        return current.error();
    return result;
}

Result<SafetyMemory> SafetyMemoryStore::load_current() const {
    if (revisions_.empty() || current_revision_id_.empty()) {
        return Result<SafetyMemory>::failure(StatusCode::InvalidArgument,
                                             "memory-store object is not initialized");
    }
    return load_revision(current_revision_id_);
}

Result<SafetyMemory> SafetyMemoryStore::load_revision(const std::string& revision_id) const {
    if (!internal::valid_sha256(revision_id)) {
        return Result<SafetyMemory>::failure(StatusCode::InvalidArgument,
                                             "memory-store revision ID is invalid");
    }
    auto found = std::find_if(revisions_.begin(), revisions_.end(),
                              [&](const auto& candidate) { return candidate.id == revision_id; });
    if (found == revisions_.end()) {
        return Result<SafetyMemory>::failure(StatusCode::InvalidArgument,
                                             "memory-store revision is not registered", revision_id);
    }
    auto memory = SafetyMemory::load(directory_ / "revisions" / revision_id, options_.memory_load);
    if (!memory)
        return memory.error();
    if (memory.value().identity() != found->memory_id) {
        return Result<SafetyMemory>::failure(StatusCode::CorruptData,
                                             "memory-store revision payload identity mismatch", revision_id);
    }
    return memory;
}

Result<SafetyMemoryRevisionInfo> SafetyMemoryStore::publish(const SafetyMemory& memory,
                                                            const std::string& expected_current_revision_id,
                                                            std::size_t maximum_revisions) {
    if (!memory.valid() || !internal::valid_sha256(expected_current_revision_id) || maximum_revisions == 0) {
        return Result<SafetyMemoryRevisionInfo>::failure(StatusCode::InvalidArgument,
                                                         "memory-store publish request is invalid");
    }
    auto lock = DirectoryLock::acquire(directory_);
    if (!lock)
        return lock.error();
    auto held_lock = std::move(lock).value();
    (void)held_lock;
    auto fresh = SafetyMemoryStore::open(directory_, options_);
    if (!fresh)
        return fresh.error();
    if (fresh.value().current_revision_id_ != expected_current_revision_id) {
        return Result<SafetyMemoryRevisionInfo>::failure(StatusCode::IdentityMismatch,
                                                         "memory-store current revision changed",
                                                         fresh.value().current_revision_id_);
    }
    const auto memory_id = memory.identity();
    if (fresh.value().revisions_.back().memory_id == memory_id) {
        *this = std::move(fresh).value();
        return revisions_.back();
    }
    const auto effective_limit = std::min(maximum_revisions, options_.maximum_revisions);
    if (fresh.value().revisions_.size() >= effective_limit) {
        return Result<SafetyMemoryRevisionInfo>::failure(StatusCode::ResourceLimit,
                                                         "memory-store revision limit reached");
    }
    const auto previous_sequence = fresh.value().revisions_.back().sequence;
    if (previous_sequence == std::numeric_limits<std::uint64_t>::max()) {
        return Result<SafetyMemoryRevisionInfo>::failure(StatusCode::ResourceLimit,
                                                         "memory-store sequence is exhausted");
    }
    SafetyMemoryRevisionInfo revision;
    revision.sequence = previous_sequence + 1;
    revision.parent_id = fresh.value().current_revision_id_;
    revision.memory_id = memory_id;
    revision.id = internal::safety_memory_revision_identity(revision);

    const auto revision_directory = directory_ / "revisions" / revision.id;
    std::error_code error;
    if (std::filesystem::exists(revision_directory, error)) {
        if (error) {
            return Result<SafetyMemoryRevisionInfo>::failure(StatusCode::IoError,
                                                             "failed to inspect memory-store revision");
        }
        auto orphan = SafetyMemory::load(revision_directory, options_.memory_load);
        if (!orphan || orphan.value().identity() != revision.memory_id) {
            return Result<SafetyMemoryRevisionInfo>::failure(
                StatusCode::CorruptData, "orphan memory-store revision is inconsistent", revision.id);
        }
    } else {
        auto saved = memory.save(revision_directory);
        if (!saved)
            return saved.error();
    }
    auto committed = write_immutable_file(directory_ / "commits" / commit_filename(revision),
                                          commit_document(revision).dump(true) + "\n");
    if (!committed)
        return committed.error();
    fresh.value().revisions_.push_back(revision);
    fresh.value().current_revision_id_ = revision.id;
    *this = std::move(fresh).value();
    return revision;
}

} // namespace rbfsafe
