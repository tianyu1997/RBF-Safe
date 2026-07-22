#include <rbfsafe/region_database.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rbfsafe {
namespace {

constexpr std::size_t kMaximumPortalIterations = 1'000'000;

double dot(std::span<const double> first, std::span<const double> second) {
    double result = 0.0;
    for (std::size_t index = 0; index < first.size(); ++index)
        result += first[index] * second[index];
    return result;
}

void append_obb_constraints(const CspaceObb& box, std::vector<double>& normals,
                            std::vector<double>& offsets) {
    const std::size_t dimension = box.dimension();
    for (std::size_t axis = 0; axis < dimension; ++axis) {
        const auto normal = std::span<const double>(box.basis()).subspan(axis * dimension, dimension);
        const double center_projection = dot(normal, box.center());
        normals.insert(normals.end(), normal.begin(), normal.end());
        offsets.push_back(center_projection + box.half_widths()[axis]);
        for (const double value : normal)
            normals.push_back(-value);
        offsets.push_back(-center_projection + box.half_widths()[axis]);
    }
}

std::optional<CspaceAabb> intersect_enclosures(const CspaceAabb& first, const CspaceAabb& second) {
    if (!first.valid() || !second.valid() || first.dimension() != second.dimension())
        return std::nullopt;
    std::vector<Interval> axes;
    axes.reserve(first.dimension());
    for (std::size_t axis = 0; axis < first.dimension(); ++axis) {
        const double lower = std::max(first.axes()[axis].lower, second.axes()[axis].lower);
        const double upper = std::min(first.axes()[axis].upper, second.axes()[axis].upper);
        if (lower > upper)
            return std::nullopt;
        axes.emplace_back(lower, upper);
    }
    return CspaceAabb(std::move(axes));
}

bool finite_configuration(std::span<const double> configuration) {
    return std::all_of(configuration.begin(), configuration.end(),
                       [](double value) { return std::isfinite(value); });
}

} // namespace

Result<std::optional<CspacePortal>> CspacePortal::intersect(const CspaceObb& first, const CspaceObb& second,
                                                            const PortalIntersectionOptions& options) {
    if (!first.valid() || !second.valid()) {
        return Result<std::optional<CspacePortal>>::failure(StatusCode::InvalidArgument,
                                                            "portal OBB is invalid");
    }
    if (first.dimension() != second.dimension()) {
        return Result<std::optional<CspacePortal>>::failure(StatusCode::DimensionMismatch,
                                                            "portal OBB dimensions do not match");
    }
    if (options.maximum_iterations == 0 || options.maximum_iterations > kMaximumPortalIterations ||
        !std::isfinite(options.feasibility_tolerance) || options.feasibility_tolerance < 0.0 ||
        options.feasibility_tolerance > 1e-3) {
        return Result<std::optional<CspacePortal>>::failure(StatusCode::InvalidArgument,
                                                            "invalid portal intersection options");
    }
    if (options.cancellation.cancelled()) {
        return Result<std::optional<CspacePortal>>::failure(StatusCode::Cancelled,
                                                            "portal intersection was cancelled");
    }
    auto enclosure = intersect_enclosures(first.enclosing_aabb(), second.enclosing_aabb());
    if (!enclosure)
        return std::optional<CspacePortal>{};

    CspacePortal portal;
    portal.enclosing_aabb_ = std::move(*enclosure);
    append_obb_constraints(first, portal.normals_, portal.offsets_);
    append_obb_constraints(second, portal.normals_, portal.offsets_);

    std::vector<Configuration> initial_points;
    initial_points.push_back(first.center());
    initial_points.push_back(second.center());
    initial_points.push_back(portal.enclosing_aabb_.center());
    Configuration midpoint(first.dimension(), 0.0);
    for (std::size_t axis = 0; axis < first.dimension(); ++axis)
        midpoint[axis] = 0.5 * (first.center()[axis] + second.center()[axis]);
    initial_points.push_back(std::move(midpoint));

    for (auto candidate : initial_points) {
        for (std::size_t axis = 0; axis < candidate.size(); ++axis) {
            candidate[axis] = std::clamp(candidate[axis], portal.enclosing_aabb_.axes()[axis].lower,
                                         portal.enclosing_aabb_.axes()[axis].upper);
        }
        for (std::size_t iteration = 0; iteration < options.maximum_iterations; ++iteration) {
            if (options.cancellation.cancelled()) {
                return Result<std::optional<CspacePortal>>::failure(StatusCode::Cancelled,
                                                                    "portal intersection was cancelled");
            }
            double maximum_violation = 0.0;
            for (std::size_t constraint = 0; constraint < portal.offsets_.size(); ++constraint) {
                const auto normal = std::span<const double>(portal.normals_)
                                        .subspan(constraint * first.dimension(), first.dimension());
                const double violation = dot(normal, candidate) - portal.offsets_[constraint];
                maximum_violation = std::max(maximum_violation, violation);
                if (violation <= 0.0)
                    continue;
                double norm_squared = dot(normal, normal);
                if (!(norm_squared > 0.0))
                    continue;
                const double correction = violation / norm_squared;
                for (std::size_t axis = 0; axis < candidate.size(); ++axis)
                    candidate[axis] -= correction * normal[axis];
            }
            const bool feasible = maximum_violation <= options.feasibility_tolerance &&
                                  first.contains(candidate, options.feasibility_tolerance) &&
                                  second.contains(candidate, options.feasibility_tolerance);
            if (feasible) {
                portal.witness_ = std::move(candidate);
                if (!portal.valid()) {
                    return Result<std::optional<CspacePortal>>::failure(StatusCode::InternalError,
                                                                        "constructed portal is inconsistent");
                }
                return std::optional<CspacePortal>{std::move(portal)};
            }
        }
    }
    return std::optional<CspacePortal>{};
}

