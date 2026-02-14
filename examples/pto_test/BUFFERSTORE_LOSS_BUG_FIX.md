# BufferStore 丢失问题修复总结

## 问题描述

在使用 `ForLoopUnroller` 和 `LoopRebuilder` 进行循环展开和重建时，发现丢失了一行业务代码：

**原始代码：**
```python
for ii in range(CC):  # 外层循环
    ofs = ii * C
    s_ub[ofs + 0] = g_ub[ofs + 0]  # ❌ 这行 BufferStore 丢失了
    for i in range(1, C):           # 内层循环
        tmp2 = s_ub[ofs + i - 1] + g_ub[ofs + i]
        s_ub[ofs + i] = tmp2
```

**生成的代码：**
```cpp
for (int32_t ii = 0; ii < 8; ++ii) {
  for (int32_t i = 1; i < 128; ++i) {
    AscendC::PipeBarrier<PIPE_ALL>();
    AscendC::PipeBarrier<PIPE_ALL>();
    float tmp2 = (s_ub.GetValue((((ii * 128) + i) - 1)) + g_ub.GetValue(((ii * 128) + i)));
    s_ub.SetValue(((ii * 128) + i), tmp2);
  }
}
```

## 问题分析过程

### 1. 添加调试日志

在关键位置添加了调试日志：

| 位置 | 功能 |
|------|------|
| `ForLoopUnroller::operator()` | 记录展开开始和结束 |
| `ForLoopUnroller::VisitStmt_(ForNode*)` | 记录每个循环的展开过程 |
| `LoopRebuilder::operator()` | 记录重建开始 |
| `LoopRebuilder::VisitStmt_(AttrStmtNode*)` | 记录 unrolled_loop 标记的处理 |
| `LoopRebuilder::MergeIterations()` | **核心**：记录语句分离和合并过程 |
| `LoopRebuilder::MergeStatementSequences()` | **核心**：记录4阶段合并逻辑 |
| `StmtFlattener` | 记录语句展平过程 |
| `IsSyncStatement()` | 记录同步语句识别 |

### 2. 通过日志定位问题

从日志第 173-215 行分析 `loop_0` 的重建过程：

```
[DEBUG] MergeIterations: Starting for loop 'loop_0'
[DEBUG] StmtFlattener: SeqStmt with 8 statements
[DEBUG] StmtFlattener: Added iteration_start marker (total=1)
[DEBUG] StmtFlattener: Added Evaluate (total=2, op=(empty))
[DEBUG] StmtFlattener: Added For loop var=i (total=3)  ← 这是合并后的 loop_1
[DEBUG] StmtFlattener: Added iteration_end marker (total=4)
[DEBUG] MergeIterations: [0] Found iter1 start marker
[DEBUG] MergeIterations: [1] Skip empty evaluate
[DEBUG] MergeIterations: [2] Added to iter1 (type=tir.For, total=1)
```

**问题发现：** `BufferStore` 语句完全没有出现在展平结果中！

### 3. 代码结构分析

**ForLoopUnroller 生成的结构：**
```cpp
AttrStmt(unrolled_loop, loop_0, SeqStmt([
  AttrStmt(iteration_start, loop_0_iter1, Evaluate(0)),
  SeqStmt([                                    ← processed_body
    BufferStore(...),                          ← s_ub[ofs+0] = g_ub[ofs+0]
    AttrStmt(unrolled_loop, loop_1, SeqStmt([...]))
  ]),
  AttrStmt(iteration_end, loop_0_iter1, Evaluate(0)),
  AttrStmt(iteration_start, loop_0_iter2, Evaluate(0)),
  SeqStmt([BufferStore, ...]),
  AttrStmt(iteration_end, loop_0_iter2, Evaluate(0))
]))
```

**StmtFlattener 展平后的期望结果：**
1. `iteration_start` 标记
2. `BufferStore` ← **这个丢失了！**
3. `iteration_end` 标记
4. ...

### 4. 根本原因

**问题根源：** `StmtFlattener` 继承自 `StmtVisitor`（基类），而不是 `StmtMutator`。

当 `StmtFlattener` 遇到 `BufferStore` 语句时：
- 调用 `VisitStmt(BufferStore)`
- `StmtVisitor` 基类没有 `VisitStmt_(const BufferStoreNode*)` 的默认实现
- 默认实现：**不将语句添加到任何结果容器中**
- 结果：`BufferStore` 被"访问"了，但没有被保留

