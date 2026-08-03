# 连续车队占用 bundle schema 1 与 2

> 英文原文：[Continuous fleet occupancy bundle schemas 1 and 2](../continuous-fleet-occupancy-format.md)

bundle 是单个规范化 UTF-8 JSON 文件，保存格式/schema、写入库版本、时间线与坐标系、部署占用、切片、报告、身份和 SHA-256。

schema 1 保留固定平移部署；schema 2 支持右手旋转和显式平移/角度不确定性。读取器兼容两者，写入器使用当前 schema。

加载会限制文件字节、部署数、连杆数、切片数、总包络数和字符串长度，并重算轨迹/机器人/报告/bundle 身份。非正交旋转、负不确定性、时间不连续、重复部署和错误校验和会被拒绝。
