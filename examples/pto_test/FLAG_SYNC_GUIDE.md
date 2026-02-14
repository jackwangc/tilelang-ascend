# set_flag / wait_flag 流水线同步机制详解

## 概述

`set_flag` 和 `wait_flag` 是华为昇腾AI Core的**流水线同步指令**，用于协调不同硬件流水线之间的执行顺序。

AI Core有多个并行流水线，数据传输和计算可以同时进行。同步机制确保：
- 计算流水线等待数据加载完成
- 数据回写等待计算完成

---

## 函数原型

```cpp
// 设置标志：源流水线完成操作后发出信号
#define set_flag(srcPipe, dstPipe, event_id)

// 等待标志：阻塞当前流水线，直到源流水线发出信号
#define wait_flag(srcPipe, dstPipe, event_id)
```

---

## 参数说明

| 参数 | 类型 | 含义 |
|------|------|------|
| `srcPipe` | `pipe_t` | **源流水线** - 执行操作后设置标志的流水线 |
| `dstPipe` | `pipe_t` | **目标流水线** - 需要等待源流水线的流水线 |
| `event_id` | `event_t` | **事件ID** - 取值 `EVENT_ID0` ~ `EVENT_ID7`，用于区分多个同步事件 |

### 流水线类型 (pipe_t)

```cpp
const pipe_t PIPE_S   = 0;    // Scalar流水线（标量计算）
const pipe_t PIPE_V   = 1;    // Vector流水线（向量计算）
const pipe_t PIPE_MTE1 = 2;   // Memory Transfer Engine 1（搬运引擎1）
const pipe_t PIPE_MTE2 = 3;   // Memory Transfer Engine 2（GM → UB，数据加载）
const pipe_t PIPE_MTE3 = 4;   // Memory Transfer Engine 3（UB → GM，数据回写）
const pipe_t PIPE_M   = 5;    // Matrix流水线（矩阵计算）
```

### 事件ID (event_t)

```cpp
typedef int event_t;
#define EVENT_ID0 0
#define EVENT_ID1 1
// ...
#define EVENT_ID7 7
```

**注意**：每个 `(srcPipe, dstPipe)` 组合最多支持 8 个独立的同步事件（EVENT_ID0 ~ EVENT_ID7）。

---

## 工作原理

```
时间线 →

流水线A (srcPipe)          流水线B (dstPipe)
     |                         |
     |  [执行操作]             |  [等待中...]
     |                         |
     |  set_flag(A,B,ID0) ─────┼──→ wait_flag(A,B,ID0) 收到信号
     |                         |
     |                         |  [继续执行]
```

1. **set_flag**: 流水线A完成操作后，向流水线B发送"我完成了"的信号
2. **wait_flag**: 流水线B阻塞等待，直到收到流水线A的信号后才继续执行

---

## 典型使用场景

### 场景1：等待数据加载完成（最常用）

```cpp
// GM → UB 数据传输（MTE2流水线）
copy_gm_to_ub(GM_addr, ub_data);

// 通知Vector流水线：数据已加载完成
set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);

// Vector流水线等待数据加载完成后才开始计算
wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);

// 现在可以安全使用ub_data进行计算
vector_compute(ub_data);
```

**执行顺序**：
1. MTE2开始从GM加载数据到UB
2. Vector流水线在wait_flag处阻塞等待
3. MTE2加载完成后，set_flag发出信号
4. Vector流水线收到信号，解除阻塞，开始计算

---

### 场景2：等待计算完成后回写数据

```cpp
// Vector计算流水线
vector_compute(ub_data);

// 通知MTE3流水线：计算已完成，可以回写了
set_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);

// MTE3流水线等待计算完成
wait_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);

// 现在可以安全地将结果回写到GM
copy_ub_to_gm(ub_data, GM_addr);
```

---

### 场景3：完整的流水线双缓冲

```cpp
AICORE void main_kernel(__gm__ float *G_handle, __gm__ float *S_handle) {
    auto cid = get_block_idx();

    // 分配UB缓冲区
    TileUbDataND<float, 1, 1024> g_ub;
    TileUbDataND<float, 1, 1024> s_ub;

    // ========== 阶段1：加载数据 ==========
    tl::ascend_pto::copy_gm_to_ub(G_handle + offset, g_ub);

    // 同步点1：等待数据加载完成
    set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
    wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);

    // ========== 阶段2：计算 ==========
    for (int i = 0; i < 1024; ++i) {
        s_ub.SetValue(i, g_ub.GetValue(i) * 2.0f);  // 示例：简单乘法
    }

    // 同步点2：等待计算完成
    set_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);
    wait_flag(PIPE_V, PIPE_MTE3, EVENT_ID0);

    // ========== 阶段3：回写结果 ==========
    tl::ascend_pto::copy_ub_to_gm(S_handle + offset, s_ub);
}
```

---

