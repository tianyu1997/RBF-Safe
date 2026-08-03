# 安全记忆目录格式 schema 1

> 英文原文：[Safety memory directory format, schema 1](../safety-memory-format.md)

```text
memory/
├── manifest.json
└── memory.json
```

manifest 记录 `rbfsafe-safety-memory`、schema 1、写入库版本、记录计数和载荷 SHA-256。`memory.json` 保存制品、生命周期事件、复用事件、稳定排序和整体身份。

加载时重新计算每个制品/事件/记忆 ID，检查生命周期单调性、引用完整性、内容摘要格式、资源上限和规范顺序。定位符只是外部位置，不会被加载器自动读取。

安全记忆 schema 与被引用 Atlas、走廊、策略或执行格式独立。
