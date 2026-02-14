# AscendSyncInsert 方法使用与问题分析

## 概述

`AscendSyncInsert` 是 TileLang 中用于华为昇腾NPU的**自动同步插入Pass**，它在编译过程中分析数据依赖关系，自动插入 `set_flag/wait_flag` 和 `pipe_barrier` 等同步指令。

### 调用位置

**Python端** (`tilelang/transform/__init__.py:371-380`):
```python
def AscendSyncInsert(target: Target, platform: str):
    """Auto insert sync for Ascend.

    Returns
    -------
    fpass : tvm.transform.Pass
        The result pass
    ----
    """
    return _ffi_api.AscendSyncInsert(target, platform)
```

**C++端** (`src/transform/ascend_sync_insert.cc:1437-1438`):
```cpp
TVM_REGISTER_GLOBAL("tl.transform.AscendSyncInsert")
    .set_body_typed(AscendSyncInsert);
```

**Pass流程** (`tilelang/engine/phase.py:101`):
```python
mod = tir.transform.UnrollLoop()(mod)
mod = tir.transform.RenormalizeSplitPattern()(mod)
mod = tir.transform.Simplify()(mod)
mod = tir.transform.RemoveNoOp()(mod)
mod = tir.transform.RewriteUnsafeSelect()(mod)
mod = tir.transform.HoistIfThenElse()(mod)
mod = tilelang.transform.AscendMemoryPlanning()(mod)
mod = tilelang.transform.AscendSyncInsert(target, platform)(mod)  # 在这里调用
```

---

## 整体计算逻辑

### 流程图

```
输入 PrimFunc
     │
     ▼
┌─────────────────────────────────────┐
│ 1. 检查配置: TL_ASCEND_AUTO_SYNC    │
│    如果为 False，直接返回原函数       │
└─────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────┐
│ 2. 初始化配置                        │
│    - 加载操作配置 (operation_config_) │
│    - 初始化事件映射 (event_mapping_) │
│    - 获取地址映射 (address_map_)      │
└─────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────┐
│ 3. 预处理: 展开循环                  │
│    ForLoopUnroller 将 for 循环展开   │
│    为 iter1 + iter2 两个迭代         │
└─────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────┐
│ 4. 遍历并分析语句                    │
│    对每个语句:                        │
│    - 分析缓冲区访问                  │
│    - 检查数据依赖                    │
│    - 计算需要的同步类型              │
└─────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────┐
│ 5. 插入同步指令                      │
│    - PipeBarrier: 同一流水线内       │
│    - EventPair: 不同流水线间         │
└─────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────┐
│ 6. 重建循环                          │
│    LoopRebuilder 将展开的代码        │
│    重新组装成 for 循环               │
└─────────────────────────────────────┘
     │
     ▼
┌─────────────────────────────────────┐
│ 7. 返回处理后的 PrimFunc             │
└─────────────────────────────────────┘
```

---

## 核心数据结构

### BufferAccess (缓冲区访问信息)

```cpp
struct BufferAccess {
    std::string buffer_name;      // 缓冲区名称
    bool is_write;                // 是否写操作
    std::string pipeline;         // 所属流水线 (PIPE_MTE2/PIPE_V/PIPE_MTE3等)
    std::string operation;        // 操作名称
    SyncGraph sync_graph;         // 已建立的同步图
    std::set<std::string> pipe_barriers;  // 已添加的屏障
    int64_t physical_address;     // 物理地址（用于别名分析）
    bool is_sliced;               // 是否是切片操作
};
```

### SyncGraph (同步图)

用于跟踪已建立的同步关系，支持传递闭包计算：

```cpp
struct SyncGraph {
    std::unordered_map<std::string, std::unordered_set<std::string>> graph;

    // 添加同步关系: src -> dst
    void AddSync(const std::string& sync_type);

    // 检查是否存在从 src 到 dst 的路径
    bool HasPath(const std::string& src, const std::string& dst) const;

    // 计算传递闭包
    SyncGraph ComputeTransitiveClosure() const;
};
```

