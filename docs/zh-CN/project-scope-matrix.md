# `project.md` 完成矩阵

> 英文原文：[project.md completion matrix](../project-scope-matrix.md)；机器清单：[project_scope_manifest.json](../../data/project_scope_manifest.json)

CI 使用 `tools/check_project_scope.py` 验证原始工程计划的 18 项需求。每项必须同时存在公共 API、行为测试和维护文档证据，中文说明不替代机器清单中的精确符号检查。

## 应用层

| 需求 | 已实现证据 |
|---|---|
| Safe IK | `SafeIkSolver`、区域内求解、认证连通路线 |
| 学习/VLA shield | 关节、末端、轨迹动作的 ACCEPT/REPAIR/REJECT |
| 优化适配器 | TrajOpt、CHOMP、STOMP、MPC |
| MoveIt 2 | 请求/响应、约束采样、Kinematics 插件 |
| OMPL | 状态/运动有效性、采样、RRT/RRT*/PRM/BIT* |
| 规划输出审核 | CERTIFIED/PARTIAL/INVALID、覆盖率与缺口 |

## 核心与数据

| 需求 | 已实现证据 |
|---|---|
| Atlas 与连通性 | 区域、证书、索引、图、路由与持久化 |
| 区域/走廊认证 | IFK-AA、OBB、Portal、HiPaC |
| 几何 | FK、解析 Jacobian、保守连杆包络 |
| LECT/HiPaC | 稳定路径分区与递归走廊覆盖 |
| 动态更新 | 失效、重新验证、局部修复、版本历史 |
| 证据等级 | Unknown 到 RuntimeExecutable 的窄范围层级 |
| 区域族 | AABB、OBB、Portal、TrajectoryTube、Zonotope、Taylor |
| C++/Python | 构建、保存、查询、审核、IK、更新、shield |

## 产品与质量

| 需求 | 已实现证据 |
|---|---|
| 基准 | IIWA、UR5、Panda、Franka 与四类场景 |
| v1 几何产品 | 区域、规划集成、Safe IK、审核 |
| v2 智能安全 | 学习策略、安全门与运行监视 |
| v3 持久/多机器人/部署 | 安全记忆、车队、部署、执行与审计 |

“完成”表示仓库中的软件声明具有可追踪实现，不表示物理硬件、感知、时钟、网络和控制器已经通过安全认证。
