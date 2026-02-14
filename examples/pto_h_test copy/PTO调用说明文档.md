# PTO Kernel Call 方法调用说明文档

## 一、Call 方法调用和参数生成

### 1.1 基本调用模式

基于 PTO 生成的 C++ kernel (pto.cpp) 的 call 函数签名：

```cpp
extern "C" void call(
    uint8_t *K_handle,      // 输入: Key 张量
    uint8_t *W_handle,      // 输入: Weight 张量
    uint8_t *U_handle,      // 输入: U 张量
    uint8_t *G_handle,      // 输入: G 张量 (gate, 经过 cumsum 处理)
    uint8_t *workspace_1_handle,  // Workspace 1
    uint8_t *workspace_2_handle,  // Workspace 2
    uint8_t *workspace_3_handle,  // Workspace 3 (需初始化为 0)
    uint8_t *workspace_4_handle,  // Workspace 4
    uint8_t *S_handle,      // 输出: State 张量 (需初始化为 0)
    uint8_t *V_handle,      // 输出: New_V 张量
    uint8_t *FS_handle,     // 输出: Final State 张量
    void *stream            // NPU stream
)
```

### 1.2 Python 调用代码模板

```python
import ctypes
import torch
import torch.nn.functional as F

# 1. 加载库
lib_path = "./pto.so"  # 或 "./ascendc.so"
lib = ctypes.CDLL(lib_path)

# 2. 获取 NPU stream
stream = torch.npu.current_stream()._as_parameter_

# 3. 定义测试配置
B, H, L, DK, DV, C = 2, 16, 16384, 128, 128, 128
BV = DV
chunk_num = (L + C - 1) // C  # = 128
bv_num = (DV + DV - 1) // DV  # = 1

# 4. 初始化输入张量
torch.manual_seed(0)
k = torch.randn((B, H, L, DK)).npu().to(torch.float16)
w = torch.randn((B, H, L, DK)).npu().to(torch.float16)
u = torch.randn((B, H, L, DV)).npu().to(torch.float16)
g = torch.randn((B, H, L)).npu().to(torch.float)
g = F.logsigmoid(g)
k, w = F.normalize(k, dim=-1, p=2), F.normalize(w, dim=-1, p=2)

# 5. 计算 g chunk cumsum
g = ref_chunk_cumsum(g, C)

# 6. 创建 Workspace 张量
workspace_1 = torch.zeros((B * H * bv_num, C, BV)).npu().to(torch.float16)  # [32, 128, 128]
workspace_2 = torch.zeros((B * H * bv_num, C, DK)).npu().to(torch.float16)  # [32, 128, 128]
workspace_3 = torch.zeros((B * H * bv_num, DK, BV)).npu().to(torch.float16)  # [32, 128, 128]
workspace_4 = torch.zeros((B * H * bv_num, DK, BV)).npu().to(torch.float16)  # [32, 128, 128]

# 7. 创建输出张量
S = torch.zeros((B, H, chunk_num, DK, DV)).npu().to(torch.float16)  # [2, 16, 128, 128, 128]
V = torch.zeros((B, H, L, DV)).npu().to(torch.float16)               # [2, 16, 16384, 128]
FS = torch.zeros((B, H, DK, DV)).npu().to(torch.float16)           # [2, 16, 128, 128]

# 8. 调用 kernel
def call_kernel():
    return lib.call(
        ctypes.c_void_p(k.data_ptr()),
        ctypes.c_void_p(w.data_ptr()),
        ctypes.c_void_p(u.data_ptr()),
        ctypes.c_void_p(g.data_ptr()),
        ctypes.c_void_p(workspace_1.data_ptr()),
        ctypes.c_void_p(workspace_2.data_ptr()),
        ctypes.c_void_p(workspace_3.data_ptr()),
        ctypes.c_void_p(workspace_4.data_ptr()),
        ctypes.c_void_p(S.data_ptr()),
        ctypes.c_void_p(V.data_ptr()),
        ctypes.c_void_p(FS.data_ptr()),
        stream
    )

call_kernel()
```

