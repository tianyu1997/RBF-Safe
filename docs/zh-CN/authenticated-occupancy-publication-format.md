# 认证占用发布格式

> 英文原文：[Authenticated occupancy publication format](../authenticated-occupancy-publication-format.md)

schema 1 使用规范化 UTF-8 JSON，包含发布 payload、身份字段、Ed25519 签名与文件级校验和。占用 payload 本身由摘要和字节长度引用，不隐式修改。

解码会限制文件/字符串/签名字节，验证十六进制长度、父序列规则、闭合有效窗、bundle/报告/时间线/坐标系绑定和确定性发布 ID。

格式验证不选择可信根，也不自动接受“最新”密钥；调用方必须提供明确的 `ServiceTrustBundle` 和固定策略。
