## AscendC vs PTO `copy_ub_to_gm` 对比分析

---

## 1. AscendC 版本

### 调用代码
```cpp
tl::ascend::copy_ub_to_gm<float, 1024>(
    S[((((cid / 16) * 32768) + (vid * 16384)) + ((cid % 16) * 1024))],  // dstTensor
    s_ub[0],                                                               // srcTensor
    524288                                                                 // realdstN
);
```

### 函数签名
```cpp
template <typename T, uint32_t srcN, uint32_t srcM = 1>
CATLASS_DEVICE void copy_ub_to_gm(
    GlobalTensor<T> dstTensor,    // 目标 GM Tensor 对象
    LocalTensor<T> srcTensor,     // 源 UB Tensor 对象
    uint32_t realdstN = 1         // 目标 GM stride（元素数）
);
```

### 参数详解

| 参数 | 值 | 含义 |
|------|-----|------|
| **T** | `float` | 数据类型 |
| **srcN** | `1024` | 源 UB 大小：1024 个 float 元素 |
| **srcM** | `1` (默认) | 源 UB M 维度 |
| **dstTensor** | `S[offset]` | 目标 **GlobalTensor 对象**，带类型和形状信息 |
| **srcTensor** | `s_ub[0]` | 源 **LocalTensor 对象**，带类型和形状信息 |
| **realdstN** | `524288` | 目标 GM 的 stride：524288 个 float 元素 |

### 数据搬运
```
源: s_ub[0] → 1024 个 float (4096 字节)
目标: S[offset] → GM 中的对应位置
```

---

## 2. PTO 版本

### 调用代码
```cpp
tl::ascend_pto::copy_ub_to_gm<
    float, float,               // T1, T2
    1, 1, 1, 1, 1024, 1,        // shape1-6
    16384 * 16 * 2,             // stride1
    16384 * 16,                 // stride2
    16384,                      // stride3
    1, 1, 1,                    // stride4-6
    1, 1024,                    // ub_shape1, ub_shape2
    1, 1024                     // valid1, valid2
>(
    S_handle + offset,          // handle: GM 原始指针
    0,                          // ub_shape_addr
    0,                          // ub_offset
    4                           // len
);
```

### 函数签名
```cpp
template <typename T1, typename T2, 
          int32_t shape1, int32_t shape2, int32_t shape3,
          int32_t shape4, int32_t shape5, int32_t shape6,
          int32_t stride1, int32_t stride2,
          int32_t stride3, int32_t stride4, int32_t stride5, int32_t stride6,
          uint32_t ub_shape1, uint32_t ub_shape2,
          uint32_t valid1, uint32_t valid2>
AICORE PTO_INLINE void copy_ub_to_gm(
    __gm__ T1 *handle,          // 目标 GM 原始指针
    int32_t ub_shape_addr,      // UB 基址
    int32_t ub_offset,          // UB 偏移（以 len 为单位）
    int32_t len                 // 复制长度单位
);
```

### 参数详解

| 参数 | 值 | 含义 |
|------|-----|------|
| **T1, T2** | `float, float` | GM 和 UB 的数据类型 |
| **shape1-6** | `1, 1, 1, 1, 1024, 1` | Global Tensor 6 维形状 |
| **stride1** | `524288` (16384×16×2) | 第0维 stride |
| **stride2** | `262144` (16384×16) | 第1维 stride |
| **stride3** | `16384` | 第2维 stride |
| **stride4-6** | `1, 1, 1` | 第3-5维 stride |
| **ub_shape1,2** | `1, 1024` | UB Tensor 形状 [1, 1024] |
| **valid1,2** | `1, 1024` | UB 有效数据大小 |
| **handle** | `S_handle + offset` | 目标 **GM 原始指针** |
| **ub_shape_addr** | `0` | UB 基址 |
| **ub_offset** | `0` | UB 偏移量 |
| **len** | `4` | **⚠️ 只影响偏移计算，不控制复制长度！** |

### 数据搬运
```
源: UB 地址 0 + (0 * 4) = 0 → 实际复制整个 temp_ub (1024 个 float)
目标: S_handle + offset
```

---

## 3. 核心区别对比表

