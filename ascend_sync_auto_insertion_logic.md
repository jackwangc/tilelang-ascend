# Ascend 自动同步插入逻辑说明

## 概述

`ascend_sync_insert.cc` 实现了针对华为 Ascend NPU 的自动同步插入机制，用于在编译阶段自动插入必要的同步操作（PipeBarrier 和 EventPair）以确保多 pipeline 之间的数据依赖关系得到正确处理。

## 核心数据结构

### BufferAccess
记录每次 buffer 访问的详细信息：
```cpp
struct BufferAccess {
    std::string buffer_name;      // buffer 名称
    bool is_write;                // 是否写操作
    std::string pipeline;          // 使用的 pipeline（如 "V", "MTE1", "MTE2" 等）
    std::string operation;         // 操作名称
    SyncGraph sync_graph;          // 同步图，记录已满足的事件依赖
    std::set<std::string> pipe_barriers;  // 已插入的 pipe barrier
    int64_t physical_address;       // 物理地址（用于别名分析）
    bool is_sliced;               // 是否为切片访问
};
```

### SyncRequirement
记录同步需求：
```cpp
struct SyncRequirement {
    std::string sync_type;        // 同步类型（如 "PipeBarrier_ALL", "PipeBarrier_V", "EventPair_V_MTE1"）
    std::string buffer_name;       // buffer 名称
};
```

## 自动插入 PipeBarrier_ALL 的场景

### 1. 切片访问（Sliced Access）

**判断条件：**
- 在 `ExprAccessAnalyzer` 中检测到以下情况之一：
  1. `tvm_access_ptr` 的 offset 参数不是 0（非零偏移）
  2. `tvm_access_ptr` 的 offset 参数不是常量（变量索引）
  3. 使用 `BufferLoad` 进行访问（BufferLoad 总是被视为切片访问）

**代码位置：** `src/transform/ascend_sync_insert.cc:142-144`

**插入逻辑：**
```cpp
if (current_access.is_sliced) {
    sync_requirements.push_back({"PipeBarrier_ALL", current_access.buffer_name});
}
```

**原因：**
切片访问可能只操作 buffer 的一部分，需要强制同步所有 pipeline 以避免数据竞争。

---

### 2. LetStmt 中的切片访问

**判断条件：**
- `LetStmt` 的 value 部分包含切片访问

**代码位置：** `src/transform/ascend_sync_insert.cc:203-217`

**插入逻辑：**
```cpp
Stmt VisitStmt_(const LetStmtNode* op) override {
    auto value_accesses = AnalyzeExprAccesses(op->value);

    bool has_sliced_access = false;
    for (const auto& access : value_accesses) {
        if (access.is_sliced) {
            has_sliced_access = true;
            break;
        }
    }

    std::vector<Stmt> stmts_before_let;
    if (has_sliced_access) {
        InsertSynchronization("PipeBarrier_ALL", stmts_before_let);
    }
    // ...
}
```

**原因：**
在变量定义阶段如果检测到切片访问，需要在 LetStmt 之前插入同步。

---

### 3. IfThenElse 语句分支

**判断条件：**
- 进入 `IfThenElse` 语句的 then 分支或 else 分支前
- 退出 `IfThenElse` 语句后

**代码位置：** `src/transform/ascend_sync_insert.cc:235-252`

**插入逻辑：**
```cpp
Stmt VisitStmt_(const IfThenElseNode* op) override {
    std::vector<Stmt> stmts;
    InsertSynchronization("PipeBarrier_ALL", stmts);

    current_access_history_.clear();
    Stmt then_case = VisitStmt(op->then_case);

    Optional<Stmt> else_case;
    if (op->else_case.defined()) {
        current_access_history_.clear();
        else_case = VisitStmt(op->else_case.value());
    }

    stmts.push_back(IfThenElse(op->condition, then_case, else_case));

    InsertSynchronization("PipeBarrier_ALL", stmts);
    current_access_history_.clear();
    return SeqStmt(stmts);
}
```

**原因：**
- 分支前同步：确保进入分支前所有 pipeline 操作完成
- 分支后同步：确保分支内的操作完成后才继续执行
- 清除访问历史：分支间的数据依赖被阻断

---

### 4. 数据依赖分析（其他 PipeBarrier）

除了 PipeBarrier_ALL，代码还会根据数据依赖关系插入特定 pipeline 的 barrier。

**判断条件：**
- 当前访问与历史访问存在数据依赖（WAW、RAW、WAR）
- 两次访问使用相同的 pipeline
- 该 pipeline 还没有插入过 barrier

**代码位置：** `src/transform/ascend_sync_insert.cc:146-158`

