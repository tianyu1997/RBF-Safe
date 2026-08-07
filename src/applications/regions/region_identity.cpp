#include "internal/region_identity.h"

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <utility>

namespace rbfsafe::internal {
namespace {

Json configuration_json(std::span<const double> configuration) {
    Json::Array values;
    values.reserve(configuration.size());
    for (const double value : configuration)
        values.emplace_back(value);
    return values;
}

Json id_array(std::span<const RegionId> ids) {
    Json::Array values;
    values.reserve(ids.size());
    for (const auto id : ids)
        values.emplace_back(std::to_string(id));
    return values;
}

Json configuration_array(const std::vector<Configuration>& configurations) {
    Json::Array values;
    values.reserve(configurations.size());
    for (const auto& configuration : configurations)
        values.emplace_back(configuration_json(configuration));
    return values;
}

} // namespace

std::uint64_t digest_prefix_id(const std::string& digest) {
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < 16 && index < digest.size(); ++index) {
        const char digit = digest[index];
        const unsigned value = digit >= '0' && digit <= '9' ? static_cast<unsigned>(digit - '0')
                                                            : static_cast<unsigned>(digit - 'a' + 10);
        result = (result << 4U) | value;
    }
    return result == 0 ? 1 : result;
}

std::string obb_subject_digest(const CspaceObb& region) {
    Json::Array basis;
    basis.reserve(region.basis().size());
    for (const double value : region.basis())
        basis.emplace_back(value);
    return sha256(Json(Json::Object{
                           {"basis", std::move(basis)},
                           {"center", configuration_json(region.center())},
                           {"half_widths", configuration_json(region.half_widths())},
                           {"type", "cspace-obb"},
                       })
                      .dump(false));
}

std::string portal_subject_digest(RegionId left, RegionId right, const CspacePortal& portal) {
    Json::Array normals;
    Json::Array offsets;
    normals.reserve(portal.normals().size());
    offsets.reserve(portal.offsets().size());
    for (const double value : portal.normals())
        normals.emplace_back(value);
    for (const double value : portal.offsets())
        offsets.emplace_back(value);
    return sha256(Json(Json::Object{
                           {"left_region", std::to_string(left)},
                           {"normals", std::move(normals)},
                           {"offsets", std::move(offsets)},
                           {"right_region", std::to_string(right)},
                           {"type", "cspace-portal-polytope"},
                           {"witness", configuration_json(portal.witness())},
                       })
                      .dump(false));
}

std::string trajectory_tube_subject_digest(const TrajectoryTubeGeometry& tube) {
    return sha256(Json(Json::Object{
                           {"cell_ids", id_array(tube.cell_ids)},
                           {"centerline", configuration_array(tube.centerline)},
                           {"portal_ids", id_array(tube.portal_ids)},
                           {"type", "trajectory-tube"},
                       })
                      .dump(false));
}

std::string zonotope_subject_digest(const CspaceZonotope& region) {
    Json::Array generators;
    generators.reserve(region.generators().size());
    for (const double value : region.generators())
        generators.emplace_back(value);
    return sha256(Json(Json::Object{
                           {"center", configuration_json(region.center())},
                           {"generator_count", std::to_string(region.generator_count())},
                           {"generators", std::move(generators)},
                           {"type", "cspace-zonotope"},
                       })
                      .dump(false));
}

std::string taylor_region_subject_digest(const CspaceTaylorRegion& region) {
    Json::Array linear;
    linear.reserve(region.linear().size());
    for (const double value : region.linear())
        linear.emplace_back(value);
    return sha256(Json(Json::Object{
                           {"center", configuration_json(region.center())},
                           {"linear", std::move(linear)},
                           {"remainder_radii", configuration_json(region.remainder_radii())},
                           {"type", "cspace-taylor1"},
                           {"variable_count", std::to_string(region.variable_count())},
                       })
                      .dump(false));
}

Result<std::string> primary_region_subject_digest(const RegionGeometry& geometry) {
    switch (region_type(geometry)) {
    case RegionType::Aabb:
        return cspace_aabb_subject_digest(std::get<CspaceAabb>(geometry));
    case RegionType::Obb:
        return obb_subject_digest(std::get<CspaceObb>(geometry));
    case RegionType::Zonotope:
        return zonotope_subject_digest(std::get<CspaceZonotope>(geometry));
    case RegionType::Taylor:
        return taylor_region_subject_digest(std::get<CspaceTaylorRegion>(geometry));
    case RegionType::Portal:
    case RegionType::TrajectoryTube:
        return Result<std::string>::failure(StatusCode::InvalidArgument,
                                            "only primary region geometry has an independent subject digest");
    }
    return Result<std::string>::failure(StatusCode::InvalidArgument, "unknown region geometry type");
}

std::string region_record_identity(const std::string& robot_digest, const std::string& scene_digest,
                                   RegionType type, const std::string& subject_digest,
                                   std::string_view source) {
    return sha256(Json(Json::Object{
                           {"robot_digest", robot_digest},
                           {"scene_digest", scene_digest},
                           {"source", std::string(source)},
                           {"subject_digest", subject_digest},
                           {"type", region_type_name(type)},
                       })
                      .dump(false));
}

} // namespace rbfsafe::internal
