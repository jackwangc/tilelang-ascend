# BinaryVecOpsCodegen 算术表达式代码生成说明

## 修改概述

本次修改使 `BinaryVecOpsCodegen` 方法能够正确识别和处理算术表达式（如 `prev_max[n_idx] + prev_sum[n_idx]`），并生成符合昇腾 PTO 规范的代码。

## 问题场景

### 输入表达式
```python
# TileLang 代码
T.tile.sub(dst_ub, src_ub, prev_max[n_idx] + prev_sum[n_idx])
```

在 TVM TIR IR 中，`prev_max[n_idx] + prev_sum[n_idx]` 是一个 **`tir::AddNode`**，而不是 `CallNode`。

## 期望生成的代码

```cpp
pipe_barrier(PIPE_V);
// 1. 获取当前行对应的标量（取负）
float scalar = -(prev_max.GetValue(n_idx) + prev_sum.GetValue(n_idx));
// 2. 定义一个只代表"一行"的临时视图
tl::ascend_pto::TileUbDataND<float, 1, 128, 1, 128> x_32_row_view;
// 3. 将视图绑定到 x_32 的具体行起始地址
TASSIGN(x_32_row_view, 16896 + (n_idx * 128) * 4);
pipe_barrier(PIPE_ALL);
// 4. 只对这一行进行 TADDS 操作
TADDS(x_32_row_view, x_32_row_view, scalar);
```

## 关键步骤

1. **检测算术表达式**：使用 `IsComplexExpression` 辅助函数检测 `tir::AddNode` 等节点
2. **生成标量声明**：将算术表达式打印为标量赋值语句
3. **创建行视图**：定义 1 行 × N 列的 `TileUbDataND` 视图
4. **TASSIGN 绑定**：将视图绑定到正确的内存地址
5. **执行操作**：对行视图执行向量运算

## 代码实现

### 1. 辅助函数 `IsComplexExpression`

```cpp
bool IsComplexExpression(const PrimExpr& expr) {
    // Check if it's a CallNode (e.g., buffer.GetValue(index))
    if (expr.as<CallNode>()) {
        return true;
    }
    // Check if it's an arithmetic operation (Add/Sub/Mul/Div/Mod/etc.)
    if (expr.as<tir::AddNode>() || expr.as<tir::SubNode>() ||
        expr.as<tir::MulNode>() || expr.as<tir::DivNode>() ||
        expr.as<tir::ModNode>() || expr.as<tir::FloorDivNode>() ||
        expr.as<tir::FloorModNode>() || expr.as<tir::MaxNode>() ||
        expr.as<tir::MinNode>()) {
        return true;
    }
    return false;
}
```

### 2. 算术表达式分支处理逻辑

```cpp
if (is_complex) {
    if (auto call_node = op->args[2].as<CallNode>()) {
        // ... CallNode 分支（原有逻辑）...
    } else {
        // 算术表达式分支（新增）

        std::string ub_name = var_names[1];
        std::string index = PrintExpr(op->args[op->args.size() - 2]);

        // 获取 UB 数据信息
        std::vector<std::string> ub_data_vector = ub_data_map_[ub_name];
        std::string ub_data_type = ub_data_vector[0];
        std::string row_view_name = ub_name + "_row_view";
        std::string scalar_name = "scalar";

        // 计算行视图维度
        int32_t ub_data_col = std::stoi(ub_data_vector[1]);  // 列数 (例如 128)

        // Step 1: pipe_barrier(PIPE_V);
        this->PrintIndent();
        this->stream << "pipe_barrier(PIPE_V);\n";

        // Step 2: 生成标量声明（可能取负）
        this->PrintIndent();
        scalar_expr = PrintExpr(op->args[2]);

        if (operation == "TSUBS") {
            this->stream << ub_data_type << " " << scalar_name << " = -("
                        << scalar_expr << ");\n";
            operation = "TADDS";  // TSUBS 转换为 TADDS
        } else {
            this->stream << ub_data_type << " " << scalar_name << " = "
                        << scalar_expr << ";\n";
        }

        // Step 3: 定义行视图 (1 行, 完整列数)
        this->PrintIndent();
        this->stream << kAscendPtoScope << "TileUbDataND<" << ub_data_type
                    << ", 1, " << ub_data_col << ", 1, " << ub_data_col << "> "
                    << row_view_name << ";\n";

        // Step 4: TASSIGN 绑定到特定地址
        this->PrintIndent();
        this->stream << "TASSIGN(" << row_view_name << ", "
                    << ub_data_vector[3] << " + (" << index
                    << " * " << ub_data_col << ") * "
                    << GetTypeLenString(ub_data_type) << ");\n";

        // Step 5: pipe_barrier(PIPE_ALL);
        this->PrintIndent();
        this->stream << "pipe_barrier(PIPE_ALL);\n";

        // Step 6: 对行视图执行操作
        this->PrintIndent();
        this->stream << operation << "(" << row_view_name << ", "
                    << row_view_name << ", " << scalar_name << ");\n";
    }
}
```

## 生成的代码示例

### TADDS 操作

**输入**：
```python
T.tile.add(dst_ub, src_ub, prev_max[n_idx] + prev_sum[n_idx])
```

