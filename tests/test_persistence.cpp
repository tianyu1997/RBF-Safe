#include "test_support.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("failed to read test file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("failed to write test file");
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output)
        throw std::runtime_error("failed to complete test write");
}

void replace_once(std::string& text, std::string_view needle, std::string_view replacement) {
    const auto position = text.find(needle);
    if (position == std::string::npos)
        throw std::runtime_error("test manifest field was not found");
    text.replace(position, needle.size(), replacement);
}

} // namespace

int main() try {
    using namespace rbfsafe;
    const auto robot = planar_robot();
    const SceneSnapshot scene({}, "persistence-v1");
    auto built = AtlasBuilder{}.build(robot, scene, {{0.0, 0.0}});
    CHECK(built);

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root =
        std::filesystem::temp_directory_path() / ("rbfsafe-atlas-test-" + std::to_string(nonce));
    const auto path = root / "atlas";
    CHECK(built.value().atlas.save(path));
    CHECK(!built.value().atlas.save(path));
    auto loaded = SafeAtlas::load(path);
    CHECK(loaded);
    CHECK(loaded.value().robot_digest() == robot.digest());
    CHECK(loaded.value().scene_digest() == scene.digest());
    CHECK(loaded.value().regions().size() == built.value().atlas.regions().size());
    CHECK(loaded.value().contains(Configuration{0.0, 0.0}));
    CHECK(built.value().atlas.save(path, SaveOptions{true}));

    // This fixed schema-v1 record is checked on every CI platform. Matching
    // payload hashes imply byte-for-byte little-endian interoperability.
    const auto manifest = read_text(path / "manifest.json");
    CHECK(manifest.find("9261ed7043c63c1ee9c35470c19e2473886e723137c3f6941c5d03fef220df90") !=
          std::string::npos);
    CHECK(manifest.find("d102e92e2620eb355012ffad743727f9ceb81017fb058afe01c0d0c88f71fbd3") !=
          std::string::npos);
    CHECK(manifest.find("9a88166372941a9978b5ff15ad7fb900fdccb7b031e37ece950a0377acf95354") !=
          std::string::npos);
    CHECK(manifest.find("e1fc5e6add42009d3e0c9b68ec50189b01fdd867f75be0e2c699fb37e96d466e") !=
          std::string::npos);

    // Thread count must not affect any persisted byte.
    BuildOptions parallel_options;
    parallel_options.threads = 4;
    auto parallel = AtlasBuilder{}.build(robot, scene, {{0.0, 0.0}}, parallel_options);
    CHECK(parallel);
    const auto parallel_path = root / "parallel";
    CHECK(parallel.value().atlas.save(parallel_path));
    for (const auto& relative :
         {std::filesystem::path("manifest.json"), std::filesystem::path("certificates.json"),
          std::filesystem::path("regions.bin"), std::filesystem::path("graph.bin"),
          std::filesystem::path("lect/nodes.bin")}) {
        CHECK(read_text(path / relative) == read_text(parallel_path / relative));
    }

    const auto unknown_schema = root / "unknown-schema";
    std::filesystem::copy(path, unknown_schema, std::filesystem::copy_options::recursive);
    auto unknown_manifest = read_text(unknown_schema / "manifest.json");
    replace_once(unknown_manifest, "\"schema\": 1", "\"schema\": 99");
    write_text(unknown_schema / "manifest.json", unknown_manifest);
    auto unknown = SafeAtlas::load(unknown_schema);
    CHECK(!unknown);
    CHECK(unknown.error().code == StatusCode::IncompatibleFormat);

    const auto huge_count = root / "huge-count";
    std::filesystem::copy(path, huge_count, std::filesystem::copy_options::recursive);
    auto huge_manifest = read_text(huge_count / "manifest.json");
    replace_once(huge_manifest, "\"regions\": 1", "\"regions\": 10000001");
    write_text(huge_count / "manifest.json", huge_manifest);
    auto huge = SafeAtlas::load(huge_count);
    CHECK(!huge);
    CHECK(huge.error().code == StatusCode::CorruptData);

    const auto wrong_checksum = root / "wrong-checksum";
    std::filesystem::copy(path, wrong_checksum, std::filesystem::copy_options::recursive);
    auto checksum_manifest = read_text(wrong_checksum / "manifest.json");
    replace_once(checksum_manifest, "e1fc5e6add42009d3e0c9b68ec50189b01fdd867f75be0e2c699fb37e96d466e",
                 "01fc5e6add42009d3e0c9b68ec50189b01fdd867f75be0e2c699fb37e96d466e");
    write_text(wrong_checksum / "manifest.json", checksum_manifest);
    auto bad_checksum = SafeAtlas::load(wrong_checksum);
    CHECK(!bad_checksum);
    CHECK(bad_checksum.error().code == StatusCode::CorruptData);

    const auto truncated_path = root / "truncated";
    std::filesystem::copy(path, truncated_path, std::filesystem::copy_options::recursive);
    std::filesystem::resize_file(truncated_path / "regions.bin", 8u);
    auto truncated = SafeAtlas::load(truncated_path);
    CHECK(!truncated);
    CHECK(truncated.error().code == StatusCode::CorruptData);

    // Corrupt a payload without updating the manifest. Loading must fail before
    // any binary record is trusted.
    {
        std::fstream file(path / "regions.bin", std::ios::binary | std::ios::in | std::ios::out);
        CHECK(file);
        file.seekp(0);
        char value = 'X';
        file.write(&value, 1);
    }
    auto corrupted = SafeAtlas::load(path);
    CHECK(!corrupted);
    CHECK(corrupted.error().code == StatusCode::CorruptData);

    std::error_code error;
    std::filesystem::remove_all(root, error);
    CHECK(!error);
    return EXIT_SUCCESS;
} catch (const std::exception& exception) {
    std::cerr << "persistence fixture error: " << exception.what() << '\n';
    return EXIT_FAILURE;
}
