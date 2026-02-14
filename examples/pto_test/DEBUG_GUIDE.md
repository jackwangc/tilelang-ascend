# PTO.cpp 调试指南

## 环境概述

你的调试环境是华为昇腾NPU AI Core核外代码场景：
- **源码**: `pto.cpp` - 运行在AI Core上的AscendC核外代码
- **编译**: `build_pto.sh` - 使用bisheng编译器生成`pto.so`
- **调用**: Python通过tiling API加载并执行`.so`文件

## 调试方法对比

| 方法 | 难度 | 效果 | 推荐度 |
|------|------|------|--------|
| TPRINT打印 | 低 | 中 | ★★★★★ |
| msprof分析 | 中 | 高 | ★★★★ |
| SIM仿真器 | 中 | 高 | ★★★★ |
| GDB单步调试 | 高 | 最高 | ★★★ |

---

## 方法一：TPRINT打印调试（推荐，最简单）

### 1.1 启用打印功能

确认 `build_pto.sh` 已添加打印相关编译选项：

```bash
bisheng \
    --cce-aicore-arch="$CCE_ARCH" \
    -D_DEBUG --cce-enable-print \     # 必须：启用打印功能
    -O0 \                              # 可选：降低优化级别，便于调试
    ...
```

### 1.2 使用TPRINT宏

在 `pto.cpp` 中使用 `TPRINT` 打印变量：

```cpp
// 打印单个变量
TPRINT(g_ub.GetValue(ii * 128));

// 打印多个变量
TPRINT(ii, i, s_ub.GetValue(ii * 128), g_ub.GetValue(ii * 128));

// 条件打印（避免输出过多）
if (cid == 0 && vid == 0) {
    TPRINT(s_ub.GetValue(ii * 128));
}
```

### 1.3 查看打印输出

打印信息会输出到NPU日志，需要配置环境变量：

```bash
# 设置日志级别
export ASCEND_SLOG_PRINT_TO_STDOUT=1
export ASCEND_GLOBAL_LOG_LEVEL=1

# 或查看日志文件
cat /var/log/npu/slog/device-*/log/*.log
```

### 1.4 调试技巧

**技巧1：打印数组/向量**
```cpp
for (int32_t i = 0; i < 128; ++i) {
    if (cid == 0 && vid == 0) {
        TPRINT(i, g_ub.GetValue(i));
    }
}
```

**技巧2：打印执行路径**
```cpp
if (cid == 0 && vid == 0) {
    TPRINT(1);  // 标记到达点1
}
// ... 代码 ...
if (cid == 0 && vid == 0) {
    TPRINT(2);  // 标记到达点2
}
```

**技巧3：打印中间计算结果**
```cpp
float tmp2 = s_ub.GetValue((ii * 128) + i - 1) + g_ub.GetValue((ii * 128) + i);
if (cid == 0 && vid == 0 && ii == 0 && i < 10) {
    TPRINT(ii, i, s_ub.GetValue((ii * 128) + i - 1),
           g_ub.GetValue((ii * 128) + i), tmp2);
}
```

---

## 方法二：msprof性能分析工具

### 2.1 开启性能采集

在Python测试脚本中添加：

```python
import torch
import torch_npu

# 开启性能采集
torch_npu.npu.profile.profile()

# 运行测试
g_sum = chunk_cumsum(g, C)

# 结束采集
torch_npu.npu.profile.end_profile()
```

### 2.2 查看分析结果

```bash
# 使用msprof查看
msprof --output=./prof_data --port=0

# 或使用msprof_viewer可视化分析
msprof_viewer -i ./prof_data
```

### 2.2 查看汇编代码

```bash
# 编译时生成汇编
bisheng -S pto.cpp -o pto.s

# 查看生成的汇编
cat pto.s
```

---

## 方法三：SIM仿真器调试

### 3.1 编译仿真版本

修改 `build_pto.sh`：

```bash
# 添加仿真器相关选项
bisheng \
    --cce-aicore-arch="$CCE_ARCH" \
    --cce-sim \                    # 启用仿真模式
    -D_DEBUG \
    ...
```

### 3.2 运行仿真

```bash
# 使用仿真器运行
python opt_gdn_chunk_cumsum.py
```

