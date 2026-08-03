# 走廊 schema 1

> 英文原文：[Corridor schema 1](../corridor-format.md)

`HipacCorridor` 保存为版本化目录：

```text
corridor/
├── manifest.json
└── corridor.json
```

manifest 绑定格式名、schema、库版本、配置维度、机器人与场景摘要、记录数量以及 `corridor.json` 的 SHA-256。

载荷保存 OBB 区域、主题绑定证书、见证 Portal、邻接关系、候选路径覆盖信息和未认证间隙。记录按稳定 ID 排序；浮点数使用规范化 JSON 表示。

加载会限制字节数、区域数、Portal 数、维度和图边数，并拒绝未知字段组合、重复 ID、悬空引用、无效 OBB 基、错误证据等级、错误校验和、截断和符号链接。

schema 与库 SemVer 独立。精确 JSON 字段、枚举字符串与默认上限以英文格式规范为准。
