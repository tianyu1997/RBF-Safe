# 认证规划消费者

> 英文原文：[Certified planning consumers](../planning-consumers.md)

`RBFSafe::planning` 将认证区域转换为与具体规划器无关的可复用数据结构。

## 主要组件

- `CertifiedSampler`：只从认证区域并集中采样；
- `CertifiedRoadmapBuilder`：以区域或见证点构建确定性路线图；
- 精确见证边：只有区域相交或 Portal 证据成立时连接；
- 规划输出审核：候选路径仍交给 `TrajectoryAuditor` 独立验证。

采样器按稳定区域顺序和显式种子运行，具有最大尝试数。返回样本位于认证区域，但从当前状态移动到该样本仍需连通证书。

路线图节点/边 ID 由规范化几何和证书身份导出，不使用内存地址或线程完成顺序。规划器可以消费该图，但不能把启发式边、近邻关系或未经验证的插值标记为认证。
