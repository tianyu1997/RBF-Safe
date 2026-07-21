#include <rbfsafe/corridor.h>

#include "internal/certificate_utils.h"
#include "internal/json.h"
#include "internal/sha256.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <functional>
#include <limits>
#include <numeric>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rbfsafe {

Result<void> save_corridor_directory(const HipacCorridor&, const std::filesystem::path&, const SaveOptions&);
Result<HipacCorridor> load_corridor_directory(const std::filesystem::path&);

namespace {

constexpr double orthonormal_tolerance = 1e-10;

internal::Json configuration_json(std::span<const double> configuration) {
    internal::Json::Array values;
    values.reserve(configuration.size());
    for (const double value : configuration)
        values.emplace_back(value);
    return values;
}

std::string obb_subject_digest(const CspaceObb& obb) {
    internal::Json::Array basis;
    basis.reserve(obb.basis().size());
    for (const double value : obb.basis())
        basis.emplace_back(value);
    return internal::sha256(internal::Json(internal::Json::Object{
                                               {"basis", std::move(basis)},
                                               {"center", configuration_json(obb.center())},
                                               {"half_widths", configuration_json(obb.half_widths())},
                                               {"type", "cspace-obb"},
                                           })
                                .dump(false));
}

std::uint64_t digest_prefix_id(const std::string& digest) {
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < 16; ++index) {
        const char digit = digest[index];
        const unsigned value = digit >= '0' && digit <= '9' ? static_cast<unsigned>(digit - '0')
                                                            : static_cast<unsigned>(digit - 'a' + 10);
        result = (result << 4U) | value;
    }
    return result == 0 ? 1 : result;
}

Configuration interpolate(std::span<const double> first, std::span<const double> second, double fraction) {
    Configuration result(first.size());
    for (std::size_t axis = 0; axis < first.size(); ++axis)
        result[axis] = first[axis] + fraction * (second[axis] - first[axis]);
    return result;
}

double squared_distance(std::span<const double> first, std::span<const double> second) {
    double result = 0.0;
    for (std::size_t axis = 0; axis < first.size(); ++axis) {
        const double difference = first[axis] - second[axis];
        result += difference * difference;
    }
    return result;
}

bool same_configuration(std::span<const double> first, std::span<const double> second, double tolerance) {
    if (first.size() != second.size())
        return false;
    for (std::size_t axis = 0; axis < first.size(); ++axis) {
        const double scale = std::max({1.0, std::abs(first[axis]), std::abs(second[axis])});
        if (std::abs(first[axis] - second[axis]) > tolerance * scale)
            return false;
    }
    return true;
}

std::string cell_identity(const std::string& robot_digest, const std::string& scene_digest,
                          const std::string& subject_digest, std::size_t segment_index, double start_fraction,
                          double end_fraction) {
    return internal::sha256(internal::Json(internal::Json::Object{
                                               {"end_fraction", end_fraction},
                                               {"robot_digest", robot_digest},
                                               {"scene_digest", scene_digest},
                                               {"segment_index", std::to_string(segment_index)},
                                               {"start_fraction", start_fraction},
                                               {"subject_digest", subject_digest},
                                           })
                                .dump(false));
}

std::string portal_subject_digest(RegionId left, RegionId right, std::span<const double> witness) {
    return internal::sha256(internal::Json(internal::Json::Object{
                                               {"left_region", std::to_string(left)},
                                               {"right_region", std::to_string(right)},
                                               {"type", "witness-portal"},
                                               {"witness", configuration_json(witness)},
                                           })
                                .dump(false));
}

std::vector<TrajectoryInterval> merge_uncovered(std::vector<TrajectoryInterval> intervals) {
    if (intervals.empty())
        return intervals;
    std::sort(intervals.begin(), intervals.end(), [](const auto& left, const auto& right) {
        if (left.segment_index != right.segment_index)
            return left.segment_index < right.segment_index;
        if (left.start_fraction != right.start_fraction)
            return left.start_fraction < right.start_fraction;
        return left.end_fraction < right.end_fraction;
    });
    std::vector<TrajectoryInterval> merged;
    for (const auto& interval : intervals) {
        if (!merged.empty() && merged.back().segment_index == interval.segment_index &&
            interval.start_fraction <= merged.back().end_fraction + 1e-15) {
            merged.back().end_fraction = std::max(merged.back().end_fraction, interval.end_fraction);
            merged.back().end_included = interval.end_included;
        } else {
            merged.push_back(interval);
        }
    }
    return merged;
}

} // namespace

