"""Tk/Matplotlib front end for :mod:`rbfsafe.envelope_lab`.

This module is intentionally private and imported lazily so importing rbfsafe
does not require the optional visualization stack.
"""

from __future__ import annotations

import math
import threading
from pathlib import Path
from typing import Any, Sequence

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from matplotlib.figure import Figure
from mpl_toolkits.mplot3d.art3d import Line3DCollection, Poly3DCollection
import numpy as np

from .envelope_lab import (
    EXPERIMENT_VARIANTS,
    EnvelopeExperimentReport,
    default_domain,
    domain_from_bounds,
    export_report_csv,
    export_report_json,
    load_preset_robot,
    make_probe_scene,
    run_experiment,
)


_BOX_FACES = (
    (0, 1, 3, 2),
    (4, 6, 7, 5),
    (0, 4, 5, 1),
    (2, 3, 7, 6),
    (0, 2, 6, 4),
    (1, 5, 7, 3),
)


def _add(left: Sequence[float], right: Sequence[float]) -> list[float]:
    return [left[index] + right[index] for index in range(3)]


def _subtract(left: Sequence[float], right: Sequence[float]) -> list[float]:
    return [left[index] - right[index] for index in range(3)]


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


def _aabb_vertices(box: Any) -> list[list[float]]:
    return [
        [
            box.upper[0] if mask & 4 else box.lower[0],
            box.upper[1] if mask & 2 else box.lower[1],
            box.upper[2] if mask & 1 else box.lower[2],
        ]
        for mask in range(8)
    ]


def _obb_vertices(box: Any) -> list[list[float]]:
    axes = [[box.basis[row * 3 + column] for column in range(3)] for row in range(3)]
    result = []
    for mask in range(8):
        point = list(box.center)
        for axis in range(3):
            sign = 1.0 if mask & (4 >> axis) else -1.0
            point = _add(point, _scale(axes[axis], sign * box.half_widths[axis]))
        result.append(point)
    return result


def _ordered_face(vertices: list[list[float]], indices: list[int], normal: Sequence[float]) -> list[int]:
    center = [sum(vertices[index][axis] for index in indices) / len(indices) for axis in range(3)]
    reference = [1.0, 0.0, 0.0] if abs(normal[0]) < 0.8 else [0.0, 1.0, 0.0]
    first = _normalized(_cross(normal, reference))
    second = _cross(normal, first)
    return sorted(
        indices,
        key=lambda index: math.atan2(
            _dot(_subtract(vertices[index], center), second),
            _dot(_subtract(vertices[index], center), first),
        ),
    )


def _kdop_faces(shape: Any) -> tuple[list[list[float]], list[list[int]]]:
    vertices = [list(point) for point in shape.vertices]
    faces: list[list[int]] = []
    seen: set[frozenset[int]] = set()
    for direction, projection in zip(shape.directions, shape.projections):
        scale = max(1.0, abs(projection.lower), abs(projection.upper))
        tolerance = 1e-7 * scale
        for bound, outward in ((projection.lower, _scale(direction, -1.0)), (projection.upper, direction)):
            indices = [
                index
                for index, point in enumerate(vertices)
                if abs(_dot(direction, point) - bound) <= tolerance
            ]
            identity = frozenset(indices)
            if len(indices) >= 3 and identity not in seen:
                seen.add(identity)
                faces.append(_ordered_face(vertices, indices, outward))
    return vertices, faces


def _box_segments(vertices: list[list[float]]) -> list[list[list[float]]]:
    edges = (
        (0, 1), (0, 2), (0, 4), (1, 3), (1, 5), (2, 3),
        (2, 6), (3, 7), (4, 5), (4, 6), (5, 7), (6, 7),
    )
    return [[vertices[first], vertices[second]] for first, second in edges]


def _face_segments(vertices: list[list[float]], faces: list[list[int]]) -> list[list[list[float]]]:
    result: list[list[list[float]]] = []
    seen: set[tuple[int, int]] = set()
    for face in faces:
        for position, first in enumerate(face):
            second = face[(position + 1) % len(face)]
            edge = tuple(sorted((first, second)))
            if edge not in seen:
                seen.add(edge)
                result.append([vertices[first], vertices[second]])
    return result


def _draw_polyhedron(
    axes: Any,
    vertices: list[list[float]],
    faces: list[list[int]],
    color: str,
    alpha: float,
    surface: bool,
    wireframe: bool,
    linestyle: str = "solid",
    linewidth: float = 0.8,
) -> None:
    if surface and faces:
        collection = Poly3DCollection(
            [[vertices[index] for index in face] for face in faces],
            facecolors=color,
            edgecolors=color if wireframe else "none",
            linewidths=linewidth,
            alpha=alpha,
        )
        axes.add_collection3d(collection)
    elif wireframe:
        segments = _box_segments(vertices) if len(vertices) == 8 and len(faces) == 6 else _face_segments(vertices, faces)
        collection = Line3DCollection(segments, colors=color, linewidths=linewidth, linestyles=linestyle)
        axes.add_collection3d(collection)


