# 可验证来源 bundle schema 1

> 英文原文：[Verifiable provenance bundle schema 1](../verifiable-provenance-format.md)

schema 1 是单个规范化 UTF-8 JSON，包含格式/schema、写入库版本、身份载荷与文件校验和。

载荷绑定信任历史/检查点、硬件声明链、外部时间源链、审核策略、结果和确定性 ID。加载时重算全部派生 ID、签名、父链和评估，限制声明、来源、签名、字节和字符串数量。

未知 schema、重复服务、断链、未来/溢出 tick、错误 scope/key、校验和、截断和符号链接都会被拒绝。精确字段与枚举以英文格式规范为准。
