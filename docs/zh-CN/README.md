# RBF-Safe 中文文档

> 中文区仅保留最重要的使用、安全、集成和维护指南。详细 schema、历史格式、信任协议和逐字段兼容规则请查阅[英文文档总索引](../README.md)。公共类型名、JSON 字段、命令行参数和磁盘格式标识保持英文不变。

## 入门

- [安装](installation.md)
- [快速开始](getting-started.md)
- [输入格式](input-formats.md)

## 核心概念

- [安全模型](safety-model.md)
- [体系结构](architecture.md)
- [API 总览](api.md)
- [运动学与解析 Jacobian](kinematics.md)

## 证书与规划

- [Atlas 磁盘格式](atlas-format.md)
- [OBB、Portal 与 HiPaC 走廊](corridors.md)
- [轨迹审核器](trajectory-auditor.md)
- [Safe IK](safe-ik.md)
- [OMPL 适配器](ompl-adapter.md)
- [MoveIt 2 集成](moveit2.md)
- [运行时动作 Shield](runtime-shield.md)
- [动态更新](dynamic-updates.md)

## 高级能力

- [学习策略安全门](policy-safety.md)
- [持久安全记忆](safety-memory.md)

这些概览会链接到对应的英文格式规范。英文规范仍保留全部多机器人占用、认证发布、信任轮换、透明日志、执行账本和来源验证细节。

## 工程与发布

- [版本与兼容性](versioning.md)
- [路线图](roadmap.md)
- [代码与算法来源](provenance.md)
- [发布流程](releasing.md)

## 阅读规则

1. 先阅读安全模型，再解释 `contains`、`connected`、审核和执行结果。
2. 中文文档说明概念、工作流和安全边界；英文文档保留逐字段规范。
3. 任何成功结果都只能表达其 `EvidenceLevel` 对应的窄范围结论。
4. 硬件状态、感知真实性、外部时钟、网络传输和控制器执行必须由调用方明确建立信任。

## 社区与安全

- [贡献指南](../../CONTRIBUTING.zh-CN.md)
- [安全报告](../../SECURITY.zh-CN.md)
- [支持渠道](../../SUPPORT.md)
- [行为准则](../../CODE_OF_CONDUCT.md)
- [维护者](../../MAINTAINERS.md)
- [第三方声明](../../THIRD_PARTY_NOTICES.md)
