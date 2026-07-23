#include "test_support.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace {

class SplitValidator final : public rbfsafe::RegionValidator {
  public:
    rbfsafe::Result<rbfsafe::RegionValidation> validate(const rbfsafe::SerialRobotModel& robot,
                                                        const rbfsafe::SceneSnapshot&,
                                                        const rbfsafe::CspaceAabb& domain) const override {
        if (domain.axes().front().width() > 1.0)
            return rbfsafe::RegionValidation{};
        auto envelope = rbfsafe::compute_ifk_aa_link_envelope(robot, domain);
        if (!envelope)
            return envelope.error();
        return rbfsafe::RegionValidation{rbfsafe::ValidationDisposition::CertifiedFree, 0.5,
                                         std::move(envelope).value()};
    }

    std::string algorithm_name() const override { return "calibration-test-split"; }
    std::string algorithm_version() const override { return "1"; }
};

rbfsafe::PolicyCalibrationProfileInput profile_input() {
    rbfsafe::PolicyCalibrationProfileInput input;
    input.policy_id = "vla-policy-a";
    input.policy_model_digest = std::string(64, 'a');
    input.scope_id = "factory-cell-a";
    input.task_id = "shelf-pick";
    input.dataset_digest = std::string(64, 'b');
    input.method = "held-out-reliability-bins";
    input.method_version = "1";
    input.outcome_definition = "shield accepted or repaired proposal";
    input.state_uncertainty_unit = "normalized-joint-range-rms";
    input.action_uncertainty_unit = "normalized-joint-range-rms";
    input.bins = {{0.0, 0.5, 0.25, 500, 100}, {0.5, 1.0, 0.85, 500, 400}};
    return input;
}