### 3.3 查看仿真日志

仿真器会提供更详细的调试信息，包括：
- 内存访问日志
- 寄存器状态
- 执行流跟踪

---

## 方法四：GDB单步调试（高级）

### 4.1 编译调试版本

修改 `build_pto.sh` 使用 `-O0 -g`：

```bash
bisheng \
    --cce-aicore-arch="$CCE_ARCH" \
    -O0 -g \                       # 无优化，带调试信息
    -D_DEBUG \
    ...
```

### 4.2 Python侧调试

修改Python测试脚本，添加等待调试的机制：

```python
import sys
import time

# 在kernel调用前暂停，便于附加gdb
print("PID:", os.getpid())
print("Waiting for debugger...")
time.sleep(10)  # 给你10秒时间附加gdb

g_sum = chunk_cumsum(g, C)
```

### 4.3 使用GDB

```bash
# 找到Python进程ID
ps -ef | grep python

# 附加gdb
gdb -p <PID>

# 在gdb中设置断点（针对加载的.so）
(gdb) set solib-search-path /mnt/workspace/cann/tilelang/tilelang-ascend/examples/pto_test
(gdb) break main_kernel
(gdb) continue
```

**注意**：由于AI Core代码运行在设备端，GDB只能调试host端代码，无法直接单步调试AI Core上的代码。

### 4.4 使用lldb（替代方案）

对于NPU场景，华为提供了专门的调试工具：

```bash
# 使用cia工具（CANN Internal Analyzer）
cia --attach <PID> --symbol-file ./pto.so
```

---

## 推荐的调试工作流

### 快速调试（推荐）
```
1. 添加 TPRINT 打印关键变量
2. 条件打印 (cid==0 && vid==0) 减少输出
3. 重新编译：./build_pto.sh pto.cpp
4. 运行测试：python opt_gdn_chunk_cumsum.py
5. 查看日志：tail -f /var/log/npu/slog/device-*/log/*.log
```

### 深度调试
```
1. 降低优化级别：-O0
2. 生成汇编：bisheng -S pto.cpp -o pto.s
3. 使用msprof采集性能数据
4. 对比汇编和源码，定位问题
5. 在关键位置添加TPRINT验证
```

---

## 常见问题

### Q1: TPRINT没有输出
**解决**：
- 检查编译选项是否有 `-D_DEBUG --cce-enable-print`
- 设置环境变量 `export ASCEND_SLOG_PRINT_TO_STDOUT=1`
- 检查日志级别 `export ASCEND_GLOBAL_LOG_LEVEL=0`

### Q2: 输出太多导致刷屏
**解决**：使用条件打印
```cpp
if (cid == 0 && vid == 0 && ii < 2) {  // 只打印前2个迭代
    TPRINT(...);
}
```

### Q3: 需要查看整个数组
**解决**：
```cpp
for (int i = 0; i < 1024; ++i) {
    if (cid == 0 && vid == 0) {
        TPRINT(i, g_ub.GetValue(i));
    }
}
```

### Q4: 想要单步调试AI Core代码
**现实**：由于异构架构，host端GDB无法直接调试AI Core代码。最佳方案是：
- 使用SIM仿真器（最接近单步调试）
- 或大量使用TPRINT + 汇编分析

---

## 实用调试宏定义

在 `pto.cpp` 顶部添加：

```cpp
// 调试开关
#define DEBUG_PRINT (cid == 0 && vid == 0)  // 只打印第一个block

// 简化的打印宏
#define DPRINT(x) if (DEBUG_PRINT) { TPRINT(x); }
#define DPRINT2(x, y) if (DEBUG_PRINT) { TPRINT(x, y); }
#define DPRINT3(x, y, z) if (DEBUG_PRINT) { TPRINT(x, y, z); }

// 使用示例
DPRINT(ii);
DPRINT3(ii, i, g_ub.GetValue(ii * 128));
```

---

## 总结建议

1. **日常调试**：TPRINT + 条件打印（最快速有效）
2. **性能问题**：msprof + 汇编分析
3. **复杂问题**：SIM仿真器 + TPRINT
4. **避免**：尝试GDB直接调试AI Core代码（不适用）