### 5. 对比其他语句类型

`StmtFlattener` 中已经处理的语句类型：

```cpp
void VisitStmt_(const SeqStmtNode*)     // ✓ 遍历子语句
void VisitStmt_(const IfThenElseNode*)  // ✓ 添加到 result_
void VisitStmt_(const EvaluateNode*)     // ✓ 添加到 result_
void VisitStmt_(const LetStmtNode*)      // ✓ 添加到 result_
void VisitStmt_(const ForNode*)          // ✓ 添加到 result_
void VisitStmt_(const AllocateNode*)     // ✓ 添加到 result_
// ❌ 缺少 BufferStoreNode 的处理！
```

## 解决方案

### 修复代码

在 `StmtFlattener` 类中添加对 `BufferStoreNode` 的处理：

```cpp
void VisitStmt_(const BufferStoreNode* op) override {
  result_.push_back(GetRef<Stmt>(op));
  std::cerr << "[DEBUG] StmtFlattener: Added BufferStore (total=" << result_.size() << ")" << std::endl;
}
```

### 修复位置

文件：`src/transform/ascend_sync_insert.cc`
类：`LoopRebuilder::StmtFlattener`
位置：在 `VisitStmt_(const AllocateNode*)` 之后添加

### 修复后的效果

修复后，`StmtFlattener` 会正确展平所有语句类型：

```
[DEBUG] StmtFlattener: Added iteration_start marker (total=1)
[DEBUG] StmtFlattener: Added Evaluate (total=2, op=(empty))
[DEBUG] StmtFlattener: Added BufferStore (total=3)  ← 现在会被添加
[DEBUG] StmtFlattener: Added For loop var=i (total=4)
[DEBUG] StmtFlattener: Added iteration_end marker (total=5)
```

## 调试指南

### 1. 添加调试日志的位置

当遇到类似问题时，在以下位置添加日志：

**循环展开阶段：**
```cpp
// ForLoopUnroller
std::cerr << "[DEBUG] ForLoopUnroller: Processing loop '" << loop_id << "'" << std::endl;
std::cerr << "[DEBUG] ForLoopUnroller: Creating iter1/iter2" << std::endl;
```

**循环重建阶段：**
```cpp
// LoopRebuilder
std::cerr << "[DEBUG] LoopRebuilder: Found unrolled_loop marker '" << marker << "'" << std::endl;
std::cerr << "[DEBUG] LoopRebuilder: Calling MergeIterations" << std::endl;
```

**语句展平阶段（关键）：**
```cpp
// StmtFlattener
std::cerr << "[DEBUG] StmtFlattener: SeqStmt with " << op->seq.size() << " statements" << std::endl;
std::cerr << "[DEBUG] StmtFlattener: Added " << stmt->GetTypeKey() << " (total=" << result_.size() << ")" << std::endl;
```

**语句合并阶段：**
```cpp
// MergeIterations
std::cerr << "[DEBUG] MergeIterations: [" << i << "] Added to iter1/iter2 (type=" << stmt_type << ")" << std::endl;
std::cerr << "[DEBUG] MergeIterations: Final counts - iter1=" << iter1_stmts.size()
          << ", iter2=" << iter2_stmts.size() << ", outside=" << outside_stmts.size() << std::endl;
```

### 2. 关键日志模式

**正常情况：**
```
[DEBUG] MergeIterations: Flattened 12 stmts
[DEBUG] MergeIterations: [2] Added to iter1 (type=tir.Evaluate, total=1)
[DEBUG] MergeIterations: [3] Added to iter1 (type=tir.BufferStore, total=2)  ← 注意这里
[DEBUG] MergeIterations: Final counts - iter1=3, iter2=3, outside=0
```

**异常情况（语句丢失）：**
```
[DEBUG] MergeIterations: Flattened 10 stmts  ← 数量不对
[DEBUG] MergeIterations: Final counts - iter1=1, iter2=1, outside=0  ← 数量不对
// 缺少 "Added to iter1/iter2 (type=tir.BufferStore)" 的日志
```

### 3. 使用 TVM IR 打印工具

```python
import tvm
from tvm import tir

# 在关键位置打印 IR
print("Before rebuild:\n", processed_stmt)
print("After flatten:\n", all_stmts)
print("iter1_stmts:\n", iter1_stmts)
print("iter2_stmts:\n", iter2_stmts)
print("merged:\n", merged_stmts)
```

### 4. 检查清单