**数据依赖判断：**
```cpp
bool HasDataDependency(const BufferAccess& prev, const BufferAccess& curr) {
    // 基于物理地址判断
    if (prev.physical_address != -1 && curr.physical_address != -1 &&
        prev.physical_address == curr.physical_address) {
        if ((prev.is_write && curr.is_write) ||      // WAW: Write-After-Write
            (prev.is_write && !curr.is_write) ||     // RAW: Read-After-Write
            (!prev.is_write && curr.is_write)) {     // WAR: Write-After-Read
            return true;
        }
    }

    // 基于 buffer 名称判断
    if (prev.buffer_name == curr.buffer_name) {
        if ((prev.is_write && curr.is_write) ||      // WAW
            (prev.is_write && !curr.is_write) ||     // RAW
            (!prev.is_write && curr.is_write)) {     // WAR
            return true;
        }
    }
    return false;
}
```

**同步类型选择：**
```cpp
std::string GetRequiredSyncType(const BufferAccess& prev_access, const BufferAccess& curr_access) {
    // 相同 pipeline，且该 pipeline 还没有 barrier
    if (prev_access.pipeline == curr_access.pipeline
        && prev_access.pipe_barriers.find("PipeBarrier_" + prev_access.pipeline) == prev_access.pipe_barriers.end()) {
        return "PipeBarrier_" + prev_access.pipeline;  // 如 "PipeBarrier_V"
    } else {
        // 不同 pipeline，查找事件映射
        std::string event_type = GetEventType(prev_access.pipeline, curr_access.pipeline);
        if (!event_type.empty()) {
            return "EventPair_" + event_type;  // 如 "EventPair_V_MTE1"
        }
    }
    return "";
}
```

---

## 同步优化机制

代码包含一个优化函数 `OptimizeSyncRequirements`，用于去除冗余的同步操作：

**优化步骤：**

1. **去重：** 移除重复的同步类型
2. **依赖检查：** 对于每个同步需求，检查是否已经被其他同步或已有的同步图满足
3. **过滤：** 只保留真正需要的同步

**代码位置：** `src/transform/ascend_sync_insert.cc:1315-1356`

```cpp
std::vector<std::string> OptimizeSyncRequirements(const std::vector<SyncRequirement>& requirements) {
    if (requirements.empty()) {
        return {};
    }

    // 收集所有需要的同步类型
    std::vector<std::string> all_required_syncs;
    for (const auto& req : requirements) {
        all_required_syncs.push_back(req.sync_type);
    }

    // 排序并去重
    std::sort(all_required_syncs.begin(), all_required_syncs.end());
    all_required_syncs.erase(std::unique(all_required_syncs.begin(), all_required_syncs.end()), all_required_syncs.end());

    // 检查每个同步是否真正需要
    std::vector<std::string> final_syncs;
    for (const auto& sync_type : all_required_syncs) {
        bool needed = false;

        for (const auto& req : requirements) {
            if (req.sync_type == sync_type) {
                SyncGraph extended_graph = GetBufferSyncGraph(req.buffer_name);

                // 扩展同步图，添加所有其他同步
                for (const auto& other_sync : all_required_syncs) {
                    if (other_sync != sync_type) {
                        extended_graph.AddSync(other_sync);
                    }
                }

                // 如果同步图已经满足该同步，则不需要插入
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

    return final_syncs;
}
```

---

## 切片访问的详细判断

### ExprAccessAnalyzer 中的判断逻辑

**1. tvm_access_ptr 切片判断：**
```cpp
void VisitExpr_(const CallNode* op) override {
    if (op->op.same_as(builtin::tvm_access_ptr())) {
        if (op->args.size() >= 5) {
            if (auto var = op->args[1].as<VarNode>()) {
                std::string buffer_name = var->name_hint;
                accessed_buffers_.insert(buffer_name);

                // offset 不是 0 → 切片访问
                if (auto offset = op->args[2].as<IntImmNode>()) {
                    if (offset->value != 0) {
                        sliced_buffers_.insert(buffer_name);
                    }
                } else {
                    // offset 不是常量（是变量）→ 切片访问
                    sliced_buffers_.insert(buffer_name);
                }
            }
        }
    }
}
```

**2. BufferLoad 切片判断：**
```cpp
void VisitExpr_(const BufferLoadNode* op) override {
    std::string buffer_name = op->buffer->data->name_hint;
    accessed_buffers_.insert(buffer_name);

    // BufferLoad 总是被视为切片访问
    sliced_buffers_.insert(buffer_name);

    for (const auto& index : op->indices) {
        VisitExpr(index);
    }
}
```

