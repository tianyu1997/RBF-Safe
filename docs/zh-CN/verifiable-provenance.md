# 可验证来源与外部时间

> 英文原文：[Verifiable provenance and external time](../verifiable-provenance.md)

`RBFSafe::provenance` 为规范化硬件 key 声明和签名外部时间链提供只读审计边界。

硬件声明固定 vendor、authority、adapter、scope、设备/固件/测量摘要和序号；外部时间声明固定 source、前驱、tick、不确定性和有效范围。策略要求明确服务 quorum、允许来源、作用域与最大不确定性。

审核会报告满足/不满足硬件来源，以及 fresh/stale/future/inconsistent/incomplete 时间状态。适配器或供应商字符串从不被隐式信任；所有 key 与根由调用方固定。

`ready` 结果仍为非授权 `Unknown`，不能证明物理设备真实或本地时钟正确。
