# 连续机器人—场景占用 bundle schema 1

> 英文原文：[Continuous robot-scene occupancy bundle schema 1](../continuous-robot-scene-occupancy-format.md)

单文件 JSON bundle 组合机器人扫掠连杆占用、移动障碍物扫掠占用、闭合时间窗、坐标系、比较报告和确定性身份。

读取器要求两侧时间线与坐标系精确一致，覆盖相同闭合窗口；限制机器人/障碍物/切片/包络数量并重算全部引用 ID。未知 schema、截断、非有限数、时间空洞、错误摘要和符号链接均被拒绝。

该文件不嵌入原始传感器证据，也不签发区域或执行证书。
