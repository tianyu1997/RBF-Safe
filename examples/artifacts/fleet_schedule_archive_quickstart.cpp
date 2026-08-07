#include <rbfsafe/rbfsafe.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string digest(char value) { return std::string(64, value); }

rbfsafe::WorkspaceAabb box(double lower, double upper) { return {{lower, -0.1, -0.1}, {upper, 0.1, 0.1}}; }

rbfsafe::MemoryArtifactInput artifact(std::string deployment_id, std::string robot_digest,
                                      std::string content_digest) {
    rbfsafe::MemoryArtifactInput input;
    input.type = rbfsafe::MemoryArtifactType::SafeAtlas;
    input.deployment_id = std::move(deployment_id);
    input.robot_digest = std::move(robot_digest);
    input.scene_digest = digest('c');
    input.task_id = "coordinated-pick";
    input.content_digest = std::move(content_digest);
    input.locator = "artifacts/" + input.deployment_id;
    input.evidence = rbfsafe::EvidenceLevel::CertifiedRegion;
    return input;
}

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;
    if (argc != 2) {
        std::cerr << "usage: rbfsafe_fleet_schedule_archive_quickstart <new-archive-directory>\n";
        return 2;
    }

    const auto robot_a = digest('a');
    const auto robot_b = digest('b');
    SafetyMemory memory;
    auto source_a = memory.register_artifact(artifact("arm-a", robot_a, digest('1')));
    auto source_b = memory.register_artifact(artifact("arm-b", robot_b, digest('2')));
    if (!source_a || !source_b) {
        std::cerr << (!source_a ? source_a.error().describe() : source_b.error().describe()) << '\n';
        return 1;
    }

    std::vector<FleetMember> members{{"arm-a", robot_a, box(-2.0, 2.0)}, {"arm-b", robot_b, box(-2.0, 2.0)}};
    auto fleet = make_fleet_snapshot("cell-1", digest('c'), std::move(members));
    if (!fleet) {
        std::cerr << fleet.error().describe() << '\n';
        return 1;
    }
    auto reservation_a = make_fleet_reservation(fleet.value(), memory, "arm-a", source_a.value().id,
                                                box(-1.0, -0.8), 0, 10, 0.05);
    auto clear_b = make_fleet_reservation(fleet.value(), memory, "arm-b", source_b.value().id, box(0.8, 1.0),
                                          0, 10, 0.05);
    auto colliding_b =
        make_fleet_reservation(fleet.value(), memory, "arm-b", source_b.value().id, box(-0.9, -0.7), 0, 10);
    if (!reservation_a || !clear_b || !colliding_b) {
        std::cerr << "reservation construction failed\n";
        return 1;
    }

    auto archive = FleetScheduleArchive::create("cell-1");
    if (!archive) {
        std::cerr << archive.error().describe() << '\n';
        return 1;
    }
    const std::vector clear{clear_b.value(), reservation_a.value()};
    auto root = archive.value().publish(fleet.value(), memory, clear, "");
    if (!root) {
        std::cerr << root.error().describe() << '\n';
        return 1;
    }
    const std::vector conflict{reservation_a.value(), colliding_b.value()};
    auto current = archive.value().publish(fleet.value(), memory, conflict, root.value().id);
    if (!current) {
        std::cerr << current.error().describe() << '\n';
        return 1;
    }
    auto saved = archive.value().save(std::filesystem::path(argv[1]));
    if (!saved) {
        std::cerr << saved.error().describe() << '\n';
        return 1;
    }

    std::cout << "root=" << root.value().id << '\n'
              << "current=" << current.value().id << '\n'
              << "versions=" << archive.value().versions().size() << '\n'
              << "status=" << fleet_schedule_status_name(current.value().report.status) << '\n'
              << "conflicts=" << current.value().report.conflicts.size() << '\n';
    return 0;
}
