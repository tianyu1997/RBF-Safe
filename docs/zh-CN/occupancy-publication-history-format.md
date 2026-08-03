# 占用发布历史格式

> 英文原文：[Occupancy publication-history format](../occupancy-publication-history-format.md)

```text
history/
├── manifest.json
├── trust-bundle.json
├── records/
├── publications/
└── payloads/
```

文件名包含零填充序号与内容 ID；payload 以 SHA-256 命名并可被多个记录复用。manifest 固定 stream、publisher、trust、root/head、计数与文件摘要。

读取器拒绝意外条目、符号链接、序号空洞、重复内容、父链分叉、payload 替换、错误签名、错误校验和以及字节/记录上限。临时文件不属于已提交历史。