---

## 同步类型选择逻辑

### GetRequiredSyncType 函数 (ascend_sync_insert.cc:1240-1251)

```cpp
std::string GetRequiredSyncType(const BufferAccess& prev_access,
                                 const BufferAccess& curr_access) {
    // 情况1: 同一流水线内的访问
    if (prev_access.pipeline == curr_access.pipeline
        && prev_access.pipe_barriers.find("PipeBarrier_" + prev_access.pipeline)
            == prev_access.pipe_barriers.end()) {
      return "PipeBarrier_" + prev_access.pipeline;
    }
    // 情况2: 不同流水线间的访问
    else {
      std::string event_type = GetEventType(prev_access.pipeline, curr_access.pipeline);
      if (!event_type.empty()) {
        return "EventPair_" + event_type;
      }
    }
    return "";
}
```

### 同步类型映射表

| 场景 | 同步类型 | 示例 |
|------|---------|------|
| 同一流水线 | `PipeBarrier_PIPELINE` | `PipeBarrier_PIPE_V` |
| MTE2 → Vector | `EventPair_mte2_v` | `set_flag(PIPE_MTE2, PIPE_V, id)` + `wait_flag(...)` |
| Vector → MTE3 | `EventPair_v_mte3` | `set_flag(PIPE_V, PIPE_MTE3, id)` + `wait_flag(...)` |
| 所有流水线 | `PipeBarrier_ALL` | `pipe_barrier(PIPE_ALL)` |

---

## 代码丢失问题分析

### 可能的问题点

#### 问题1: 循环展开/重建导致同步丢失

**位置**: `ForLoopUnroller` (行260-340) 和 `LoopRebuilder` (行342-396)

**问题原因**:
```cpp
// ForLoopUnroller 将 for 循环展开为 iter1 + iter2
unrolled_stmts.push_back(processed_body);  // iter1
unrolled_stmts.push_back(processed_body);  // iter2

// LoopRebuilder 的 MergeIterations 合并两个迭代
// 如果合并逻辑有问题，可能导致某些语句被丢弃
```

**调试方法**:
```python
# 在 phase.py 中添加打印
mod = tilelang.transform.AscendSyncInsert(target, platform)(mod)
print("=== After AscendSyncInsert ===")
print(mod)  # 检查生成的IR
```

#### 问题2: 同步优化过于激进

**位置**: `OptimizeSyncRequirements` (行1289-1330)

```cpp
// 优化逻辑会移除"冗余"的同步
for (const auto& sync_type : all_required_syncs) {
  bool needed = false;
  for (const auto& req : requirements) {
    if (req.sync_type == sync_type) {
      SyncGraph extended_graph = GetBufferSyncGraph(req.buffer_name);
      // 如果已存在传递关系，可能跳过插入
      if (!IsSyncSatisfiedByGraph(sync_type, extended_graph)) {
        needed = true;
        break;
      }
    }
  }
  if (needed) {
    final_syncs.push_back(sync_type);
  }
}
```

**问题**: `IsSyncSatisfiedByGraph` 只检查 `EventPair`，不检查 `PipeBarrier`！

```cpp
bool IsSyncSatisfiedByGraph(const std::string& sync_type, const SyncGraph& graph) {
    if (sync_type.find("EventPair_") == 0) {  // 只处理 EventPair
      // ...
    }
    return false;  // PipeBarrier 永远返回 false！
}
```

#### 问题3: 切片缓冲区处理不当

**位置**: `VisitStmt_(const LetStmtNode*)` (行203-233)

```cpp
bool has_sliced_access = false;
for (const auto& access : value_accesses) {
  if (access.is_sliced) {
    has_sliced_access = true;
    break;
  }
}

if (has_sliced_access) {
  InsertSynchronization("PipeBarrier_ALL", stmts_before_let);
}
```