rbfsafe::PolicyProposal proposal(std::uint64_t sequence, double confidence) {
    rbfsafe::PolicyProposalMetadata metadata;
    metadata.policy_id = "vla-policy-a";
    metadata.task_id = "shelf-pick";
    metadata.episode_id = "episode-1";
    metadata.sequence = sequence;
    metadata.confidence = confidence;
    metadata.state_uncertainty = 0.1;
    metadata.action_uncertainty = 0.1;
    return {rbfsafe::JointDeltaAction{{0.1}}, std::move(metadata)};
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
    auto profile = PolicyCalibrationProfile::create(profile_input());
    CHECK(profile);
    CHECK(profile.value().valid());
    CHECK(profile.value().id().size() == 64);
    CHECK(profile.value().sample_count() == 1'000);
    CHECK(std::abs(profile.value().expected_calibration_error() - 0.05) < 1e-12);
    CHECK(std::abs(profile.value().maximum_calibration_error() - 0.05) < 1e-12);
    CHECK(profile.value().bins().size() == 2);
    CHECK(profile.value().bins()[1].observed_success_rate == 0.8);
    CHECK(profile.value().bins()[1].lower_confidence_bound_95 > 0.7);
    CHECK(profile.value().bins()[1].lower_confidence_bound_95 < 0.8);
    CHECK(PolicyCalibrationProfile::create(profile_input()).value().id() == profile.value().id());

    auto fixed = PolicyCalibrationProfile::load(std::filesystem::path(RBFSAFE_TEST_DATA_DIR) /
                                                "policy_calibration_profile_schema1" / "profile.json");
    CHECK(fixed);
    CHECK(fixed.value().id() == "7df9fb165b202f0d6e77dc98e409253f991780992e0b1be4920b690bcb0060c4");
    CHECK(fixed.value().sample_count() == 1'000);

    auto low_lookup = profile.value().lookup(0.4);
    auto high_lookup = profile.value().lookup(0.9);
    auto boundary_lookup = profile.value().lookup(0.5);
    auto final_lookup = profile.value().lookup(1.0);
    CHECK(low_lookup && high_lookup && boundary_lookup && final_lookup);
    CHECK(low_lookup.value().bin_index == 0);
    CHECK(high_lookup.value().bin_index == 1);
    CHECK(boundary_lookup.value().bin_index == 1);
    CHECK(final_lookup.value().bin_index == 1);
    CHECK(high_lookup.value().conservative_confidence == profile.value().bins()[1].lower_confidence_bound_95);
    CHECK(!profile.value().lookup(-0.1));

    auto invalid = profile_input();
    invalid.bins[1].lower_confidence = 0.6;
    CHECK(!PolicyCalibrationProfile::create(invalid));
    invalid = profile_input();
    invalid.bins[0].successes = 501;
    CHECK(!PolicyCalibrationProfile::create(invalid));
    invalid = profile_input();
    invalid.policy_model_digest = "not-a-digest";
    CHECK(!PolicyCalibrationProfile::create(invalid));
    invalid = profile_input();
    invalid.bins[0].samples = 1'000'000'000'001ULL;
    auto sample_limited = PolicyCalibrationProfile::create(invalid);
    CHECK(!sample_limited);
    CHECK(sample_limited.error().code == StatusCode::ResourceLimit);
    invalid = profile_input();
    invalid.bins.assign(4'097, PolicyCalibrationBinInput{});
    auto bin_resource_limited = PolicyCalibrationProfile::create(invalid);
    CHECK(!bin_resource_limited);
    CHECK(bin_resource_limited.error().code == StatusCode::ResourceLimit);

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root =
        std::filesystem::temp_directory_path() / ("rbfsafe-calibration-test-" + std::to_string(nonce));
    std::filesystem::create_directories(root);
    const auto path = root / "profile.json";
    CHECK(profile.value().save(path));
    CHECK(!profile.value().save(path));
    CHECK(profile.value().save(path, SaveOptions{true}));
    auto loaded = PolicyCalibrationProfile::load(path);
    CHECK(loaded);
    CHECK(loaded.value().valid());
    CHECK(loaded.value().id() == profile.value().id());
    PolicyCalibrationLoadOptions byte_limited;
    byte_limited.maximum_payload_bytes = 1;
    CHECK(!PolicyCalibrationProfile::load(path, byte_limited));
    PolicyCalibrationLoadOptions bin_limited;
    bin_limited.maximum_bins = 1;
    CHECK(!PolicyCalibrationProfile::load(path, bin_limited));

    const std::string saved = read_text(path);
    std::string unknown_schema = saved;
    const auto schema = unknown_schema.find("\"schema\": 1");
    CHECK(schema != std::string::npos);
    unknown_schema.replace(schema, std::string("\"schema\": 1").size(), "\"schema\": 99");
    write_text(path, unknown_schema);
    auto incompatible = PolicyCalibrationProfile::load(path);
    CHECK(!incompatible);
    CHECK(incompatible.error().code == StatusCode::IncompatibleFormat);
    std::string altered_statistics = saved;
    const std::string expected_error_prefix = "\"expected_calibration_error\": ";
    const auto expected_error = altered_statistics.find(expected_error_prefix);
    CHECK(expected_error != std::string::npos);
    const auto expected_error_value = expected_error + expected_error_prefix.size();
    CHECK(altered_statistics[expected_error_value] == '0');
    altered_statistics[expected_error_value] = '1';
    write_text(path, altered_statistics);
    auto corrupted = PolicyCalibrationProfile::load(path);
    CHECK(!corrupted);
    CHECK(corrupted.error().code == StatusCode::CorruptData);
    write_text(path, saved);

    const SerialRobotModel robot("calibration-1r", {{0.0, 1.0, 0.0, 0.0, JointType::Revolute}}, {{-1.0, 1.0}},
                                 {0.02});
    const SceneSnapshot scene({}, "calibration-empty-v1");
    AtlasBuilder builder(std::make_shared<SplitValidator>());
    auto built = builder.build(robot, scene, {{-0.5}, {0.5}});
    CHECK(built);
    const Configuration current{-0.5};
    const std::vector<PolicyProposal> proposals{proposal(1, 0.4), proposal(2, 0.9)};
    CalibratedPolicyGateOptions options;
    options.minimum_total_samples = 1'000;
    options.minimum_bin_samples = 500;
    options.maximum_expected_calibration_error = 0.06;
    options.maximum_bin_calibration_error = 0.06;
    options.policy.minimum_confidence = 0.7;
    options.policy.selection_mode = PolicySelectionMode::HighestConfidence;

    CalibratedPolicySafetyGate gate;
    auto report = gate.check_proposals(profile.value(), "factory-cell-a", std::string(64, 'a'), robot, scene,
                                       built.value().atlas, current, proposals, options);
    CHECK(report);
    CHECK(report.value().profile_id == profile.value().id());
    CHECK(report.value().applications.size() == 2);
    CHECK(report.value().applications[0].raw_metadata.confidence == 0.4);
    CHECK(report.value().applications[0].effective_metadata.confidence < 0.4);
    CHECK(report.value().applications[1].effective_metadata.confidence > 0.7);
    CHECK(report.value().applications[0].id.size() == 64);
    CHECK(report.value().policy_report.decisions[0].reason == PolicyGateReason::ConfidenceBelowMinimum);
    CHECK(report.value().policy_report.decisions[1].reason == PolicyGateReason::ShieldAccepted);
    CHECK(report.value().policy_report.selected_index == 1);
    CHECK(report.value().policy_report.decisions[1].evidence == EvidenceLevel::CertifiedConnectivity);
    CHECK(gate.telemetry().batches == 1);
    gate.reset_telemetry();
    CHECK(gate.telemetry().batches == 0);

    CHECK(!gate.check_proposals(profile.value(), "wrong-scope", std::string(64, 'a'), robot, scene,
                                built.value().atlas, current, proposals, options));
    CHECK(!gate.check_proposals(profile.value(), "factory-cell-a", std::string(64, 'c'), robot, scene,
                                built.value().atlas, current, proposals, options));
    auto wrong_policy = proposals;
    wrong_policy.front().metadata.policy_id = "other-policy";
    CHECK(!gate.check_proposals(profile.value(), "factory-cell-a", std::string(64, 'a'), robot, scene,
                                built.value().atlas, current, wrong_policy, options));
    options.minimum_total_samples = 1'001;
    CHECK(!gate.check_proposals(profile.value(), "factory-cell-a", std::string(64, 'a'), robot, scene,
                                built.value().atlas, current, proposals, options));
    options.minimum_total_samples = 1'000;
    options.minimum_bin_samples = 501;
    CHECK(!gate.check_proposals(profile.value(), "factory-cell-a", std::string(64, 'a'), robot, scene,
                                built.value().atlas, current, proposals, options));
    options.minimum_bin_samples = 500;
    options.maximum_expected_calibration_error = 0.04;
    CHECK(!gate.check_proposals(profile.value(), "factory-cell-a", std::string(64, 'a'), robot, scene,
                                built.value().atlas, current, proposals, options));
    options.maximum_expected_calibration_error = 0.06;
    options.policy.shield.cancellation.cancel();
    auto cancelled = gate.check_proposals(profile.value(), "factory-cell-a", std::string(64, 'a'), robot,
                                          scene, built.value().atlas, current, proposals, options);
    CHECK(!cancelled);
    CHECK(cancelled.error().code == StatusCode::Cancelled);

    std::error_code error;
    std::filesystem::remove_all(root, error);
    CHECK(!error);
    return EXIT_SUCCESS;
}
