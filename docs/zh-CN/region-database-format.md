# 区域数据库 schema 1

> 英文原文：[Region database schema 1](../region-database-format.md)

目录布局：

```text
database/
├── manifest.json
└── regions.json
```

manifest 记录配置维度、机器人和场景 SHA-256、场景版本、区域/证书数量以及载荷校验和。`regions.json` 使用规范化 UTF-8 JSON 保存带类型标签的几何、证书和确定性图关系。

读取器在分配前应用字节、维度、记录、顶点、生成元、Taylor 项和图边限制；拒绝非有限浮点数、非正交 OBB、无效区间、错误向量维度、重复 ID、悬空证书和证据/主题不一致。

该 schema 独立于 Atlas 和走廊 schema。导入其他格式应通过公共 API 重新验证并生成数据库记录，不能直接拼接文件。