Result<CspaceObb> CspaceObb::create(Configuration center, std::vector<double> basis,
                                    Configuration half_widths) {
    CspaceObb result;
    result.center_ = std::move(center);
    result.basis_ = std::move(basis);
    result.half_widths_ = std::move(half_widths);
    if (!result.valid()) {
        return Result<CspaceObb>::failure(
            StatusCode::InvalidArgument,
            "C-space OBB requires finite center/extents and an orthonormal row-major basis");
    }
    return result;
}

bool CspaceObb::valid() const noexcept {
    const std::size_t size = dimension();
    if (size == 0 || half_widths_.size() != size || basis_.size() != size * size)
        return false;
    if (!std::all_of(center_.begin(), center_.end(), [](double value) { return std::isfinite(value); }) ||
        !std::all_of(half_widths_.begin(), half_widths_.end(),
                     [](double value) { return std::isfinite(value) && value >= 0.0; }) ||
        !std::all_of(basis_.begin(), basis_.end(), [](double value) { return std::isfinite(value); }))
        return false;
    for (std::size_t left = 0; left < size; ++left) {
        for (std::size_t right = left; right < size; ++right) {
            double dot = 0.0;
            for (std::size_t coordinate = 0; coordinate < size; ++coordinate)
                dot += basis_[left * size + coordinate] * basis_[right * size + coordinate];
            const double expected = left == right ? 1.0 : 0.0;
            if (std::abs(dot - expected) > orthonormal_tolerance * static_cast<double>(size))
                return false;
        }
    }
    return true;
}

bool CspaceObb::contains(std::span<const double> configuration, double tolerance) const noexcept {
    if (!valid() || configuration.size() != dimension() || !std::isfinite(tolerance) || tolerance < 0.0)
        return false;
    for (const double value : configuration)
        if (!std::isfinite(value))
            return false;
    for (std::size_t axis = 0; axis < dimension(); ++axis) {
        double coordinate = 0.0;
        for (std::size_t index = 0; index < dimension(); ++index)
            coordinate += basis_[axis * dimension() + index] * (configuration[index] - center_[index]);
        if (std::abs(coordinate) > half_widths_[axis] + tolerance)
            return false;
    }
    return true;
}

CspaceAabb CspaceObb::enclosing_aabb() const {
    if (!valid())
        return {};
    std::vector<Interval> axes;
    axes.reserve(dimension());
    for (std::size_t coordinate = 0; coordinate < dimension(); ++coordinate) {
        double radius = 0.0;
        for (std::size_t axis = 0; axis < dimension(); ++axis)
            radius += std::abs(basis_[axis * dimension() + coordinate]) * half_widths_[axis];
        axes.emplace_back(
            std::nextafter(center_[coordinate] - radius, -std::numeric_limits<double>::infinity()),
            std::nextafter(center_[coordinate] + radius, std::numeric_limits<double>::infinity()));
    }
    return CspaceAabb(std::move(axes));
}

double CspaceObb::volume() const noexcept {
    if (!valid())
        return 0.0;
    double result = 1.0;
    for (const double half_width : half_widths_)
        result *= 2.0 * half_width;
    return result;
}

