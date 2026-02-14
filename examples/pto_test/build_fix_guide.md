# AscendC/PTO 编译脚本修复指南

## 问题现象

执行 `build.sh` 或 `build_pto.sh` 编译时出现以下错误：

```
ascendc.cpp:1:10: fatal error: 'tl_templates/ascend/common.h' file not found
#include "tl_templates/ascend/common.h"
         ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

## 根本原因

| 项目 | 错误配置 | 正确配置 |
|------|----------|----------|
| 模板路径 | `$TL_ROOT/templates` | `$TL_ROOT/src` |
| 实际文件位置 | - | `$TL_ROOT/src/tl_templates/ascend/common.h` |

源码中使用 `#include "tl_templates/ascend/common.h"`，编译器需要在 include 路径中找到 `tl_templates` 目录。

## 修复内容

### build.sh 修复

**文件位置**: `/mnt/workspace/cann/tilelang/tilelang-ascend/examples/pto_test/build.sh`

```bash
# 修改前
TILELANG_TEMPLATE_PATH="${TILELANG_TEMPLATE_PATH:-$TL_ROOT/templates}"

# 修改后
TILELANG_TEMPLATE_PATH="${TILELANG_TEMPLATE_PATH:-$TL_ROOT/src}"
```

### build_pto.sh 修复

**文件位置**: `/mnt/workspace/cann/tilelang/tilelang-ascend/examples/pto_test/build_pto.sh`

```bash
# 修复 1: shebang 语法错误（多余的 #）
# 修改前
##!/bin/bash

# 修改后
#!/bin/bash

# 修复 2: 模板路径
# 修改前
TILELANG_TEMPLATE_PATH="${TILELANG_TEMPLATE_PATH:-$TL_ROOT/templates}"

# 修改后
TILELANG_TEMPLATE_PATH="${TILELANG_TEMPLATE_PATH:-$TL_ROOT/src}"
```

## 编译命令

### AscendC 内核编译

```bash
cd /mnt/workspace/cann/tilelang/tilelang-ascend/examples/pto_test/
source set_env.sh
bash build.sh ascendc.cpp
```

### PTO 内核编译

```bash
cd /mnt/workspace/cann/tilelang/tilelang-ascend/examples/pto_test/
source set_env.sh
bash build_pto.sh pto.cpp          # A2 平台 (默认)
bash build_pto.sh pto.cpp A5       # A5 平台
```

## 平台参数对照

| 平台 | CCE Arch | Memory 模式 |
|------|----------|-------------|
| A2   | dav-c220 | MEMORY_BASE |
| A5   | dav-c310 | REGISTER_BASE |

## 输出文件

| 脚本 | 输出文件 |
|------|----------|
| build.sh | ascendc.so |
| build_pto.sh | pto.so |

---

## Python 运行环境配置

### 问题现象

执行 Python 测试脚本时出现以下错误：

```
ModuleNotFoundError: No module named 'tilelang'
```

### 根本原因

tilelang 模块未安装到系统 Python 环境中，需要通过 `PYTHONPATH` 环境变量指定 tilelang 源码位置。

### 解决方案

在执行 Python 脚本前，设置以下环境变量：

```bash
export TL_ROOT=/mnt/workspace/cann/tilelang/tilelang-ascend
export PYTHONPATH=${TL_ROOT}:$PYTHONPATH
export ACL_OP_INIT_MODE=1
```

### 快捷执行方式

**单次执行：**
```bash
export TL_ROOT=/mnt/workspace/cann/tilelang/tilelang-ascend && \
export PYTHONPATH=${TL_ROOT}:$PYTHONPATH && \
python opt_gdn_chunk_cumsum.py
```

**永久配置（推荐添加到 ~/.bashrc 或项目 set_env.sh）：**
```bash
# 添加到 ~/.bashrc
echo 'export TL_ROOT=/mnt/workspace/cann/tilelang/tilelang-ascend' >> ~/.bashrc
echo 'export PYTHONPATH=${TL_ROOT}:$PYTHONPATH' >> ~/.bashrc
source ~/.bashrc
```

### 完整测试流程

