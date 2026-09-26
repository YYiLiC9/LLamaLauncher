#include "core/i18n.h"

namespace i18n {

static Lang g_lang = Lang::Zh;

struct Entry {
    const wchar_t* en;
    const wchar_t* zh;
};

// Order MUST match the Str enum exactly.
static const Entry kTable[] = {
    /*AppTitle*/ {L"LlamaLauncher", L"LlamaLauncher"},
    /*AppTagline*/ {L"Fast launcher for llama.cpp", L"llama.cpp 快捷启动器"},

    /*Settings*/ {L"Settings", L"设置"},
    /*ImportConfig*/ {L"Import", L"导入"},
    /*ExportConfig*/ {L"Export", L"导出"},
    /*Help*/ {L"Help", L"帮助"},
    /*WebChat*/ {L"Chat", L"对话"},

    /*Configurations*/ {L"Configurations", L"配置列表"},
    /*NewConfiguration*/ {L"New configuration", L"新建配置"},
    /*SearchPlaceholder*/ {L"Search configurations", L"搜索配置"},
    /*NoConfigs*/ {L"No configurations yet", L"还没有配置"},
    /*NoConfigsHint*/ {L"Create one to get started.", L"创建一个即可开始使用。"},

    /*WelcomeTitle*/ {L"Launch llama.cpp in one click", L"一键启动 llama.cpp"},
    /*WelcomeBody*/ {L"Pick a configuration on the left to see its parameters, or create a new one. "
                     L"Parameters are saved as YAML and can be imported or exported at any time.",
                     L"在左侧选择一个配置即可查看其参数，或新建一个。参数以 YAML 保存，"
                     L"可随时导入导出。"},
    /*CreateConfiguration*/ {L"Create configuration", L"创建配置"},

    /*DetailParams*/ {L"Launch parameters", L"启动参数"},
    /*DetailCommand*/ {L"Command line", L"命令行"},
    /*DetailNotes*/ {L"Notes", L"备注"},
    /*Start*/ {L"Start", L"启动"},
    /*Modify*/ {L"Modify", L"修改"},
    /*Delete*/ {L"Delete", L"删除"},
    /*Stop*/ {L"Stop", L"停止"},
    /*CopyCommand*/ {L"Copy command", L"复制命令行"},
    /*Copied*/ {L"Copied to clipboard", L"已复制到剪贴板"},
    /*CoreParams*/ {L"Built-in", L"内置"},
    /*CustomParams*/ {L"Custom", L"自定义"},

    /*RunningTitle*/ {L"Live resource usage", L"实时资源占用"},
    /*RunLog*/ {L"Server log", L"服务日志"},
    /*OpenInBrowser*/ {L"Open chat page", L"打开对话页面"},
    /*StopServer*/ {L"Stop server", L"停止服务"},
    /*Port*/ {L"Port", L"端口"},
    /*Uptime*/ {L"Uptime", L"运行时长"},
    /*MetricCpu*/ {L"CPU", L"CPU"},
    /*MetricGpu*/ {L"GPU", L"GPU"},
    /*MetricMemory*/ {L"Memory", L"内存"},
    /*MetricVram*/ {L"VRAM", L"显存"},
    /*MetricModel*/ {L"Model", L"模型"},
    /*MetricThreads*/ {L"Threads", L"线程"},
    /*ServerReady*/ {L"Server ready", L"服务已就绪"},
    /*ServerStarting*/ {L"Starting, loading model...", L"启动中，正在加载模型..."},
    /*ServerStopped*/ {L"Server stopped", L"服务已停止"},
    /*ServerFailed*/ {L"Exited with code", L"已退出，代码"},
    /*NotAvailable*/ {L"n/a", L"不可用"},
    /*TotalLabel*/ {L"total", L"总计"},

    /*SettingsGeneral*/ {L"General", L"常规"},
    /*SettingsLlama*/ {L"llama.cpp location", L"llama.cpp 位置"},
    /*SettingsBackup*/ {L"Backup", L"备份"},
    /*Language*/ {L"Interface language", L"界面语言"},
    /*LangZh*/ {L"简体中文", L"简体中文"},
    /*LangEn*/ {L"English", L"English"},
    /*LlamaPath*/ {L"llama.cpp folder", L"llama.cpp 目录"},
    /*LlamaPathHint*/ {L"Folder containing llama-server.exe", L"包含 llama-server.exe 的目录"},
    /*AutoDetect*/ {L"Auto-detect", L"自动查找"},
    /*AutoDetected*/ {L"Found llama.cpp", L"已找到 llama.cpp"},
    /*AutoDetectFailed*/ {L"Not found automatically. Please choose the folder manually.",
                          L"未能自动找到，请手动选择目录。"},
    /*Browse*/ {L"Browse...", L"浏览..."},
    /*UsingFolders*/ {L"Searched", L"已搜索"},
    /*BackupEnabled*/ {L"Back up configurations automatically", L"自动备份配置"},
    /*BackupDir*/ {L"Backup folder", L"备份位置"},
    /*BackupDirHint*/ {L"A copy is written whenever a configuration is created, modified or deleted.",
                       L"创建、修改或删除配置时都会写入一份副本。"},
    /*BackupNow*/ {L"Back up now", L"立即备份"},
    /*BackupDone*/ {L"Backup written", L"备份已写入"},
    /*Theme*/ {L"Follows the Windows 11 light theme", L"跟随 Windows 11 浅色主题"},

    /*CreateConfigTitle*/ {L"Create configuration", L"创建配置"},
    /*EditConfigTitle*/ {L"Modify configuration", L"修改配置"},
    /*ConfigName*/ {L"Name", L"名称"},
    /*ConfigNamePlaceholder*/ {L"e.g. Qwen3 14B on GPU", L"例如：Qwen3 14B 显卡加速"},
    /*ParamGroups*/ {L"Groups", L"分组"},
    /*GroupBasic*/ {L"Model & context", L"模型与上下文"},
    /*GroupSampling*/ {L"Sampling", L"采样"},
    /*GroupServer*/ {L"Server", L"服务"},
    /*GroupPerformance*/ {L"Performance", L"性能"},
    /*GroupGpu*/ {L"GPU offload", L"GPU 卸载"},
    /*GroupChat*/ {L"Chat template", L"对话模板"},
    /*GroupCustom*/ {L"Custom parameters", L"自定义参数"},
    /*AddCustomParam*/ {L"Add parameter", L"添加参数"},
    /*ParamName*/ {L"Parameter (name or shorthand)", L"参数（名称或缩写）"},
    /*ParamValue*/ {L"Value", L"值"},
    /*ParamDesc*/ {L"Description", L"说明"},
    /*Remove*/ {L"Remove", L"移除"},
    /*Add*/ {L"Add", L"添加"},
    /*BrowseFile*/ {L"Choose file", L"选择文件"},
    /*Save*/ {L"Save", L"保存"},
    /*Cancel*/ {L"Cancel", L"取消"},
    /*PleaseSelectParam*/ {L"Select a parameter to edit its value.", L"请选择一个参数以编辑其值。"},
    /*ValueRequired*/ {L"Please fill in a value first.", L"请先填写值。"},
    /*NameRequired*/ {L"Please enter a name.", L"请输入名称。"},
    /*InvalidParam*/ {L"Parameters must start with '-' or '--'.", L"参数必须以 '-' 或 '--' 开头。"},

    /*ImportExportTitle*/ {L"Import & export", L"导入与导出"},
    /*ImportSection*/ {L"Import from YAML", L"从 YAML 导入"},
    /*ImportHint*/ {L"Same id overwrites, new ones are appended.",
                    L"同 id 覆盖，新配置追加到列表。"},
    /*ChooseFiles*/ {L"Choose files", L"选择文件"},
    /*ExportSection*/ {L"Export to YAML", L"导出为 YAML"},
    /*ExportHint*/ {L"Tick what to export, then pick a folder.",
                    L"勾选要导出的配置，再选目标文件夹。"},
    /*SelectAll*/ {L"Select all", L"全选"},
    /*ExportSelected*/ {L"Export selected", L"导出所选"},
    /*Exported*/ {L"Exported", L"已导出"},
    /*ImportedCount*/ {L"Imported", L"已导入"},
    /*ImportError*/ {L"Import failed", L"导入失败"},
    /*ExportError*/ {L"Export failed", L"导出失败"},
    /*DeleteConfirmTitle*/ {L"Delete configuration", L"删除配置"},
    /*DeleteConfirmBody*/ {L"This configuration will be removed. A backup copy is kept when "
                           L"automatic backup is enabled. Continue?",
                           L"该配置将被删除。若已开启自动备份，会保留一份备份副本。是否继续？"},
    /*Confirm*/ {L"Confirm", L"确定"},

    /*HelpTitle*/ {L"Help", L"帮助"},
    /*HelpQuickStart*/ {L"Quick start", L"快速开始"},
    /*HelpQuickStartBody*/ {L"1. Open Settings and point the app at your llama.cpp folder "
                            L"(the Auto-detect button usually finds it).\n"
                            L"2. Create a configuration and give it a name such as \"Qwen3 8B\".\n"
                            L"3. Fill in the model file, context size and GPU layers.\n"
                            L"4. Select the configuration and press Start, then open the chat page.",
                            L"1. 打开“设置”，指定 llama.cpp 目录（通常“自动查找”即可找到）。\n"
                            L"2. 新建配置并命名，例如“Qwen3 8B”。\n"
                            L"3. 填写模型文件、上下文长度与 GPU 层数。\n"
                            L"4. 选中该配置，点击“启动”，再打开对话页面。"},
    /*HelpParams*/ {L"Parameters", L"参数说明"},
    /*HelpParamsBody*/ {L"Parameters marked as built-in are locked so that a typo cannot break the "
                        L"launch. Use the custom section to add anything else, for example "
                        L"--jinja or --no-mmap. One flag per row; values may stay empty.",
                        L"标记为“内置”的参数不可修改，避免手误导致启动失败。其他参数在“自定义参数”"
                        L"中追加，例如 --jinja、--no-mmap。每行一个参数，值可以为空。"},
    /*HelpTroubleshoot*/ {L"Troubleshooting", L"疑难解答"},
    /*HelpTroubleshootBody*/ {L"llama.cpp not found: use Auto-detect, or browse to the folder "
                              L"containing llama-server.exe. Under winget it usually lives in "
                              L"%LOCALAPPDATA%\\Microsoft\\WinGet\\Packages\\ggml.llamacpp_*.\n"
                              L"Port already in use: change the --port value in the configuration.\n"
                              L"Decoding is slow: raise --n-gpu-layers (-ngl) so more layers run on "
                              L"the GPU, and match --threads to your physical core count.",
                              L"找不到 llama.cpp：点击“自动查找”，或手动选择包含 llama-server.exe 的目录。"
                              L"winget 安装通常在 %LOCALAPPDATA%\\Microsoft\\WinGet\\Packages\\ggml.llamacpp_* 下。\n"
                              L"端口被占用：修改配置中的 --port 值。\n"
                              L"推理慢：提高 --n-gpu-layers (-ngl) 让更多层跑在 GPU，并把 --threads "
                              L"设为物理核心数。"},
    /*HelpAbout*/ {L"About", L"关于"},
    /*HelpVersion*/ {L"Version", L"版本"},
    /*HelpDataDir*/ {L"Data folder", L"数据目录"},
    /*OpenDataDir*/ {L"Open data folder", L"打开数据目录"},

    /*StatusReady*/ {L"Ready", L"就绪"},
    /*StatusRunning*/ {L"Server running on port", L"服务运行中，端口"},
    /*StatusLlamaReady*/ {L"llama.cpp found", L"已找到 llama.cpp"},
    /*StatusLlamaMissing*/ {L"llama.cpp not configured - open Settings", L"未配置 llama.cpp — 请打开设置"},
    /*AutoBackupOn*/ {L"Auto backup on", L"自动备份已开启"},

    /*Ok*/ {L"OK", L"确定"},
    /*Close*/ {L"Close", L"关闭"},
    /*Yes*/ {L"Yes", L"是"},
    /*No*/ {L"No", L"否"},
    /*SaveFailed*/ {L"Could not write the file.", L"文件写入失败。"},
    /*LoadFailed*/ {L"Could not read the file.", L"文件读取失败。"},
    /*UnnamedConfig*/ {L"Untitled", L"未命名"},
    /*WebViewLoading*/ {L"Loading the chat page...", L"正在加载对话页..."},
    /*WebViewFailed*/ {L"The WebView2 runtime is missing. Get it from Microsoft, "
                        L"or open the page in your browser instead.",
                        L"未检测到 WebView2 运行时。请安装该组件，或改用系统浏览器打开。"},
    /*Reload*/ {L"Reload", L"重新加载"},
    /*ChatNeedsServer*/ {L"Start the server first - the chat page needs it running.",
                         L"服务尚未启动，启动后才能打开对话页。"},
    /*MonitorMinimize*/ {L"Minimize", L"最小化"},
    /*LogEmpty*/ {L"No log output yet - lines appear here as the server prints them.",
                  L"暂无日志。服务的控制台输出会实时显示在这里。"},
    /*LogExternal*/ {L"This server was started outside the launcher, so its console output "
                     L"cannot be captured.",
                     L"该服务是在启动器外部启动的，无法捕获它的控制台输出。"},
    /*CommandPreview*/ {L"Command preview", L"启动命令预览"},
    /*NameDuplicate*/ {L"A configuration with this name already exists.",
                       L"已存在同名配置，请换一个名称。"},
    /*AllParams*/ {L"All parameters", L"全部参数"},
    /*ThemeMode*/ {L"Appearance", L"外观主题"},
    /*ThemeSystem*/ {L"Follow Windows", L"跟随系统"},
    /*ThemeLight*/ {L"Light", L"浅色"},
    /*ThemeDark*/ {L"Dark", L"深色"},
    /*ImportExportShort*/ {L"Import/Export", L"导入导出"},
    /*ConfigsCount*/ {L"configs", L"个配置"},
};

static_assert(sizeof(kTable) / sizeof(kTable[0]) == (size_t)Str::ConfigsCount + 1,
              "i18n table is out of sync with the Str enum");

Lang current() { return g_lang; }

void set(Lang lang) { g_lang = lang; }

const wchar_t* raw(Str id) {
    size_t i = (size_t)id;
    if (i >= sizeof(kTable) / sizeof(kTable[0])) return L"?";
    return g_lang == Lang::Zh ? kTable[i].zh : kTable[i].en;
}

}  // namespace i18n