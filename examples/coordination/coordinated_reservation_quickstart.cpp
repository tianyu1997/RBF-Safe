#include <rbfsafe/rbfsafe.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

template <typename T> T require(rbfsafe::Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().describe());
    return std::move(result).value();
}

void require_ok(rbfsafe::Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().describe());
}

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
        std::cerr << "usage: rbfsafe-coordinated-reservation-quickstart "
                     "<occupancy-payload> <new-output-directory>\n";
        return 2;
    }

    try {
        const std::filesystem::path occupancy_path(argv[1]);
        const std::filesystem::path output_directory(argv[2]);
        if (std::filesystem::exists(output_directory)) {
            std::cerr << "output directory already exists: " << output_directory << '\n';
            return 2;
        }
        std::filesystem::create_directories(output_directory);
        const auto occupancy = require(ContinuousFleetOccupancyBundle::load(occupancy_path));

        std::vector<std::string> deployment_ids{"arm-a", "arm-b"};
        std::vector<RotatingOccupancyPublicationHistory> histories;
        histories.reserve(deployment_ids.size());
        for (std::size_t index = 0; index < deployment_ids.size(); ++index) {
            const auto& deployment_id = deployment_ids[index];
            const auto pair = require(ed25519_key_pair_from_seed(deterministic_seed(index == 0 ? 11 : 51)));
            const auto service_id = deployment_id + "-reservation-publisher";
            const auto stream_id = deployment_id + "-reservation-stream-v1";
            const auto key = require(make_service_public_key(service_id, pair.public_key, 1, 0,
                                                             ServiceKeyState::Active, false, true, true));
            const auto trust = require(ServiceTrustBundle::create(1, "", {key}));
            const auto source_directory = output_directory / (deployment_id + "-source-trust");
            const auto source = require(ServiceTrustHistory::create(source_directory, trust, trust.id()));
            const auto publication = require(sign_continuous_fleet_occupancy_publication(
                occupancy_path, trust, stream_id, service_id, key.id, pair.secret_key, 1, "", 0, 32));
            histories.push_back(require(RotatingOccupancyPublicationHistory::create(
                output_directory / (deployment_id + "-history"), publication, occupancy_path, source,
                stream_id, service_id, trust.id(), trust.id(), publication.id)));
        }

        const auto agreement = require(make_coordinated_reservation_agreement(
            "coordinated-cell-reservation-v1", 1, "", 16, occupancy, deployment_ids, histories));
        require_ok(verify_coordinated_reservation_agreement(agreement, occupancy, deployment_ids, histories));
        require_ok(agreement.save(output_directory / "agreement.json"));

        std::cout << "agreement=" << agreement.id << '\n'
                  << "protocol=" << agreement.protocol_id << '\n'
                  << "round=" << agreement.round << '\n'
                  << "occupancy_bundle=" << agreement.occupancy_bundle_id << '\n'
                  << "participants=" << agreement.participants.size() << '\n'
                  << "valid_from=" << agreement.valid_from_tick << '\n'
                  << "valid_through=" << agreement.valid_through_tick << '\n'
                  << "unanimous_payload=true\n"
                  << "replay_verified=true\n"
                  << "evidence=unknown\n"
                  << "authorizes_execution=false\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
