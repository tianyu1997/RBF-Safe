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

void require(rbfsafe::Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().describe());
}

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;

    if (argc != 3) {
        std::cerr << "usage: rbfsafe-occupancy-publication-quickstart "
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
        if (!std::filesystem::create_directory(output_directory))
            throw std::runtime_error("failed to create output directory");

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
        require(trust_bundle.save(output_directory / "trust-bundle.json"));

        const auto publication = require(sign_continuous_fleet_occupancy_publication(
            occupancy_path, trust_bundle, "fixture-cell-occupancy-stream-v1", "fixture-occupancy-publisher",
            service_key.id, key_pair.secret_key, 1, "", 0, 32));
        require(publication.save(output_directory / "publication.json"));
        const auto verified = require(verify_continuous_fleet_occupancy_publication(
            occupancy_path, publication, trust_bundle, "fixture-cell-occupancy-stream-v1",
            "fixture-occupancy-publisher", trust_bundle.id(), "", 16));

        std::cout << "publication=" << publication.id << '\n'
                  << "trust_bundle=" << trust_bundle.id() << '\n'
                  << "occupancy_bundle=" << publication.occupancy_bundle_id << '\n'
                  << "evaluation_tick=" << verified.evaluation_tick << '\n'
                  << "authentication=ed25519\n"
                  << "evidence=unknown\n"
                  << "authorizes_execution=false\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