Result<CspaceObb> ObbGenerator::segment_tube(std::span<const double> first, std::span<const double> second,
                                             double lateral_half_width, double longitudinal_margin) {
    if (first.empty() || first.size() != second.size()) {
        return Result<CspaceObb>::failure(StatusCode::DimensionMismatch,
                                          "OBB segment endpoints must have equal nonzero dimension");
    }
    if (!std::isfinite(lateral_half_width) || lateral_half_width < 0.0 ||
        !std::isfinite(longitudinal_margin) || longitudinal_margin < 0.0) {
        return Result<CspaceObb>::failure(StatusCode::InvalidArgument,
                                          "OBB segment widths must be finite and non-negative");
    }
    for (std::size_t axis = 0; axis < first.size(); ++axis) {
        if (!std::isfinite(first[axis]) || !std::isfinite(second[axis])) {
            return Result<CspaceObb>::failure(StatusCode::InvalidArgument,
                                              "OBB segment endpoint is non-finite");
        }
    }

    const std::size_t dimension = first.size();
    Configuration center(dimension);
    Configuration direction(dimension);
    double length_squared = 0.0;
    for (std::size_t axis = 0; axis < dimension; ++axis) {
        center[axis] = 0.5 * (first[axis] + second[axis]);
        direction[axis] = second[axis] - first[axis];
        length_squared += direction[axis] * direction[axis];
    }
    const double length = std::sqrt(length_squared);

    std::vector<Configuration> vectors;
    vectors.reserve(dimension);
    if (length > 0.0) {
        for (double& value : direction)
            value /= length;
        vectors.push_back(direction);
    }
    for (std::size_t canonical = 0; canonical < dimension && vectors.size() < dimension; ++canonical) {
        Configuration candidate(dimension, 0.0);
        candidate[canonical] = 1.0;
        for (const auto& existing : vectors) {
            double projection = 0.0;
            for (std::size_t coordinate = 0; coordinate < dimension; ++coordinate)
                projection += candidate[coordinate] * existing[coordinate];
            for (std::size_t coordinate = 0; coordinate < dimension; ++coordinate)
                candidate[coordinate] -= projection * existing[coordinate];
        }
        double norm_squared = 0.0;
        for (const double value : candidate)
            norm_squared += value * value;
        if (norm_squared <= 256.0 * std::numeric_limits<double>::epsilon())
            continue;
        const double norm = std::sqrt(norm_squared);
        for (double& value : candidate)
            value /= norm;
        vectors.push_back(std::move(candidate));
    }
    if (vectors.size() != dimension) {
        return Result<CspaceObb>::failure(StatusCode::InternalError,
                                          "failed to construct deterministic OBB basis");
    }

    std::vector<double> basis;
    basis.reserve(dimension * dimension);
    for (const auto& vector : vectors)
        basis.insert(basis.end(), vector.begin(), vector.end());
    Configuration half_widths(dimension, lateral_half_width);
    if (length > 0.0)
        half_widths[0] = 0.5 * length + longitudinal_margin;
    else
        half_widths[0] = lateral_half_width + longitudinal_margin;
    return CspaceObb::create(std::move(center), std::move(basis), std::move(half_widths));
}

Result<ObbValidation> ObbRegionValidator::validate(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                                   const CspaceObb& region) const {
    if (!region.valid())
        return Result<ObbValidation>::failure(StatusCode::InvalidArgument, "C-space OBB is invalid");
    if (region.dimension() != robot.dimension()) {
        return Result<ObbValidation>::failure(StatusCode::DimensionMismatch,
                                              "C-space OBB dimension does not match robot");
    }
    const CspaceAabb enclosure = region.enclosing_aabb();
    IfkAaLinkAabbValidator validator(options_);
    auto validation = validator.validate(robot, scene, enclosure);
    if (!validation)
        return validation.error();
    return ObbValidation{validation.value().disposition, validation.value().clearance_lower_bound, enclosure,
                         std::move(validation).value().envelope};
}

ObbGrower::ObbGrower() = default;

ObbGrower::ObbGrower(std::shared_ptr<const ObbRegionValidator> validator)
    : validator_(std::move(validator)) {}