当遇到语句丢失问题时，依次检查：

- [ ] **StmtFlattener 是否处理了该语句类型？**
  检查 `StmtFlattener` 类中是否有对应的 `VisitStmt_(const XxxNode*)` 方法

- [ ] **语句是否在标记范围内？**
  检查日志中语句是在 `iteration_start` 之后，`iteration_end` 之前

- [ ] **语句是否被错误分类？**
  检查日志中是否有 "WARNING: Statement outside both iterations"

- [ ] **合并过程中是否去重过度？**
  检查 `MergeStatementSequences` 的日志，确认去重逻辑是否正确

## 相关代码位置

| 类/方法 | 行号 | 功能 |
|----------|--------|------|
| `ForLoopUnroller::operator()` | 263-268 | 循环展开入口 |
| `ForLoopUnroller::VisitStmt_(ForNode*)` | 273-324 | 展开单个 For 循环 |
| `LoopRebuilder::MergeAndRebuildNested()` | 452-457 | 递归处理嵌套循环 |
| `LoopRebuilder::MergeIterations()` | 459-508 | **核心**：合并迭代 |
| `LoopRebuilder::MergeStatementSequences()` | 513-586 | 合并同步语句 |
| `StmtFlattener` | 849-931 | 展平语句结构 |
| `StmtFlattener::VisitStmt_(const AttrStmtNode*)` | 874-910 | 处理标记语句 |
| `StmtFlattener::VisitStmt_(const BufferStoreNode*)` | **917-920** | **修复点** |

## 经验总结

### 1. StmtVisitor vs StmtMutator

- **StmtVisitor**：只遍历和访问，不修改
  - 用于分析和提取信息
  - 需要手动添加到结果容器

- **StmtMutator**：用于转换和修改
  - 默认返回原始语句
  - 但对于特定节点类型需要显式实现

### 2. 语句展平的常见陷阱

```cpp
// ❌ 错误：只处理标记本身
result_.push_back(marker_stmt);
VisitStmt(marker_stmt->body);  // body 的内容被忽略

// ✓ 正确：展开标记内的语句
result_.push_back(marker_stmt);
VisitStmt(marker_stmt->body);
if (!IsEmpty(marker_stmt->body)) {
  result_.push_back(marker_stmt->body);  // 显式添加 body
}
```

### 3. 嵌套循环的处理顺序

```
ForLoopUnroller (展开)
  ↓
AttrStmt(unrolled_loop, outer_loop, ...)
  ↓
LoopRebuilder::VisitStmt_(AttrStmt*)
  ↓
LoopRebuilder::MergeAndRebuildNested (先处理内层)
  ↓
LoopRebuilder::MergeIterations (再处理外层)
```

### 4. 调试时关注的数据

| 数据点 | 预期值 | 实际值（错误时） |
|--------|----------|------------------|
| 展平后的语句总数 | 应该 ≈ 原始数量 × 2 | 明显少于预期 |
| iter1_stmts.size() | iter2_stmts.size() | 不相等（对称情况） |
| outside_stmts.size() | 应该 = 0 | > 0 表示有语句在迭代外 |
| 最终 merged_stmts.size() | 应该 ≈ iter1 + iter2 - 去重 | 明显少于预期 |

## 测试验证

修复后应生成的代码：
```cpp
for (int32_t ii = 0; ii < 8; ++ii) {
  s_ub.SetValue((ii * 128), g_ub.GetValue(ii * 128));  ← 恢复了！
  for (int32_t i = 1; i < 128; ++i) {
    AscendC::PipeBarrier<PIPE_ALL>();
    AscendC::PipeBarrier<PIPE_ALL>();
    float tmp2 = (s_ub.GetValue((((ii * 128) + i) - 1)) + g_ub.GetValue(((ii * 128) + i)));
    s_ub.SetValue(((ii * 128) + i), tmp2);
  }
}
```

## 后续改进建议

1. **添加更多语句类型的处理**
   - `BufferLoadNode`
   - 其他可能缺失的节点类型

2. **增强 StmtFlattener 的健壮性**
   - 添加对未处理节点类型的警告
   - 使用 `static_assert` 确保所有必要方法都已实现

3. **单元测试**
   - 创建只包含 BufferStore 的简单测试用例
   - 验证展平前后语句数量一致

4. **代码审查**
   - 检查所有继承 `StmtVisitor` 或 `StmtMutator` 的类
   - 确保对相关节点类型都有处理