### 场景4：多个独立同步事件

```cpp
// 事件1：MTE2 → V (数据加载)
set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);

// ... 计算代码 ...

// 事件2：V → MTE3 (计算完成)
set_flag(PIPE_V, PIPE_MTE3, EVENT_ID1);  // 使用不同的event_id
wait_flag(PIPE_V, PIPE_MTE3, EVENT_ID1);

// 事件3：V → S (向量到标量同步)
set_flag(PIPE_V, PIPE_S, EVENT_ID2);
wait_flag(PIPE_V, PIPE_S, EVENT_ID2);
```

---

## 常见错误与注意事项

### 错误1：流水线方向反了

```cpp
// ❌ 错误：方向反了
set_flag(PIPE_V, PIPE_MTE2, EVENT_ID0);   // Vector等待MTE2？不对！
wait_flag(PIPE_V, PIPE_MTE2, EVENT_ID0);

// ✅ 正确：MTE2加载完成后，通知Vector开始计算
set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
```

**记忆方法**：`set_flag(src, dst, id)` 表示 "src完成，通知dst"

### 错误2：忘记同步

```cpp
// ❌ 危险：没有同步，可能读到未加载的数据
copy_gm_to_ub(GM_addr, ub_data);
for (int i = 0; i < 1024; ++i) {
    sum += ub_data.GetValue(i);  // ub_data可能还未加载完成！
}

// ✅ 正确：等待加载完成
copy_gm_to_ub(GM_addr, ub_data);
set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
for (int i = 0; i < 1024; ++i) {
    sum += ub_data.GetValue(i);  // 安全：数据已加载完成
}
```

### 错误3：Event ID 超出范围

```cpp
// ❌ 错误：event_id 最大是7
set_flag(PIPE_MTE2, PIPE_V, 8);

// ✅ 正确
#define EVENT_ID0 0
#define EVENT_ID1 1
// ...
#define EVENT_ID7 7
```

---

## 封装函数：set_flag_pipeline / wait_flag_pipeline

TileLang提供了简化的封装函数：

```cpp
template<pipe_t pipe, pipe_t tpipe>
void set_flag_pipeline(int32_t pipeID);

template<pipe_t pipe, pipe_t tpipe>
void wait_flag_pipeline(int32_t pipeID);
```

**使用示例**：

```cpp
// 等价于 set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0)
tl::ascend_pto::set_flag_pipeline<PIPE_MTE2, PIPE_V>(0);

// 等价于 set_flag(PIPE_MTE2, PIPE_V, EVENT_ID1)
tl::ascend_pto::set_flag_pipeline<PIPE_MTE2, PIPE_V>(1);

// 等价于 wait_flag(PIPE_V, PIPE_MTE3, EVENT_ID0)
tl::ascend_pto::wait_flag_pipeline<PIPE_V, PIPE_MTE3>(0);
```

**参数**：
- `<pipe, tpipe>`: 模板参数，指定源和目标流水线
- `pipeID`: 0~7，对应 EVENT_ID0 ~ EVENT_ID7

---

## 快速参考表

| 场景 | set_flag参数 | wait_flag参数 |
|------|-------------|--------------|
| GM→UB加载完成后计算 | `set_flag(PIPE_MTE2, PIPE_V, id)` | `wait_flag(PIPE_MTE2, PIPE_V, id)` |
| 计算完成后UB→GM回写 | `set_flag(PIPE_V, PIPE_MTE3, id)` | `wait_flag(PIPE_V, PIPE_MTE3, id)` |
| 标量计算等待向量 | `set_flag(PIPE_V, PIPE_S, id)` | `wait_flag(PIPE_V, PIPE_S, id)` |
| 矩阵计算等待向量 | `set_flag(PIPE_V, PIPE_M, id)` | `wait_flag(PIPE_V, PIPE_M, id)` |

---

## 你的代码中的使用

在你的 `pto.cpp:23-24` 中：

```cpp
set_flag(PIPE_V, PIPE_MTE2, EVENT_ID0);
wait_flag(PIPE_V, PIPE_MTE2, EVENT_ID0);
```

**分析**：这里的方向可能有误，应该是：

```cpp
// MTE2 (GM→UB) 完成后，通知 Vector 开始计算
set_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
wait_flag(PIPE_MTE2, PIPE_V, EVENT_ID0);
```

或者如果你想等待Vector流水线完成某些操作后再做MTE2操作，那需要根据具体逻辑来判断。

---

## 总结

1. **set_flag(src, dst, id)**: src流水线完成，通知dst流水线
2. **wait_flag(src, dst, id)**: dst流水线等待src流水线完成
3. **常用组合**：
   - `PIPE_MTE2 → PIPE_V`: 数据加载完成后开始计算
   - `PIPE_V → PIPE_MTE3`: 计算完成后开始回写
4. **Event ID**: 同一对流水线可以有0~7共8个独立事件
