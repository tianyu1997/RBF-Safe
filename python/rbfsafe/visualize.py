"""Optional matplotlib visualization for 2-D atlas slices."""

from __future__ import annotations

from collections.abc import Sequence


def plot_slice(atlas, dims: tuple[int, int] = (0, 1), fixed: Sequence[float] | None = None):
    try:
        import matplotlib.pyplot as plt
        from matplotlib.patches import Rectangle
    except ImportError as error:  # pragma: no cover - dependency-specific
        raise RuntimeError("install rbfsafe[visualization] to plot atlas slices") from error

    x_dim, y_dim = dims
    if x_dim == y_dim or min(dims) < 0 or max(dims) >= atlas.dimension:
        raise ValueError("dims must name two distinct atlas dimensions")
    point = list(fixed) if fixed is not None else [0.0] * atlas.dimension
    if len(point) != atlas.dimension:
        raise ValueError("fixed must have atlas.dimension entries")

    figure, axes = plt.subplots()
    for region in atlas.regions:
        bounds = region.bounds.axes
        if any(
            dimension not in dims and not bounds[dimension].contains(point[dimension])
            for dimension in range(atlas.dimension)
        ):
            continue
        x_axis, y_axis = bounds[x_dim], bounds[y_dim]
        axes.add_patch(
            Rectangle(
                (x_axis.lower, y_axis.lower),
                x_axis.width,
                y_axis.width,
                alpha=0.28,
                edgecolor="black",
                facecolor=f"C{region.component % 10}",
            )
        )
    axes.autoscale()
    axes.set_aspect("equal", adjustable="box")
    axes.set_xlabel(f"q[{x_dim}]")
    axes.set_ylabel(f"q[{y_dim}]")
    axes.set_title("RBF-Safe certified-region slice")
    return figure
