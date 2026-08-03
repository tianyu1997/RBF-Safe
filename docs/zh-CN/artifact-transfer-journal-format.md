# 制品传输日志 schema 1 与 2

> 英文原文：[Artifact transfer journal schemas 1 and 2](../artifact-transfer-journal-format.md)

传输日志保存已离线验证的远程制品获取/发布记录。目录包含 manifest 与规范化 records 载荷，格式独立于 Atlas、安全记忆和策略 schema。

schema 1 记录共享密钥认证；schema 2 也记录 Ed25519 公共身份、信任包和验证 key。记录绑定请求、响应、精确字节摘要、生命周期、服务证明和前一记录 ID。

加载会重算链、请求/响应 ID 和校验和，并限制记录数、总字节和字符串长度。日志只说明软件验证发生过，不保证网络传输、远端持久性或当前制品可用性。