**问题**: 如果切片检测不准确，可能导致多余的同步或缺少必要的同步。

#### 问题4: 事件ID分配冲突

**位置**: `AllocateEventId` (行1397-1399)

```cpp
int AllocateEventId() {
    event_id_counter_ = (event_id_counter_ + 1) % 8;  // 0-7 循环
    return event_id_counter_;
}
```

**问题**: 如果超过8个同步事件，ID会重复使用，可能导致同步混乱。

---

## 调试方法

### 1. 启用详细日志

修改 `ascend_sync_insert.cc` 添加调试打印：

```cpp
void InsertSynchronization(const std::string& sync_type, std::vector<Stmt>& stmts) {
    // 添加调试输出
    std::cerr << "[DEBUG] Inserting sync: " << sync_type << std::endl;

    if (sync_type == "PipeBarrier_ALL") {
      stmts.push_back(CreatePipeBarrier("PIPE_ALL"));
    } else if (sync_type.find("PipeBarrier_") == 0) {
      // ...
    }
}
```

### 2. 打印中间IR

```python
# 在测试脚本中
import tilelang
from tilelang import TL

# 启用 IR 打印
tl.PrintIR()

@tilelang.jit(...)
def kernel(...):
    ...
```

### 3. 检查配置文件

确保操作配置文件正确：

```cpp
// 示例配置 (从配置文件加载)
{
    "copy_gm_to_ub": {
        "default_pipeline": "PIPE_MTE2",
        "buffer_accesses": {
            "0": "read"   // 第一个参数是读取
        }
    },
    "copy_ub_to_gm": {
        "default_pipeline": "PIPE_MTE3",
        "buffer_accesses": {
            "0": "write"  // 第一个参数是写入
        }
    }
}
```

### 4. 对比开启/关闭自动同步

```python
# 方案1: 启用自动同步
pass_configs = {
    tilelang.PassConfigKey.TL_ASCEND_AUTO_SYNC: True,
}
ker1 = cumsum_ker(B, H, L, C, pass_configs=pass_configs)
print("=== With Auto Sync ===")
print(ker1.get_kernel_source())

# 方案2: 禁用自动同步
pass_configs = {
    tilelang.PassConfigKey.TL_ASCEND_AUTO_SYNC: False,
}
ker2 = cumsum_ker(B, H, L, C, pass_configs=pass_configs)
print("=== Without Auto Sync ===")
print(ker2.get_kernel_source())

# 对比差异
```

---

## 常见问题与解决

### 问题1: 自动同步插入的代码不完整

**症状**: 某些地方缺少 `wait_flag` 或 `set_flag`

**原因**: `MergeIterations` 合并逻辑有问题

**解决**:
```cpp
// 检查 MergeStatementSequences (行513-585)
// 确保同步语句正确保留
```

### 问题2: 精度错误

**症状**: 使用自动同步时结果不正确

**原因**: `IsSyncSatisfiedByGraph` 逻辑错误，跳过了必要的 `PipeBarrier`

**解决**:
```cpp
// 修改 IsSyncSatisfiedByGraph
bool IsSyncSatisfiedByGraph(const std::string& sync_type, const SyncGraph& graph) {
    if (sync_type.find("EventPair_") == 0) {
      // 现有逻辑
    } else if (sync_type.find("PipeBarrier_") == 0) {
      // 新增: 检查 PipeBarrier 是否已满足
      std::string pipeline = sync_type.substr(12);
      // 检查该流水线是否已有屏障
      for (const auto& pair : graph.graph) {
        for (const auto& dst : pair.second) {
          if (dst == "BARRIER_" + pipeline) {
            return true;
          }
        }
      }
      return false;
    }
    return false;
}
```

### 问题3: 某些同步被优化掉

**症状**: 需要的同步被 `OptimizeSyncRequirements` 移除

**原因**: 优化逻辑过于激进

