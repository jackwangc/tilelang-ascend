# copy_ub_to_gm 函数参考文档

## 概述

`copy_ub_to_gm` 是用于将数据从 **Union Buffer (UB)** 复制到 **Global Memory (GM)** 的函数。在华为 NPU 编程中，这是一个关键的数据传输操作。

**数据流向：** `UB (本地高速缓存)` → `GM (全局内存)`

---

## 1. AscendC 版本

### 函数签名

```cpp
template <typename T, uint32_t srcN, uint32_t srcM = 1>
CATLASS_DEVICE void copy_ub_to_gm(
    GlobalTensor<T> dstTensor,    // 目标 Global Tensor
    LocalTensor<T> srcTensor,     // 源 Local Tensor (UB)
    uint32_t realdstN = 1         // 实际目标维度大小
);
```

### 参数说明

| 参数 | 类型 | 说明 |
|------|------|------|
| `T` | 模板类型参数 | 数据类型（如 `float`, `half`, `int32_t` 等） |
| `srcN` | 模板参数（uint32_t） | 源 Tensor 的 N 维度大小（必须显式指定） |
| `srcM` | 模板参数（uint32_t） | 源 Tensor 的 M 维度大小，默认为 1 |
| `dstTensor` | `GlobalTensor<T>` | 目标全局内存 Tensor |
| `srcTensor` | `LocalTensor<T>` | 源本地内存 Tensor（UB） |
| `realdstN` | `uint32_t` | 实际要复制到目标 GM 的 N 维度大小，默认为 1 |

### 内部实现

```cpp
AscendC::DataCopyExtParams dataCopyParams(
    srcM,                          // M 维度
    srcN * sizeof(T),              // 源每块大小
    0,                             // 源 stride
    (realdstN - srcN) * sizeof(T), // 目标 stride
    0                              // 填充值
);
AscendC::DataCopyPad(dstTensor, srcTensor, dataCopyParams);
```

### 使用样例

#### 示例 1：简单的一维复制

```cpp
#include "tl_templates/ascend/common.h"

extern "C" __global__ __aicore__ void example_kernel(
    GM_ADDR G_handle,   // GM 输入
    GM_ADDR S_handle    // GM 输出
) {
    AscendC::TPipe pipe;

    // 分配 UB 空间
    AscendC::TBuf<AscendC::TPosition::VECCALC> ub;
    pipe.InitBuffer(ub, 1024 * sizeof(float));

    // 创建 LocalTensor 和 GlobalTensor
    auto s_ub = ub.GetWithOffset<float>(1024, 0);
    AscendC::GlobalTensor<float> S_gm;
    S_gm.SetGlobalBuffer((__gm__ float*)S_handle);

    // ... 在 UB 中进行计算 ...

    // 将 UB 数据复制到 GM（1024 个 float）
    tl::ascend::copy_ub_to_gm<float, 1024>(S_gm, s_ub);

    pipe.Destroy();
}
```

#### 示例 2：带 stride 的二维复制

```cpp
// 假设有一个 16x128 的数据，但只想复制 16x64
auto s_ub = ub.GetWithOffset<float>(2048, 0);  // 16 * 128 = 2048
AscendC::GlobalTensor<float> S_gm;
S_gm.SetGlobalBuffer((__gm__ float*)S_handle);

// 复制 16 行，每行 64 个元素，目标 stride 为 128
tl::ascend::copy_ub_to_gm<float, 64, 16>(S_gm, s_ub, 128);
```

---

## 2. PTO 版本

### 函数签名

```cpp
template <typename T1, typename T2,
          int32_t shape1, int32_t shape2, int32_t shape3,
          int32_t shape4, int32_t shape5,
          int32_t stride1, int32_t stride2,
          int32_t stride3, int32_t stride4, int32_t stride5,
          uint32_t ub_shape1, uint32_t ub_shape2,
          uint32_t valid1, uint32_t valid2>
AICORE PTO_INLINE void copy_ub_to_gm(
    __gm__ T1 *handle,       // GM 内存地址
    int32_t ub_shape_addr,   // UB Tensor 基地址
    int32_t ub_offset,       // UB 偏移量（以 len 为单位）
    int32_t len              // 每次复制长度
);
```

