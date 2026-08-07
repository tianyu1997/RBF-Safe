#include "internal/workspace_envelope_json.h"

#include <cmath>
#include <string>
#include <utility>

namespace rbfsafe::internal {
namespace {

Json point_json(const WorkspacePoint& point) { return Json::Array{point[0], point[1], point[2]}; }

Result<WorkspacePoint> decode_point(const Json& value, std::string context) {
    if (!value.is_array() || value.as_array().size() != 3) {
        return Result<WorkspacePoint>::failure(
            StatusCode::CorruptData, "workspace point must be a length-three array", std::move(context));
    }
    WorkspacePoint result;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!value.as_array()[axis].is_number() || !std::isfinite(value.as_array()[axis].as_number())) {
            return Result<WorkspacePoint>::failure(
                StatusCode::CorruptData, "workspace point coordinate must be finite", std::move(context));
        }
        result[axis] = value.as_array()[axis].as_number();
    }
    return result;
}

Result<std::vector<WorkspacePoint>> decode_points(const Json* value, std::size_t maximum,
                                                  std::string context) {
    if (value == nullptr || !value->is_array() || value->as_array().empty() ||
        value->as_array().size() > maximum) {
        return Result<std::vector<WorkspacePoint>>::failure(
            StatusCode::CorruptData, "workspace point array is invalid", std::move(context));
    }
    std::vector<WorkspacePoint> result;
    result.reserve(value->as_array().size());
    for (const auto& item : value->as_array()) {
        auto point = decode_point(item, context);
        if (!point)
            return point.error();
        result.push_back(point.value());
    }
    return result;
}

Result<WorkspaceEnvelope> decode_aabb(const Json& value) {
    const auto* lower = value.find("lower");
    const auto* upper = value.find("upper");
    if (lower == nullptr || upper == nullptr) {
        return Result<WorkspaceEnvelope>::failure(StatusCode::CorruptData,
                                                  "workspace AABB bounds are missing");
    }
    auto decoded_lower = decode_point(*lower, "lower");
    auto decoded_upper = decode_point(*upper, "upper");
    if (!decoded_lower)
        return decoded_lower.error();
    if (!decoded_upper)
        return decoded_upper.error();
    WorkspaceAabb box{decoded_lower.value(), decoded_upper.value()};
    if (!box.valid())
        return Result<WorkspaceEnvelope>::failure(StatusCode::CorruptData, "workspace AABB is invalid");
    return WorkspaceEnvelope(box);
}

} // namespace

Json workspace_envelope_json(const WorkspaceEnvelope& envelope, bool include_type) {
    Json::Object result;
    if (include_type)
        result.emplace("type", workspace_envelope_type_name(envelope.type()));
    if (const auto* aabb = envelope.aabb()) {
        result.emplace("lower", point_json(aabb->lower));
        result.emplace("upper", point_json(aabb->upper));
    } else if (const auto* obb = envelope.obb()) {
        Json::Array basis;
        for (double value : obb->basis())
            basis.emplace_back(value);
        result.emplace("basis", std::move(basis));
        result.emplace("center", point_json(obb->center()));
        result.emplace("half_widths", point_json(obb->half_widths()));
    } else if (const auto* kdop = envelope.kdop()) {
        Json::Array directions;
        Json::Array projections;
        for (std::size_t index = 0; index < kdop->directions().size(); ++index) {
            directions.emplace_back(point_json(kdop->directions()[index]));
            projections.emplace_back(
                Json::Array{kdop->projections()[index].lower, kdop->projections()[index].upper});
        }
        result.emplace("directions", std::move(directions));
        result.emplace("projections", std::move(projections));
    } else if (const auto* hull = envelope.support_hull()) {
        Json::Array points;
        for (const auto& point : hull->points())
            points.emplace_back(point_json(point));
        result.emplace("points", std::move(points));
        result.emplace("radius", hull->radius());
    }
    return result;
}