**输出**：
```cpp
pipe_barrier(PIPE_V);
float scalar = prev_max.GetValue(n_idx) + prev_sum.GetValue(n_idx);
tl::ascend_pto::TileUbDataND<float, 1, 128, 1, 128> x_32_row_view;
TASSIGN(x_32_row_view, 16896 + (n_idx * 128) * 4);
pipe_barrier(PIPE_ALL);
TADDS(x_32_row_view, x_32_row_view, scalar);
```

### TSUBS 操作

**输入**：
```python
T.tile.sub(dst_ub, src_ub, prev_max[n_idx] + prev_sum[n_idx])
```

**输出**：
```cpp
pipe_barrier(PIPE_V);
float scalar = -(prev_max.GetValue(n_idx) + prev_sum.GetValue(n_idx));
tl::ascend_pto::TileUbDataND<float, 1, 128, 1, 128> x_32_row_view;
TASSIGN(x_32_row_view, 16896 + (n_idx * 128) * 4);
pipe_barrier(PIPE_ALL);
TADDS(x_32_row_view, x_32_row_view, scalar);
```

## 关键技术点

### 1. 命名空间问题
- **CallNode**：在 `namespace tvm` 中可直接访问
- **AddNode 等**：需要使用 `tir::AddNode` 前缀

```cpp
// 正确
if (expr.as<CallNode>()) { ... }
if (expr.as<tir::AddNode>()) { ... }

// 错误
if (expr.as<tir::CallNode>()) { ... }  // 不需要 tir:: 前缀
if (expr.as<AddNode>()) { ... }        // 需要 tir:: 前缀
```

### 2. TSUBS 特殊处理
- 当操作是 `TSUBS` 且标量是算术表达式时：
  - 整个表达式前加负号：`-(a + b)`
  - 操作类型转换为 `TADDS`
  - 结果等价于：`dst - (a + b)` = `dst + (-(a + b))`

### 3. 行视图地址计算
```cpp
TASSIGN(row_view, base_addr + (row_index * columns) * type_size);
```

- **base_addr**: `ub_data_vector[3]`（UB 基地址，例如 16896）
- **row_index**: 循环索引变量（例如 `n_idx`）
- **columns**: 列数（例如 128）
- **type_size**: 数据类型大小（float = 4, half = 2）

## 支持的算术运算

| 节点类型 | 示例 | 说明 |
|----------|------|------|
| `tir::AddNode` | `a + b` | 加法 |
| `tir::SubNode` | `a - b` | 减法 |
| `tir::MulNode` | `a * b` | 乘法 |
| `tir::DivNode` | `a / b` | 除法 |
| `tir::ModNode` | `a % b` | 取模 |
| `tir::FloorDivNode` | `a // b` | 整除 |
| `tir::FloorModNode` | `floormod(a, b)` | 取模 |
| `tir::MaxNode` | `max(a, b)` | 最大值 |
| `tir::MinNode` | `min(a, b)` | 最小值 |

## 与 CallNode 分支的对比

| 特性 | CallNode 分支 | 算术表达式分支 |
|------|--------------|--------------|
| **标量来源** | `buffer.GetValue(index)` | `a + b` 等算术运算 |
| **标量声明** | `auto scalar = buffer.GetValue(i)` | `float scalar = a + b` |
| **视图类型** | `TileUbDataND<type, 1, N, 1, N>` | `TileUbDataND<type, 1, N, 1, N>` |
| **TASSIGN 地址** | `base + offset * size` | `base + (row * col) * size` |
| **TSUBS 处理** | 标量名前加 `-` | 整个表达式前加 `-()` |

## 代码变更位置

**文件**：`src/target/codegen_ascend_pto.cc`

**变更位置**：
- **第 1357-1374 行**：新增 `IsComplexExpression` 辅助函数
- **第 1377-1420 行**：CallNode 分支（原有逻辑，保持不变）
- **第 1419-1472 行**：算术表达式分支（新增逻辑）

## 测试验证

### 测试用例

```python
import tilelang.language as T

@T.prim_func
def test_func(
    A: T.Tensor((1024, 128), "float16"),
    PrevMax: T.Tensor((8,), "float16"),
    PrevSum: T.Tensor((8,), "float16"),
    B: T.Tensor((1024, 128), "float16"),
):
    with T.Kernel(8, is_npu=True) as (cid, vid):
        # 使用算术表达式作为标量
        T.tile.sub(B[cid], A[cid], PrevMax[cid] + PrevSum[cid])
```

### 验证要点

1. ✅ 正确检测 `tir::AddNode`
2. ✅ 生成行视图定义
3. ✅ TASSIGN 地址计算正确
4. ✅ TSUBS 正确转换为 TADDS 并取负
5. ✅ 生成的代码符合昇腾 PTO 规范

## 注意事项

1. **类型一致性**：确保 `ub_data_type` 与实际数据类型匹配
2. **边界检查**：`ub_data_vector` 必须包含至少 4 个元素
3. **索引范围**：行索引必须在有效范围内
4. **内存对齐**：地址计算需考虑类型大小

## 后续优化

- [ ] 支持更复杂的嵌套算术表达式
- [ ] 优化行视图的重用
- [ ] 添加代码注释说明生成逻辑
- [ ] 支持其他类型的二元操作

## 相关文档

- **分析文档**：`BinaryVecOpsCodegen分析文档.md`
- **测试文件**：`testing/python/language/test_binary_vec_ops_arithmetic_expr.py`
- **修复说明**：`BinaryVecOpsCodegen算术表达式支持修复说明.md`
