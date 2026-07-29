#include <rbfsafe/rbfsafe.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {

template <typename T> T require(rbfsafe::Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().describe());
    return std::move(result).value();
}

class TemporaryDirectory {
  public:
    explicit TemporaryDirectory(std::filesystem::path path) : path_(std::move(path)) {}
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

  private:
    std::filesystem::path path_;
};

std::array<std::byte, rbfsafe::kEd25519SeedBytes> deterministic_seed(std::size_t first) {
    std::array<std::byte, rbfsafe::kEd25519SeedBytes> seed{};
    for (std::size_t index = 0; index < seed.size(); ++index)
        seed[index] = static_cast<std::byte>(first + index);
    return seed;
}

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;

    if (argc != 3) {
        std::cerr << "usage: rbfsafe-rotating-occupancy-history-quickstart "
                     "<occupancy-payload> <new-history-directory>\n";
        return 2;
    }

    try {
        const std::filesystem::path occupancy_path(argv[1]);
        const std::filesystem::path history_directory(argv[2]);
        if (std::filesystem::exists(history_directory)) {
            std::cerr << "history directory already exists: " << history_directory << '\n';
            return 2;
        }

        // Reproducible demonstration seeds only. Production secret keys must
        // remain in a protected key manager and never enter a history artifact.
        const auto root_pair = require(ed25519_key_pair_from_seed(deterministic_seed(41)));
        const auto successor_pair = require(ed25519_key_pair_from_seed(deterministic_seed(81)));
        const auto root_key =
            require(make_service_public_key("rotating-occupancy-publisher", root_pair.public_key, 1, 0,
                                            ServiceKeyState::Active, false, true, true));
        const auto successor_key =
            require(make_service_public_key("rotating-occupancy-publisher", successor_pair.public_key, 2, 0,
                                            ServiceKeyState::Active, false, true, true));
        const auto root_bundle = require(ServiceTrustBundle::create(1, "", {root_key}));

        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        TemporaryDirectory source_trust_directory(
            history_directory.parent_path() / (".rbfsafe-rotating-trust-source-" + std::to_string(nonce)));
        auto source_trust = require(
            ServiceTrustHistory::create(source_trust_directory.path(), root_bundle, root_bundle.id()));

        const auto root_publication = require(sign_continuous_fleet_occupancy_publication(
            occupancy_path, root_bundle, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
            root_key.id, root_pair.secret_key, 1, "", 0, 32));
        auto history = require(RotatingOccupancyPublicationHistory::create(
            history_directory, root_publication, occupancy_path, source_trust, "rotating-cell-stream-v1",
            "rotating-occupancy-publisher", root_bundle.id(), root_bundle.id(), root_publication.id));

        auto retired_root_key = root_key;
        retired_root_key.state = ServiceKeyState::Retired;
        retired_root_key.valid_through_sequence = 1;
        const auto successor_bundle =
            require(rotate_service_trust_bundle(root_bundle, {retired_root_key, successor_key}));
        const auto authorization = require(authorize_service_trust_bundle_successor(
            root_bundle, successor_bundle, root_key.service_id, root_key.id, root_pair.secret_key));
        const auto trust_record =
            require(history.rotate_trust(successor_bundle, authorization, root_bundle.id()));

        const auto successor_publication = require(sign_continuous_fleet_occupancy_publication(
            occupancy_path, successor_bundle, "rotating-cell-stream-v1", "rotating-occupancy-publisher",
            successor_key.id, successor_pair.secret_key, 2, root_publication.id, 1, 31));
        const auto publication_record = require(history.publish(successor_publication, occupancy_path,
                                                                root_publication.id, successor_bundle.id()));
        const auto reopened = require(RotatingOccupancyPublicationHistory::open(
            history_directory, "rotating-cell-stream-v1", "rotating-occupancy-publisher", root_bundle.id(),
            successor_bundle.id(), root_publication.id, successor_publication.id));
        require(reopened.verify(root_publication.id, 16));
        require(reopened.verify(successor_publication.id, 31));
        const auto audit = require(audit_rotating_occupancy_publication_histories(history, reopened));
        const auto embedded_trust_history = require(reopened.trust_history());

        std::cout << "trust_root=" << root_bundle.id() << '\n'
                  << "trust_head=" << successor_bundle.id() << '\n'
                  << "trust_record=" << trust_record.id << '\n'
                  << "publication_root=" << root_publication.id << '\n'
                  << "publication_head=" << successor_publication.id << '\n'
                  << "publication_record=" << publication_record.id << '\n'
                  << "trust_bundles=" << embedded_trust_history.records().size() << '\n'
                  << "publications=" << reopened.records().size() << '\n'
                  << "trust_relation=" << occupancy_publication_history_relation_name(audit.trust_relation)
                  << '\n'
                  << "publication_relation="
                  << occupancy_publication_history_relation_name(audit.publication_relation) << '\n'
                  << "historical_root_verified=true\n"
                  << "current_head_verified=true\n"
                  << "evidence=unknown\n"
                  << "authorizes_execution=false\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