Result<WorkspaceEnvelope> decode_workspace_envelope(const Json& value, bool typed) {
    if (!value.is_object())
        return Result<WorkspaceEnvelope>::failure(StatusCode::CorruptData,
                                                  "workspace envelope must be an object");
    if (!typed)
        return decode_aabb(value);
    const auto* type = value.find("type");
    if (type == nullptr || !type->is_string()) {
        return Result<WorkspaceEnvelope>::failure(StatusCode::CorruptData,
                                                  "workspace envelope type is missing");
    }
    if (type->as_string() == "aabb")
        return decode_aabb(value);
    if (type->as_string() == "obb") {
        const auto* center = value.find("center");
        const auto* half_widths = value.find("half_widths");
        const auto* basis = value.find("basis");
        if (center == nullptr || half_widths == nullptr || basis == nullptr || !basis->is_array() ||
            basis->as_array().size() != 9) {
            return Result<WorkspaceEnvelope>::failure(StatusCode::CorruptData,
                                                      "workspace OBB fields are invalid");
        }
        auto decoded_center = decode_point(*center, "center");
        auto decoded_widths = decode_point(*half_widths, "half_widths");
        if (!decoded_center)
            return decoded_center.error();
        if (!decoded_widths)
            return decoded_widths.error();
        std::array<double, 9> decoded_basis;
        for (std::size_t index = 0; index < decoded_basis.size(); ++index) {
            if (!basis->as_array()[index].is_number() ||
                !std::isfinite(basis->as_array()[index].as_number())) {
                return Result<WorkspaceEnvelope>::failure(StatusCode::CorruptData,
                                                          "workspace OBB basis is invalid");
            }
            decoded_basis[index] = basis->as_array()[index].as_number();
        }
        auto decoded_box =
            WorkspaceObb::create(decoded_center.value(), decoded_basis, decoded_widths.value());
        if (!decoded_box)
            return Result<WorkspaceEnvelope>::failure(StatusCode::CorruptData, decoded_box.error().message);
        return WorkspaceEnvelope(std::move(decoded_box).value());
    }
    if (type->as_string() == "kdop") {
        auto directions = decode_points(value.find("directions"), 64, "directions");
        const auto* projections_json = value.find("projections");
        if (!directions)
            return directions.error();
        if (projections_json == nullptr || !projections_json->is_array() ||
            projections_json->as_array().size() != directions.value().size()) {
            return Result<WorkspaceEnvelope>::failure(StatusCode::CorruptData,
                                                      "workspace k-DOP projections are invalid");
        }
        std::vector<Interval> projections;
        projections.reserve(projections_json->as_array().size());
        for (const auto& item : projections_json->as_array()) {
            if (!item.is_array() || item.as_array().size() != 2 || !item.as_array()[0].is_number() ||
                !item.as_array()[1].is_number()) {
                return Result<WorkspaceEnvelope>::failure(StatusCode::CorruptData,
                                                          "workspace k-DOP projection is invalid");
            }
            projections.emplace_back(item.as_array()[0].as_number(), item.as_array()[1].as_number());
        }
        auto kdop = WorkspaceKdop::create(std::move(directions).value(), std::move(projections));
        if (!kdop)
            return Result<WorkspaceEnvelope>::failure(StatusCode::CorruptData, kdop.error().message);
        return WorkspaceEnvelope(std::move(kdop).value());
    }
    if (type->as_string() == "support_hull") {
        auto points = decode_points(value.find("points"), 100'000, "points");
        const auto* radius = value.find("radius");
        if (!points)
            return points.error();
        if (radius == nullptr || !radius->is_number() || !std::isfinite(radius->as_number())) {
            return Result<WorkspaceEnvelope>::failure(StatusCode::CorruptData,
                                                      "workspace support-hull radius is invalid");
        }
        auto hull = WorkspaceSupportHull::create(std::move(points).value(), radius->as_number());
        if (!hull)
            return Result<WorkspaceEnvelope>::failure(StatusCode::CorruptData, hull.error().message);
        return WorkspaceEnvelope(std::move(hull).value());
    }
    return Result<WorkspaceEnvelope>::failure(StatusCode::CorruptData, "unknown workspace envelope type",
                                              type->as_string());
}

} // namespace rbfsafe::internal
