# 远程安全制品服务契约

> 英文原文：[Remote artifact service contract](../remote-artifact-service.md)

`RBFSafe::remote` 是传输中立的请求/响应契约，不包含 HTTP、gRPC、重试、服务发现或凭据存储。

模块为 fetch/publish 生成确定性请求，绑定安全记忆当前状态、制品身份、预期摘要/长度、生命周期、服务和 nonce。验证响应时检查请求回放、完整字节、证明、服务身份和生命周期替换。

成功验证可写入 `ArtifactTransferJournal`。调用方负责实际网络、TLS、超时、可用性和信任根。远端服务不能通过声明改变本地制品证据等级。
