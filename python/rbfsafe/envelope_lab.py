"""Interactive workspace-envelope comparison laboratory.

The computation layer deliberately has no matplotlib dependency.  The GUI is
loaded lazily by :func:`launch_gui`, so experiments and JSON exports remain
usable in headless installations.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence


@dataclass(frozen=True)
class ExperimentVariant:
    key: str
    label: str
    endpoint_source: str
    envelope_type: str
    kdop_k: int = 26
    color: str = "#4c78a8"


EXPERIMENT_VARIANTS: tuple[ExperimentVariant, ...] = (
    ExperimentVariant("ifk_aabb", "IFK-AA × AABB", "ifk_aa", "aabb", color="#4c78a8"),
    ExperimentVariant("ifk_obb", "IFK-AA × OBB", "ifk_aa", "obb", color="#f58518"),
    ExperimentVariant("ifk_14dop", "IFK-AA × 14-DOP", "ifk_aa", "kdop", 14, "#54a24b"),
    ExperimentVariant("ifk_26dop", "IFK-AA × 26-DOP", "ifk_aa", "kdop", 26, "#e45756"),
    ExperimentVariant("ifk_hull", "IFK-AA × SupportHull", "ifk_aa", "support_hull", color="#b279a2"),
    ExperimentVariant("crit_aabb", "CritSample × AABB", "crit_sample", "aabb", color="#72b7b2"),
    ExperimentVariant("crit_26dop", "CritSample × 26-DOP", "crit_sample", "kdop", 26, "#8c8c38"),
    ExperimentVariant(
        "crit_hull", "CritSample × SupportHull", "crit_sample", "support_hull", color="#ff9da6"
    ),
)


@dataclass
class EnvelopeExperimentResult:
    variant: ExperimentVariant
    workspace_envelope: Any
    endpoint_bounds_certified: bool
    evaluated_configurations: int
    enclosing_aabb_volumes: list[float]
    enclosing_aabb_volume_sum: float
    link_obstacle_overlaps: list[bool]
    overlaps_any_obstacle: bool
    link_distance_lower_bounds: list[float]
    distance_lower_bound: float
    validator_eligible: bool
    validator_certified_free: bool | None
    validator_disposition: str
    validator_clearance_lower_bound: float | None
    validator_algorithm: str | None
    computation_ms: float

    def to_dict(self) -> dict[str, Any]:
        return {
            "key": self.variant.key,
            "combination": self.variant.label,
            "endpoint_source": self.variant.endpoint_source,
            "workspace_envelope_type": self.variant.envelope_type,
            "kdop_k": self.variant.kdop_k if self.variant.envelope_type == "kdop" else None,
            "endpoint_bounds_certified": self.endpoint_bounds_certified,
            "evaluated_configurations": self.evaluated_configurations,
            "enclosing_aabb_volumes": self.enclosing_aabb_volumes,
            "enclosing_aabb_volume_sum": self.enclosing_aabb_volume_sum,
            "link_obstacle_overlaps": self.link_obstacle_overlaps,
            "overlaps_any_obstacle": self.overlaps_any_obstacle,
            "link_distance_lower_bounds": self.link_distance_lower_bounds,
            "distance_lower_bound": self.distance_lower_bound,
            "validator_eligible": self.validator_eligible,
            "validator_certified_free": self.validator_certified_free,
            "validator_disposition": self.validator_disposition,
            "validator_clearance_lower_bound": self.validator_clearance_lower_bound,
            "validator_algorithm": self.validator_algorithm,
            "computation_ms": self.computation_ms,
        }


@dataclass
class EnvelopeExperimentReport:
    robot: Any
    scene: Any
    domain: Any
    obstacle_padding: float
    endpoint_results: dict[str, Any]
    results: list[EnvelopeExperimentResult]

    def to_dict(self) -> dict[str, Any]:
        return {
            "robot": {
                "name": self.robot.name,
                "dimension": self.robot.dimension,
                "link_count": self.robot.link_count,
                "digest": self.robot.digest,
            },
            "scene": {
                "version": self.scene.version,
                "digest": self.scene.digest,
                "obstacle_count": len(self.scene.obstacles),
            },
            "cspace_aabb": [[axis.lower, axis.upper] for axis in self.domain.axes],
            "obstacle_padding": self.obstacle_padding,
            "results": [result.to_dict() for result in self.results],
        }


def _api():
    import rbfsafe

    return rbfsafe


def _joint(api: Any, alpha: float, a: float, d: float, theta: float = 0.0) -> Any:
    return api.DhJoint(alpha, a, d, theta, api.JointType.REVOLUTE)


def load_preset_robot(name: str) -> Any:
    """Construct one of the self-contained robot presets.

    Values match the repository's release robot fixtures, but embedding the
    model here keeps presets available from an installed wheel where top-level
    ``data/`` files may not exist.
    """

    api = _api()
    normalized = name.strip().lower().replace("_", "-")
    if normalized in {"planar", "planar-2r", "2r", "2-link"}:
        return api.SerialRobotModel(
            "envelope-lab-planar-2r",
            [_joint(api, 0.0, 1.0, 0.0), _joint(api, 0.0, 1.0, 0.0)],
            [api.Interval(-1.5, 1.5), api.Interval(-1.5, 1.5)],
            [0.05, 0.05],
        )
    if normalized in {"ur5", "universal-robots-ur5"}:
        joints = [
            _joint(api, 0.0, 0.0, 0.089159),
            _joint(api, math.pi / 2.0, 0.0, 0.0),
            _joint(api, 0.0, -0.425, 0.0),
            _joint(api, 0.0, -0.39225, 0.10915),
            _joint(api, math.pi / 2.0, 0.0, 0.09465),
            _joint(api, -math.pi / 2.0, 0.0, 0.0823),
        ]
        return api.SerialRobotModel(
            "envelope-lab-ur5",
            joints,
            [api.Interval(-math.pi, math.pi) for _ in joints],
            [0.075, 0.075, 0.065, 0.06, 0.05, 0.045],
        )
    if normalized in {"iiwa", "iiwa14", "kuka-iiwa"}:
        joints = [
            _joint(api, 0.0, 0.0, 0.36),
            _joint(api, -math.pi / 2.0, 0.0, 0.0),
            _joint(api, math.pi / 2.0, 0.0, 0.42),
            _joint(api, math.pi / 2.0, 0.0, 0.0),
            _joint(api, -math.pi / 2.0, 0.0, 0.4),
            _joint(api, -math.pi / 2.0, 0.0, 0.0),
            _joint(api, math.pi / 2.0, 0.0, 0.081),
        ]
        limits = [
            (-2.9668, 2.9668),
            (-2.0942, 2.0942),
            (-2.9668, 2.9668),
            (-2.0942, 2.0942),
            (-2.9668, 2.9668),
            (-2.0942, 2.0942),
            (-3.0541, 3.0541),
        ]
        tool = _joint(api, 0.0, 0.0, 0.2)
        return api.SerialRobotModel(
            "envelope-lab-iiwa14",
            joints,
            [api.Interval(lower, upper) for lower, upper in limits],
            [0.10, 0.06, 0.075, 0.055, 0.065, 0.05, 0.062, 0.052],
            tool,
        )
    if normalized in {"franka", "fr3", "panda", "franka-research-3"}:
        joints = [
            _joint(api, 0.0, 0.0, 0.333),
            _joint(api, -math.pi / 2.0, 0.0, 0.0),
            _joint(api, math.pi / 2.0, 0.0, 0.316),
            _joint(api, math.pi / 2.0, 0.0825, 0.0),
            _joint(api, -math.pi / 2.0, -0.0825, 0.384),
            _joint(api, math.pi / 2.0, 0.0, 0.0),
            _joint(api, math.pi / 2.0, 0.088, 0.107),
        ]
        limits = [
            (-2.8973, 2.8973),
            (-1.7628, 1.7628),
            (-2.8973, 2.8973),
            (-3.0718, -0.0698),
            (-2.8973, 2.8973),
            (-0.0175, 3.7525),
            (-2.8973, 2.8973),
        ]
        tool = _joint(api, 0.0, 0.0, 0.1034)
        return api.SerialRobotModel(
            "envelope-lab-franka-research-3",
            joints,
            [api.Interval(lower, upper) for lower, upper in limits],
            [0.075, 0.07, 0.07, 0.065, 0.06, 0.055, 0.05, 0.045],
            tool,
        )
    raise ValueError(f"unknown robot preset: {name}")


def load_robot(robot_preset: str = "planar-2r", robot_file: str | Path | None = None) -> Any:
    if robot_file:
        return _api().SerialRobotModel.from_json(Path(robot_file))
    return load_preset_robot(robot_preset)


def default_domain(robot: Any) -> Any:
    api = _api()
    axes = []
    for limit in robot.joint_limits:
        half_width = min(0.08, max(0.005, 0.02 * limit.width))
        axes.append(api.Interval(max(limit.lower, limit.center - half_width),
                                 min(limit.upper, limit.center + half_width)))
    return api.CspaceAabb(axes)


def domain_from_bounds(robot: Any, bounds: Sequence[Sequence[float]]) -> Any:
    api = _api()
    if len(bounds) != robot.dimension:
        raise ValueError(f"expected {robot.dimension} C-space intervals, got {len(bounds)}")
    axes = []
    for index, (values, limit) in enumerate(zip(bounds, robot.joint_limits)):
        if len(values) != 2:
            raise ValueError(f"joint {index} interval must contain lower and upper")
        lower, upper = float(values[0]), float(values[1])
        if not (math.isfinite(lower) and math.isfinite(upper) and lower <= upper):
            raise ValueError(f"joint {index} interval is invalid")
        if lower < limit.lower - 1e-12 or upper > limit.upper + 1e-12:
            raise ValueError(
                f"joint {index} interval [{lower}, {upper}] exceeds [{limit.lower}, {limit.upper}]"
            )
        axes.append(api.Interval(lower, upper))
    return api.CspaceAabb(axes)


def _subtract(left: Sequence[float], right: Sequence[float]) -> list[float]:
    return [left[index] - right[index] for index in range(3)]


def _add(left: Sequence[float], right: Sequence[float]) -> list[float]:
    return [left[index] + right[index] for index in range(3)]


def _scale(value: Sequence[float], factor: float) -> list[float]:
    return [factor * value[index] for index in range(3)]


def _dot(left: Sequence[float], right: Sequence[float]) -> float:
    return sum(left[index] * right[index] for index in range(3))


def _cross(left: Sequence[float], right: Sequence[float]) -> list[float]:
    return [
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0],
    ]


def _normalized(value: Sequence[float]) -> list[float]:
    magnitude = math.sqrt(_dot(value, value))
    return _scale(value, 1.0 / magnitude) if magnitude > 1e-15 else [1.0, 0.0, 0.0]


def make_probe_scene(robot: Any, domain: Any, obstacle_scale: float = 0.025) -> Any:
    """Place a small AABB near the longest center-configuration link.

    The two perpendicular offsets make this a useful AABB-vs-rounded-envelope
    corner probe without assuming that the robot is planar or metre-scaled.
    """

    api = _api()
    points = robot.forward_kinematics(domain.center)
    best_link = 0
    best_length = -1.0
    for link in range(robot.link_count):
        delta = _subtract(points[link + 1], points[link])
        length = math.sqrt(_dot(delta, delta))
        if length > best_length:
            best_length = length
            best_link = link
    proximal = points[best_link]
    distal = points[best_link + 1]
    axis = _normalized(_subtract(distal, proximal))
    reference = [1.0, 0.0, 0.0]
    if abs(axis[0]) > 0.8:
        reference = [0.0, 1.0, 0.0]
    perpendicular = _normalized(_cross(axis, reference))
    third = _normalized(_cross(axis, perpendicular))
    midpoint = _scale(_add(proximal, distal), 0.5)
    radius = max(float(robot.link_radii[best_link]), 1e-4)
    center = _add(midpoint, _add(_scale(perpendicular, 0.90 * radius), _scale(third, 0.90 * radius)))
    half_width = max(radius * obstacle_scale, max(best_length, radius) * 1e-3, 1e-5)
    box = api.WorkspaceAabb(
        [coordinate - half_width for coordinate in center],
        [coordinate + half_width for coordinate in center],
    )
    return api.SceneSnapshot([api.SceneObstacle("corner-probe", box)], "envelope-lab-probe-v1")


def _source_enum(api: Any, source: str) -> Any:
    return api.EndpointAabbSource.IFK_AA if source == "ifk_aa" else api.EndpointAabbSource.CRIT_SAMPLE


def _type_enum(api: Any, envelope_type: str) -> Any:
    return {
        "aabb": api.WorkspaceEnvelopeType.AABB,
        "obb": api.WorkspaceEnvelopeType.OBB,
        "kdop": api.WorkspaceEnvelopeType.KDOP,
        "support_hull": api.WorkspaceEnvelopeType.SUPPORT_HULL,
    }[envelope_type]


def _aabb_volume(box: Any) -> float:
    result = 1.0
    for axis in range(3):
        result *= max(0.0, float(box.upper[axis]) - float(box.lower[axis]))
    return result


def run_experiment(
    robot: Any,
    scene: Any,
    domain: Any,
    obstacle_padding: float = 0.0,
) -> EnvelopeExperimentReport:
    """Compute all eight requested combinations against one fixed input set."""

    api = _api()
    if not math.isfinite(obstacle_padding) or obstacle_padding < 0.0:
        raise ValueError("obstacle_padding must be finite and non-negative")

    endpoint_results: dict[str, Any] = {}
    for source in ("ifk_aa", "crit_sample"):
        options = api.EnvelopeOptions()
        options.endpoint_aabb_source = _source_enum(api, source)
        endpoint_results[source] = api.compute_endpoint_aabbs(robot, domain, options)

    results: list[EnvelopeExperimentResult] = []
    for variant in EXPERIMENT_VARIANTS:
        started = time.perf_counter()
        options = api.EnvelopeOptions()
        options.obstacle_padding = obstacle_padding
        options.endpoint_aabb_source = _source_enum(api, variant.endpoint_source)
        options.workspace_envelope_type = _type_enum(api, variant.envelope_type)
        options.kdop_k = variant.kdop_k
        workspace = api.compute_workspace_link_envelope(robot, domain, options)

        volumes = [_aabb_volume(link.enclosing_aabb()) for link in workspace.links]
        overlap_flags: list[bool] = []
        link_distances: list[float] = []
        for link in workspace.links:
            if not scene.obstacles:
                overlap_flags.append(False)
                link_distances.append(0.0)
                continue
            per_obstacle_overlap = [link.overlaps(obstacle.bounds) for obstacle in scene.obstacles]
            per_obstacle_distance = [link.distance_lower_bound(obstacle.bounds) for obstacle in scene.obstacles]
            overlap_flags.append(any(per_obstacle_overlap))
            link_distances.append(min(per_obstacle_distance))

        validator_eligible = variant.endpoint_source == "ifk_aa"
        validator_certified: bool | None = None
        validator_disposition = "Not eligible (CritSample endpoint bounds are non-certified)"
        validator_clearance: float | None = None
        validator_algorithm: str | None = None
        if validator_eligible:
            validator_options = api.EnvelopeOptions()
            validator_options.obstacle_padding = obstacle_padding
            validator_options.workspace_envelope_type = _type_enum(api, variant.envelope_type)
            validator_options.kdop_k = variant.kdop_k
            validator = api.IfkAaWorkspaceEnvelopeValidator(validator_options)
            validation = validator.validate(robot, scene, domain)
            validator_certified = validation.disposition == api.ValidationDisposition.CERTIFIED_FREE
            validator_disposition = "CertifiedFree" if validator_certified else "Undetermined"
            validator_clearance = float(validation.clearance_lower_bound)
            validator_algorithm = validator.algorithm_name

        results.append(
            EnvelopeExperimentResult(
                variant=variant,
                workspace_envelope=workspace,
                endpoint_bounds_certified=bool(workspace.endpoint_bounds_certified),
                evaluated_configurations=int(workspace.evaluated_configurations),
                enclosing_aabb_volumes=volumes,
                enclosing_aabb_volume_sum=sum(volumes),
                link_obstacle_overlaps=overlap_flags,
                overlaps_any_obstacle=any(overlap_flags),
                link_distance_lower_bounds=link_distances,
                distance_lower_bound=min(link_distances) if link_distances else 0.0,
                validator_eligible=validator_eligible,
                validator_certified_free=validator_certified,
                validator_disposition=validator_disposition,
                validator_clearance_lower_bound=validator_clearance,
                validator_algorithm=validator_algorithm,
                computation_ms=(time.perf_counter() - started) * 1000.0,
            )
        )
    return EnvelopeExperimentReport(robot, scene, domain, obstacle_padding, endpoint_results, results)


def export_report_json(report: EnvelopeExperimentReport, path: str | Path) -> Path:
    destination = Path(path)
    destination.write_text(json.dumps(report.to_dict(), indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return destination


def export_report_csv(report: EnvelopeExperimentReport, path: str | Path) -> Path:
    destination = Path(path)
    rows = [result.to_dict() for result in report.results]
    scalar_keys = [
        "key",
        "combination",
        "endpoint_source",
        "workspace_envelope_type",
        "kdop_k",
        "endpoint_bounds_certified",
        "evaluated_configurations",
        "enclosing_aabb_volume_sum",
        "overlaps_any_obstacle",
        "distance_lower_bound",
        "validator_eligible",
        "validator_certified_free",
        "validator_disposition",
        "validator_clearance_lower_bound",
        "validator_algorithm",
        "computation_ms",
    ]
    with destination.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=scalar_keys)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row[key] for key in scalar_keys})
    return destination


def print_report(report: EnvelopeExperimentReport) -> None:
    print(f"robot={report.robot.name} dimension={report.robot.dimension} obstacles={len(report.scene.obstacles)}")
    print("combination                       cert  eval    sum_AABB_volume  overlap  min_distance  validator")
    for result in report.results:
        validator = (
            "YES" if result.validator_certified_free is True else "NO" if result.validator_certified_free is False else "N/A"
        )
        print(
            f"{result.variant.label:<33} "
            f"{'yes' if result.endpoint_bounds_certified else 'no':<5} "
            f"{result.evaluated_configurations:<7d} "
            f"{result.enclosing_aabb_volume_sum:<16.7g} "
            f"{'yes' if result.overlaps_any_obstacle else 'no':<8} "
            f"{result.distance_lower_bound:<13.7g} {validator}"
        )


def _parse_bound_overrides(robot: Any, specifications: Iterable[str]) -> Any:
    bounds = [[axis.lower, axis.upper] for axis in default_domain(robot).axes]
    for specification in specifications:
        try:
            index_text, lower_text, upper_text = specification.split(":", 2)
            index = int(index_text)
            bounds[index] = [float(lower_text), float(upper_text)]
        except (ValueError, IndexError) as error:
            raise ValueError("--q-range must use INDEX:LOWER:UPPER") from error
    return domain_from_bounds(robot, bounds)


def launch_gui(
    robot: Any,
    scene: Any,
    domain: Any,
    obstacle_padding: float = 0.0,
    initial_report: EnvelopeExperimentReport | None = None,
    scene_editable: bool = False,
) -> None:
    try:
        from ._envelope_lab_gui import EnvelopeLabApp
    except ImportError as error:
        raise RuntimeError(
            "the interactive lab requires matplotlib and a Tk GUI; install rbfsafe[visualization]"
        ) from error
    EnvelopeLabApp(robot, scene, domain, obstacle_padding, initial_report, scene_editable).run()


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Compare and visualize RBF-Safe workspace envelopes")
    parser.add_argument("--robot", default="planar-2r", choices=("planar-2r", "iiwa", "ur5", "franka"))
    parser.add_argument("--robot-file", type=Path, help="load a custom RBF-Safe robot JSON")
    parser.add_argument("--scene-file", type=Path, help="load a custom RBF-Safe scene JSON")
    parser.add_argument(
        "--q-range",
        action="append",
        default=[],
        metavar="INDEX:LOWER:UPPER",
        help="override one initial C-space interval; repeat for multiple joints",
    )
    parser.add_argument("--padding", type=float, default=0.0, help="extra link-envelope padding")
    parser.add_argument("--export-json", type=Path, help="write the initial experiment report as JSON")
    parser.add_argument("--export-csv", type=Path, help="write the initial experiment table as CSV")
    parser.add_argument("--no-gui", action="store_true", help="compute/export without opening a window")
    arguments = parser.parse_args(argv)

    api = _api()
    robot = load_robot(arguments.robot, arguments.robot_file)
    domain = _parse_bound_overrides(robot, arguments.q_range)
    scene = api.SceneSnapshot.from_json(arguments.scene_file) if arguments.scene_file else make_probe_scene(robot, domain)
    report = run_experiment(robot, scene, domain, arguments.padding)
    print_report(report)
    if arguments.export_json:
        export_report_json(report, arguments.export_json)
    if arguments.export_csv:
        export_report_csv(report, arguments.export_csv)
    if not arguments.no_gui:
        launch_gui(
            robot,
            scene,
            domain,
            arguments.padding,
            initial_report=report,
            scene_editable=arguments.scene_file is None,
        )
    return 0


if __name__ == "__main__":  # pragma: no cover - manual entry point
    raise SystemExit(main())