### 参数说明

| 参数 | 类型 | 说明 |
|------|------|------|
| `T1` | 模板类型参数 | GM 数据类型 |
| `T2` | 模板类型参数 | UB 数据类型 |
| `shape1-5` | 模板参数（int32_t） | Global Tensor 的 5 维形状 |
| `stride1-5` | 模板参数（int32_t） | Global Tensor 的 5 维步长 |
| `ub_shape1` | 模板参数（uint32_t） | UB Tensor 第 1 维大小 |
| `ub_shape2` | 模板参数（uint32_t） | UB Tensor 第 2 维大小 |
| `valid1` | 模板参数（uint32_t） | UB 第 1 维有效大小 |
| `valid2` | 模板参数（uint32_t） | UB 第 2 维有效大小 |
| `handle` | `__gm__ T1*` | 目标 GM 内存地址 |
| `ub_shape_addr` | `int32_t` | UB Tensor 的基址 |
| `ub_offset` | `int32_t` | UB 偏移量（以 `len` 为单位） |
| `len` | `int32_t` | 每次复制操作的长度 |

### 内部实现

```cpp
// 创建 GlobalTensor 描述
pto::GlobalTensor<T1, pto::Shape<shape1, shape2, shape3, shape4, shape5>,
    pto::Stride<stride1, stride2, stride3, stride4, stride5>> global_tensor(handle);

// 创建临时 UB Tensor
TileUbDataND<T2, ub_shape1, ub_shape2, valid1, valid2> temp_ub;

// 计算 UB 地址并赋值
pto::TASSIGN(temp_ub, ub_shape_addr + ub_offset * len);

// 执行存储操作（UB → GM）
pto::TSTORE(global_tensor, temp_ub);
```

### 使用样例

#### 示例 1：一维数据复制

```cpp
#include "tl_templates/pto/common.h"

AICORE void example_kernel(
    __gm__ float *G_handle,
    __gm__ float *S_handle
) {
    auto cid = get_block_idx();

    // 分配 UB 空间
    tl::ascend_pto::TileUbDataND<float, 1, 1024, 1, 1024> s_ub;
    TASSIGN(s_ub, 0);

    // ... 在 UB 中进行计算 ...

    // 将 UB 数据复制到 GM
    // 参数：GM地址, UB基址, 偏移量, 长度
    tl::ascend_pto::copy_ub_to_gm<
        float, float,           // 类型
        1, 1, 1, 1024, 1,       // shape: [1, 1, 1, 1024, 1]
        1024, 1024, 1024, 1024, 1024,  // stride
        1, 1024,                // ub_shape: [1, 1024]
        1, 1024                 // valid: [1, 1024]
    >(S_handle, 0, 0, 1024);
}
```

#### 示例 2：二维分块复制

```cpp
// 复制 1024 个元素，分块处理
auto cid = get_block_idx();
auto vid = get_subblockid();

// 计算 GM 偏移
int gm_offset = ((cid / 16) * 32768) + (vid * 16384) + ((cid % 16) * 1024);

tl::ascend_pto::TileUbDataND<float, 1, 1024, 1, 1024> s_ub;
TASSIGN(s_ub, 0);

// ... 计算 ...

// 复制到 GM
tl::ascend_pto::copy_ub_to_gm<
    float, float,
    2, 16, 16, 128, 1,          // shape: [B=2, H=16, chunk=16, C=128, 1]
    16384 * 16 * 2,             // stride0: B*H*C*2
    16384 * 16,                 // stride1: H*C*2
    16384,                      // stride2: C*2
    1, 1, 1, 1,                 // stride3-6
    1, 1024,                    // ub_shape
    1, 1024                     // valid
>(S_handle + gm_offset, 0, 0, 1024);
```

---

## 3. 两版本对比

