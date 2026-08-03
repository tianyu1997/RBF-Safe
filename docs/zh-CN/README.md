# RBF-Safe 中文文档

> 英文文档入口：[Documentation](../getting-started.md)。中文文档与 4.7.0 公共接口同步；公共类型名、JSON 字段、命令行参数和磁盘格式标识保持英文不变。

## 入门与基础

- [安装](installation.md)
- [快速开始](getting-started.md)
- [愿景](vision.md)
- [体系结构](architecture.md)
- [API 总览](api.md)
- [安全模型](safety-model.md)
- [输入格式](input-formats.md)
- [运动学与解析 Jacobian](kinematics.md)

## 几何证书与规划

- [Atlas 磁盘格式](atlas-format.md)
- [OBB、Portal 与 HiPaC 走廊](corridors.md)
- [走廊格式](corridor-format.md)
- [统一区域数据库](region-database.md)
- [区域数据库格式](region-database-format.md)
- [轨迹审核器](trajectory-auditor.md)
- [动态更新](dynamic-updates.md)
- [Safe IK](safe-ik.md)
- [规划消费者](planning-consumers.md)
- [优化适配器](optimization.md)
- [OMPL 适配器](ompl-adapter.md)
- [MoveIt 2 集成](moveit2.md)
- [运行时动作 Shield](runtime-shield.md)

## 学习策略与校准

- [学习策略安全门](policy-safety.md)
- [策略反馈格式](policy-feedback-format.md)
- [策略校准](policy-calibration.md)
- [校准漂移与生命周期](policy-calibration-lifecycle.md)

## 安全记忆与多机器人

- [持久安全记忆](safety-memory.md)
- [安全记忆格式](safety-memory-format.md)
- [事务化安全记忆仓库](safety-memory-store.md)
- [车队调度历史](fleet-schedule-archive.md)
- [连续车队占用](continuous-fleet-occupancy.md)
- [连续车队占用格式](continuous-fleet-occupancy-format.md)
- [移动障碍物连续占用](continuous-moving-obstacles.md)
- [机器人—场景占用格式](continuous-robot-scene-occupancy-format.md)
- [认证占用发布](authenticated-occupancy-publication.md)
- [认证占用发布格式](authenticated-occupancy-publication-format.md)
- [占用发布历史](occupancy-publication-history.md)
- [占用发布历史格式](occupancy-publication-history-format.md)
- [信任轮换占用历史](rotating-occupancy-publication-history.md)
- [信任轮换占用历史格式](rotating-occupancy-publication-history-format.md)
- [协调预留协议](coordinated-reservation-agreements.md)
- [协调预留格式](coordinated-reservation-agreement-format.md)

## 身份、部署与可审计执行

- [制品认证](artifact-attestation.md)
- [远程制品服务](remote-artifact-service.md)
- [制品传输日志格式](artifact-transfer-journal-format.md)
- [公共服务身份](public-service-identities.md)
- [服务信任包格式](service-trust-bundle-format.md)
- [服务信任历史格式](service-trust-history-format.md)
- [服务信任检查点格式](service-trust-checkpoint-format.md)
- [部署配置格式](deployment-profile-format.md)
- [有界执行会话格式](bounded-execution-session-format.md)
- [执行账本格式](execution-ledger-format.md)
- [见证透明度](witnessed-transparency.md)
- [透明日志格式](transparency-log-format.md)
- [可验证来源与外部时间](verifiable-provenance.md)
- [可验证来源格式](verifiable-provenance-format.md)

## 工程、兼容性与发布

- [公共 API 稳定策略](api-stability.md)
- [Schema 迁移](schema-migrations.md)
- [版本与兼容性](versioning.md)
- [迁移映射](migration-map.md)
- [代码与算法来源](provenance.md)
- [路线图](roadmap.md)
- [项目范围完成矩阵](project-scope-matrix.md)
- [发布固定数据与基准](release-fixtures.md)
- [发布流程](releasing.md)

## 阅读规则

1. 先阅读安全模型，再解释 `contains`、`connected`、审核和执行结果。
2. 中文文档说明概念、工作流和安全边界；英文同名文档保留逐字段兼容性规范。
3. 任何成功结果都只能表达其 `EvidenceLevel` 对应的窄范围结论。
4. 硬件状态、感知真实性、外部时钟、网络传输和控制器执行必须由调用方明确建立信任。

## 社区与安全

- [贡献指南](../../CONTRIBUTING.zh-CN.md)
- [安全报告](../../SECURITY.zh-CN.md)
- [支持渠道](../../SUPPORT.zh-CN.md)
- [行为准则](../../CODE_OF_CONDUCT.zh-CN.md)
- [维护者](../../MAINTAINERS.zh-CN.md)
- [第三方声明](../../THIRD_PARTY_NOTICES.zh-CN.md)
