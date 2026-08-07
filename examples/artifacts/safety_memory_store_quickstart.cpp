#include <rbfsafe/rbfsafe.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::string digest(char value) { return std::string(64, value); }

rbfsafe::MemoryArtifactInput artifact() {
    rbfsafe::MemoryArtifactInput result;
    result.type = rbfsafe::MemoryArtifactType::SafeAtlas;
    result.deployment_id = "arm-a";
    result.robot_digest = digest('a');
    result.scene_digest = digest('c');
    result.task_id = "shelf-pick";
    result.content_digest = digest('1');
    result.locator = "artifacts/shelf-atlas";
    result.evidence = rbfsafe::EvidenceLevel::CertifiedRegion;
    result.tags = {"production", "shelf"};
    return result;
}

} // namespace

int main(int argc, char** argv) {
    using namespace rbfsafe;
    if (argc != 2) {
        std::cerr << "usage: rbfsafe_safety_memory_store_quickstart <new-store-directory>\n";
        return 2;
    }
    SafetyMemory memory;
    auto registered = memory.register_artifact(artifact());
    if (!registered) {
        std::cerr << registered.error().describe() << '\n';
        return 1;
    }
    auto store = SafetyMemoryStore::create(std::filesystem::path(argv[1]), memory);
    if (!store) {
        std::cerr << store.error().describe() << '\n';
        return 1;
    }
    const auto root = store.value().current_revision_id();
    auto transitioned = memory.transition(registered.value().id, registered.value().generation,
                                          MemoryArtifactState::Stale, "scene maintenance");
    if (!transitioned) {
        std::cerr << transitioned.error().describe() << '\n';
        return 1;
    }
    auto published = store.value().publish(memory, root);
    if (!published) {
        std::cerr << published.error().describe() << '\n';
        return 1;
    }
    auto current_memory = store.value().load_current();
    if (!current_memory) {
        std::cerr << current_memory.error().describe() << '\n';
        return 1;
    }
    std::cout << "root=" << root << '\n'
              << "current=" << published.value().id << '\n'
              << "revisions=" << store.value().revisions().size() << '\n'
              << "stale=" << current_memory.value().summary().stale << '\n';
    return 0;
}