### 1.3 参数对应表

| C++ 参数 | Python 变量 | 形状 | 数据类型 | 说明 |
|----------|-------------|------|----------|------|
| `K_handle` | `k` | `[B, H, L, DK]` | float16 | Key 输入 |
| `W_handle` | `w` | `[B, H, L, DK]` | float16 | Weight 输入 |
| `U_handle` | `u` | `[B, H, L, DV]` | float16 | U 输入 |
| `G_handle` | `g` | `[B, H, L]` | float | Gate 值 (已 cumsum) |
| `workspace_1_handle` | `workspace_1` | `[B*H*bv_num, C, BV]` | float16 | W * S 结果 |
| `workspace_2_handle` | `workspace_2` | `[B*H*bv_num, C, DK]` | float16 | 临时 K |
| `workspace_3_handle` | `workspace_3` | `[B*H*bv_num, DK, BV]` | float16 | 上一轮 S (需为 0) |
| `workspace_4_handle` | `workspace_4` | `[B*H*bv_num, DK, BV]` | float16 | K * V 结果 |
| `S_handle` | `S` | `[B, H, chunk_num, DK, DV]` | float16 | State 输出 (需为 0) |
| `V_handle` | `V` | `[B, H, L, DV]` | float16 | New_V 输出 |
| `FS_handle` | `FS` | `[B, H, DK, DV]` | float16 | Final State 输出 |
| `stream` | `stream` | - | - | NPU stream |

### 1.4 参考实现函数

```python
def ref_chunk_cumsum(g, C):
    """计算 chunk-wise cumsum"""
    B, H, L = g.shape
    chunk_num = (L + C - 1) // C
    g = g.view(B, H, chunk_num, C)
    g_sum = torch.cumsum(g, dim=-1)
    g_sum = g_sum.view(B, H, L)
    return g_sum

def ref_chunk_h(k, w, u, g, C):
    """参考实现: 计算 hidden state"""
    B, H, L, DK = k.shape
    DV = u.shape[-1]
    chunk_num = (L + C - 1) // C
    s = torch.zeros((B, H, chunk_num, DK, DV)).npu().to(torch.float)
    new_v = torch.zeros((B, H, L, DV)).npu().to(torch.float)
    k = k.float()
    u = u.float()

    for i in range(chunk_num):
        las_s = s[:, :, i, :, :]
        k_c = k[:, :, i * C : (i + 1) * C, :]
        w_c = w[:, :, i * C : (i + 1) * C, :]
        u_c = u[:, :, i * C : (i + 1) * C, :]
        g_c = g[:, :, i * C : (i + 1) * C]
        ws = torch.matmul(w_c, las_s.to(torch.float16)).float()
        new_v_c = u_c - ws
        new_v[:, :, i * C : (i + 1) * C, :] = new_v_c
        g_last = g[:, :, (i + 1) * C - 1].view(B, H, 1, 1)
        coeff_k = g_last - g_c.view(B, H, C, 1)
        g_last = torch.exp(g_last)
        coeff_k = torch.exp(coeff_k)
        k_c = (k_c * coeff_k).transpose(-2, -1)
        las_s = las_s * g_last
        kv = torch.matmul(k_c.to(torch.float16), new_v_c.to(torch.float16)).float()
        s_c = las_s + kv
        if i < chunk_num - 1:
            s[:, :, i + 1, :, :] = s_c

    return s.to(torch.float16), new_v.to(torch.float16), s_c.to(torch.float16)
```

## 二、精度验证方法

