# 公共 API 稳定策略

> 英文原文：[Public API stability policy](../api-stability.md)

RBF-Safe 遵循语义化版本，但 C++ ABI 与源码兼容性、Python API 和磁盘 schema 分别管理。

## 承诺

- 同一主版本内，不会无说明删除已审查公共类型、函数或枚举；
- 新能力优先采用附加接口、配置字段和新 CMake 目标；
- 公共头文件保持标准库 ABI，不泄漏 Eigen/JSON/存储实现；
- Python 镜像稳定高层 API，异常类型保持可区分；
- 磁盘兼容性由独立 schema 表控制，而不是仅看库版本。

`data/api_surface_v4.sha256` 固定当前主版本的 35 个公共表面，CI 对规范化文件计算摘要。更新快照必须伴随兼容性审查、changelog、版本与下游 consumer 测试。

1.0 以前的历史接口可能按路线图演进；4.x 当前仍保留 1.0–4.6 的已审查公共表面，并附加 4.7 Jacobian。内部命名空间、未安装头文件、测试 helper 和实验实现不属于公共承诺。
