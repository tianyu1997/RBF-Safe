# 认证区域优化适配器

> 英文原文：[Certified-region optimization adapters](../optimization.md)

`RBFSafe::optimization` 把异构认证凸区域编译为求解器中立的约束记录，并为 TrajOpt、CHOMP、STOMP 和 MPC 提供前端。

## 工作流

1. 选择与机器人/场景兼容的认证区域；
2. 将 AABB、OBB 等几何转换为线性/凸约束描述；
3. 对候选轨迹的每个 waypoint 绑定允许区域；
4. 调用外部优化器；
5. 对优化结果执行独立连续轨迹审核。

适配器只生成约束和确定性身份，不内嵌具体商业或开源求解器。投影和区域选择具有最大迭代、最大区域测试和取消限制。

优化器报告“可行”不等于 RBF-Safe 认证。数值容差、waypoint 离散化和求解器近似可能在点之间留下间隙，因此最终输出必须由 `TrajectoryAuditor` 或走廊验证器检查。
