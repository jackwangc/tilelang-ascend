# PTO.cpp 精度问题定位指南

## 一、问题描述

### 1.1 问题现象

执行测试脚本时出现精度问题：
```bash
python examples/pto_h_test/opt_gdn_chunk_h.py
```

当 `lib_path` 设置为 `"./pto.so"` 时，精度验证失败；而设置为 `"./ascendc.so"` 时，精度验证通过。

### 1.2 文件关系

```
examples/pto_h_test/
├── opt_gdn_chunk_h.py      # 测试脚本（精度验证）
├── pto.cpp                  # PTO 生成的代码（有精度问题）
├── pto.so                   # pto.cpp 编译生成的库
├── ascendc.cpp              # AscendC 原始代码（精度正常）
└── ascendc.so               # ascendc.cpp 编译生成的库
```

**关键信息**：
- `pto.cpp` 和 `ascendc.cpp` 结构类似，都是 AscendC 代码
- `ascendc.so` 精度正常，`pto.so` 有精度问题
- 需要找出 `pto.cpp` 中与 `ascendc.cpp` 行为不同的地方

---

## 二、调试方法：添加日志对比

### 2.1 ascendc.cpp 添加日志

使用 `TPRINT` 宏打印张量：

```cpp
if (cid == 0 && vid == 0) {
    TPRINT(s_ub);  // 打印 s_ub 张量
}
```

### 2.2 pto.cpp 添加日志

使用 `DumpTensor` 函数打印张量：

```cpp
if (cid == 0 && vid == 0) {
    AscendC::DumpTensor(s_ub[0], 0, 528);  // 打印 s_ub[0]，从位置 0 到 528
}
```

### 2.3 日志添加位置建议

在关键计算步骤后添加日志：

| 计算步骤 | 变量名示例 | 说明 |
|----------|-----------|------|
| 数据加载 | `k_ub`, `g_ub` | 验证从 GM 加载的数据是否正确 |
| 中间计算 | `coeff_ub`, `ws_ub` | 验证中间计算结果 |
| 关键输出 | `s_ub`, `v_ub` | 验证最终计算结果 |

---

## 三、对比分析流程

### 3.1 编译运行

1. 在 `pto.cpp` 和 `ascendc.cpp` 的相同位置添加日志
2. 重新编译生成 `.so` 文件
3. 运行测试脚本

### 3.2 对比输出

```bash
# 运行后对比两个文件中的 TPRINT 和 DumpTensor 输出
```

### 3.3 判断标准

- **如果输出的前10个数字一致**：该步骤没有精度问题
- **如果输出的前10个数字不一致**：该步骤存在精度问题，需要检查前面的计算逻辑

---

## 四、常见精度问题原因

### 4.1 数据类型问题

```cpp
// 错误示例：float16 精度不足
float16 tmp = ...;  // 可能溢出或精度丢失

// 正确做法：关键中间变量使用 float
float tmp = ...;    // 保持更高精度
```

### 4.2 计算顺序问题

```cpp
// 可能导致精度损失
result = a * b + c * d;  // 顺序可能导致不同的舍入误差

// 建议使用括号明确优先级
result = (a * b) + (c * d);
```

### 4.3 初始化问题

```cpp
// 确保所有累加器正确初始化
TASSIGN(s_ub, 0);  // 必须初始化为 0
```

### 4.4 Pipeline 同步问题

```cpp
// 确保 MTE 和 Vector 计算正确同步
set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
```

---

## 五、快速定位步骤

### Step 1: 验证输入数据

在数据加载后立即添加日志：
```cpp
// ascendc.cpp
if (cid == 0 && vid == 0) {
    TPRINT(k_ub);
    TPRINT(g_ub);
}

// pto.cpp
if (cid == 0 && vid == 0) {
    AscendC::DumpTensor(k_ub[0], 0, 528);
    AscendC::DumpTensor(g_ub[0], 0, 128);
}
```

### Step 2: 验证中间计算

在关键计算后添加日志：
```cpp
// 在 coeff 计算后
if (cid == 0 && vid == 0) {
    TPRINT(coeff_ub);  // ascendc.cpp
    AscendC::DumpTensor(coeff_ub[0], 0, 64);  // pto.cpp
}
```

### Step 3: 验证输出结果

在写入 GM 前添加日志：
```cpp
// 在写入结果前
if (cid == 0 && vid == 0) {
    TPRINT(s_ub);  // ascendc.cpp
    AscendC::DumpTensor(s_ub[0], 0, 528);  // pto.cpp
}
```

---

## 六、注意事项

1. **日志位置必须一致**：在两个文件的相同逻辑位置添加日志
2. **条件判断相同**：使用相同的 `cid` 和 `vid` 条件
3. **打印范围一致**：确保打印相同数量的元素
4. **重新编译**：每次修改代码后都需要重新编译 `.so` 文件

---