def _draw_aabb(
    axes: Any,
    box: Any,
    color: str,
    alpha: float,
    surface: bool,
    wireframe: bool,
    linestyle: str = "solid",
    linewidth: float = 0.8,
) -> None:
    _draw_polyhedron(
        axes,
        _aabb_vertices(box),
        [list(face) for face in _BOX_FACES],
        color,
        alpha,
        surface,
        wireframe,
        linestyle,
        linewidth,
    )


def _support_surface(shape: Any, azimuth_steps: int = 32, elevation_steps: int = 17):
    xs: list[list[float]] = []
    ys: list[list[float]] = []
    zs: list[list[float]] = []
    envelope = shape
    for elevation_index in range(elevation_steps):
        phi = math.pi * elevation_index / (elevation_steps - 1)
        row_x: list[float] = []
        row_y: list[float] = []
        row_z: list[float] = []
        for azimuth_index in range(azimuth_steps + 1):
            theta = 2.0 * math.pi * azimuth_index / azimuth_steps
            direction = [math.sin(phi) * math.cos(theta), math.sin(phi) * math.sin(theta), math.cos(phi)]
            point = envelope.support_point(direction)
            row_x.append(point[0])
            row_y.append(point[1])
            row_z.append(point[2])
        xs.append(row_x)
        ys.append(row_y)
        zs.append(row_z)
    # ``Axes3D.plot_surface`` accesses ``ndim`` directly, so nested Python
    # lists are not accepted by recent Matplotlib releases.
    return np.asarray(xs), np.asarray(ys), np.asarray(zs)


def _draw_envelope(
    axes: Any,
    envelope: Any,
    color: str,
    alpha: float,
    surface: bool,
    wireframe: bool,
    linestyle: str,
    support_points: bool,
) -> None:
    if envelope.aabb is not None:
        _draw_aabb(axes, envelope.aabb, color, alpha, surface, wireframe, linestyle)
        return
    if envelope.obb is not None:
        _draw_polyhedron(
            axes,
            _obb_vertices(envelope.obb),
            [list(face) for face in _BOX_FACES],
            color,
            alpha,
            surface,
            wireframe,
            linestyle,
        )
        return
    if envelope.kdop is not None:
        vertices, faces = _kdop_faces(envelope.kdop)
        _draw_polyhedron(axes, vertices, faces, color, alpha, surface, wireframe, linestyle)
        if support_points and vertices:
            axes.scatter(
                [point[0] for point in vertices],
                [point[1] for point in vertices],
                [point[2] for point in vertices],
                color=color,
                s=8,
                alpha=min(1.0, alpha + 0.3),
            )
        return
    if envelope.support_hull is not None:
        xs, ys, zs = _support_surface(envelope)
        if surface:
            axes.plot_surface(
                xs,
                ys,
                zs,
                color=color,
                alpha=alpha,
                edgecolor=color if wireframe else "none",
                linewidth=0.2 if wireframe else 0.0,
                antialiased=True,
                shade=False,
            )
        elif wireframe:
            axes.plot_wireframe(xs, ys, zs, color=color, linewidth=0.35, linestyle=linestyle, alpha=0.9)
        if support_points:
            points = envelope.support_hull.points
            axes.scatter(
                [point[0] for point in points],
                [point[1] for point in points],
                [point[2] for point in points],
                color=color,
                s=10,
                marker="o",
            )


def _format_number(value: float | None) -> str:
    if value is None:
        return "N/A"
    if value == 0.0:
        return "0"
    return f"{value:.6g}"


