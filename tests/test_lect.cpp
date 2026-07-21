#include "test_support.h"

#include <chrono>
#include <filesystem>

int main() {
    using namespace rbfsafe;
    auto created = LectTree::create(CspaceAabb({{-1.0, 1.0}, {-2.0, 2.0}}));
    CHECK(created);
    auto tree = std::move(created).value();
    CHECK(tree.size() == 1);
    auto root_children = tree.split(tree.root_key());
    CHECK(root_children);
    CHECK(root_children.value().first.path() == "0");
    CHECK(root_children.value().second.path() == "1");
    auto left_children = tree.split(root_children.value().first);
    CHECK(left_children);
    CHECK(tree.size() == 5);
    auto located = tree.locate(Configuration{-0.8, 1.0});
    CHECK(located);
    CHECK(located.value().path() == "01");
    auto overlaps = tree.overlap_leaves(CspaceAabb({{-0.1, 0.1}, {-0.1, 0.1}}));
    CHECK(overlaps);
    CHECK(overlaps.value().size() >= 2);

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() / ("rbfsafe-lect-test-" + std::to_string(nonce));
    CHECK(tree.save(path));
    auto snapshot = LectSnapshot::open(path);
    CHECK(snapshot);
    CHECK(snapshot.value().size() == tree.size());
    auto restored = snapshot.value().locate(Configuration{-0.8, 1.0});
    CHECK(restored);
    CHECK(restored.value() == located.value());
    CHECK(!tree.save(path));
    std::error_code error;
    std::filesystem::remove_all(path, error);
    CHECK(!error);
    return EXIT_SUCCESS;
}
