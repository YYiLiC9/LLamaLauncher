# LlamaLauncher

Windows 11 原生的 llama.cpp 启动器。纯 Win32 + GDI 手绘界面，零第三方 UI 依赖，
静态 CRT 单文件输出。为「每次启动 llama-server 都要手挑一遍参数」而生：参数保存为
YAML 配置，侧栏一键启动，运行时实时监控显存/内存/CPU/GPU。

## 功能

- **配置管理**：内置参数目录（`-m`、`-ngl`、`-c`、`--port` 等）+ 自定义参数，
  按组编辑；配置以 YAML 存储在数据目录，支持导入/导出、自动备份。
- **一键启动/停止**：内置拉起 `llama-server.exe` 并捕获控制台输出；
  也可接管外部启动的 llama-server 进程。
- **实时监控**：CPU / GPU / 显存 / 内存磁贴，两栏容量堆叠条
  （显存紫系 / 内存绿系：模型权重、KV 缓存、计算缓冲、其他进程、空闲），
  每段悬停气泡说明数据来源；无服务时缩为悬浮球。
- **服务日志**：像素级滚动、实时跟尾、自由选择复制（Ctrl+C / Ctrl+A）。
- **对话页**：内嵌 WebView2 打开 llama-server 自带聊天 UI，主题跟随程序；
  运行时缺失时自动回退系统浏览器。
- **中英双语**、深浅色主题（跟随系统 / 手动固定）、最小化到托盘。

## 环境

- Windows 10 1809+ / Windows 11
- MSVC 14.3x+ 与 Windows SDK（含 VS 自带 cmake / ninja）
- 对话页需要 WebView2 Runtime（Win11 自带）

## 构建

```bash
bash build_dev.sh            # 配置 + 编译
bash build_dev.sh --clean    # 全量重建
```

产物：`build/bin/LlamaLauncher.exe`（静态链接，自包含）。

也可使用 `build.bat` / `build.ps1`。工具链版本不匹配时，先在 VS 开发者环境中运行
cmake 重新生成。

## 使用

1. 设置 → 指定 llama.cpp 所在目录（或自动探测）。
2. 新建配置：选模型、调参数（改动的命令实时预览）。
3. 侧栏点击启动；顶部「对话」进入聊天页，「服务日志」查看输出。
4. 所有设置（语言、主题、目录、备份）在设置对话框中修改，**保存后生效**。

## 数据位置

| 内容 | 路径 |
| --- | --- |
| 配置 / 设置 / 备份 | `%APPDATA%\LlamaLauncher` |
| 运行日志 | `%APPDATA%\LlamaLauncher\logs` |
| WebView 配置文件 | `%APPDATA%\LlamaLauncher\webview` |

## 项目结构

```
src/
  core/    无 UI 依赖层：monitor / gguf / catalog / store / yaml / i18n / util
  ui/      控件与对话框：theme / shell(Canvas) / dialog / views / webview
  *.cpp    应用装配：app / app_paint / app_input / main
tools/     开发辅助脚本（截图、探测、验证）
```

分层约束：`core` 不依赖 `ui`；界面文案一律走 `core/i18n` 的 `Str` 枚举（中英同步）；
新增 llama.cpp 参数加进 `core/catalog.cpp` 的参数表。
