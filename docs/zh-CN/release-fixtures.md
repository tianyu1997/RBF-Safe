# 发布固定数据与基准

> 英文原文：[Release fixtures and benchmark](../release-fixtures.md)

发布 fixtures 为 IIWA shelf、UR5 industrial-cell、Panda clutter 和 Franka mobile-manipulation 提供确定性机器人、场景和起终点输入，不依赖旧仓库、下载资源、ROS 或先前二进制。

`rbfsafe-release-benchmark` 对每个案例执行 Atlas 构建、查询、轨迹审核、动态更新以及当前版本的记忆/信任/执行/占用协议重放。它报告区域、证书、零 false-safe 点回归、覆盖率、内存与时间诊断。

4.7 的逻辑摘要为 `da1c963f7d137ef3`，并绑定每个起点的解析几何 Jacobian 形状及量化值。128 次 smoke 与 8192 次 soak 必须得到相同摘要。

逻辑摘要只保护离散行为一致性；时间和内存不是跨平台逐位门禁。高密度采样用于回归，不签发区域证书。