---

## 同步插入的完整流程

### EvaluateNode 处理流程

```
1. AnalyzeStmtAccesses(stmt)
   ↓
2. 检查每个访问是否为切片访问
   - 是切片 → 添加 PipeBarrier_ALL 需求
   ↓
3. 查找相关 buffer 的历史访问
   - 存在历史访问？
   - 存在数据依赖？
   - 是 → 添加对应的同步需求（PipeBarrier_X 或 EventPair_X_Y）
   ↓
4. OptimizeSyncRequirements()
   - 去重和优化同步需求
   ↓
5. InsertSynchronization()
   - 将同步需求转换为实际的同步语句
   ↓
6. UpdateSyncStatesAfterSync()
   - 更新访问历史中的同步状态
   ↓
7. UpdateLatestAccessHistory()
   - 将当前访问记录到历史
```

---

## BufferStoreNode 中的动态同步

在循环重建阶段（`StmtFlattener`），`BufferStoreNode` 会动态插入同步：

**代码位置：** `src/transform/ascend_sync_insert.cc:771-787`

```cpp
void VisitStmt_(const BufferStoreNode* op) override {
    // 获取 buffer 名称
    std::string buffer_name = op->buffer->data->name_hint;
    std::string pipeline = "PIPE_ALL";  // 默认

    // 从访问历史中查找 pipeline
    auto it = access_history_.find(buffer_name);
    if (it != access_history_.end()) {
        const BufferAccess& access = it->second;
        if (!access.pipeline.empty() && access.pipeline != "UNKNOWN") {
            pipeline = "PIPE_" + access.pipeline;
        }
    }

    // 在 BufferStore 前插入 PipeBarrier
    result_.push_back(Evaluate(Call(DataType::Handle(),
                                    Op::Get("tl.ascend_auto_barrier"),
                                    {StringImm(pipeline)})));
    result_.push_back(GetRef<Stmt>(op));
}
```

**特点：**
- 根据 buffer 实际使用的 pipeline 动态选择 barrier 类型
- 如果找不到访问记录，使用默认的 "PIPE_ALL"
- 确保 BufferStore 操作前 pipeline 已同步

---

## 访问历史管理

### resource_scope 边界

**代码位置：** `src/transform/ascend_sync_insert.cc:181-191`

```cpp
Stmt VisitStmt_(const AttrStmtNode* op) override {
    if (op->attr_key == "resource_scope") {
        auto saved_access_history = current_access_history_;

        // 清空访问历史（新的 resource scope）
        current_access_history_.clear();

        Stmt new_body = VisitStmt(op->body);

        // 恢复访问历史
        current_access_history_ = saved_access_history;

        return AttrStmt(op->node, op->attr_key, op->value, new_body);
    }
    // ...
}
```

**原因：**
不同 resource scope 之间的 buffer 访问不会产生数据依赖，因此需要隔离访问历史。

---

## 特殊处理

### A5 平台 PIPE_V 过滤

**代码位置：** `src/transform/ascend_sync_insert.cc:1410-1413`

```cpp
void InsertSynchronization(const std::string& sync_type, std::vector<Stmt>& stmts) {
    if (sync_type == "PipeBarrier_ALL") {
        stmts.push_back(CreatePipeBarrier("PIPE_ALL"));
    } else if (sync_type.find("PipeBarrier_") == 0) {
        std::string pipeline = sync_type.substr(12);
        // A5 AIC dont need PIPE_V
        if (pipeline == "PIPE_V" && this->platform_ == "A5") {
            return;  // 跳过 PIPE_V
        }
        stmts.push_back(CreatePipeBarrier(pipeline));
    }
    // ...
}
```

**原因：**
A5 平台的 AI Core 不需要 PIPE_V 同步。

---

## 总结

PipeBarrier_ALL 的插入场景：

| 场景 | 条件 | 代码位置 |
|------|--------|----------|
| 切片访问 | buffer 访问的 offset != 0 或 BufferLoad | 142-144 |
| LetStmt | Let 的 value 部分包含切片访问 | 215-217 |
| IfThenElse 入口 | 进入 if 分支前 | 237 |
| IfThenElse 出口 | 退出 if 分支后 | 250 |

其他 PipeBarrier 的插入场景：

| 场景 | 条件 | 同步类型 |
|------|--------|----------|
| 相同 pipeline 依赖 | WAW/RAW/WAR + 相同 pipeline + 未插入 barrier | PipeBarrier_X |
| 跨 pipeline 依赖 | WAW/RAW/WAR + 不同 pipeline | EventPair_X_Y |

优化机制确保不会插入冗余的同步操作。
