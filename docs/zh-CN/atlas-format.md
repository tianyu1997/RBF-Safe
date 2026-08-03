# Atlas 目录格式

> 英文原文：[Atlas directory format](../atlas-format.md)

Atlas 是版本化目录，而不是 C++ 结构体内存转储：

```text
atlas/
├── manifest.json
├── certificates.json
├── lect/
│   └── nodes.bin
├── regions.bin
└── graph.bin
```

`manifest.json` 记录格式名、schema、库版本、维度、机器人/场景摘要、记录数量、资源信息和每个载荷文件的 SHA-256。二进制文件使用显式小端编码；内部数组地址、ABI padding 和旧缓存 ID 不进入格式。

## 保存

保存先在同级临时目录中写入全部文件，校验长度、计数和摘要后再发布。默认拒绝覆盖现有目标；临时目录或部分文件不被视为已提交 Atlas。

## 加载

加载顺序为：

1. 拒绝符号链接、意外条目和未知 schema；
2. 检查 manifest 与文件大小；
3. 在分配内存前应用记录数和字节上限；
4. 校验 SHA-256；
5. 解码 LECT、区域、证书和图；
6. 验证引用、排序、身份与确定性不变量。

`library_version` 仅记录写入者版本；兼容性由独立 schema 决定。加载完成后仍应对当前机器人和场景调用 `verify_compatible`。
