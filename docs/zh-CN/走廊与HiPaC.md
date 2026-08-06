# OBB 走廊、Portal 与 HiPaC（v4.7）

> 英文原文：[OBB corridors, portals, and HiPaC](../corridors.md)

HiPaC 将调用方提供的分段线性候选路径覆盖为一系列凸 OBB 单元，并通过共享见证 Portal 连接相邻单元。它不负责寻找任意路径，也不会把未认证间隙隐藏起来。

## 构建

```cpp
std::vector<rbfsafe::Configuration> path{
    {-1.0, -1.0}, {0.0, 0.0}, {1.0, 1.0}};
auto report = rbfsafe::HipacCorridorBuilder{}.build(robot, scene, path);
```

构建器对候选段生成定向盒、调用保守 `ObbRegionValidator`，并对未决段递归细分。验证预算、最小段宽、最大深度和取消令牌均为硬限制。

## 结果

- `Certified`：整个候选路径被认证单元覆盖；
- `Partial`：报告显式未认证间隙；
- `Invalid`：输入、身份或几何不变量失败。

连续 OBB 只有在存在认证相交见证时才能通过 Portal 连接。`route` 在凸单元并集中恢复确定性几何路线。

走廊证书不包含速度、加速度、时间参数、控制器跟踪误差或动态障碍物结论。候选路径的来源可以是规划器、优化器或人工输入，但来源本身不影响认证规则。
