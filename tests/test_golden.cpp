#include "test_support.h"

#include "internal/json.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

const rbfsafe::internal::Json& field(const rbfsafe::internal::Json& object, std::string_view name) {
    if (!object.is_object())
        throw std::runtime_error("golden value is not an object");
    const auto* value = object.find(name);
    if (value == nullptr)
        throw std::runtime_error("missing golden field: " + std::string(name));
    return *value;
}

std::vector<double> numbers(const rbfsafe::internal::Json& array) {
    if (!array.is_array())
        throw std::runtime_error("golden value is not an array");
    std::vector<double> result;
    result.reserve(array.as_array().size());
    for (const auto& value : array.as_array()) {
        if (!value.is_number())
            throw std::runtime_error("golden array member is not numeric");
        result.push_back(value.as_number());
    }
    return result;
}

} // namespace

int main() try {
    using namespace rbfsafe;
    const std::filesystem::path data_dir(RBFSAFE_TEST_DATA_DIR);
    auto document = internal::read_json_file(data_dir / "golden" / "legacy_geometry_v1.json");
    CHECK(document);
    const double fk_tolerance = field(document.value(), "fk_tolerance").as_number();
    const double envelope_tolerance = field(document.value(), "envelope_tolerance").as_number();
    bool envelope_match = true;

    for (const auto& test_case : field(document.value(), "cases").as_array()) {
        const auto model_path = data_dir / field(test_case, "model").as_string();
        auto robot_result = SerialRobotModel::from_json(model_path);
        CHECK(robot_result);
        const auto& robot = robot_result.value();
        const auto configuration = numbers(field(test_case, "configuration"));
        auto fk = robot.forward_kinematics(configuration);
        CHECK(fk);
        const auto& expected_fk = field(test_case, "fk").as_array();
        CHECK(fk.value().size() == expected_fk.size());
        CHECK(robot.link_count() + 1u == expected_fk.size());
        for (std::size_t frame = 0; frame < expected_fk.size(); ++frame) {
            const auto expected = numbers(expected_fk[frame]);
            CHECK(expected.size() == 3u);
            for (std::size_t axis = 0; axis < 3; ++axis) {
                CHECK(close(fk.value()[frame][axis], expected[axis], fk_tolerance));
            }
        }

        std::vector<Interval> axes;
        for (const auto& interval_json : field(test_case, "domain").as_array()) {
            const auto endpoints = numbers(interval_json);
            CHECK(endpoints.size() == 2u);
            axes.push_back({endpoints[0], endpoints[1]});
        }
        const CspaceAabb domain(std::move(axes));
        auto envelope = compute_ifk_aa_link_envelope(robot, domain);
        CHECK(envelope);
        CHECK(envelope.value().links.size() == robot.link_count());

        std::size_t last_active_link = 0;
        for (const auto& active : field(test_case, "active_links").as_array()) {
            const auto index = static_cast<std::size_t>(field(active, "index").as_number());
            const auto bounds = numbers(field(active, "endpoint_bounds"));
            CHECK(index < robot.link_count());
            CHECK(bounds.size() == 12u);
            last_active_link = index;
            const double radius = robot.link_radii()[index];
            const auto& actual = envelope.value().links[index];
            for (std::size_t axis = 0; axis < 3; ++axis) {
                const double expected_lower = std::min(bounds[axis], bounds[6u + axis]) - radius;
                const double expected_upper = std::max(bounds[3u + axis], bounds[9u + axis]) + radius;
                if (!close(actual.lower[axis], expected_lower, envelope_tolerance) ||
                    !close(actual.upper[axis], expected_upper, envelope_tolerance)) {
                    std::cerr << model_path.string() << " link=" << index << " axis=" << axis << " actual=["
                              << actual.lower[axis] << ',' << actual.upper[axis] << "] legacy=["
                              << expected_lower << ',' << expected_upper << "]\n";
                    envelope_match = false;
                }
            }
        }

        IfkAaLinkAabbValidator validator;
        SceneSnapshot far_scene({{"far", {{10.0, 10.0, 10.0}, {11.0, 11.0, 11.0}}}}, "golden-far");
        auto certified = validator.validate(robot, far_scene, domain);
        CHECK(certified);
        CHECK(certified.value().disposition == ValidationDisposition::CertifiedFree);

        const auto& point = fk.value()[last_active_link + 1u];
        SceneSnapshot blocked_scene({{"block",
                                      {{point[0] - 1e-4, point[1] - 1e-4, point[2] - 1e-4},
                                       {point[0] + 1e-4, point[1] + 1e-4, point[2] + 1e-4}}}},
                                    "golden-blocked");
        auto blocked = validator.validate(robot, blocked_scene, domain);
        CHECK(blocked);
        CHECK(blocked.value().disposition == ValidationDisposition::Undetermined);
    }
    CHECK(envelope_match);
    return EXIT_SUCCESS;
} catch (const std::exception& exception) {
    std::cerr << "golden fixture error: " << exception.what() << '\n';
    return EXIT_FAILURE;
}
