#include "test_support.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
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

    std::string algorithm_name() const override { return "policy-test-split"; }
    std::string algorithm_version() const override { return "1"; }
};

rbfsafe::SerialRobotModel robot_model() {
    return rbfsafe::SerialRobotModel("policy-1r", {{0.0, 1.0, 0.0, 0.0, rbfsafe::JointType::Revolute}},
                                     {{-1.0, 1.0}}, {0.02});
}

rbfsafe::PolicyProposalMetadata metadata(std::uint64_t sequence, double confidence = 0.9) {
    rbfsafe::PolicyProposalMetadata result;
    result.policy_id = "vla-policy-a";
    result.task_id = "shelf-pick";
    result.episode_id = "episode-7";
    result.sequence = sequence;
    result.confidence = confidence;
    result.state_uncertainty = 0.1;
    result.action_uncertainty = 0.1;
    result.observation_age_seconds = 0.01;
    result.inference_latency_seconds = 0.02;
    return result;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

} // namespace

int main() {
    using namespace rbfsafe;
    const auto robot = robot_model();
    const SceneSnapshot scene({}, "policy-empty-v1");
    AtlasBuilder builder(std::make_shared<SplitValidator>());
    auto built = builder.build(robot, scene, {{-0.5}, {0.5}});
    CHECK(built);
    const Configuration current{-0.5};

    std::vector<PolicyProposal> proposals;
    auto low_confidence = metadata(0, 0.4);
    proposals.push_back({JointDeltaAction{{0.1}}, low_confidence});
    auto high_state_uncertainty = metadata(1);
    high_state_uncertainty.state_uncertainty = 0.6;
    proposals.push_back({JointDeltaAction{{0.1}}, high_state_uncertainty});
    auto high_action_uncertainty = metadata(2);
    high_action_uncertainty.action_uncertainty = 0.6;
    proposals.push_back({JointDeltaAction{{0.1}}, high_action_uncertainty});
    auto stale_observation = metadata(3);
    stale_observation.observation_age_seconds = 0.2;
    proposals.push_back({JointDeltaAction{{0.1}}, stale_observation});
    auto slow_inference = metadata(4);
    slow_inference.inference_latency_seconds = 0.2;
    proposals.push_back({JointDeltaAction{{0.1}}, slow_inference});
    auto accepted_lower_confidence = metadata(5, 0.8);
    accepted_lower_confidence.state_uncertainty = 0.05;
    accepted_lower_confidence.action_uncertainty = 0.05;
    proposals.push_back({JointDeltaAction{{0.1}}, accepted_lower_confidence});
    auto accepted_higher_confidence = metadata(6, 0.95);
    accepted_higher_confidence.state_uncertainty = 0.2;
    accepted_higher_confidence.action_uncertainty = 0.2;
    proposals.push_back({JointDeltaAction{{0.2}}, accepted_higher_confidence});
    proposals.push_back({JointDeltaAction{{2.0}}, metadata(7, 0.99)});
    proposals.push_back({JointDeltaAction{{3.0}}, metadata(8, 1.0)});

    PolicyGateOptions options;
    options.minimum_confidence = 0.5;
    options.maximum_state_uncertainty = 0.5;
    options.maximum_action_uncertainty = 0.5;
    options.maximum_observation_age_seconds = 0.1;
    options.maximum_inference_latency_seconds = 0.1;
    options.maximum_proposals = 16;
    options.selection_mode = PolicySelectionMode::HighestConfidence;
    options.shield.maximum_waypoint_repair_distance = 0.6;
    options.shield.maximum_total_repair_distance = 2.0;

    LearningPolicySafetyGate gate;
    auto report = gate.check_proposals(robot, scene, built.value().atlas, current, proposals, options);
    CHECK(report);
    CHECK(report.value().decisions.size() == proposals.size());
    CHECK(report.value().feedback.size() == proposals.size());
    CHECK(report.value().selected_index == 6);
    CHECK(report.value().decisions[0].reason == PolicyGateReason::ConfidenceBelowMinimum);
    CHECK(report.value().decisions[1].reason == PolicyGateReason::StateUncertaintyExceeded);
    CHECK(report.value().decisions[2].reason == PolicyGateReason::ActionUncertaintyExceeded);
    CHECK(report.value().decisions[3].reason == PolicyGateReason::ObservationTooOld);
    CHECK(report.value().decisions[4].reason == PolicyGateReason::InferenceLatencyExceeded);
    CHECK(report.value().decisions[5].reason == PolicyGateReason::ShieldAccepted);
    CHECK(report.value().decisions[6].selected);
    CHECK(report.value().decisions[6].shield_decision->outcome == ShieldOutcome::Accept);
    CHECK(report.value().decisions[7].shield_decision->outcome == ShieldOutcome::Repair);
    CHECK(report.value().decisions[8].reason == PolicyGateReason::ShieldRejected);
    CHECK(report.value().decisions[8].shield_decision->outcome == ShieldOutcome::Reject);
    CHECK(report.value().feedback[0].label == PolicyFeedbackLabel::PolicyRejected);
    CHECK(report.value().feedback[5].label == PolicyFeedbackLabel::EligibleNotSelected);
    CHECK(report.value().feedback[6].label == PolicyFeedbackLabel::SelectedAccepted);
    CHECK(report.value().feedback[7].label == PolicyFeedbackLabel::EligibleNotSelected);
    CHECK(report.value().feedback[8].label == PolicyFeedbackLabel::ShieldRejected);
    CHECK(report.value().feedback[7].repair_distance > 0.0);
    for (const auto& decision : report.value().decisions) {
        CHECK(decision.id.size() == 64);
        CHECK(decision.proposal_id.size() == 64);
        CHECK(decision.evidence != EvidenceLevel::RuntimeExecutable);
    }
    for (const auto& feedback : report.value().feedback) {
        CHECK(feedback.id.size() == 64);
        CHECK(feedback.evidence != EvidenceLevel::RuntimeExecutable);
    }

    auto repeated = gate.check_proposals(robot, scene, built.value().atlas, current, proposals, options);
    CHECK(repeated);
    for (std::size_t index = 0; index < proposals.size(); ++index) {
        CHECK(repeated.value().decisions[index].id == report.value().decisions[index].id);
        CHECK(repeated.value().feedback[index].id == report.value().feedback[index].id);
    }

    options.selection_mode = PolicySelectionMode::InputOrder;
    auto input_order = gate.check_proposals(robot, scene, built.value().atlas, current, proposals, options);
    CHECK(input_order);
    CHECK(input_order.value().selected_index == 5);
    options.selection_mode = PolicySelectionMode::LowestUncertainty;
    auto lowest_uncertainty =
        gate.check_proposals(robot, scene, built.value().atlas, current, proposals, options);
    CHECK(lowest_uncertainty);
    CHECK(lowest_uncertainty.value().selected_index == 5);
    auto repair_only = gate.check_proposals(robot, scene, built.value().atlas, current,
                                            std::span<const PolicyProposal>(&proposals[7], 1), options);
    CHECK(repair_only);
    CHECK(repair_only.value().selected_index == 0);
    CHECK(repair_only.value().decisions[0].reason == PolicyGateReason::ShieldRepaired);
    CHECK(repair_only.value().feedback[0].label == PolicyFeedbackLabel::SelectedRepaired);
    auto rejected_only = gate.check_proposals(robot, scene, built.value().atlas, current,
                                              std::span<const PolicyProposal>(&proposals[0], 1), options);
    CHECK(rejected_only);
    CHECK(!rejected_only.value().selected_index);
    CHECK(rejected_only.value().feedback[0].label == PolicyFeedbackLabel::PolicyRejected);

    gate.reset_telemetry();
    auto telemetry_report =
        gate.check_proposals(robot, scene, built.value().atlas, current, proposals, options);
    CHECK(telemetry_report);
    const auto telemetry = gate.telemetry();
    CHECK(telemetry.batches == 1);
    CHECK(telemetry.proposals == 9);
    CHECK(telemetry.policy_rejections == 5);
    CHECK(telemetry.shield_checks == 4);
    CHECK(telemetry.shield_accepts == 2);
    CHECK(telemetry.shield_repairs == 1);
    CHECK(telemetry.shield_rejections == 1);
    CHECK(telemetry.selected_accepts == 1);

    PolicyFeedbackDatabase database;
    CHECK(database.append(report.value().feedback));
    CHECK(database.valid());
    CHECK(!database.append(report.value().feedback));
    const auto summary = database.summary();
    CHECK(summary.records == 9);
    CHECK(summary.selected_accepted == 1);
    CHECK(summary.eligible_not_selected == 2);
    CHECK(summary.policy_rejected == 5);
    CHECK(summary.shield_rejected == 1);
    PolicyFeedbackQuery query;
    query.policy_id = "vla-policy-a";
    query.task_id = "shelf-pick";
    query.label = PolicyFeedbackLabel::PolicyRejected;
    auto queried = database.query(query);
    CHECK(queried);
    CHECK(queried.value().size() == 5);
    query.maximum_results = 2;
    queried = database.query(query);
    CHECK(queried);
    CHECK(queried.value().size() == 2);
    query.maximum_results = 0;
    CHECK(!database.query(query));

    auto invalid_record = report.value().feedback.front();
    invalid_record.evidence = EvidenceLevel::RuntimeExecutable;
    CHECK(!PolicyFeedbackDatabase::create({invalid_record}));

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root =
        std::filesystem::temp_directory_path() / ("rbfsafe-policy-test-" + std::to_string(nonce));
    const auto directory = root / "feedback";
    const auto repeated_directory = root / "feedback-repeated";
    CHECK(database.save(directory));
    CHECK(!database.save(directory));
    CHECK(database.save(directory, SaveOptions{true}));
    CHECK(database.save(repeated_directory));
    CHECK(read_text(directory / "manifest.json") == read_text(repeated_directory / "manifest.json"));
    CHECK(read_text(directory / "records.json") == read_text(repeated_directory / "records.json"));
    auto loaded = PolicyFeedbackDatabase::load(directory);
    CHECK(loaded);
    CHECK(loaded.value().records().size() == database.records().size());
    CHECK(loaded.value().records()[6].id == database.records()[6].id);

    PolicyFeedbackLoadOptions tiny_load;
    tiny_load.maximum_records = 2;
    auto limited = PolicyFeedbackDatabase::load(directory, tiny_load);
    CHECK(!limited);
    CHECK(limited.error().code == StatusCode::ResourceLimit);
    tiny_load.maximum_records = 100;
    tiny_load.maximum_payload_bytes = 1;
    limited = PolicyFeedbackDatabase::load(directory, tiny_load);
    CHECK(!limited);
    CHECK(limited.error().code == StatusCode::ResourceLimit);
    tiny_load.maximum_records = 0;
    CHECK(!PolicyFeedbackDatabase::load(directory, tiny_load));

    const auto unknown_schema = root / "unknown-schema";
    std::filesystem::copy(directory, unknown_schema, std::filesystem::copy_options::recursive);
    auto manifest = read_text(unknown_schema / "manifest.json");
    const auto schema_position = manifest.find("\"schema\": 1");
    CHECK(schema_position != std::string::npos);
    manifest.replace(schema_position, std::string("\"schema\": 1").size(), "\"schema\": 99");
    write_text(unknown_schema / "manifest.json", manifest);
    auto unknown = PolicyFeedbackDatabase::load(unknown_schema);
    CHECK(!unknown);
    CHECK(unknown.error().code == StatusCode::IncompatibleFormat);

    auto corrupted_payload = read_text(directory / "records.json");
    CHECK(!corrupted_payload.empty());
    corrupted_payload[corrupted_payload.size() / 2] =
        corrupted_payload[corrupted_payload.size() / 2] == '0' ? '1' : '0';
    write_text(directory / "records.json", corrupted_payload);
    auto corrupted = PolicyFeedbackDatabase::load(directory);
    CHECK(!corrupted);
    CHECK(corrupted.error().code == StatusCode::CorruptData);

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    CHECK(!cleanup_error);

    PolicyGateOptions invalid_options = options;
    invalid_options.maximum_proposals = 0;
    CHECK(!gate.check_proposals(robot, scene, built.value().atlas, current, proposals, invalid_options));
    PolicyGateOptions limited_options = options;
    limited_options.maximum_proposals = 1;
    auto over_budget =
        gate.check_proposals(robot, scene, built.value().atlas, current, proposals, limited_options);
    CHECK(!over_budget);
    CHECK(over_budget.error().code == StatusCode::ResourceLimit);
    PolicyGateOptions cancelled_options = options;
    cancelled_options.shield.cancellation.cancel();
    auto cancelled =
        gate.check_proposals(robot, scene, built.value().atlas, current, proposals, cancelled_options);
    CHECK(!cancelled);
    CHECK(cancelled.error().code == StatusCode::Cancelled);
    auto malformed = proposals;
    malformed.front().metadata.confidence = std::numeric_limits<double>::quiet_NaN();
    CHECK(!gate.check_proposals(robot, scene, built.value().atlas, current, malformed, options));
    malformed = proposals;
    malformed.front().metadata.confidence = 0.0;
    malformed.front().action = JointDeltaAction{{std::numeric_limits<double>::infinity()}};
    CHECK(!gate.check_proposals(robot, scene, built.value().atlas, current, malformed, options));
    auto duplicate = proposals;
    duplicate[1] = duplicate[0];
    CHECK(!gate.check_proposals(robot, scene, built.value().atlas, current, duplicate, options));
    CHECK(!gate.check_proposals(robot, SceneSnapshot({}, "wrong-scene"), built.value().atlas, current,
                                proposals, options));

    CHECK(policy_selection_mode_name(PolicySelectionMode::HighestConfidence) == "highest_confidence");
    CHECK(policy_gate_reason_name(PolicyGateReason::ObservationTooOld) == "observation_too_old");
    CHECK(policy_feedback_label_name(PolicyFeedbackLabel::SelectedRepaired) == "selected_repaired");
    return EXIT_SUCCESS;
}