Result<ObbGrowthResult> ObbGrower::grow(const SerialRobotModel& robot, const SceneSnapshot& scene,
                                        std::span<const double> first, std::span<const double> second,
                                        const ObbGrowthOptions& options) const {
    if (!std::isfinite(options.initial_lateral_half_width) ||
        !std::isfinite(options.maximum_lateral_half_width) || !std::isfinite(options.longitudinal_margin) ||
        !std::isfinite(options.obstacle_padding) || options.initial_lateral_half_width < 0.0 ||
        options.maximum_lateral_half_width < options.initial_lateral_half_width ||
        options.longitudinal_margin < 0.0 || options.obstacle_padding < 0.0 ||
        options.maximum_iterations > 128 || options.maximum_validations == 0) {
        return Result<ObbGrowthResult>::failure(StatusCode::InvalidArgument, "invalid OBB growth options");
    }
    std::shared_ptr<const ObbRegionValidator> default_validator;
    const ObbRegionValidator* validator = validator_.get();
    if (validator == nullptr) {
        default_validator = std::make_shared<ObbRegionValidator>(EnvelopeOptions{options.obstacle_padding});
        validator = default_validator.get();
    } else if (std::abs(validator->options().obstacle_padding - options.obstacle_padding) > 1e-15) {
        return Result<ObbGrowthResult>::failure(
            StatusCode::InvalidArgument, "custom OBB validator padding must match growth obstacle_padding");
    }

    ObbGrowthResult result;
    auto validate = [&](const CspaceObb& candidate) -> Result<ObbValidation> {
        if (options.cancellation.cancelled())
            return Result<ObbValidation>::failure(StatusCode::Cancelled, "OBB growth was cancelled");
        if (result.validations == options.maximum_validations)
            return Result<ObbValidation>::failure(StatusCode::ResourceLimit,
                                                  "OBB growth validation budget exhausted");
        ++result.validations;
        return validator->validate(robot, scene, candidate);
    };

    auto initial = ObbGenerator::segment_tube(first, second, options.initial_lateral_half_width,
                                              options.longitudinal_margin);
    if (!initial)
        return initial.error();
    auto initial_validation = validate(initial.value());
    if (!initial_validation)
        return initial_validation.error();
    result.region = std::move(initial).value();
    result.validation = std::move(initial_validation).value();
    result.achieved_lateral_half_width = options.initial_lateral_half_width;
    result.certified = result.validation.disposition == ValidationDisposition::CertifiedFree;
    if (!result.certified || options.maximum_lateral_half_width == options.initial_lateral_half_width ||
        result.validations == options.maximum_validations)
        return result;

    double lower = options.initial_lateral_half_width;
    double upper = options.maximum_lateral_half_width;
    auto maximum = ObbGenerator::segment_tube(first, second, upper, options.longitudinal_margin);
    if (!maximum)
        return maximum.error();
    auto maximum_validation = validate(maximum.value());
    if (!maximum_validation)
        return maximum_validation.error();
    ++result.growth_attempts;
    if (maximum_validation.value().disposition == ValidationDisposition::CertifiedFree) {
        result.region = std::move(maximum).value();
        result.validation = std::move(maximum_validation).value();
        result.achieved_lateral_half_width = upper;
        return result;
    }

    for (std::size_t iteration = 0;
         iteration < options.maximum_iterations && result.validations < options.maximum_validations;
         ++iteration) {
        const double midpoint = 0.5 * (lower + upper);
        auto candidate = ObbGenerator::segment_tube(first, second, midpoint, options.longitudinal_margin);
        if (!candidate)
            return candidate.error();
        auto candidate_validation = validate(candidate.value());
        if (!candidate_validation)
            return candidate_validation.error();
        ++result.growth_attempts;
        if (candidate_validation.value().disposition == ValidationDisposition::CertifiedFree) {
            lower = midpoint;
            result.region = std::move(candidate).value();
            result.validation = std::move(candidate_validation).value();
            result.achieved_lateral_half_width = midpoint;
        } else {
            upper = midpoint;
        }
    }
    return result;
}

HipacCorridorBuilder::HipacCorridorBuilder() = default;

HipacCorridorBuilder::HipacCorridorBuilder(std::shared_ptr<const ObbRegionValidator> validator)
    : validator_(std::move(validator)) {}

