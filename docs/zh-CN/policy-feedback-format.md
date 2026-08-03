# 策略反馈 schema 1

> 英文原文：[Policy feedback schema 1](../policy-feedback-format.md)

策略反馈保存为版本化目录：

```text
policy-feedback/
├── manifest.json
└── records.json
```

manifest 包含格式、schema、库版本、记录数量、身份范围和载荷 SHA-256。记录按稳定 ID 排序，绑定策略模型、任务、观测、提议、shield 决策、选择结果和标签。

读取器限制总字节、记录数、提议数和字符串长度；拒绝重复 ID、非有限指标、身份不一致、错误校验和、符号链接与意外条目。该格式不是模型 checkpoint，也不是 Atlas 或运行许可。
