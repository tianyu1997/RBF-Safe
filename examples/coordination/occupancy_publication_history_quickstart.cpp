#include <rbfsafe/rbfsafe.h>

#include <array>
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

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;

    if (argc != 3) {
        std::cerr << "usage: rbfsafe-occupancy-history-quickstart "
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

        // Reproducible demonstration seed only. Production keys must come from a
        // cryptographically secure key manager and never be embedded in artifacts.
        std::array<std::byte, kEd25519SeedBytes> seed{};
        for (std::size_t index = 0; index < seed.size(); ++index)
            seed[index] = static_cast<std::byte>(index + 1);
        const auto key_pair = require(ed25519_key_pair_from_seed(seed));
        const auto service_key =
            require(make_service_public_key("fixture-occupancy-publisher", key_pair.public_key, 1, 0,
                                            ServiceKeyState::Active, false, true, false));
        const auto trust_bundle = require(ServiceTrustBundle::create(1, "", {service_key}));
        const auto root = require(sign_continuous_fleet_occupancy_publication(
            occupancy_path, trust_bundle, "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher",
            service_key.id, key_pair.secret_key, 1, "", 0, 32));
        auto history = require(OccupancyPublicationHistory::create(
            history_directory, root, occupancy_path, trust_bundle, "fixture-cell-occupancy-stream-v1",
            "fixture-occupancy-publisher", trust_bundle.id(), root.id));
        const auto successor = require(sign_continuous_fleet_occupancy_publication(
            occupancy_path, trust_bundle, "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher",
            service_key.id, key_pair.secret_key, 2, root.id, 1, 31));
        const auto record = require(history.publish(successor, occupancy_path, root.id));
        const auto reopened = require(OccupancyPublicationHistory::open(
            history_directory, "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher",
            trust_bundle.id(), root.id, successor.id));
        const auto audit = require(audit_occupancy_publication_histories(history, reopened));

        std::cout << "root=" << root.id << '\n'
                  << "head=" << successor.id << '\n'
                  << "head_record=" << record.id << '\n'
                  << "trust_bundle=" << trust_bundle.id() << '\n'
                  << "publications=" << reopened.records().size() << '\n'
                  << "self_relation=" << occupancy_publication_history_relation_name(audit.relation) << '\n'
                  << "replay_verified=true\n"
                  << "evidence=unknown\n"
                  << "authorizes_execution=false\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