```bash
cd /mnt/workspace/cann/tilelang/tilelang-ascend/examples/pto_test/

# 设置环境变量
export TL_ROOT=/mnt/workspace/cann/tilelang/tilelang-ascend
export PYTHONPATH=${TL_ROOT}:$PYTHONPATH

# 运行测试
python opt_gdn_chunk_cumsum.py
```

---

## PTO 新版头文件兼容性问题修复

### 问题背景

CANN 8.5.0 自带的 pto 头文件 (`/home/developer/Ascend/cann-8.5.0/include/pto`) 是旧版本，而 tilelang-ascend 项目有更新的 pto-isa 头文件 (`$TL_ROOT/3rdparty/pto-isa`)。需要优先使用新版头文件。

### 问题 1：旧版头文件优先级过高

**错误现象：**
```
error: use of undeclared identifier 'TPRINT_IMPL'
```
编译时仍然从 `/home/developer/Ascend/cann-8.5.0/include/pto` 查找头文件。

**根本原因：**
`-I` 参数的顺序问题，编译器优先使用了旧版 CANN 路径中的头文件。

**解决方案：**
调整 `-I` 参数顺序，将 pto-isa 路径放在 ASCEND_HOME_PATH/include 前面：

```bash
# 修改前
-I"$ASCEND_HOME_PATH/include" \
...
-I"$TL_ROOT/3rdparty/pto-isa/include" \

# 修改后
-I"$TL_ROOT/3rdparty/pto-isa/include" \  # 新版优先
-I"$ASCEND_HOME_PATH/include" \
```

### 问题 2：TPRINT 未定义

**错误现象：**
```
error: use of undeclared identifier 'TPRINT_IMPL'
MAP_INSTR_IMPL(TPRINT, src);
```

**根本原因：**
新版 pto-isa 库中 `TPRINT` 只在 `_DEBUG` 模式下可用：

```cpp
// pto/common/pto_instr_impl.hpp
#ifdef _DEBUG
#include "pto/npu/a2a3/TPrint.hpp"
#endif
```

而旧版本无条件包含 TPrint.hpp。

**解决方案：**
添加 `-D_DEBUG` 编译选项。

### 问题 3：cce::printf 不兼容

**错误现象：**
```
error: use of undeclared identifier 'cce'; did you mean 'bisheng::cce'?
error: no member named 'printf' in namespace 'bisheng::cce'
```

**根本原因：**
新版 pto-isa 库的调试宏（如 `PTO_ASSERT`）使用 `cce::printf`，但需要启用 bisheng 的 printf 支持。

**解决方案：**
添加 `--cce-enable-print` 编译选项。

### 完整的 build_pto.sh 修复

**文件位置**: `/mnt/workspace/cann/tilelang/tilelang-ascend/examples/pto_test/build_pto.sh`

**关键修改：**

```bash
bisheng \
    --cce-aicore-arch="$CCE_ARCH" \
    -D_DEBUG --cce-enable-print \     # 新增：启用调试模式和 printf 支持
    -D"$MEMORY_DEF" \
    -O2 \
    -std=gnu++17 \
    -xcce \
    ... \
    -I"../../src/" \
    -I"$TL_ROOT/3rdparty/pto-isa/include" \  # 移到前面，优先使用新版头文件
    -I"$ASCEND_HOME_PATH/include" \
    ...
```

### 修复对比表

| 问题 | 旧版本 CANN | 新版 pto-isa | 解决方案 |
|------|-------------|--------------|----------|
| TPRINT 包含条件 | 无条件 | 仅 `_DEBUG` 模式 | 添加 `-D_DEBUG` |
| cce::printf 支持 | 默认支持 | 需要显式启用 | 添加 `--cce-enable-print` |
| Include 优先级 | 默认优先 | 需要调整顺序 | 调整 `-I` 参数顺序 |

### 验证编译

```bash
cd /mnt/workspace/cann/tilelang/tilelang-ascend/examples/pto_test/
bash build_pto.sh pto.cpp
```

成功输出：
```
==========================================
Build successful! Output: pto.so
==========================================
```
