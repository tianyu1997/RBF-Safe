#include <rbfsafe/rbfsafe.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

std::string digest(char value) { return std::string(64, value); }

template <std::size_t Offset> std::array<std::byte, rbfsafe::kEd25519SeedBytes> seed() {
    std::array<std::byte, rbfsafe::kEd25519SeedBytes> result{};
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = static_cast<std::byte>(index + Offset);
    return result;
}

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

    try {
        const std::filesystem::path output =
            argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("provenance-example");
        if (std::filesystem::exists(output)) {
            std::cerr << "output already exists: " << output << '\n';
            return 2;
        }
        std::filesystem::create_directories(output);

        const auto subject_pair = require(ed25519_key_pair_from_seed(seed<1>()));
        const auto attester_one_pair = require(ed25519_key_pair_from_seed(seed<33>()));
        const auto attester_two_pair = require(ed25519_key_pair_from_seed(seed<65>()));
        const auto time_one_pair = require(ed25519_key_pair_from_seed(seed<97>()));
        const auto time_two_pair = require(ed25519_key_pair_from_seed(seed<129>()));
        const auto governance_pair = require(ed25519_key_pair_from_seed(seed<161>()));

        const auto subject_key =
            require(make_service_public_key("fixture-controller", subject_pair.public_key, 1, 0,
                                            ServiceKeyState::Active, false, true, false));
        const auto attester_one_key =
            require(make_service_public_key("fixture-attester-one", attester_one_pair.public_key, 1, 0,
                                            ServiceKeyState::Active, false, true, false));
        const auto attester_two_key =
            require(make_service_public_key("fixture-attester-two", attester_two_pair.public_key, 1, 0,
                                            ServiceKeyState::Active, false, true, false));
        const auto time_one_key = require(make_service_public_key(
            "fixture-time-one", time_one_pair.public_key, 1, 0, ServiceKeyState::Active, false, true, false));
        const auto time_two_key = require(make_service_public_key(
            "fixture-time-two", time_two_pair.public_key, 1, 0, ServiceKeyState::Active, false, true, false));
        const auto governance_key =
            require(make_service_public_key("fixture-governance", governance_pair.public_key, 1, 0,
                                            ServiceKeyState::Active, false, false, true));
        const auto trust_bundle = require(ServiceTrustBundle::create(
            1, "",
            {subject_key, attester_one_key, attester_two_key, time_one_key, time_two_key, governance_key}));
        auto history =
            require(ServiceTrustHistory::create(output / "trust-history", trust_bundle, trust_bundle.id()));
        const auto checkpoint_signature = require(sign_service_trust_checkpoint(
            history, governance_key.service_id, governance_key.id, governance_pair.secret_key));
        const auto checkpoint = require(assemble_service_trust_checkpoint(history, {checkpoint_signature}));
        require(checkpoint.save(output / "checkpoint.json"));

        const HardwareAttestationAdapterPin adapter{"fixture-tpm2-normalizer", "1.0.0",
                                                    "application/vnd.tcg.tpm2-quote"};
        const auto hardware_policy = require(HardwareKeyProvenancePolicy::create(
            2, true, 8,
            {HardwareAttestationScope::ArtifactPublish, HardwareAttestationScope::ExecutionControl},
            {adapter},
            {{attester_one_key.service_id, attester_one_key.id},
             {attester_two_key.service_id, attester_two_key.id}},
            {"fixture-vendor"}));

        HardwareKeyAttestationInput first_input;
        first_input.subject_service_id = subject_key.service_id;
        first_input.subject_key_id = subject_key.id;
        first_input.subject_public_key = subject_key.public_key;
        first_input.adapter = adapter;
        first_input.vendor_id = "fixture-vendor";
        first_input.product_id = "fixture-secure-element";
        first_input.evidence_digest = digest('a');
        first_input.nonce_digest = digest('b');
        first_input.scopes = {HardwareAttestationScope::ArtifactPublish,
                              HardwareAttestationScope::ExecutionControl};
        const auto first = require(
            sign_hardware_key_attestation_statement(first_input, trust_bundle, attester_one_key.service_id,
                                                    attester_one_key.id, attester_one_pair.secret_key));
        auto second_input = first_input;
        second_input.sequence = 2;
        second_input.parent_statement_id = first.id;
        second_input.evidence_digest = digest('c');
        second_input.nonce_digest = digest('d');
        const auto second = require(
            sign_hardware_key_attestation_statement(second_input, trust_bundle, attester_two_key.service_id,
                                                    attester_two_key.id, attester_two_pair.secret_key));

        const auto freshness_policy = require(ExternalTimeFreshnessPolicy::create(
            "unix-utc-ns", 100'000, 25'000, 20'000, 2, true, 16,
            {{time_one_key.service_id, time_one_key.id}, {time_two_key.service_id, time_two_key.id}}));
        ExternalTimeAssertionInput time_one_input;
        time_one_input.subject_id = subject_key.id;
        time_one_input.clock_id = freshness_policy.clock_id;
        time_one_input.asserted_time_ns = 990'000;
        time_one_input.uncertainty_ns = 20'000;
        const auto time_one =
            require(sign_external_time_assertion(time_one_input, trust_bundle, time_one_key.service_id,
                                                 time_one_key.id, time_one_pair.secret_key));
        auto time_two_input = time_one_input;
        time_two_input.asserted_time_ns = 1'000'000;
        time_two_input.uncertainty_ns = 10'000;
        const auto time_two =
            require(sign_external_time_assertion(time_two_input, trust_bundle, time_two_key.service_id,
                                                 time_two_key.id, time_two_pair.secret_key));

        const auto provenance =
            require(VerifiableProvenanceBundle::create(subject_key, hardware_policy, freshness_policy,
                                                       {first, second}, {time_one, time_two}, trust_bundle));
        require(provenance.save(output / "provenance.json"));
        const auto audit = require(replay_verifiable_provenance(provenance, trust_bundle, 1'000'000));

        std::cout << "provenance=" << provenance.id() << '\n'
                  << "trust_root=" << trust_bundle.id() << '\n'
                  << "checkpoint=" << checkpoint.id << '\n'
                  << "hardware=" << hardware_provenance_status_name(audit.hardware.status) << '\n'
                  << "freshness=" << external_time_freshness_status_name(audit.freshness.status) << '\n'
                  << "ready=" << (audit.ready() ? "true" : "false") << '\n'
                  << "evidence=unknown\n"
                  << "authorizes_execution=false\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