| 特性 | AscendC 版本 | PTO 版本 |
|------|-------------|---------|
| **数据结构** | `GlobalTensor` / `LocalTensor` | `__gm__ T*` + 手动地址计算 |
| **参数复杂度** | 简单，自动推导 stride | 复杂，需要显式指定 shape 和 stride |
| **类型安全** | 高（模板强类型） | 中等 |
| **灵活性** | 中等 | 高（支持复杂的 stride 模式） |
| **使用场景** | 标准 AscendC 编程 | PTO 指令级编程 |

---

## 4. 注意事项

### 4.1 AscendC 版本

1. **`srcN` 必须显式指定**：这是模板参数，编译期必须确定
2. **Stride 计算**：`realdstN` 用于计算目标 GM 的 stride
3. **内存对齐**：确保数据按照 32 字节对齐

### 4.2 PTO 版本

1. **Shape 和 Stride 必须精确匹配**：5 维形状和步长必须与实际数据布局一致
2. **`ub_offset` 单位**：是以 `len` 为单位的偏移，不是字节
3. **valid 参数**：用于指定实际有效数据大小（可能小于分配大小）

---

## 5. 常见问题

### Q1: 为什么 PTO 版本需要指定 5 维 shape？

**A:** PTO 使用 N 维 Tensor 描述来支持复杂的数据布局（如 NZ 格式）。即使数据实际只有 1 维或 2 维，也需要填充成 5 维描述。

### Q2: `realdstN` 和 `srcN` 的区别是什么？

**A:** `srcN` 是源 Tensor 的实际大小，`realdstN` 是目标 GM 中的 stride。当 `realdstN > srcN` 时，会在写入时添加间隔（padding）。

### Q3: 如何选择使用哪个版本？

**A:**
- 使用 **AscendC 版本**：如果你在编写标准的 AscendC 内核
- 使用 **PTO 版本**：如果你需要精确控制 PTO 指令或使用 PTO 特定功能

---

## 6. 完整示例对比

### AscendC 完整示例

```cpp
#include "tl_templates/ascend/common.h"

extern "C" __global__ __aicore__ void kernel(
    GM_ADDR input, GM_ADDR output
) {
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECCALC> ub;
    pipe.InitBuffer(ub, 1024 * sizeof(float));

    auto data_ub = ub.GetWithOffset<float>(1024, 0);
    AscendC::GlobalTensor<float> output_gm;
    output_gm.SetGlobalBuffer((__gm__ float*)output);

    // 计算逻辑...
    AscendC::SetFlag<AscendC::HardEvent::MTE2_V>(0);
    AscendC::WaitFlag<AscendC::HardEvent::MTE2_V>(0);

    // 复制回 GM
    tl::ascend::copy_ub_to_gm<float, 1024>(output_gm, data_ub);

    AscendC::SetFlag<AscendC::HardEvent::V_MTE3>(0);
    AscendC::WaitFlag<AscendC::HardEvent::V_MTE3>(0);

    pipe.Destroy();
}
```

### PTO 完整示例

```cpp
#include "tl_templates/pto/common.h"

AICORE void kernel(__gm__ float *input, __gm__ float *output) {
    auto cid = get_block_idx();

    tl::ascend_pto::TileUbDataND<float, 1, 1024, 1, 1024> data_ub;
    TASSIGN(data_ub, 0);

    // 计算逻辑...
    tl::ascend_pto::set_flag_pipeline<PIPE_MTE2, PIPE_V>(0);
    tl::ascend_pto::wait_flag_pipeline<PIPE_MTE2, PIPE_V>(0);

    // 复制回 GM
    tl::ascend_pto::copy_ub_to_gm<
        float, float, 1, 1, 1, 1024, 1,
        1024, 1024, 1024, 1024, 1024,
        1, 1024, 1, 1024
    >(output, 0, 0, 1024);

    tl::ascend_pto::set_flag_pipeline<PIPE_V, PIPE_MTE3>(0);
    tl::ascend_pto::wait_flag_pipeline<PIPE_V, PIPE_MTE3>(0);
}
```