```python
# 计算 CPU 参考值
ref_s, ref_new_v, ref_final_s = ref_chunk_h(k, w, u, g, C)

# 验证精度
torch.testing.assert_close(S.cpu(), ref_s.cpu(), rtol=1e-5, atol=1e-5)
torch.testing.assert_close(V.cpu(), ref_new_v.cpu(), rtol=1e-5, atol=1e-5)
torch.testing.assert_close(FS.cpu(), ref_final_s.cpu(), rtol=1e-5, atol=1e-5)

print("Test passed!")
print("Kernel Output Match!")
```

## 三、常见问题及注意事项

### 3.1 数据类型注意事项

1. **输入张量类型**：
   - `k`, `w`, `u` 需要 `float16`
   - `g` 需要 `float`（用于精度计算）

2. **Workspace 张量类型**：
   - 所有 workspace 都是 `float16`

3. **输出张量类型**：
   - `S`, `V`, `FS` 都是 `float16`

### 3.2 初始化要求

1. **必须初始化为 0 的张量**：
   - `workspace_3`: 上一轮的 state，初始为 0
   - `S`: state 输出，初始为 0

2. **其他张量**：可以不初始化（会被 kernel 覆盖）

### 3.3 代码结构注意事项

1. **不要重复定义函数**：避免同一个函数被定义多次，造成混乱

2. **避免重复调用 kernel**：`call_kernel()` 只应该调用一次，重复调用会导致结果被覆盖

3. **正确设置 lib_path**：确保加载正确的 .so 文件，不要被覆盖

4. **使用 test_configs 循环**：保持代码结构清晰，便于扩展测试用例

### 3.4 Stream 获取

```python
stream = torch.npu.current_stream()._as_parameter_
```

必须使用 `_as_parameter_` 获取正确的 stream 指针。

## 四、精度问题分析

### 4.1 为什么会发生精度问题？

在最初的实现中，存在以下问题导致精度验证失败：

#### 问题 1: 函数重复定义

```python
def ref_chunk_cumsum(g, C):  # 第一次定义 (第 40-46 行)
    ...

def ref_chunk_cumsum(g, C):  # 第二次定义 (第 71-77 行)
    ...
```

**影响**：第二个定义会覆盖第一个，但如果两处实现有差异，会导致实际行为与预期不符。

#### 问题 2: 缩进错误

```python
g = ref_chunk_cumsum(g, C)

  # Compute g chunk cumsum (reference)  <- 这行缩进错误
def ref_chunk_cumsum(g, C):  # 这个函数定义位置错误
    ...
```

**影响**：缩进错误可能导致函数定义在错误的作用域内，或被误认为是某个块的内部函数。

#### 问题 3: 重复调用 kernel

```python
call_kernel()  # 第一次调用 (第 109 行)
print(f"Testing...")
call_kernel()  # 第二次调用 (第 113 行)
```

**影响**：第一次调用已经写入结果，第二次调用会覆盖，如果在两次调用之间有其他操作（如 print），可能导致混淆。

#### 问题 4: lib_path 被覆盖

```python
lib_path = "./pto.so"
lib_path = "./ascendc.so"  # 覆盖了上面的设置
lib = ctypes.CDLL(lib_path)
```

**影响**：实际加载的库与预期不符。

### 4.2 如何避免精度问题

1. **保持代码整洁**：不要有重复定义、冗余代码
2. **仔细检查缩进**：Python 对缩进敏感，错误的缩进会导致逻辑错误
3. **只调用一次 kernel**：确保每个张量只被写入一次
4. **与参考实现保持一致**：参考函数应该与原始版本完全相同
5. **使用相同的随机种子**：`torch.manual_seed(0)` 确保输入数据可复现
6. **逐步验证**：
   - 先验证输入数据是否一致
   - 再验证中间计算结果
   - 最后验证输出结果

## 五、完整测试代码示例

参考文件：`examples/pto_h_test/opt_gdn_chunk_h.py`

关键点：
- 使用 `test_configs` 循环结构
- 每次测试都重新初始化所有张量
- 参考函数与原版完全相同
- 只调用一次 kernel
- 清晰的错误提示和进度输出