**解决**: 临时禁用优化
```cpp
std::vector<std::string> OptimizeSyncRequirements(const std::vector<SyncRequirement>& requirements) {
    if (requirements.empty()) {
      return {};
    }
    // 临时禁用优化
    std::vector<std::string> final_syncs;
    for (const auto& req : requirements) {
      final_syncs.push_back(req.sync_type);
    }
    return final_syncs;
}
```

---

## 推荐配置

### 调试阶段

```python
pass_configs = {
    tilelang.PassConfigKey.TL_ASCEND_AUTO_SYNC: False,  # 禁用自动同步
    # 手动控制同步点
}
```

### 生产环境

```python
pass_configs = {
    tilelang.PassConfigKey.TL_ASCEND_AUTO_SYNC: True,   # 启用自动同步
    tilelang.PassConfigKey.TL_ASCEND_AUTO_CV_COMBINE: True,
    tilelang.PassConfigKey.TL_ASCEND_MEMORY_PLANNING: True,
}
```

---

## 相关文件位置

| 文件 | 路径 | 说明 |
|------|------|------|
| Python入口 | `tilelang/transform/__init__.py:371` | `AscendSyncInsert()` 函数 |
| Pass流程 | `tilelang/engine/phase.py:101` | Pass调用位置 |
| 核心实现 | `src/transform/ascend_sync_insert.cc` | C++实现 |
| 操作配置 | (配置文件路径) | 定义各操作的流水线属性 |

---

## 总结

`AscendSyncInsert` 是一个复杂的Pass，涉及：
1. **循环展开/重建**: 可能导致语句丢失
2. **数据依赖分析**: 缓冲区访问检测
3. **同步类型选择**: PipeBarrier vs EventPair
4. **同步优化**: 可能过于激进

**调试步骤**:
1. 先禁用自动同步，手动编写同步代码
2. 启用自动同步，对比生成代码的差异
3. 在C++代码中添加调试输出
4. 检查 `MergeIterations` 和 `OptimizeSyncRequirements` 逻辑

**建议**: 对于关键代码，使用手动同步（`TL_ASCEND_AUTO_SYNC: False`）更可靠。



-----------------

## 问题分析

我仔细阅读了代码，发现了几个可能导致代码丢失的潜在问题位置：

### 问题1：`LoopRebuilder::MergeStatementSequences` 的 iter2 处理逻辑 (行536-552)

**最严重的问题在这里：**

```cpp:src/transform/ascend_sync_insert.cc
for (const auto& stmt : iter2_stmts) {
  if (IsSyncStatement(stmt)) {
    if (exec_index < iter2_syncs_before_execs.size()) {
      iter2_syncs_before_execs[exec_index].push_back(stmt);
    } else {
      current_syncs.push_back(stmt);
    }
  } else if (!IsMarkerStatement(stmt)) {
    exec_index++;  // ⚠️ 问题：只有遇到exec语句才递增
  }
}
```

**问题场景：**
假设 iter1 有 `[exec1, exec2, exec3]`，iter2 有 `[sync1, sync2, exec1, exec2, sync3, exec3]`

- iter2 开头的 `sync1`, `sync2` 会放入 `iter2_syncs_before_execs[0]`
- 然后 `exec_index` 遇到 `exec1` 才递增为 1
- 如果 iter2 结尾有 sync（如 `sync3`），它会被放入 `current_syncs`
- 但如果 iter2 的 exec 语句数量少于 iter1，末尾的 sync 可能被丢弃

**修复建议：** 需要正确匹配 iter2 的 exec 索引位置，确保所有同步操作都被保留。

---

### 问题2：`ForLoopUnroller::VisitStmt_(const SeqStmtNode*)` (行316-325)