Result<HipacBuildReport> HipacCorridorBuilder::build(const SerialRobotModel& robot,
                                                     const SceneSnapshot& scene,
                                                     std::span<const Configuration> path,
                                                     const HipacOptions& options) const {
    auto robot_status = robot.validate();
    if (!robot_status)
        return robot_status.error();
    auto scene_status = scene.validate();
    if (!scene_status)
        return scene_status.error();
    if (path.size() < 2) {
        return Result<HipacBuildReport>::failure(StatusCode::InvalidArgument,
                                                 "HiPaC path requires at least two waypoints");
    }
    if (!std::isfinite(options.minimum_lateral_half_width) ||
        !std::isfinite(options.maximum_lateral_half_width) || !std::isfinite(options.longitudinal_margin) ||
        !std::isfinite(options.portal_tolerance) || !std::isfinite(options.obstacle_padding) ||
        options.minimum_lateral_half_width < 0.0 ||
        options.maximum_lateral_half_width < options.minimum_lateral_half_width ||
        options.longitudinal_margin < 0.0 || options.growth_iterations > 128 ||
        options.maximum_subdivision_depth > 64 || options.maximum_validations == 0 ||
        options.portal_tolerance < 0.0 || options.obstacle_padding < 0.0) {
        return Result<HipacBuildReport>::failure(StatusCode::InvalidArgument, "invalid HiPaC options");
    }
    for (std::size_t index = 0; index < path.size(); ++index) {
        auto status =
            validate_configuration(path[index], robot.dimension(), "HiPaC waypoint " + std::to_string(index));
        if (!status)
            return status.error();
        if (!robot.configuration_domain().contains(path[index], options.portal_tolerance)) {
            return Result<HipacBuildReport>::failure(
                StatusCode::InvalidArgument, "HiPaC waypoint exceeds joint limits", std::to_string(index));
        }
    }

    std::shared_ptr<const ObbRegionValidator> default_validator;
    const ObbRegionValidator* validator = validator_.get();
    if (validator == nullptr) {
        default_validator = std::make_shared<ObbRegionValidator>(EnvelopeOptions{options.obstacle_padding});
        validator = default_validator.get();
    } else if (std::abs(validator->options().obstacle_padding - options.obstacle_padding) > 1e-15) {
        return Result<HipacBuildReport>::failure(
            StatusCode::InvalidArgument,
            "custom OBB validator padding must match HipacOptions obstacle_padding");
    }

    HipacBuildReport report;
    report.waypoint_count = path.size();
    report.segment_count = path.size() - 1;
    report.corridor.dimension_ = robot.dimension();
    report.corridor.robot_digest_ = robot.digest();
    report.corridor.scene_digest_ = scene.digest();
    std::set<RegionId> region_ids;

    auto validate_candidate = [&](const CspaceObb& candidate) -> Result<ObbValidation> {
        if (options.cancellation.cancelled()) {
            return Result<ObbValidation>::failure(StatusCode::Cancelled, "HiPaC build was cancelled");
        }
        if (report.stats.validations == options.maximum_validations) {
            return Result<ObbValidation>::failure(StatusCode::ResourceLimit,
                                                  "HiPaC validation budget exhausted");
        }
        ++report.stats.validations;
        return validator->validate(robot, scene, candidate);
    };

    std::function<Result<void>(std::size_t, double, double, std::size_t)> cover;
    cover = [&](std::size_t segment_index, double start_fraction, double end_fraction,
                std::size_t depth) -> Result<void> {
        if (options.cancellation.cancelled())
            return Result<void>::failure(StatusCode::Cancelled, "HiPaC build was cancelled");
        const Configuration entry = interpolate(path[segment_index], path[segment_index + 1], start_fraction);
        const Configuration exit = interpolate(path[segment_index], path[segment_index + 1], end_fraction);
        auto minimum = ObbGenerator::segment_tube(entry, exit, options.minimum_lateral_half_width,
                                                  options.longitudinal_margin);
        if (!minimum)
            return minimum.error();
        auto minimum_validation = validate_candidate(minimum.value());
        if (!minimum_validation)
            return minimum_validation.error();

        if (minimum_validation.value().disposition != ValidationDisposition::CertifiedFree) {
            if (depth < options.maximum_subdivision_depth && squared_distance(entry, exit) > 0.0) {
                ++report.stats.recursive_splits;
                const double midpoint = 0.5 * (start_fraction + end_fraction);
                auto left = cover(segment_index, start_fraction, midpoint, depth + 1);
                if (!left)
                    return left;
                return cover(segment_index, midpoint, end_fraction, depth + 1);
            }
            ++report.stats.failed_leaf_segments;
            report.uncovered_intervals.push_back({segment_index, start_fraction, end_fraction, true, true});
            return Result<void>::success();
        }

        CspaceObb selected = std::move(minimum).value();
        ObbValidation selected_validation = std::move(minimum_validation).value();
        double lower = options.minimum_lateral_half_width;
        double upper = options.maximum_lateral_half_width;
        if (upper > lower) {
            ++report.stats.growth_attempts;
            auto maximum = ObbGenerator::segment_tube(entry, exit, upper, options.longitudinal_margin);
            if (!maximum)
                return maximum.error();
            auto maximum_validation = validate_candidate(maximum.value());
            if (!maximum_validation)
                return maximum_validation.error();
            if (maximum_validation.value().disposition == ValidationDisposition::CertifiedFree) {
                selected = std::move(maximum).value();
                selected_validation = std::move(maximum_validation).value();
                lower = upper;
            } else {
                for (std::size_t iteration = 0; iteration < options.growth_iterations; ++iteration) {
                    const double midpoint = 0.5 * (lower + upper);
                    auto candidate =
                        ObbGenerator::segment_tube(entry, exit, midpoint, options.longitudinal_margin);
                    if (!candidate)
                        return candidate.error();
                    auto candidate_validation = validate_candidate(candidate.value());
                    if (!candidate_validation)
                        return candidate_validation.error();
                    ++report.stats.growth_attempts;
                    if (candidate_validation.value().disposition == ValidationDisposition::CertifiedFree) {
                        lower = midpoint;
                        selected = std::move(candidate).value();
                        selected_validation = std::move(candidate_validation).value();
                    } else {
                        upper = midpoint;
                    }
                }
            }
        }

        const std::string subject = obb_subject_digest(selected);
        auto certificate =
            internal::make_subject_certificate(EvidenceLevel::CertifiedRegion, robot.digest(), scene.digest(),
                                               {validator->algorithm_name(), validator->algorithm_version(),
                                                validator->options().obstacle_padding},
                                               subject, selected_validation.clearance_lower_bound);
        if (!certificate)
            return certificate.error();
        RegionId id = digest_prefix_id(cell_identity(robot.digest(), scene.digest(), subject, segment_index,
                                                     start_fraction, end_fraction));
        while (!region_ids.insert(id).second)
            ++id;
        report.corridor.regions_.push_back({id, std::move(selected), std::move(certificate).value(), 0,
                                            segment_index, start_fraction, end_fraction, entry, exit});
        ++report.stats.certified_cells;
        return Result<void>::success();
    };

    for (std::size_t segment = 0; segment < report.segment_count; ++segment) {
        auto covered = cover(segment, 0.0, 1.0, 0);
        if (!covered)
            return covered.error();
    }

    std::set<PortalId> portal_ids;
    std::vector<std::vector<std::size_t>> adjacency(report.corridor.regions_.size());
    for (std::size_t right = 1; right < report.corridor.regions_.size(); ++right) {
        const std::size_t left = right - 1;
        const auto& left_region = report.corridor.regions_[left];
        const auto& right_region = report.corridor.regions_[right];
        if (!same_configuration(left_region.exit, right_region.entry, options.portal_tolerance))
            continue;
        const Configuration witness = left_region.exit;
        if (!left_region.bounds.contains(witness, options.portal_tolerance) ||
            !right_region.bounds.contains(witness, options.portal_tolerance))
            continue;
        const std::string subject = portal_subject_digest(left_region.id, right_region.id, witness);
        const double clearance = std::min(left_region.certificate.clearance_lower_bound,
                                          right_region.certificate.clearance_lower_bound);
        auto certificate = internal::make_subject_certificate(
            EvidenceLevel::CertifiedConnectivity, robot.digest(), scene.digest(),
            {"hipac-witness-portal", "1", validator->options().obstacle_padding}, subject, clearance);
        if (!certificate)
            return certificate.error();
        PortalId id = digest_prefix_id(certificate.value().id);
        while (!portal_ids.insert(id).second)
            ++id;
        report.corridor.portals_.push_back(
            {id, left_region.id, right_region.id, witness, std::move(certificate).value()});
        const std::size_t portal_index = report.corridor.portals_.size() - 1;
        adjacency[left].push_back(portal_index);
        adjacency[right].push_back(portal_index);
        ++report.stats.portals;
    }

    ComponentId component = 0;
    std::vector<bool> visited(report.corridor.regions_.size(), false);
    std::unordered_map<RegionId, std::size_t> index_by_id;
    for (std::size_t index = 0; index < report.corridor.regions_.size(); ++index)
        index_by_id.emplace(report.corridor.regions_[index].id, index);
    for (std::size_t start = 0; start < report.corridor.regions_.size(); ++start) {
        if (visited[start])
            continue;
        ++component;
        std::deque<std::size_t> frontier{start};
        visited[start] = true;
        while (!frontier.empty()) {
            const std::size_t current = frontier.front();
            frontier.pop_front();
            report.corridor.regions_[current].component = component;
            for (const std::size_t portal_index : adjacency[current]) {
                const auto& portal = report.corridor.portals_[portal_index];
                const RegionId neighbor_id = portal.left_region == report.corridor.regions_[current].id
                                                 ? portal.right_region
                                                 : portal.left_region;
                const std::size_t neighbor = index_by_id.at(neighbor_id);
                if (!visited[neighbor]) {
                    visited[neighbor] = true;
                    frontier.push_back(neighbor);
                }
            }
        }
    }

    double covered_measure = 0.0;
    for (const auto& region : report.corridor.regions_)
        covered_measure += region.end_fraction - region.start_fraction;
    report.coverage_ratio = std::clamp(covered_measure / static_cast<double>(report.segment_count), 0.0, 1.0);
    report.uncovered_intervals = merge_uncovered(std::move(report.uncovered_intervals));
    if (report.corridor.regions_.empty())
        report.status = HipacBuildStatus::Invalid;
    else if (report.uncovered_intervals.empty())
        report.status = HipacBuildStatus::Certified;
    else
        report.status = HipacBuildStatus::Partial;
    return report;
}