class _ScrollableFrame(ttk.Frame):
    def __init__(self, parent: Any, height: int = 240):
        super().__init__(parent)
        self.canvas = tk.Canvas(self, highlightthickness=0, height=height)
        scrollbar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.body = ttk.Frame(self.canvas)
        self.window = self.canvas.create_window((0, 0), window=self.body, anchor="nw")
        self.body.bind("<Configure>", lambda _event: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.bind("<Configure>", lambda event: self.canvas.itemconfigure(self.window, width=event.width))
        self.canvas.configure(yscrollcommand=scrollbar.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")


class EnvelopeLabApp:
    def __init__(
        self,
        robot: Any,
        scene: Any,
        domain: Any,
        obstacle_padding: float = 0.0,
        initial_report: EnvelopeExperimentReport | None = None,
        scene_editable: bool = False,
    ):
        self.robot = robot
        self.scene = scene
        self.domain = domain
        self.report: EnvelopeExperimentReport | None = initial_report
        self._computing = False
        self._scene_editable = False

        self.root = tk.Tk()
        self.root.title("RBF-Safe Workspace Envelope Laboratory")
        self.root.geometry("1600x980")
        self.root.minsize(1180, 720)

        self.status = tk.StringVar(value="Ready")
        self.padding_var = tk.StringVar(value=f"{obstacle_padding:.6g}")
        self.robot_preset_var = tk.StringVar(value=self._guess_robot_preset(robot.name))
        self.robot_file_var = tk.StringVar()
        self.scene_file_var = tk.StringVar()
        self.scene_label_var = tk.StringVar(value=f"Current: {scene.version} ({len(scene.obstacles)} obstacles)")
        self.alpha_var = tk.DoubleVar(value=0.22)
        self.link_var = tk.StringVar(value="All links")

        default_visible = {"ifk_aabb", "ifk_obb", "ifk_26dop", "ifk_hull", "crit_hull"}
        self.variant_vars = {
            variant.key: tk.BooleanVar(value=variant.key in default_visible) for variant in EXPERIMENT_VARIANTS
        }
        self.display_vars = {
            "surface": tk.BooleanVar(value=True),
            "wireframe": tk.BooleanVar(value=True),
            "robot": tk.BooleanVar(value=True),
            "obstacles": tk.BooleanVar(value=True),
            "endpoint_aabbs": tk.BooleanVar(value=False),
            "enclosing_aabbs": tk.BooleanVar(value=False),
            "support_points": tk.BooleanVar(value=False),
            "legend": tk.BooleanVar(value=True),
            "equal_axes": tk.BooleanVar(value=True),
        }
        self.joint_vars: list[tuple[tk.StringVar, tk.StringVar]] = []
        self.obstacle_center_vars = [tk.StringVar() for _ in range(3)]
        self.obstacle_half_vars = [tk.StringVar() for _ in range(3)]

        self._build_layout()
        self._rebuild_joint_editor()
        if scene_editable:
            self._try_set_editable_scene(scene)
        if initial_report is None:
            self.root.after(50, self.recompute)
        else:
            self.root.after(50, lambda: self._finish_compute(initial_report, None))

    @staticmethod
    def _guess_robot_preset(name: str) -> str:
        lowered = name.lower()
        if "iiwa" in lowered:
            return "iiwa"
        if "ur5" in lowered:
            return "ur5"
        if "franka" in lowered or "panda" in lowered:
            return "franka"
        return "planar-2r"

    def _build_layout(self) -> None:
        paned = ttk.Panedwindow(self.root, orient="horizontal")
        paned.pack(fill="both", expand=True)

        plot_frame = ttk.Frame(paned)
        control_frame = ttk.Frame(paned, width=470)
        paned.add(plot_frame, weight=4)
        paned.add(control_frame, weight=2)

        self.figure = Figure(figsize=(10, 8), dpi=100)
        self.axes = self.figure.add_subplot(111, projection="3d")
        self.figure.subplots_adjust(left=0.04, right=0.97, bottom=0.08, top=0.94)
        self.canvas = FigureCanvasTkAgg(self.figure, master=plot_frame)
        self.canvas.get_tk_widget().pack(fill="both", expand=True)
        toolbar = NavigationToolbar2Tk(self.canvas, plot_frame, pack_toolbar=False)
        toolbar.update()
        toolbar.pack(fill="x")
        ttk.Label(
            plot_frame,
            text="Drag on the 3D axes to orbit; use the toolbar to pan, zoom, reset, and save a screenshot.",
        ).pack(anchor="w", padx=8, pady=(2, 5))

        notebook = ttk.Notebook(control_frame)
        notebook.pack(fill="both", expand=True)
        experiment_tab = ttk.Frame(notebook)
        display_tab = ttk.Frame(notebook)
        results_tab = ttk.Frame(notebook)
        notebook.add(experiment_tab, text="Experiment")
        notebook.add(display_tab, text="Display")
        notebook.add(results_tab, text="Results")

        self._build_experiment_tab(experiment_tab)
        self._build_display_tab(display_tab)
        self._build_results_tab(results_tab)
        ttk.Label(self.root, textvariable=self.status, relief="sunken", anchor="w").pack(fill="x", side="bottom")

    def _build_experiment_tab(self, parent: Any) -> None:
        robot_frame = ttk.LabelFrame(parent, text="Robot model")
        robot_frame.pack(fill="x", padx=8, pady=6)
        ttk.Label(robot_frame, text="Preset").grid(row=0, column=0, sticky="w", padx=4, pady=3)
        preset = ttk.Combobox(
            robot_frame,
            textvariable=self.robot_preset_var,
            values=("planar-2r", "iiwa", "ur5", "franka"),
            state="readonly",
            width=16,
        )
        preset.grid(row=0, column=1, sticky="ew", padx=4, pady=3)
        preset.bind("<<ComboboxSelected>>", lambda _event: self._load_preset())
        ttk.Button(robot_frame, text="Load preset", command=self._load_preset).grid(row=0, column=2, padx=4)
        ttk.Label(robot_frame, text="Custom JSON").grid(row=1, column=0, sticky="w", padx=4, pady=3)
        ttk.Entry(robot_frame, textvariable=self.robot_file_var).grid(row=1, column=1, sticky="ew", padx=4)
        ttk.Button(robot_frame, text="Browse…", command=self._browse_robot).grid(row=1, column=2, padx=4)
        ttk.Button(robot_frame, text="Load JSON", command=self._load_custom_robot).grid(
            row=2, column=1, sticky="w", padx=4, pady=3
        )
        self.robot_summary = ttk.Label(robot_frame, text=self._robot_summary())
        self.robot_summary.grid(row=3, column=0, columnspan=3, sticky="w", padx=4, pady=3)
        robot_frame.columnconfigure(1, weight=1)

        domain_frame = ttk.LabelFrame(parent, text="C-space AABB (radians or model units)")
        domain_frame.pack(fill="both", expand=True, padx=8, pady=6)
        self.joint_scroll = _ScrollableFrame(domain_frame, height=245)
        self.joint_scroll.pack(fill="both", expand=True, padx=3, pady=3)
        domain_buttons = ttk.Frame(domain_frame)
        domain_buttons.pack(fill="x", padx=3, pady=3)
        ttk.Button(domain_buttons, text="Local box", command=self._set_local_domain).pack(side="left", padx=2)
        ttk.Button(domain_buttons, text="Point box", command=self._set_point_domain).pack(side="left", padx=2)
        ttk.Button(domain_buttons, text="Full limits", command=self._set_full_domain).pack(side="left", padx=2)

        scene_frame = ttk.LabelFrame(parent, text="Shared scene")
        scene_frame.pack(fill="x", padx=8, pady=6)
        ttk.Label(scene_frame, textvariable=self.scene_label_var).grid(
            row=0, column=0, columnspan=4, sticky="w", padx=4, pady=3
        )
        ttk.Button(scene_frame, text="Generate corner probe", command=self._generate_probe).grid(
            row=1, column=0, columnspan=2, sticky="w", padx=4, pady=3
        )
        ttk.Entry(scene_frame, textvariable=self.scene_file_var).grid(row=2, column=0, columnspan=2, sticky="ew", padx=4)
        ttk.Button(scene_frame, text="Browse…", command=self._browse_scene).grid(row=2, column=2, padx=3)
        ttk.Button(scene_frame, text="Load scene", command=self._load_scene).grid(row=2, column=3, padx=3)
        ttk.Label(scene_frame, text="Editable AABB center").grid(row=3, column=0, sticky="w", padx=4)
        for axis, label in enumerate("xyz"):
            ttk.Label(scene_frame, text=label).grid(row=3, column=axis + 1, sticky="e")
            ttk.Entry(scene_frame, textvariable=self.obstacle_center_vars[axis], width=9).grid(
                row=4, column=axis + 1, padx=2, pady=2
            )
        ttk.Label(scene_frame, text="Half-widths").grid(row=5, column=0, sticky="w", padx=4)
        for axis in range(3):
            ttk.Entry(scene_frame, textvariable=self.obstacle_half_vars[axis], width=9).grid(
                row=5, column=axis + 1, padx=2, pady=2
            )
        scene_frame.columnconfigure(1, weight=1)

        run_frame = ttk.Frame(parent)
        run_frame.pack(fill="x", padx=8, pady=8)
        ttk.Label(run_frame, text="Envelope padding").pack(side="left")
        ttk.Entry(run_frame, textvariable=self.padding_var, width=10).pack(side="left", padx=5)
        self.recompute_button = ttk.Button(run_frame, text="Recompute all 8", command=self.recompute)
        self.recompute_button.pack(side="right")

    def _build_display_tab(self, parent: Any) -> None:
        variants_frame = ttk.LabelFrame(parent, text="Visible envelope combinations")
        variants_frame.pack(fill="x", padx=8, pady=6)
        for variant in EXPERIMENT_VARIANTS:
            ttk.Checkbutton(
                variants_frame,
                text=variant.label,
                variable=self.variant_vars[variant.key],
                command=self.redraw,
            ).pack(anchor="w", padx=4, pady=1)
        variant_buttons = ttk.Frame(variants_frame)
        variant_buttons.pack(fill="x", padx=3, pady=3)
        ttk.Button(variant_buttons, text="All", command=lambda: self._set_variants(True)).pack(side="left", padx=2)
        ttk.Button(variant_buttons, text="None", command=lambda: self._set_variants(False)).pack(side="left", padx=2)
        ttk.Button(variant_buttons, text="IFK only", command=self._set_ifk_variants).pack(side="left", padx=2)

        options_frame = ttk.LabelFrame(parent, text="Geometry layers")
        options_frame.pack(fill="x", padx=8, pady=6)
        option_labels = {
            "surface": "Translucent surfaces",
            "wireframe": "Wireframe edges",
            "robot": "Center-configuration robot",
            "obstacles": "Scene obstacles",
            "endpoint_aabbs": "Endpoint AABBs",
            "enclosing_aabbs": "Per-link enclosing AABBs",
            "support_points": "k-DOP / hull support points",
            "legend": "Legend",
            "equal_axes": "Equal axis scale",
        }
        for key, label in option_labels.items():
            ttk.Checkbutton(options_frame, text=label, variable=self.display_vars[key], command=self.redraw).pack(
                anchor="w", padx=4, pady=1
            )

        focus_frame = ttk.LabelFrame(parent, text="Focus and appearance")
        focus_frame.pack(fill="x", padx=8, pady=6)
        ttk.Label(focus_frame, text="Link").grid(row=0, column=0, sticky="w", padx=4, pady=3)
        self.link_selector = ttk.Combobox(focus_frame, textvariable=self.link_var, state="readonly")
        self.link_selector.grid(row=0, column=1, sticky="ew", padx=4, pady=3)
        self.link_selector.bind("<<ComboboxSelected>>", lambda _event: self.redraw())
        ttk.Label(focus_frame, text="Surface alpha").grid(row=1, column=0, sticky="w", padx=4)
        ttk.Scale(
            focus_frame,
            from_=0.03,
            to=0.75,
            variable=self.alpha_var,
            command=lambda _value: self.redraw(),
        ).grid(row=1, column=1, sticky="ew", padx=4)
        focus_frame.columnconfigure(1, weight=1)

        camera_frame = ttk.LabelFrame(parent, text="Camera")
        camera_frame.pack(fill="x", padx=8, pady=6)
        for label, view in (
            ("Isometric", (25, -60)),
            ("Top", (90, -90)),
            ("Front", (0, -90)),
            ("Side", (0, 0)),
        ):
            ttk.Button(camera_frame, text=label, command=lambda angles=view: self._set_camera(*angles)).pack(
                side="left", padx=2, pady=4
            )
        ttk.Button(parent, text="Autoscale and redraw", command=lambda: self.redraw(force_autoscale=True)).pack(
            anchor="e", padx=10, pady=8
        )

    def _build_results_tab(self, parent: Any) -> None:
        columns = ("combination", "cert", "eval", "volume", "overlap", "distance", "validator", "ms")
        self.results_tree = ttk.Treeview(parent, columns=columns, show="headings", height=13)
        headings = {
            "combination": "Combination",
            "cert": "Endpoint certified",
            "eval": "Evaluated q",
            "volume": "Σ enclosing AABB volume",
            "overlap": "Any overlap",
            "distance": "Min distance LB",
            "validator": "Validator",
            "ms": "ms",
        }
        widths = {"combination": 210, "cert": 100, "eval": 80, "volume": 130, "overlap": 85,
                  "distance": 105, "validator": 90, "ms": 65}
        for column in columns:
            self.results_tree.heading(column, text=headings[column])
            self.results_tree.column(column, width=widths[column], anchor="center")
        y_scroll = ttk.Scrollbar(parent, orient="vertical", command=self.results_tree.yview)
        x_scroll = ttk.Scrollbar(parent, orient="horizontal", command=self.results_tree.xview)
        self.results_tree.configure(yscrollcommand=y_scroll.set, xscrollcommand=x_scroll.set)
        self.results_tree.grid(row=0, column=0, sticky="nsew", padx=(8, 0), pady=(8, 0))
        y_scroll.grid(row=0, column=1, sticky="ns", pady=(8, 0))
        x_scroll.grid(row=1, column=0, sticky="ew", padx=(8, 0))
        self.results_tree.bind("<<TreeviewSelect>>", self._show_result_detail)

        self.detail = tk.Text(parent, height=18, wrap="word")
        self.detail.grid(row=2, column=0, columnspan=2, sticky="nsew", padx=8, pady=8)
        export_frame = ttk.Frame(parent)
        export_frame.grid(row=3, column=0, columnspan=2, sticky="ew", padx=8, pady=(0, 8))
        ttk.Button(export_frame, text="Export JSON…", command=self._export_json).pack(side="left", padx=2)
        ttk.Button(export_frame, text="Export CSV…", command=self._export_csv).pack(side="left", padx=2)
        ttk.Button(export_frame, text="Show selected envelope", command=self._show_selected_result).pack(
            side="right", padx=2
        )
        parent.rowconfigure(0, weight=2)
        parent.rowconfigure(2, weight=1)
        parent.columnconfigure(0, weight=1)

    def _robot_summary(self) -> str:
        return f"{self.robot.name}: {self.robot.dimension} DoF, {self.robot.link_count} links"

    def _rebuild_joint_editor(self) -> None:
        for child in self.joint_scroll.body.winfo_children():
            child.destroy()
        self.joint_vars.clear()
        ttk.Label(self.joint_scroll.body, text="Joint").grid(row=0, column=0, padx=3)
        ttk.Label(self.joint_scroll.body, text="Model limit").grid(row=0, column=1, padx=3)
        ttk.Label(self.joint_scroll.body, text="Lower").grid(row=0, column=2, padx=3)
        ttk.Label(self.joint_scroll.body, text="Upper").grid(row=0, column=3, padx=3)
        for index, (axis, limit) in enumerate(zip(self.domain.axes, self.robot.joint_limits), start=1):
            lower = tk.StringVar(value=f"{axis.lower:.8g}")
            upper = tk.StringVar(value=f"{axis.upper:.8g}")
            self.joint_vars.append((lower, upper))
            ttk.Label(self.joint_scroll.body, text=f"q[{index - 1}]").grid(row=index, column=0, padx=3, pady=2)
            ttk.Label(self.joint_scroll.body, text=f"[{limit.lower:.4g}, {limit.upper:.4g}]").grid(
                row=index, column=1, padx=3, pady=2
            )
            ttk.Entry(self.joint_scroll.body, textvariable=lower, width=12).grid(row=index, column=2, padx=3, pady=2)
            ttk.Entry(self.joint_scroll.body, textvariable=upper, width=12).grid(row=index, column=3, padx=3, pady=2)
        self.link_selector["values"] = ["All links"] + [f"Link {index}" for index in range(self.robot.link_count)]
        self.link_var.set("All links")

    def _domain_bounds_from_editor(self) -> list[list[float]]:
        return [[float(lower.get()), float(upper.get())] for lower, upper in self.joint_vars]

    def _set_domain_entries(self, domain: Any) -> None:
        self.domain = domain
        for (lower_var, upper_var), axis in zip(self.joint_vars, domain.axes):
            lower_var.set(f"{axis.lower:.8g}")
            upper_var.set(f"{axis.upper:.8g}")

    def _set_local_domain(self) -> None:
        self._set_domain_entries(default_domain(self.robot))

    def _set_point_domain(self) -> None:
        bounds = []
        for lower_var, upper_var in self.joint_vars:
            center = 0.5 * (float(lower_var.get()) + float(upper_var.get()))
            bounds.append([center, center])
        self._set_domain_entries(domain_from_bounds(self.robot, bounds))

    def _set_full_domain(self) -> None:
        self._set_domain_entries(
            domain_from_bounds(self.robot, [[limit.lower, limit.upper] for limit in self.robot.joint_limits])
        )

    def _browse_robot(self) -> None:
        path = filedialog.askopenfilename(title="Select robot JSON", filetypes=(("JSON files", "*.json"), ("All", "*")))
        if path:
            self.robot_file_var.set(path)

    def _browse_scene(self) -> None:
        path = filedialog.askopenfilename(title="Select scene JSON", filetypes=(("JSON files", "*.json"), ("All", "*")))
        if path:
            self.scene_file_var.set(path)

    def _load_preset(self) -> None:
        if self._computing:
            self.status.set("Wait for the current computation before replacing the robot.")
            return
        try:
            self._replace_robot(load_preset_robot(self.robot_preset_var.get()))
        except Exception as error:
            messagebox.showerror("Robot load failed", str(error))

    def _load_custom_robot(self) -> None:
        if self._computing:
            self.status.set("Wait for the current computation before replacing the robot.")
            return
        try:
            path_text = self.robot_file_var.get().strip()
            if not path_text:
                raise ValueError("select a robot JSON file")
            path = Path(path_text).expanduser()
            self._replace_robot(__import__("rbfsafe").SerialRobotModel.from_json(path))
        except Exception as error:
            messagebox.showerror("Robot load failed", str(error))

    def _replace_robot(self, robot: Any) -> None:
        self.robot = robot
        self.domain = default_domain(robot)
        self.robot_summary.configure(text=self._robot_summary())
        self._rebuild_joint_editor()
        self._generate_probe(recompute=False)
        self.recompute()

    def _load_scene(self) -> None:
        if self._computing:
            self.status.set("Wait for the current computation before replacing the scene.")
            return
        try:
            path_text = self.scene_file_var.get().strip()
            if not path_text:
                raise ValueError("select a scene JSON file")
            path = Path(path_text).expanduser()
            self.scene = __import__("rbfsafe").SceneSnapshot.from_json(path)
            self._scene_editable = False
            self.scene_label_var.set(f"Loaded: {self.scene.version} ({len(self.scene.obstacles)} obstacles)")
            self.recompute()
        except Exception as error:
            messagebox.showerror("Scene load failed", str(error))

    def _try_set_editable_scene(self, scene: Any) -> None:
        if len(scene.obstacles) == 1 and scene.obstacles[0].bounds.aabb is not None:
            box = scene.obstacles[0].bounds.aabb
            for axis in range(3):
                center = 0.5 * (box.lower[axis] + box.upper[axis])
                half = 0.5 * (box.upper[axis] - box.lower[axis])
                self.obstacle_center_vars[axis].set(f"{center:.8g}")
                self.obstacle_half_vars[axis].set(f"{half:.8g}")
            self._scene_editable = True

    def _generate_probe(self, recompute: bool = True) -> None:
        if self._computing:
            self.status.set("Wait for the current computation before replacing the scene.")
            return
        try:
            domain = domain_from_bounds(self.robot, self._domain_bounds_from_editor())
            self.scene = make_probe_scene(self.robot, domain)
            self._try_set_editable_scene(self.scene)
            self.scene_label_var.set("Editable generated corner probe (one AABB)")
            if recompute:
                self.recompute()
        except Exception as error:
            messagebox.showerror("Probe generation failed", str(error))

    def _scene_from_editor(self) -> Any:
        if not self._scene_editable:
            return self.scene
        api = __import__("rbfsafe")
        center = [float(value.get()) for value in self.obstacle_center_vars]
        half = [float(value.get()) for value in self.obstacle_half_vars]
        if any(not math.isfinite(value) or value < 0.0 for value in half):
            raise ValueError("obstacle half-widths must be finite and non-negative")
        box = api.WorkspaceAabb(
            [center[axis] - half[axis] for axis in range(3)],
            [center[axis] + half[axis] for axis in range(3)],
        )
        return api.SceneSnapshot([api.SceneObstacle("editable-probe", box)], "envelope-lab-editable-v1")

    def recompute(self) -> None:
        if self._computing:
            return
        try:
            domain = domain_from_bounds(self.robot, self._domain_bounds_from_editor())
            scene = self._scene_from_editor()
            padding = float(self.padding_var.get())
            if not math.isfinite(padding) or padding < 0.0:
                raise ValueError("envelope padding must be finite and non-negative")
        except Exception as error:
            messagebox.showerror("Invalid experiment input", str(error))
            return
        self.domain = domain
        self.scene = scene
        robot = self.robot
        self._computing = True
        self.recompute_button.configure(state="disabled")
        self.status.set("Computing all eight combinations…")

        def worker() -> None:
            try:
                report = run_experiment(robot, scene, domain, padding)
                self.root.after(0, lambda: self._finish_compute(report, None))
            except Exception as error:  # C++ validation errors surface as Python exceptions
                self.root.after(0, lambda message=str(error): self._finish_compute(None, message))

        threading.Thread(target=worker, daemon=True).start()

    def _finish_compute(self, report: EnvelopeExperimentReport | None, error: str | None) -> None:
        self._computing = False
        self.recompute_button.configure(state="normal")
        if error is not None:
            self.status.set(f"Computation failed: {error}")
            messagebox.showerror("Experiment failed", error)
            return
        assert report is not None
        self.report = report
        total_ms = sum(result.computation_ms for result in report.results)
        self.status.set(f"Computed 8 combinations for {report.robot.name} in {total_ms:.1f} ms")
        self._update_results_table()
        self.redraw(force_autoscale=True)

    def _selected_link_indices(self) -> list[int]:
        value = self.link_var.get()
        if value == "All links":
            return list(range(self.robot.link_count))
        try:
            return [int(value.split()[1])]
        except (IndexError, ValueError):
            return list(range(self.robot.link_count))

    def _set_variants(self, value: bool) -> None:
        for variable in self.variant_vars.values():
            variable.set(value)
        self.redraw()

    def _set_ifk_variants(self) -> None:
        for variant in EXPERIMENT_VARIANTS:
            self.variant_vars[variant.key].set(variant.endpoint_source == "ifk_aa")
        self.redraw()

    def _set_camera(self, elevation: float, azimuth: float) -> None:
        self.axes.view_init(elev=elevation, azim=azimuth)
        self.canvas.draw_idle()

    def redraw(self, force_autoscale: bool = False) -> None:
        if self.report is None:
            return
        elevation, azimuth = self.axes.elev, self.axes.azim
        self.axes.clear()
        alpha = float(self.alpha_var.get())
        surface = self.display_vars["surface"].get()
        wireframe = self.display_vars["wireframe"].get()
        support_points = self.display_vars["support_points"].get()
        selected_links = self._selected_link_indices()
        extent_points: list[list[float]] = []
        legend_handles = []
        legend_labels = []

        if self.display_vars["robot"].get():
            points = self.robot.forward_kinematics(self.domain.center)
            self.axes.plot(
                [point[0] for point in points],
                [point[1] for point in points],
                [point[2] for point in points],
                color="#202020",
                marker="o",
                linewidth=2.2,
                markersize=4,
                label="Robot at C-space center",
            )
            extent_points.extend([list(point) for point in points])

        if self.display_vars["obstacles"].get():
            for obstacle in self.scene.obstacles:
                _draw_envelope(
                    self.axes,
                    obstacle.bounds,
                    "#222222",
                    min(0.5, alpha + 0.12),
                    surface,
                    True,
                    "solid",
                    support_points,
                )
                extent_points.extend(_aabb_vertices(obstacle.bounds.enclosing_aabb()))
            if self.scene.obstacles:
                handle = self.axes.plot([], [], [], color="#222222", linewidth=3)[0]
                legend_handles.append(handle)
                legend_labels.append("Scene obstacle(s)")

        selected_sources = set()
        for result in self.report.results:
            variant = result.variant
            if not self.variant_vars[variant.key].get():
                continue
            selected_sources.add(variant.endpoint_source)
            linestyle = "solid" if variant.endpoint_source == "ifk_aa" else "dashed"
            for link_index in selected_links:
                envelope = result.workspace_envelope.links[link_index]
                _draw_envelope(
                    self.axes,
                    envelope,
                    variant.color,
                    alpha,
                    surface,
                    wireframe,
                    linestyle,
                    support_points,
                )
                box = envelope.enclosing_aabb()
                extent_points.extend(_aabb_vertices(box))
                if self.display_vars["enclosing_aabbs"].get():
                    _draw_aabb(self.axes, box, variant.color, 0.0, False, True, "dotted", 0.55)
            handle = self.axes.plot([], [], [], color=variant.color, linestyle=linestyle, linewidth=2)[0]
            legend_handles.append(handle)
            legend_labels.append(variant.label)

        if self.display_vars["endpoint_aabbs"].get():
            source_colors = {"ifk_aa": "#ffbf00", "crit_sample": "#17becf"}
            for source in selected_sources:
                endpoints = self.report.endpoint_results[source].endpoints
                for link_index in selected_links:
                    for endpoint_index in (2 * link_index, 2 * link_index + 1):
                        box = endpoints[endpoint_index]
                        _draw_aabb(self.axes, box, source_colors[source], 0.0, False, True, "dashdot", 0.75)
                        extent_points.extend(_aabb_vertices(box))

        if not extent_points:
            extent_points = [[-1.0, -1.0, -1.0], [1.0, 1.0, 1.0]]
        if force_autoscale or self.display_vars["equal_axes"].get():
            self._set_equal_limits(extent_points)
        self.axes.set_xlabel("workspace x")
        self.axes.set_ylabel("workspace y")
        self.axes.set_zlabel("workspace z")
        self.axes.set_title(
            f"{self.robot.name} — C-space volume {self.domain.volume:.6g}\n"
            "Solid lines: certified IFK-AA source; dashed lines: diagnostic CritSample source"
        )
        self.axes.grid(True, alpha=0.25)
        self.axes.view_init(elev=elevation, azim=azimuth)
        if self.display_vars["legend"].get() and legend_handles:
            self.axes.legend(legend_handles, legend_labels, loc="upper left", fontsize=8)
        self.canvas.draw_idle()

    def _set_equal_limits(self, points: list[list[float]]) -> None:
        lower = [min(point[axis] for point in points) for axis in range(3)]
        upper = [max(point[axis] for point in points) for axis in range(3)]
        center = [0.5 * (lower[axis] + upper[axis]) for axis in range(3)]
        radius = 0.55 * max(max(upper[axis] - lower[axis] for axis in range(3)), 1e-3)
        self.axes.set_xlim(center[0] - radius, center[0] + radius)
        self.axes.set_ylim(center[1] - radius, center[1] + radius)
        self.axes.set_zlim(center[2] - radius, center[2] + radius)
        try:
            self.axes.set_box_aspect((1, 1, 1))
        except AttributeError:  # pragma: no cover - old matplotlib fallback
            pass

    def _update_results_table(self) -> None:
        assert self.report is not None
        for item in self.results_tree.get_children():
            self.results_tree.delete(item)
        for result in self.report.results:
            validator = (
                "YES" if result.validator_certified_free is True
                else "NO" if result.validator_certified_free is False
                else "N/A"
            )
            self.results_tree.insert(
                "",
                "end",
                iid=result.variant.key,
                values=(
                    result.variant.label,
                    "yes" if result.endpoint_bounds_certified else "no",
                    result.evaluated_configurations,
                    _format_number(result.enclosing_aabb_volume_sum),
                    "yes" if result.overlaps_any_obstacle else "no",
                    _format_number(result.distance_lower_bound),
                    validator,
                    f"{result.computation_ms:.2f}",
                ),
            )
        first = self.report.results[0].variant.key
        self.results_tree.selection_set(first)
        self.results_tree.focus(first)
        self._show_result_detail(None)

    def _selected_result(self) -> Any | None:
        if self.report is None or not self.results_tree.selection():
            return None
        key = self.results_tree.selection()[0]
        return next((result for result in self.report.results if result.variant.key == key), None)

    def _show_result_detail(self, _event: Any) -> None:
        result = self._selected_result()
        if result is None:
            return
        lines = [
            result.variant.label,
            "",
            f"Endpoint source: {result.variant.endpoint_source}",
            f"Endpoint bounds certified: {result.endpoint_bounds_certified}",
            f"Evaluated configurations: {result.evaluated_configurations}",
            f"Workspace envelope type: {result.variant.envelope_type}",
            f"k-DOP k: {result.variant.kdop_k if result.variant.envelope_type == 'kdop' else 'N/A'}",
            f"Validator eligible: {result.validator_eligible}",
            f"Validator result: {result.validator_disposition}",
            f"Validator algorithm: {result.validator_algorithm or 'N/A'}",
            f"Validator clearance LB: {_format_number(result.validator_clearance_lower_bound)}",
            "",
            "Per-link metrics:",
        ]
        for link, (volume, overlap, distance) in enumerate(
            zip(result.enclosing_aabb_volumes, result.link_obstacle_overlaps, result.link_distance_lower_bounds)
        ):
            lines.append(
                f"  link {link}: enclosing-AABB volume={volume:.8g}, "
                f"overlap={overlap}, distance lower bound={distance:.8g}"
            )
        lines.extend(
            [
                "",
                "Note: Σ volume is the sum of per-link enclosing-AABB volumes, not their geometric union.",
                "CritSample rows are diagnostic and intentionally have no CertifiedRegion validator result.",
            ]
        )
        self.detail.configure(state="normal")
        self.detail.delete("1.0", "end")
        self.detail.insert("1.0", "\n".join(lines))
        self.detail.configure(state="disabled")

    def _show_selected_result(self) -> None:
        result = self._selected_result()
        if result is None:
            return
        for variable in self.variant_vars.values():
            variable.set(False)
        self.variant_vars[result.variant.key].set(True)
        self.redraw(force_autoscale=True)

    def _export_json(self) -> None:
        if self.report is None:
            return
        path = filedialog.asksaveasfilename(
            title="Export envelope experiment JSON",
            defaultextension=".json",
            filetypes=(("JSON files", "*.json"),),
        )
        if path:
            export_report_json(self.report, path)
            self.status.set(f"Exported {path}")

    def _export_csv(self) -> None:
        if self.report is None:
            return
        path = filedialog.asksaveasfilename(
            title="Export envelope experiment CSV",
            defaultextension=".csv",
            filetypes=(("CSV files", "*.csv"),),
        )
        if path:
            export_report_csv(self.report, path)
            self.status.set(f"Exported {path}")

    def run(self) -> None:
        self.root.mainloop()
