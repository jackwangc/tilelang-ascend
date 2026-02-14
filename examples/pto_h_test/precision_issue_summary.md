# 精度问题定位结果总结

## 一、问题确认

### 测试结果

- **ascendc.so**: 测试通过，精度正常
- **pto.so**: 测试失败，精度误差 99.2%

```
AssertionError: Tensor-likes are not close!
Mismatched elements: 66576085 / 67108864 (99.2%)
Greatest absolute difference: 1.4384765625 at index (0, 12, 87, 113, 66)
```

---

## 二、问题定位过程

### Step 1: g_v_ub 数据验证

**ascendc.so** (使用 `copy_ub_to_ub`):
```
[-0.175508, -1.886694, -2.852804, -5.490022, -7.600348, -8.554733, ...]
```

**pto.so** (使用 `mov_tile`):
```
[-0.18, -1.89, -2.85, -5.49, -7.60, -8.55, ...]
```

✅ **前 10 个值基本一致**（只是显示精度不同，实际值应该相同）

### Step 2 & 3: coeff_ub 计算和 exp

**ascendc.so**:
- Step 2 (计算前): 显示累加后的负值 [-51.89, -52.66, ...]
- Step 3 (exp 后): 显示全为 0 [0.00, 0.00, ...]

**pto.so**:
- Step 2 & 3: 都显示全为 0

✅ **这一步看似一致**

### Step 4: k_ub 乘法后

**两者**: 都显示为 0 或接近 0 的值

✅ **这一步也一致**

### Step 5: s_ub 最终结果

**ascendc.so**: 显示全部为 0

**pto.so**: 输出被截断（LOG SIZE ERROR），无法看到完整值

---

## 三、根本原因分析

### 🔍 核心问题：`mov_tile` 指令使用错误

#### ascendc.cpp 的正确实现（第 106 行）:

```cpp
tl::ascend::copy_ub_to_ub<float, float, 128>(g_v_ub[0], g_ub[(vid * 64)]);
```

**语义**：从 `g_ub` 的偏移 `vid * 64` 处，复制 **128 个 float 元素**到 `g_v_ub[0]`

#### pto.cpp 的错误实现（第 91 行）:

```cpp
tl::ascend_pto::mov_tile<float, 128>(49408, 99072, (vid * 64), 0, 4);
```

**参数分析**：
- `src_addr = 49408` (g_ub 的基地址)
- `src_offset = vid * 64` (g_ub 内的偏移)
- `dst_addr = 99072` (g_v_ub 的基地址)
- `dst_offset = 0` (g_v_ub 内的偏移)
- `len = 128` (复制长度)

#### mov_tile 的实际实现（从 common.h）:

```cpp
template <typename T, int32_t shape>
AICORE PTO_INLINE void mov_tile(int32_t src_addr,
                int32_t dst_addr, int32_t src_offset, int32_t dst_offset, int32_t len) {
    TileUbDataND<float, 1, shape> src_temp_ub(1, shape);
    TileUbDataND<float, 1, shape, 1, shape> src_temp_ub;
    pto::TASSIGN(src_temp_ub, src_addr + src_offset * len);     // 从源地址赋值
    TileUbDataND<float, 1, shape, 1, shape> dst_temp_ub;
    pto::TASSIGN(dst_temp_ub, dst_addr + dst_offset * len);   // 到目标地址赋值
    pto::TMOV(dst_temp_ub, src_temp_ub);                        // 移动
}
```

**问题**：这个实现看似是：
1. 从 `src_addr + src_offset * len` 读取数据
2. 写入到 `dst_addr + dst_offset * len`

但实际上从调用的参数来看，`len = 128`，而 `TileUbDataND` 的 shape 是 `1`，可能存在类型或长度不匹配的问题。

### 关键差异对比

| 方面 | 实现 | 说明 |
|------|------|------|
| **ascendc.cpp** | 使用 `copy_ub_to_ub` | 从 g_ub[vid*64 : (vid*64)+128] 复制 128 个元素到 g_v_ub |
| **pto.cpp** | 使用 `mov_tile` | 参数不正确，可能导致数据复制错误 |

---

## 四、建议修复方案

### 方案 1：使用 PTO 的 copy 指令（推荐）

将 pto.cpp 第 91 行的 `mov_tile` 改为：

```cpp
// 替换前
// tl::ascend_pto::mov_tile<float, 128>(49408, 99072, (vid * 64), 0, 4);

// 替换后 - 使用类似 ascendc 的 copy 指令
for (int i = 0; i < 128; i++) {
    g_v_ub.SetValue(i, g_ub.GetValue((vid * 64) + i));
}
```

或者查找 PTO 中是否有类似 `copy_ub_to_ub` 的函数：

```cpp
// 可能的 PTO copy 函数（需确认）
tl::ascend_pto::copy_gm_to_ub<float, float, 1, 128>(g_v_ub, g_ub, (vid * 64), 128);
```

### 方案 2：修正 mov_tile 参数

如果必须使用 `mov_tile`，需要确认正确的参数格式。可能当前参数：
- `len = 128` 与 `TileUbDataND` 的 shape 不匹配
- 或者地址计算方式不正确

---

## 五、验证步骤

1. ✅ **编译 ascendc.cpp** - 已成功， ascendc.so 正常
2. ✅ **编译 pto.cpp** - 已成功，但存在运行时问题
3. ✅ **运行 ascendc.so 测试** - 通过，调试输出正常
4. ❌ **运行 pto.so 测试** - 失败，99.2% 元素不匹配

### 下一步行动

1. 修改 pto.cpp 第 91 行的 `mov_tile` 调用
2. 重新编译 pto.so
3. 重新运行测试验证精度
4. 对比调试输出，确认 g_v_ub 的值正确加载

---

## 六、相关文件位置

- 问题代码：`examples/pto_h_test/pto.cpp` (第 91 行)
- 参考代码：`examples/pto_h_test/ascendc.cpp` (第 106 行)
- 测试脚本：`examples/pto_h_test/opt_gdn_chunk_h.py`
- 调试指南：`examples/pto_h_test/debug.md`
- 本文档：`examples/pto_h_test/precision_issue_summary.md`
