#include <rbfsafe/rbfsafe.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string digest(char value) { return std::string(64, value); }

rbfsafe::MemoryArtifactInput atlas_artifact(std::string deployment_id, std::string robot_digest,
                                            std::string scene_digest, std::string content_digest) {
    rbfsafe::MemoryArtifactInput input;
    input.type = rbfsafe::MemoryArtifactType::SafeAtlas;
    input.deployment_id = std::move(deployment_id);
    input.robot_digest = std::move(robot_digest);
    input.scene_digest = std::move(scene_digest);
    input.task_id = "shelf-pick";
    input.content_digest = std::move(content_digest);
    input.locator = "artifacts/shelf-atlas";
    input.evidence = rbfsafe::EvidenceLevel::CertifiedRegion;
    input.tags = {"production", "shelf"};
    return input;
}

rbfsafe::WorkspaceAabb box(double lower, double upper) { return {{lower, -0.1, -0.1}, {upper, 0.1, 0.1}}; }

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;
    const auto scene_digest = digest('c');
    const auto robot_a_digest = digest('a');
    const auto robot_b_digest = digest('b');

    SafetyMemory memory;
    auto arm_a = memory.register_artifact(atlas_artifact("arm-a", robot_a_digest, scene_digest, digest('1')));
    auto arm_b = memory.register_artifact(atlas_artifact("arm-b", robot_b_digest, scene_digest, digest('2')));
    if (!arm_a || !arm_b) {
        std::cerr << (!arm_a ? arm_a.error().describe() : arm_b.error().describe()) << '\n';
        return 1;
    }

    MemoryReuseQuery reuse;
    reuse.deployment_id = "arm-a";
    reuse.robot_digest = robot_a_digest;
    reuse.scene_digest = scene_digest;
    reuse.target_task_id = "shelf-place";
    reuse.minimum_evidence = EvidenceLevel::CertifiedRegion;
    reuse.required_tags = {"production"};
    auto candidates = memory.query_reuse(reuse);
    if (!candidates || candidates.value().empty()) {
        std::cerr << (candidates ? "no reusable artifact" : candidates.error().describe()) << '\n';
        return 1;
    }
    auto recorded = memory.record_reuse(candidates.value().front().artifact.id, reuse,
                                        "shelf-place deployment validation");
    if (!recorded) {
        std::cerr << recorded.error().describe() << '\n';
        return 1;
    }

    auto fleet = make_fleet_snapshot(
        "cell-1", scene_digest,
        {{"arm-a", robot_a_digest, box(-2.0, 2.0)}, {"arm-b", robot_b_digest, box(-2.0, 2.0)}});
    if (!fleet) {
        std::cerr << fleet.error().describe() << '\n';
        return 1;
    }
    auto reservation_a = make_fleet_reservation(fleet.value(), memory, "arm-a", arm_a.value().id,
                                                box(-1.0, -0.8), 0, 10, 0.05);
    auto reservation_b =
        make_fleet_reservation(fleet.value(), memory, "arm-b", arm_b.value().id, box(0.8, 1.0), 0, 10, 0.05);
    if (!reservation_a || !reservation_b) {
        std::cerr << (!reservation_a ? reservation_a.error().describe() : reservation_b.error().describe())
                  << '\n';
        return 1;
    }
    const std::vector<FleetReservation> reservations{reservation_a.value(), reservation_b.value()};
    auto schedule = analyze_fleet_schedule(fleet.value(), memory, reservations);
    if (!schedule) {
        std::cerr << schedule.error().describe() << '\n';
        return 1;
    }

    std::cout << "reuse=" << reuse_disposition_name(candidates.value().front().disposition)
              << " cross_task=" << (candidates.value().front().cross_task ? "true" : "false") << '\n'
              << "schedule=" << fleet_schedule_status_name(schedule.value().status)
              << " conflicts=" << schedule.value().conflicts.size() << '\n';

    if (argc == 2) {
        auto saved = memory.save(std::filesystem::path(argv[1]));
        if (!saved) {
            std::cerr << saved.error().describe() << '\n';
            return 1;
        }
    }
    return 0;
}