Result<CspacePortal> CspacePortal::create(std::vector<double> normals, std::vector<double> offsets,
                                          Configuration witness, CspaceAabb enclosing_aabb) {
    CspacePortal result;
    result.normals_ = std::move(normals);
    result.offsets_ = std::move(offsets);
    result.witness_ = std::move(witness);
    result.enclosing_aabb_ = std::move(enclosing_aabb);
    if (!result.valid()) {
        return Result<CspacePortal>::failure(StatusCode::InvalidArgument,
                                             "portal half-space record is invalid");
    }
    return result;
}

bool CspacePortal::valid() const noexcept {
    if (witness_.empty() || !finite_configuration(witness_) || !enclosing_aabb_.valid() ||
        enclosing_aabb_.dimension() != dimension() || offsets_.empty() ||
        normals_.size() != offsets_.size() * dimension() ||
        !std::all_of(offsets_.begin(), offsets_.end(), [](double value) { return std::isfinite(value); }) ||
        !std::all_of(normals_.begin(), normals_.end(), [](double value) { return std::isfinite(value); }) ||
        !enclosing_aabb_.contains(witness_, 1e-9)) {
        return false;
    }
    for (std::size_t constraint = 0; constraint < constraint_count(); ++constraint) {
        const auto normal = std::span<const double>(normals_).subspan(constraint * dimension(), dimension());
        double norm_squared = 0.0;
        for (const double value : normal)
            norm_squared += value * value;
        if (!(norm_squared > 0.0))
            return false;
    }
    return contains(witness_, 1e-9);
}

bool CspacePortal::contains(std::span<const double> configuration, double tolerance) const noexcept {
    if (configuration.size() != dimension() || !finite_configuration(configuration) ||
        !std::isfinite(tolerance) || tolerance < 0.0 || normals_.size() != offsets_.size() * dimension()) {
        return false;
    }
    for (std::size_t constraint = 0; constraint < constraint_count(); ++constraint) {
        const auto normal = std::span<const double>(normals_).subspan(constraint * dimension(), dimension());
        const double scale = std::max(1.0, std::abs(offsets_[constraint]));
        if (dot(normal, configuration) > offsets_[constraint] + tolerance * scale)
            return false;
    }
    return true;
}

bool TrajectoryTubeGeometry::valid(std::size_t dimension) const noexcept {
    if (dimension == 0 || cell_ids.empty() || centerline.size() < 2 ||
        portal_ids.size() + 1 != cell_ids.size()) {
        return false;
    }
    if (std::any_of(cell_ids.begin(), cell_ids.end(), [](RegionId id) { return id == 0; }) ||
        std::any_of(portal_ids.begin(), portal_ids.end(), [](RegionId id) { return id == 0; })) {
        return false;
    }
    return std::all_of(centerline.begin(), centerline.end(), [&](const auto& configuration) {
        return configuration.size() == dimension && finite_configuration(configuration);
    });
}

RegionType region_type(const RegionGeometry& geometry) noexcept {
    return static_cast<RegionType>(geometry.index());
}

std::string region_type_name(RegionType type) {
    switch (type) {
    case RegionType::Aabb:
        return "aabb";
    case RegionType::Obb:
        return "obb";
    case RegionType::Portal:
        return "portal";
    case RegionType::TrajectoryTube:
        return "trajectory_tube";
    case RegionType::Zonotope:
        return "zonotope";
    case RegionType::Taylor:
        return "taylor";
    }
    return "unknown";
}

} // namespace rbfsafe
