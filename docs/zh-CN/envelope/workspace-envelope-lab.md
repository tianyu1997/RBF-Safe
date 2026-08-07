# 工作空间包络交互实验

`rbfsafe-envelope-lab` 使用同一个机器人、场景和 `CspaceAabb` 比较以下八组实验：

```text
IFK-AA × AABB
IFK-AA × OBB
IFK-AA × 14-DOP
IFK-AA × 26-DOP
IFK-AA × SupportHull
CritSample × AABB
CritSample × 26-DOP
CritSample × SupportHull
```

每组记录：

- `endpoint_bounds_certified`；
- `evaluated_configurations`；
- 每条连杆的 enclosing-AABB 体积及其总和；
- 每条连杆是否与任意障碍物重叠；
- 每条连杆到场景的距离下界及全局最小值；
- 是否具备区域验证资格；
- 验证器是否返回 `CertifiedFree`；
- 验证器算法名、clearance 下界和计算时间。

CritSample 的端点界不是保守认证结果，因此其验证器列显示 `N/A`。实验仍会计算 CritSample 包络的重叠与距离，供紧度和诊断比较使用，但不会把它误报为 `CertifiedRegion`。

## 1. 安装与启动

安装 Python 绑定和可视化依赖：

```bash
python -m pip install -e ".[visualization]"
```

从命令行启动：

```bash
rbfsafe-envelope-lab
```

也可以直接运行示例：

```bash
python examples/workspace_envelope_lab.py
```

GUI 使用 Tk 和 Matplotlib。常见 Python 发行版通常自带 Tk；精简 Linux 环境可能还需要安装系统的 Tk 包。

## 2. 机器人选择

界面内置四个机器人：

- `planar-2r`：二维二连杆教学模型；
- `iiwa`：KUKA LBR iiwa 14；
- `ur5`：Universal Robots UR5；
- `franka`：Franka Research 3 参数模型。

预设参数与仓库 release fixtures 对齐，但直接内嵌在 Python 模块中，因此安装 wheel 后也能使用。

也可以在界面选择自己的机器人 JSON，或使用：

```bash
rbfsafe-envelope-lab --robot-file path/to/robot.json
```

文件必须符合 `docs/core/input-formats.md` 中的 RBF-Safe robot schema 1。加载新机器人后，界面会按自由度动态重建 C-space 编辑器和连杆选择器。

## 3. C-space AABB 控制

Experiment 页为每个关节显示：

- 模型关节限制；
- 当前区域下界；
- 当前区域上界。

三个快捷操作为：

- `Local box`：以各关节限制中点为中心建立小区域；
- `Point box`：把当前每个区间折叠到中点；
- `Full limits`：使用完整关节限制。

编辑完成后点击 `Recompute all 8`。计算在后台线程中进行；高自由度机器人使用完整限制时，CritSample 可能需要评估较多配置。

命令行也可以覆盖初始区间：

```bash
rbfsafe-envelope-lab \
  --robot ur5 \
  --q-range 0:-0.2:0.2 \
  --q-range 1:-0.3:0.1
```

## 4. 场景控制

默认场景是根据机器人当前中心配置自动生成的 `corner probe`：在最长连杆附近放置一个很小的 AABB，便于观察 AABB 的角落假重叠与 OBB、k-DOP、SupportHull 的差异。

生成后可以直接编辑障碍物：

- 三维中心 `x/y/z`；
- 三个半宽。

也可以加载任意 scene schema 1 或 typed-envelope schema 2：

```bash
rbfsafe-envelope-lab --scene-file path/to/scene.json
```

八组实验始终共享同一个 `SceneSnapshot`。加载的场景可以混合 AABB、OBB、k-DOP 和 SupportHull 障碍物。

`Envelope padding` 会统一加到所有八组的连杆半径上。

## 5. 3D 显示控制

Display 页可以独立选择八种包络组合，并提供：

- 一键显示全部、隐藏全部或只显示 IFK-AA；
- 只看某一条连杆或查看全部连杆；
- 半透明表面；
- 线框；
- C-space 中心配置对应的机器人骨架；
- 场景障碍物；
- IFK-AA/CritSample 原始端点 AABB；
- 每个 typed envelope 的 enclosing AABB；
- k-DOP 顶点和 SupportHull 支持点；
- 图例与等比例坐标轴；
- 表面透明度；
- 等轴、顶视、正视和侧视相机预设。

