# RusticCppIDE

一个基于 **C++ + Qt** 的轻量 C++ IDE（功能与界面风格对标 Dev‑C++），并为 **`rustic.hpp`**（Rust 风格宏/语法糖头文件库）提供“原生语法适配”的编辑体验。

> 这是一个 **vibe coding** 项目：主要由 **OpenAI Codex + GPT‑5.2** 迭代实现。
<img width="1920" height="1016" alt="4c34316cd16e2ce65009ffa71ec88977" src="https://github.com/user-attachments/assets/0db88568-a700-4d53-a1c0-52b7d86816f5" />

---

## 功能概览（对标 Dev‑C++ + 现代 LSP）

- 工程模型：使用 `.rcppide.json` 描述工程（源文件、include、编译参数、Debug/Release 配置、运行参数等）。
- 一键编译/运行：
  - `F9` 编译
  - `Ctrl+F10` 运行
  - 生成 Makefile（便于脱离 IDE 构建）
- clangd AST/LSP：
  - 诊断（红波浪）
  - 自动补全（默认行为，`Tab` 选择/确认）
  - 跳转定义、查找引用、重命名
  - 语义高亮（semanticTokens）与符号树
- rustic.hpp 适配：
  - 基础模式：正则级高亮（轻量，不做复杂多层解析）
  - 可选开启 AST/clangd 深度模式：用于更完整的语义能力（较慢）
- 调试（GDB‑MI）：
  - 断点、单步、继续、停止
  - 调用栈/局部变量/线程/监视表达式
  - 条件断点、命中次数、日志断点等
- 内置终端（Dock）：在 IDE 内直接执行 bash/zsh/powershell（按平台探测）。

工程迭代清单与完成状态见：`plan.md`；已知痛点与修复记录见：`bug.txt`。

---

## 依赖

基础依赖：

- CMake ≥ 3.16
- Qt：优先 Qt6（自动探测），否则 Qt5
- C++17 编译器（g++/clang++）

可选但强烈建议安装：

- `clangd`：用于 AST/LSP（诊断、补全、跳转、重命名等）
- `gdb`：用于调试

说明：

- 在 Wayland（KDE Plasma）下运行需要系统 Qt 平台插件正常（通常发行版默认即可）。
- 若你在终端启动时看不到窗口但进程存在，多半与窗口状态/合成器有关；目前启动阶段保留了 `DEBUG_STARTUP` 输出用于定位。

---

## 构建与运行

### 方式 A：用项目自带 Makefile（推荐）

```bash
make build
make run
```

### 方式 B：直接使用 CMake

```bash
cmake -S . -B build
cmake --build build -j
./build/RusticCppIDE
```

---

## 使用说明（最常用）

### 1) 新建/打开工程

- 菜单：工程 → 新建工程 / 打开工程
- 工程文件：`*.rcppide.json`

### 2) rustic.hpp

- 菜单：工程 → 获取 rustic.hpp (GitHub)
  - 会拉取 `rustic.hpp` 到工程（或指定目录）并配置 include

### 3) AST/clangd 深度解析开关

- 菜单：视图 → 启用 AST/clangd 解析(较慢)
  - 关闭：保持轻量高亮/轻量逻辑（避免复杂多层解析）
  - 开启：启用 clangd 语义能力（诊断、补全更完整、符号树更准确）

### 4) 终端

- 菜单：视图 → 终端
- 默认会选择系统可用 shell：
  - Linux：优先 `$SHELL`，否则 `/bin/zsh`，再否则 `/bin/bash`
  - Windows：`powershell`
当然，目前来安好像有问题捏
---

## 项目结构

- `src/`：主要源码
- `build/`：CMake 构建目录（被 `.gitignore` 忽略）
- `plan.md`：对标 Dev‑C++ 的功能迭代计划
- `bug.txt`：已知痛点/修复记录

---

## License

你可以拿去随便做任何事情