```cpp
Stmt VisitStmt_(const SeqStmtNode* op) override {
  std::vector<Stmt> new_stmts;
  for (const Stmt& stmt : op->seq) {
    new_stmts.push_back(VisitStmt(stmt));
  }
  if (new_stmts.empty()) {
    return Evaluate(0);
  }
  return SeqStmt(new_stmts);  // ⚠️ 当 new_stmts.size() == 1 时，应该返回单个 stmt
}
```

**问题：** 当处理后的 SeqStmt 只包含一个语句时，返回的仍然是 `SeqStmt` 包装，而不是直接返回该语句。这可能导致后续处理时语句结构不一致。

---

### 问题3：`LoopRebuilder::MergeIterations` 中语句过滤逻辑 (行442-446)

```cpp
if (in_iter1) {
  iter1_stmts.push_back(stmt);
} else if (in_iter2) {
  iter2_stmts.push_back(stmt);
}
// ⚠️ 如果语句不在任何迭代范围内，会被静默丢弃！
```

**问题：** 当一个语句不在 `iter1` 也不在 `iter2` 范围内时（例如在标记之前或嵌套循环中的其他语句），会被静默丢弃。

---

### 问题4：`StmtFlattener::VisitStmt_(const AttrStmtNode*)` (行743-756)

```cpp
void VisitStmt_(const AttrStmtNode* op) override {
  if (op->attr_key == "iteration_start" ||
      op->attr_key == "iteration_end") {
    result_.push_back(GetRef<Stmt>(op));
    VisitStmt(op->body);  // ⚠️ body 被访问但没有单独加入 result_
  } else if (op->attr_key == "unrolled_loop") {
    VisitStmt(op->body);  // ⚠️ AttrStmt 本身没加入，只有 body 被访问
  }
  // ...
}
```

**问题：** 对于 `iteration_start/end`，body 的内容会被递归访问但可能不会被正确展平；对于 `unrolled_loop`，外层 AttrStmt 被跳过可能导致某些上下文信息丢失。

---

## 调试建议

### 1. 添加调试日志

在关键位置添加打印语句来追踪语句流向：

```cpp
// 在 MergeIterations 开头
std::cout << "=== MergeIterations: loop_id=" << loop_id << " ===" << std::endl;
std::cout << "Flattened stmts count: " << all_stmts.size() << std::endl;

// 在循环中
std::cout << "Processing stmt type: " << stmt->GetTypeKey() << std::endl;

// 在 iter1/iter2 分离后
std::cout << "iter1_stmts: " << iter1_stmts.size() << std::endl;
std::cout << "iter2_stmts: " << iter2_stmts.size() << std::endl;
```

### 2. 使用 TVM IR 打印工具

```cpp
// 在 MergeAndRebuildForLoops 前后打印 IR
std::cout << "Before rebuild:\n" << processed_stmt << std::endl;
Stmt result = rebuilder(processed_stmt);
std::cout << "After rebuild:\n" << result << std::endl;
```

### 3. 创建最小测试用例

创建一个简单的测试 IR，包含：
- 一个 For 循环
- 循环内有多个语句（包括 sync）
- 在 iter1 和 iter2 之间有不同数量的语句

### 4. 断言检查

添加断言来捕获异常情况：

```cpp
// 在 MergeStatementSequences 中
assert(exec_stmts.size() == syncs_before_execs.size() && 
       "Exec stmts and syncs count mismatch!");

// 在 MergeIterations 结尾
assert(!iter1_stmts.empty() && !iter2_stmts.empty() &&
       "One or both iterations are empty!");
```

### 5. 对比 iter1 和 iter2 的 exec 语句数量

在 `MergeStatementSequences` 中添加：

```cpp
if (iter1_stmts.size() != iter2_stmts.size()) {
  std::cerr << "WARNING: iter1 has " << iter1_stmts.size() 
            << " execs, iter2 has " << iter2_stmts.size() << " execs" << std::endl;
  // 打印详细内容帮助调试
}
```

---

需要我帮你实现具体的修复或添加更详细的调试代码吗？