在 3D 画布上拖动鼠标可自由旋转视角。Matplotlib 工具栏还提供平移、缩放、复位和截图保存。

图中使用实线表示 IFK-AA 来源、虚线表示 CritSample 来源。每种组合使用固定颜色。

### SupportHull 显示说明

SupportHull 的真实运算使用精确 support mapping。GUI 为了显示其球形 Minkowski 扩张，会在球面方向上采样支持点并绘制近似表面；这是显示网格，不参与碰撞或认证计算。结果表中的重叠和距离均来自 C++ 几何内核，而不是显示网格。

## 6. 结果表

Results 页主表包含：

| 字段 | 含义 |
|---|---|
| Combination | 端点来源与 workspace 包络组合 |
| Endpoint certified | 端点界是否具有保守认证资格 |
| Evaluated q | CritSample 实际运行 FK 的配置数量；IFK-AA 为 0 |
| Σ enclosing AABB volume | 每条连杆外包 AABB 体积之和 |
| Any overlap | 是否有任意连杆与任意障碍物无法证明分离 |
| Min distance LB | 所有连杆—障碍物对的最小距离下界 |
| Validator | `YES`、`NO` 或 CritSample 的 `N/A` |
| ms | 该组合及其验证器计算耗时 |

选择一行后，下方会显示逐连杆体积、重叠、距离下界、算法名和验证 disposition。

体积总和不是各连杆外包盒几何并集的体积；它用于在相同机器人和区域下比较不同方法的包络紧度。

点击 `Show selected envelope` 可以暂时隐藏其他组合，单独观察选中结果。

## 7. 无 GUI 运行与导出

在服务器或 CI 中可以不启动窗口：

```bash
rbfsafe-envelope-lab \
  --robot franka \
  --no-gui \
  --export-json envelope-results.json \
  --export-csv envelope-results.csv
```

JSON 保留逐连杆数组，适合后续分析；CSV 保存八组标量汇总。

Python 中也可以直接调用计算层：

```python
from rbfsafe.envelope_lab import (
    default_domain,
    load_preset_robot,
    make_probe_scene,
    run_experiment,
)

robot = load_preset_robot("iiwa")
domain = default_domain(robot)
scene = make_probe_scene(robot, domain)
report = run_experiment(robot, scene, domain)

for result in report.results:
    print(
        result.variant.label,
        result.endpoint_bounds_certified,
        result.evaluated_configurations,
        result.enclosing_aabb_volume_sum,
        result.overlaps_any_obstacle,
        result.distance_lower_bound,
        result.validator_certified_free,
    )
```

该计算接口不导入 Matplotlib，因而可以在无可视化依赖的环境中使用和测试。

## 8. 推荐观察步骤

1. 选择 `planar-2r` 和 `Point box`，只显示 IFK-AA AABB 与 SupportHull。
2. 打开 enclosing AABB，观察 SupportHull 自身比它的外包 AABB 紧在哪里。
3. 缓慢移动小障碍物，使 AABB 显示 overlap，而 SupportHull 仍可证明分离。
4. 依次显示 OBB、14-DOP、26-DOP，比较方向数与包络紧度。
5. 打开 endpoint AABBs，观察四种连杆形状都从相同的近端/远端盒构造。
6. 同时显示 IFK-AA 与 CritSample，比较端点界、包络体积和采样数。
7. 扩大 `CspaceAabb`，观察 IFK-AA 余项和外包体积增长。
8. 切换 UR5、iiwa 和 Franka，只显示一条连杆，避免多包络叠加遮挡。
9. 加载 typed scene，观察不同类型障碍物之间的 support-mapping 分离。
10. 导出 JSON，对各连杆而不只是总体积进行比较。

解释结果时始终保持：更小或更贴合的可视化形状不等于认证来源。只有 `endpoint_bounds_certified == true` 且验证器返回 `CertifiedFree`，才能支撑区域安全结论。