Result<std::vector<RegionId>> HipacCorridor::regions_at(std::span<const double> configuration) const {
    auto status = validate_configuration(configuration, dimension_, "corridor query");
    if (!status)
        return status.error();
    std::vector<RegionId> result;
    for (const auto& region : regions_)
        if (region.bounds.contains(configuration, 1e-12))
            result.push_back(region.id);
    std::sort(result.begin(), result.end());
    return result;
}

bool HipacCorridor::contains(std::span<const double> configuration) const {
    auto result = regions_at(configuration);
    return result && !result.value().empty();
}

Result<bool> HipacCorridor::connected(std::span<const double> first, std::span<const double> second) const {
    auto result = route(first, second);
    if (!result)
        return result.error();
    return result.value().has_value();
}

Result<std::optional<CertifiedRoute>> HipacCorridor::route(std::span<const double> first,
                                                           std::span<const double> second) const {
    auto first_status = validate_configuration(first, dimension_, "corridor route start");
    if (!first_status)
        return first_status.error();
    auto second_status = validate_configuration(second, dimension_, "corridor route goal");
    if (!second_status)
        return second_status.error();

    std::vector<std::size_t> starts;
    std::vector<std::size_t> goals;
    for (std::size_t index = 0; index < regions_.size(); ++index) {
        if (regions_[index].bounds.contains(first, 1e-12))
            starts.push_back(index);
        if (regions_[index].bounds.contains(second, 1e-12))
            goals.push_back(index);
    }
    if (starts.empty() || goals.empty())
        return std::optional<CertifiedRoute>{};
    auto by_id = [&](std::size_t left, std::size_t right) { return regions_[left].id < regions_[right].id; };
    std::sort(starts.begin(), starts.end(), by_id);
    std::sort(goals.begin(), goals.end(), by_id);
    std::vector<bool> is_goal(regions_.size(), false);
    for (const auto goal : goals)
        is_goal[goal] = true;

    std::unordered_map<RegionId, std::size_t> index_by_id;
    for (std::size_t index = 0; index < regions_.size(); ++index)
        index_by_id.emplace(regions_[index].id, index);
    struct Edge {
        std::size_t neighbor = 0;
        std::size_t portal = 0;
    };
    std::vector<std::vector<Edge>> adjacency(regions_.size());
    for (std::size_t portal_index = 0; portal_index < portals_.size(); ++portal_index) {
        const auto left = index_by_id.find(portals_[portal_index].left_region);
        const auto right = index_by_id.find(portals_[portal_index].right_region);
        if (left == index_by_id.end() || right == index_by_id.end()) {
            return Result<std::optional<CertifiedRoute>>::failure(
                StatusCode::InternalError, "corridor portal references an unknown region");
        }
        adjacency[left->second].push_back({right->second, portal_index});
        adjacency[right->second].push_back({left->second, portal_index});
    }
    for (auto& edges : adjacency) {
        std::sort(edges.begin(), edges.end(), [&](const Edge& left, const Edge& right) {
            if (regions_[left.neighbor].id != regions_[right.neighbor].id)
                return regions_[left.neighbor].id < regions_[right.neighbor].id;
            return portals_[left.portal].id < portals_[right.portal].id;
        });
    }

    const std::size_t none = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> predecessor(regions_.size(), none);
    std::vector<std::size_t> predecessor_portal(regions_.size(), none);
    std::deque<std::size_t> frontier;
    for (const auto start : starts) {
        predecessor[start] = start;
        frontier.push_back(start);
    }
    std::size_t selected_goal = none;
    while (!frontier.empty()) {
        const std::size_t current = frontier.front();
        frontier.pop_front();
        if (is_goal[current]) {
            selected_goal = current;
            break;
        }
        for (const auto& edge : adjacency[current]) {
            if (predecessor[edge.neighbor] != none)
                continue;
            predecessor[edge.neighbor] = current;
            predecessor_portal[edge.neighbor] = edge.portal;
            frontier.push_back(edge.neighbor);
        }
    }
    if (selected_goal == none)
        return std::optional<CertifiedRoute>{};

    std::vector<std::size_t> reversed_regions;
    std::vector<std::size_t> reversed_portals;
    for (std::size_t current = selected_goal;; current = predecessor[current]) {
        reversed_regions.push_back(current);
        if (predecessor[current] == current)
            break;
        reversed_portals.push_back(predecessor_portal[current]);
    }
    std::reverse(reversed_regions.begin(), reversed_regions.end());
    std::reverse(reversed_portals.begin(), reversed_portals.end());

    CertifiedRoute route;
    route.waypoints.emplace_back(first.begin(), first.end());
    for (const auto index : reversed_regions)
        route.region_sequence.push_back(regions_[index].id);
    for (const auto portal_index : reversed_portals) {
        route.portal_sequence.push_back(portals_[portal_index].id);
        route.waypoints.push_back(portals_[portal_index].witness);
    }
    route.waypoints.emplace_back(second.begin(), second.end());

    double clearance = std::numeric_limits<double>::infinity();
    for (const auto index : reversed_regions)
        clearance = std::min(clearance, regions_[index].certificate.clearance_lower_bound);
    for (const auto index : reversed_portals)
        clearance = std::min(clearance, portals_[index].certificate.clearance_lower_bound);
    if (!std::isfinite(clearance))
        clearance = 0.0;
    internal::Json::Array region_ids;
    for (const auto id : route.region_sequence)
        region_ids.emplace_back(std::to_string(id));
    internal::Json::Array portal_ids;
    for (const auto id : route.portal_sequence)
        portal_ids.emplace_back(std::to_string(id));
    internal::Json::Array waypoints;
    for (const auto& waypoint : route.waypoints)
        waypoints.emplace_back(configuration_json(waypoint));
    const std::string subject =
        internal::sha256(internal::Json(internal::Json::Object{
                                            {"portal_sequence", std::move(portal_ids)},
                                            {"region_sequence", std::move(region_ids)},
                                            {"type", "hipac-convex-cell-route"},
                                            {"waypoints", std::move(waypoints)},
                                        })
                             .dump(false));
    const double padding = regions_[reversed_regions.front()].certificate.policy.obstacle_padding;
    auto certificate =
        internal::make_subject_certificate(EvidenceLevel::CertifiedConnectivity, robot_digest_, scene_digest_,
                                           {"hipac-convex-cell-chain", "1", padding}, subject, clearance);
    if (!certificate)
        return certificate.error();
    route.certificate = std::move(certificate).value();
    return std::optional<CertifiedRoute>{std::move(route)};
}

Result<void> HipacCorridor::verify_compatible(const SerialRobotModel& robot,
                                              const SceneSnapshot& scene) const {
    if (robot.digest() != robot_digest_)
        return Result<void>::failure(StatusCode::IdentityMismatch, "corridor robot identity does not match");
    if (scene.digest() != scene_digest_)
        return Result<void>::failure(StatusCode::IdentityMismatch, "corridor scene identity does not match");
    return Result<void>::success();
}

Result<void> HipacCorridor::save(const std::filesystem::path& directory, const SaveOptions& options) const {
    return save_corridor_directory(*this, directory, options);
}

Result<HipacCorridor> HipacCorridor::load(const std::filesystem::path& directory) {
    return load_corridor_directory(directory);
}

} // namespace rbfsafe
