# 认证制品证明

> 英文原文：[Authenticated artifact attestations](../artifact-attestation.md)

`RBFSafe::trust` 使用调用方管理的共享密钥和完整 HMAC-SHA256，对 `SafetyMemory` 制品引用的精确字节及生命周期元数据进行认证。

证明绑定制品 ID、内容摘要/长度、部署、机器人、场景、任务、证据等级、生命周期状态、服务/密钥 ID 与算法。验证会重新计算 bytes 摘要并检查当前记忆记录，防止内容或元数据替换。

密钥不由库生成、存储或轮换；调用方负责安全保管和服务身份映射。HMAC 成功只证明持有同一共享密钥的一方产生了声明，不证明制品几何正确或服务独立。
