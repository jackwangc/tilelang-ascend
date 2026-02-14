# 精度调试测试步骤

## 一、已添加的调试日志位置

### ascendc.cpp 调试点

| 位置 | 变量 | 说明 |
|------|------|------|
| Step 1 (第108行) | `g_v_ub` | 验证从 g_ub 复制的数据 |
| Step 2 (第115行) | `coeff_ub` | 验证 coeff 初始状态 |
| Step 3 (第124行) | `coeff_ub` | 验证 exp 后的 coeff |
| Step 4 (第145行) | `k_ub` | 验证乘法后的 k |
| Step 5 (第189行) | `s_ub` | 验证最终结果 |

### pto.cpp 调试点

| 位置 | 变量 | 说明 |
|------|------|------|
| Step 1 (第92行) | `g_v_ub` | 验证 mov_tile 后的数据 |
| Step 2 (第96行) | `coeff_ub` | 验证 coeff 初始状态 |
| Step 3 (第101行) | `coeff_ub` | 验证 exp 后的 coeff |
| Step 4 (第123行) | `k_ub` | 验证乘法后的 k |
| Step 5 (第166行) | `s_ub` | 验证最终结果 |

---

## 二、编译和测试步骤

### 2.1 编译 ascendc.cpp

```bash
cd examples/pto_h_test
# 使用您的编译命令编译 ascendc.cpp 生成 ascendc.so
# 例如:
# ascendc-build --ascending-command ascendc.cpp -o ascendc.so
```

### 2.2 编译 pto.cpp

```bash
# 使用您的编译命令编译 pto.cpp 生成 pto.so
# 例如:
# ascendc-build --ascending-command pto.cpp -o pto.so
```

### 2.3 运行测试 - ascendc.so (基准)

修改 `opt_gdn_chunk_h.py`:
```python
lib_path = "./ascendc.so"
```

运行测试：
```bash
python examples/pto_h_test/opt_gdn_chunk_h.py
```

**预期结果**：测试通过，并打印 TDEBUG 日志

### 2.4 运行测试 - pto.so (待验证)

修改 `opt_gdn_chunk_h.py`:
```python
lib_path = "./pto.so"
```

运行测试：
```bash
python examples/pto_h_test/opt_gdn_chunk_h.py
```

**预期结果**：测试失败，并打印 DumpTensor 日志

---

## 三、对比输出日志

### 3.1 Step 1: g_v_ub 数据验证

**ascendc.cpp 输出** (TPRINT):
```
g_v_ub = [g0, g1, g2, ..., g127]
```

**pto.cpp 输出** (DumpTensor):
```
g_v_ub = [g0', g1', g2', ..., g127']
```

**对比**：检查前 10 个值是否一致

- ✅ 如果一致 → 继续 Step 2
- ❌ 如果不一致 → **问题在这里！`mov_tile` 指令可能有误**

### 3.2 Step 2: coeff 初始状态

对比 `coeff_ub` 在计算前的值（应该全为 0 或相同值）

### 3.3 Step 3: coeff exp 后

对比 `exp(coeff)` 的结果

### 3.4 Step 4: k_ub 乘法后

对比 `k_ub * coeff` 的结果

### 3.5 Step 5: s_ub 最终结果

对比最终计算结果 `s_ub`

---

## 四、疑似问题点

### 🔍 重点怀疑：mov_tile 指令

**ascendc.cpp (第106行)**:
```cpp
tl::ascend::copy_ub_to_ub<float, float, 128>(g_v_ub[0], g_ub[(vid * 64)]);
```
语义：从 `g_ub` 偏移 `vid * 64` 处，复制 128 个 float 元素到 `g_v_ub`

**pto.cpp (第91行)**:
```cpp
tl::ascend_pto::mov_tile<float, 128>(49408, 99072, (vid * 64), 0, 4);
```
语义：**不清楚！这个 mov_tile 的参数格式不正确**

### 可能的正确写法

pto.cpp 应该使用类似的 copy 指令：
```cpp
// 方案1: 使用 copy 指令
tl::ascend_pto::copy<float, float, 1, 128>(g_v_ub, g_ub[(vid * 64)], 128);

// 方案2: 使用 mov 指令
for (int i = 0; i < 128; i++) {
    g_v_ub.SetValue(i, g_ub.GetValue((vid * 64) + i));
}
```

---

## 五、问题定位流程

```
开始
  │
  ├─ Step 1: g_v_ub 是否一致？
  │   ├─ 否 → mov_tile 指令错误 ✗ 找到问题！
  │   └─ 是 → 继续
  │
  ├─ Step 2: coeff 初始值是否一致？
  │   └─ 继续
  │
  ├─ Step 3: coeff exp 后是否一致？
  │   ├─ 否 → exp 计算或之前的计算有问题
  │   └─ 是 → 继续
  │
  ├─ Step 4: k_ub 乘法后是否一致？
  │   ├─ 否 → 乘法逻辑有问题
  │   └─ 是 → 继续
  │
  └─ Step 5: s_ub 最终结果是否一致？
      ├─ 否 → 累加或其他逻辑有问题
      └─ 是 → 精度问题已解决！
```

---

## 六、参考文件位置

- 测试脚本：`examples/pto_h_test/opt_gdn_chunk_h.py`
- 问题代码：`examples/pto_h_test/pto.cpp`
- 参考代码：`examples/pto_h_test/ascendc.cpp`
- 本文档：`examples/pto_h_test/debug_test_guide.md`
