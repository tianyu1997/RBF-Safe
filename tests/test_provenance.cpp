#include "test_support.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

std::string digest(char value) { return std::string(64, value); }

template <std::size_t Offset> std::array<std::byte, rbfsafe::kEd25519SeedBytes> seed() {
    std::array<std::byte, rbfsafe::kEd25519SeedBytes> result{};
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = static_cast<std::byte>(index + Offset);
    return result;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

} // namespace

int main() {
    using namespace rbfsafe;

    const auto subject_pair = ed25519_key_pair_from_seed(seed<1>());
    const auto attester_one_pair = ed25519_key_pair_from_seed(seed<33>());
    const auto attester_two_pair = ed25519_key_pair_from_seed(seed<65>());
    const auto time_one_pair = ed25519_key_pair_from_seed(seed<97>());
    const auto time_two_pair = ed25519_key_pair_from_seed(seed<129>());
    CHECK(subject_pair);
    CHECK(attester_one_pair);
    CHECK(attester_two_pair);
    CHECK(time_one_pair);
    CHECK(time_two_pair);

    auto subject_key = make_service_public_key("cell-controller", subject_pair.value().public_key, 1, 0,
                                               ServiceKeyState::Active, false, true, false);
    auto attester_one_key = make_service_public_key("attester-one", attester_one_pair.value().public_key, 1,
                                                    0, ServiceKeyState::Active, false, true, false);
    auto attester_two_key = make_service_public_key("attester-two", attester_two_pair.value().public_key, 1,
                                                    0, ServiceKeyState::Active, false, true, false);
    auto time_one_key = make_service_public_key("time-one", time_one_pair.value().public_key, 1, 0,
                                                ServiceKeyState::Active, false, true, false);
    auto time_two_key = make_service_public_key("time-two", time_two_pair.value().public_key, 1, 0,
                                                ServiceKeyState::Active, false, true, false);
    CHECK(subject_key);
    CHECK(attester_one_key);
    CHECK(attester_two_key);
    CHECK(time_one_key);
    CHECK(time_two_key);

    auto trust_bundle =
        ServiceTrustBundle::create(1, "",
                                   {subject_key.value(), attester_one_key.value(), attester_two_key.value(),
                                    time_one_key.value(), time_two_key.value()});
    CHECK(trust_bundle);

    HardwareAttestationAdapterPin adapter{"tpm2-quote-normalizer", "1.0.0", "application/vnd.tcg.tpm2-quote"};
    CHECK(adapter.valid());
    CHECK(!HardwareAttestationAdapterPin{}.valid());
    auto hardware_policy = HardwareKeyProvenancePolicy::create(
        2, true, 8, {HardwareAttestationScope::ExecutionControl, HardwareAttestationScope::ArtifactPublish},
        {adapter},
        {{attester_one_key.value().service_id, attester_one_key.value().id},
         {attester_two_key.value().service_id, attester_two_key.value().id}},
        {"example-vendor"});
    CHECK(hardware_policy);
    CHECK(hardware_policy.value().valid());

    HardwareKeyAttestationInput first_input;
    first_input.subject_service_id = subject_key.value().service_id;
    first_input.subject_key_id = subject_key.value().id;
    first_input.subject_public_key = subject_key.value().public_key;
    first_input.adapter = adapter;
    first_input.vendor_id = "example-vendor";
    first_input.product_id = "secure-element-a";
    first_input.evidence_digest = digest('a');
    first_input.nonce_digest = digest('b');
    first_input.scopes = {HardwareAttestationScope::ExecutionControl,
                          HardwareAttestationScope::ArtifactPublish};
    auto first = sign_hardware_key_attestation_statement(
        first_input, trust_bundle.value(), attester_one_key.value().service_id, attester_one_key.value().id,
        attester_one_pair.value().secret_key);
    CHECK(first);
    CHECK(first.value().valid());
    CHECK(first.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!first.value().authorizes_execution());
    CHECK(verify_hardware_key_attestation_statement(first.value(), trust_bundle.value()));

    auto second_input = first_input;
    second_input.sequence = 2;
    second_input.parent_statement_id = first.value().id;
    second_input.evidence_digest = digest('c');
    second_input.nonce_digest = digest('d');
    auto second = sign_hardware_key_attestation_statement(
        second_input, trust_bundle.value(), attester_two_key.value().service_id, attester_two_key.value().id,
        attester_two_pair.value().secret_key);
    CHECK(second);

    auto replay_hardware = [&](std::vector<HardwareKeyAttestationStatement> statements,
                               const ServicePublicKey& expected_subject,
                               const HardwareKeyProvenancePolicy& policy,
                               const ProvenanceReplayOptions& options = ProvenanceReplayOptions{}) {
        return replay_hardware_key_provenance(statements, expected_subject, trust_bundle.value(), policy,
                                              options);
    };

    auto hardware_report =
        replay_hardware({first.value(), second.value()}, subject_key.value(), hardware_policy.value());
    CHECK(hardware_report);
    CHECK(hardware_report.value().valid());
    CHECK(hardware_report.value().status == HardwareProvenanceStatus::Satisfied);
    CHECK(hardware_report.value().authenticated_statement_count == 2);
    CHECK(hardware_report.value().distinct_attester_count == 2);
    CHECK(hardware_report.value().head_statement_id == second.value().id);
    CHECK(!hardware_report.value().authorizes_execution());

    auto incomplete_hardware = replay_hardware({first.value()}, subject_key.value(), hardware_policy.value());
    CHECK(incomplete_hardware);
    CHECK(incomplete_hardware.value().status == HardwareProvenanceStatus::Incomplete);

    auto broken_chain = second.value();
    broken_chain.parent_statement_id = digest('e');
    CHECK(!broken_chain.valid());
    CHECK(!replay_hardware({first.value(), broken_chain}, subject_key.value(), hardware_policy.value()));

    auto wrong_subject = make_service_public_key("other-controller", subject_pair.value().public_key, 1, 0,
                                                 ServiceKeyState::Active, false, true, false);
    CHECK(wrong_subject);
    CHECK(!replay_hardware({first.value(), second.value()}, wrong_subject.value(), hardware_policy.value()));

    auto other_adapter_policy = HardwareKeyProvenancePolicy::create(
        1, false, 8, {HardwareAttestationScope::ExecutionControl}, {{"other-adapter", "1", "other-format"}},
        {{attester_one_key.value().service_id, attester_one_key.value().id}}, {"example-vendor"});
    CHECK(other_adapter_policy);
    CHECK(!replay_hardware({first.value()}, subject_key.value(), other_adapter_policy.value()));

    ProvenanceReplayOptions hardware_budget;
    hardware_budget.maximum_statements = 1;
    auto limited_hardware = replay_hardware({first.value(), second.value()}, subject_key.value(),
                                            hardware_policy.value(), hardware_budget);
    CHECK(!limited_hardware);
    CHECK(limited_hardware.error().code == StatusCode::ResourceLimit);
    ProvenanceReplayOptions cancelled;
    cancelled.cancellation.cancel();
    auto cancelled_hardware =
        replay_hardware({first.value()}, subject_key.value(), hardware_policy.value(), cancelled);
    CHECK(!cancelled_hardware);
    CHECK(cancelled_hardware.error().code == StatusCode::Cancelled);

    auto freshness_policy =
        ExternalTimeFreshnessPolicy::create("unix-utc-ns", 100'000, 25'000, 20'000, 2, true, 16,
                                            {{time_one_key.value().service_id, time_one_key.value().id},
                                             {time_two_key.value().service_id, time_two_key.value().id}});
    CHECK(freshness_policy);
    CHECK(freshness_policy.value().valid());

    ExternalTimeAssertionInput time_one_input;
    time_one_input.subject_id = subject_key.value().id;
    time_one_input.clock_id = "unix-utc-ns";
    time_one_input.asserted_time_ns = 990'000;
    time_one_input.uncertainty_ns = 20'000;
    auto time_one =
        sign_external_time_assertion(time_one_input, trust_bundle.value(), time_one_key.value().service_id,
                                     time_one_key.value().id, time_one_pair.value().secret_key);
    CHECK(time_one);
    CHECK(verify_external_time_assertion(time_one.value(), trust_bundle.value()));

    auto time_two_input = time_one_input;
    time_two_input.asserted_time_ns = 1'000'000;
    time_two_input.uncertainty_ns = 10'000;
    auto time_two =
        sign_external_time_assertion(time_two_input, trust_bundle.value(), time_two_key.value().service_id,
                                     time_two_key.value().id, time_two_pair.value().secret_key);
    CHECK(time_two);

    auto evaluate_time = [&](std::vector<ExternalTimeAssertion> assertions,
                             const ExternalTimeFreshnessPolicy& policy, std::uint64_t evaluated_at_ns,
                             const ProvenanceReplayOptions& options = ProvenanceReplayOptions{}) {
        return evaluate_external_time_freshness(subject_key.value().id, assertions, trust_bundle.value(),
                                                policy, evaluated_at_ns, options);
    };

    auto fresh = evaluate_time({time_two.value(), time_one.value()}, freshness_policy.value(), 1'000'000);
    CHECK(fresh);
    CHECK(fresh.value().valid());
    CHECK(fresh.value().status == ExternalTimeFreshnessStatus::Fresh);
    CHECK(fresh.value().authenticated_source_count == 2);
    CHECK(fresh.value().intersection_lower_ns == 990'000);
    CHECK(fresh.value().intersection_upper_ns == 1'010'000);
    CHECK(!fresh.value().authorizes_execution());

    auto incomplete_time = evaluate_time({time_one.value()}, freshness_policy.value(), 1'000'000);
    CHECK(incomplete_time);
    CHECK(incomplete_time.value().status == ExternalTimeFreshnessStatus::Incomplete);
    auto stale = evaluate_time({time_one.value(), time_two.value()}, freshness_policy.value(), 2'000'000);
    CHECK(stale);
    CHECK(stale.value().status == ExternalTimeFreshnessStatus::Stale);
    auto future = evaluate_time({time_one.value(), time_two.value()}, freshness_policy.value(), 500'000);
    CHECK(future);
    CHECK(future.value().status == ExternalTimeFreshnessStatus::Future);

    auto conflicting_input = time_two_input;
    conflicting_input.asserted_time_ns = 1'200'000;
    auto conflicting =
        sign_external_time_assertion(conflicting_input, trust_bundle.value(), time_two_key.value().service_id,
                                     time_two_key.value().id, time_two_pair.value().secret_key);
    CHECK(conflicting);
    auto inconsistent =
        evaluate_time({time_one.value(), conflicting.value()}, freshness_policy.value(), 1'000'000);
    CHECK(inconsistent);
    CHECK(inconsistent.value().status == ExternalTimeFreshnessStatus::Inconsistent);

    auto maximum_time_one_input = time_one_input;
    maximum_time_one_input.asserted_time_ns = std::numeric_limits<std::uint64_t>::max() - 5'000;
    maximum_time_one_input.uncertainty_ns = 10'000;
    auto maximum_time_one = sign_external_time_assertion(
        maximum_time_one_input, trust_bundle.value(), time_one_key.value().service_id,
        time_one_key.value().id, time_one_pair.value().secret_key);
    CHECK(maximum_time_one);
    auto maximum_time_two_input = maximum_time_one_input;
    auto maximum_time_two = sign_external_time_assertion(
        maximum_time_two_input, trust_bundle.value(), time_two_key.value().service_id,
        time_two_key.value().id, time_two_pair.value().secret_key);
    CHECK(maximum_time_two);
    auto maximum_time =
        evaluate_time({maximum_time_one.value(), maximum_time_two.value()}, freshness_policy.value(),
                      std::numeric_limits<std::uint64_t>::max() - 5'000);
    CHECK(maximum_time);
    CHECK(maximum_time.value().status == ExternalTimeFreshnessStatus::Fresh);
    CHECK(maximum_time.value().intersection_upper_ns == std::numeric_limits<std::uint64_t>::max());

    auto wrong_clock = time_one_input;
    wrong_clock.clock_id = "monotonic-local";
    auto wrong_clock_assertion =
        sign_external_time_assertion(wrong_clock, trust_bundle.value(), time_one_key.value().service_id,
                                     time_one_key.value().id, time_one_pair.value().secret_key);
    CHECK(wrong_clock_assertion);
    CHECK(!evaluate_time({wrong_clock_assertion.value()}, freshness_policy.value(), 1'000'000));

    auto bundle = VerifiableProvenanceBundle::create(
        subject_key.value(), hardware_policy.value(), freshness_policy.value(),
        {second.value(), first.value()}, {time_two.value(), time_one.value()}, trust_bundle.value());
    CHECK(bundle);
    CHECK(bundle.value().valid());
    CHECK(bundle.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!bundle.value().authorizes_execution());
    auto audit = replay_verifiable_provenance(bundle.value(), trust_bundle.value(), 1'000'000);
    CHECK(audit);
    CHECK(audit.value().valid());
    CHECK(audit.value().ready());
    CHECK(audit.value().evidence() == EvidenceLevel::Unknown);
    CHECK(!audit.value().authorizes_execution());

    auto wrong_bundle = ServiceTrustBundle::create(
        1, "",
        {subject_key.value(), attester_one_key.value(), attester_two_key.value(), time_one_key.value()});
    CHECK(wrong_bundle);
    CHECK(!replay_verifiable_provenance(bundle.value(), wrong_bundle.value(), 1'000'000));

    const auto temporary =
        std::filesystem::temp_directory_path() /
        ("rbfsafe-provenance-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temporary);
    const auto path = temporary / "provenance.json";
    CHECK(bundle.value().save(path));
    CHECK(!bundle.value().save(path));
    auto loaded = VerifiableProvenanceBundle::load(path);
    CHECK(loaded);
    CHECK(loaded.value().id() == bundle.value().id());
    CHECK(replay_verifiable_provenance(loaded.value(), trust_bundle.value(), 1'000'000));
    SaveOptions overwrite;
    overwrite.overwrite = true;
    CHECK(bundle.value().save(path, overwrite));

    VerifiableProvenanceBundleLoadOptions load_budget;
    load_budget.maximum_statements = 1;
    auto limited_load = VerifiableProvenanceBundle::load(path, load_budget);
    CHECK(!limited_load);
    CHECK(limited_load.error().code == StatusCode::ResourceLimit);
    VerifiableProvenanceBundleLoadOptions policy_budget;
    policy_budget.maximum_policy_entries = 1;
    auto policy_limited_load = VerifiableProvenanceBundle::load(path, policy_budget);
    CHECK(!policy_limited_load);
    CHECK(policy_limited_load.error().code == StatusCode::ResourceLimit);
    VerifiableProvenanceBundleLoadOptions payload_budget;
    payload_budget.maximum_payload_bytes = 1;
    auto payload_limited_load = VerifiableProvenanceBundle::load(path, payload_budget);
    CHECK(!payload_limited_load);
    CHECK(payload_limited_load.error().code == StatusCode::ResourceLimit);
    VerifiableProvenanceBundleLoadOptions cancelled_load;
    cancelled_load.cancellation.cancel();
    auto cancelled_result = VerifiableProvenanceBundle::load(path, cancelled_load);
    CHECK(!cancelled_result);
    CHECK(cancelled_result.error().code == StatusCode::Cancelled);

    auto corrupted = read_text(path);
    const auto checksum = corrupted.find("\"checksum\"");
    CHECK(checksum != std::string::npos);
    const auto first_hex = corrupted.find_first_of("0123456789abcdef", checksum + 10);
    CHECK(first_hex != std::string::npos);
    corrupted[first_hex] = corrupted[first_hex] == '0' ? '1' : '0';
    const auto corrupt_path = temporary / "corrupt.json";
    write_text(corrupt_path, corrupted);
    auto corrupt = VerifiableProvenanceBundle::load(corrupt_path);
    CHECK(!corrupt);
    CHECK(corrupt.error().code == StatusCode::CorruptData);

    auto incompatible_text = read_text(path);
    const auto schema = incompatible_text.find("\"schema\": 1");
    CHECK(schema != std::string::npos);
    incompatible_text.replace(schema, std::string("\"schema\": 1").size(), "\"schema\": 2");
    const auto incompatible_path = temporary / "incompatible.json";
    write_text(incompatible_path, incompatible_text);
    auto incompatible = VerifiableProvenanceBundle::load(incompatible_path);
    CHECK(!incompatible);
    CHECK(incompatible.error().code == StatusCode::IncompatibleFormat);

    const auto truncated_path = temporary / "truncated.json";
    write_text(truncated_path, read_text(path).substr(0, 64));
    auto truncated = VerifiableProvenanceBundle::load(truncated_path);
    CHECK(!truncated);
    CHECK(truncated.error().code == StatusCode::CorruptData);

    const auto symlink_path = temporary / "indirect.json";
    std::error_code symlink_error;
    std::filesystem::create_symlink(path, symlink_path, symlink_error);
    if (!symlink_error) {
        auto indirect = VerifiableProvenanceBundle::load(symlink_path);
        CHECK(!indirect);
        CHECK(indirect.error().code == StatusCode::CorruptData);
        CHECK(!bundle.value().save(symlink_path, overwrite));
    }

    std::error_code ignored;
    std::filesystem::remove_all(temporary, ignored);

    CHECK(hardware_attestation_scope_name(HardwareAttestationScope::ExecutionControl) == "ExecutionControl");
    CHECK(hardware_provenance_status_name(HardwareProvenanceStatus::Satisfied) == "SATISFIED");
    CHECK(external_time_freshness_status_name(ExternalTimeFreshnessStatus::Fresh) == "FRESH");

    return 0;
}