| 特性 | AscendC 版本 | PTO 版本 |
|------|-------------|---------|
| **抽象层次** | 高级 API，使用 Tensor 对象 | 低级 API，使用原始指针 |
| **参数数量** | 3 个运行时参数 | 16+ 个模板参数 + 4 个运行时参数 |
| **类型安全** | 强类型，编译时检查 | 模板强类型，但参数更复杂 |
| **Stride 指定** | 单个 `realdstN` 参数 | 6 个独立的 stride 参数 |
| **数据描述** | Tensor 对象自带形状信息 | 需要显式指定所有维度和 stride |
| **控制粒度** | 较粗粒度 | 更细粒度，支持复杂布局 |
| **使用复杂度** | 简单直观 | 复杂，需要精确理解参数 |

---

## 4. 详细参数映射

### 相同功能的不同表达方式

| 功能 | AscendC | PTO |
|------|---------|-----|
| **数据类型** | `T = float` | `T1 = float, T2 = float` |
| **复制大小** | `srcN = 1024` | `ub_shape = [1, 1024]`, `valid = [1, 1024]` |
| **源地址** | `s_ub[0]` (Tensor) | `ub_shape_addr + ub_offset * len = 0` |
| **目标地址** | `S[offset]` (Tensor) | `S_handle + offset` (指针) |
| **Stride** | `realdstN = 524288` | `stride = [524288, 262144, 16384, 1, 1, 1]` |

### Stride 参数的对应关系

```
AscendC: realdstN = 524288

PTO 的多维 stride 展开成线性地址时的等效值：
stride1 = 524288  (对应 AscendC 的 realdstN)
stride2 = 262144  = 524288 / 2
stride3 = 16384   = 524288 / 32
...
```

---

## 5. 内部实现对比

### AscendC 版本内部实现
```cpp
// 简洁明了
AscendC::DataCopyExtParams dataCopyParams(
    srcM, srcN * sizeof(T), 
    0, 
    (realdstN - srcN) * sizeof(T), 
    0
);
AscendC::DataCopyPad(dstTensor, srcTensor, dataCopyParams);
```

### PTO 版本内部实现
```cpp
// 需要手动构造 GlobalTensor 和临时 UB
pto::GlobalTensor<T1, pto::Shape<1,1,1,1,1024,1>,
    pto::Stride<524288,262144,16384,1,1,1>> global_tensor(handle);
    
TileUbDataND<T2, 1, 1024, 1, 1024> temp_ub;
pto::TASSIGN(temp_ub, ub_shape_addr + ub_offset * len);  // = 0 + 0*4 = 0
pto::TSTORE(global_tensor, temp_ub);  // 存储 1024 个元素
```

---

## 6. 使用场景建议

| 场景 | 推荐版本 | 原因 |
|------|---------|------|
| **标准 NPU 编程** | **AscendC** | 简单、类型安全、易维护 |
| **需要精确控制内存布局** | **PTO** | 支持 6 维形状和独立 stride |
| **复杂数据格式（如 NZ）** | **PTO** | 更细粒度的控制 |
| **性能优化** | **PTO** | 可以精确控制每一步操作 |
| **快速开发** | **AscendC** | 参数少，易理解 |

---

## 7. 实际数据搬运

### 两者实际搬运的数据是相同的

```
┌─────────────────────────────────────────────┐
│           实际数据搬运                       │
├─────────────────────────────────────────────┤
│ 数据量: 1024 个 float                       │
│ 字节数: 4096 bytes                          │
│ 源地址: UB 基址 + 0                         │
│ 目标地址: GM 基址 + offset                  │
│                                             │
│ AscendC 和 PTO 执行相同的底层操作          │
└─────────────────────────────────────────────┘
```

### 两者达到相同效果的方式不同

```
AscendC: 高层抽象 → 编译器优化 → 硬件指令
PTO:      手动指定所有参数 → 直接映射到硬件指令
```

---

## 8. 总结

| 方面 | AscendC | PTO |
|------|---------|-----|
| **代码简洁性** | ✅ 简洁 | ❌ 冗长 |
| **参数数量** | 3 个 | 20+ 个 |
| **灵活性** | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **易用性** | ✅ 易用 | ❌ 需要深入理解 |
| **性能** | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐（可精确优化） |
| **适用场景** | 通用编程 | 高性能优化 |

**两者执行相同的底层操作，只是抽象层次不同！**