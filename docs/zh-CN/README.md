# RBF-Safe 中文文档

> 中文区仅保留最重要的使用、安全、集成和维护指南。详细 schema、历史格式、信任协议和逐字段兼容规则请查阅[英文文档总索引](../README.md)。公共类型名、JSON 字段、命令行参数和磁盘格式标识保持英文不变。

目录按照 `core`、`envelope`、`geometry`、`atlas`、`applications`、
`assurance` 和 `maintenance` 分组，与 C++ 公共模块保持一致。

## 入门

- [项目阅读路线](core/项目阅读路线.md)
- [源码阅读方案与计划](core/code-reading-plan.md)
- [安装](core/安装指南.md)
- [快速开始](core/快速开始.md)
- [输入格式](core/输入格式.md)

## 核心概念

- [安全模型](core/安全模型.md)
- [体系结构](core/体系结构.md)
- [API 总览](core/API总览.md)
- [运动学与解析 Jacobian](geometry/运动学与雅可比矩阵.md)
- [模型、几何与工作空间包络源码阅读指南](geometry/model-and-geometry-reading-guide.md)
- [工作空间包络交互实验](envelope/workspace-envelope-lab.md)

## 证书与规划

- [Atlas 磁盘格式](atlas/Atlas格式.md)
- [OBB、Portal 与 HiPaC 走廊](applications/planning/走廊与HiPaC.md)
- [轨迹审核器](atlas/轨迹审核.md)
- [Safe IK](applications/planning/安全逆运动学.md)
- [OMPL 适配器](applications/planning/OMPL适配器.md)
- [MoveIt 2 集成](applications/planning/MoveIt2集成.md)
- [运行时动作 Shield](applications/planning/运行时安全屏障.md)
- [动态更新](atlas/动态更新.md)

## 高级能力

- [学习策略安全门](applications/policy/策略安全.md)
- [持久安全记忆](assurance/memory/安全记忆.md)

这些概览会链接到对应的英文格式规范。英文规范仍保留全部多机器人占用、认证发布、信任轮换、透明日志、执行账本和来源验证细节。

## 工程与发布

- [版本与兼容性](maintenance/版本与兼容性.md)
- [路线图](maintenance/路线图.md)
- [代码与算法来源](maintenance/来源说明.md)
- [发布流程](maintenance/发布流程.md)

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
