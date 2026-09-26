#include "core/catalog.h"

#include "core/util.h"

namespace catalog {

// ---------------------------------------------------------------------------
// The catalogue. Values here were chosen to be sensible starting points rather
// than llama.cpp's own defaults, because the whole point of the launcher is to
// avoid re-typing a long command line. Every value stays editable.
// ---------------------------------------------------------------------------
static const std::vector<Spec>& buildTable() {
    static const std::vector<Spec> table = {
        // ---------------------------------------------------------- model & context
        {L"-m", L"--model", nullptr, Group::Basic, false, false, true, false, L"",
         L"D:\\models\\model.gguf",
         L"Path to the GGUF model file. Required.",
         L"GGUF 模型文件路径，必填。"},

        {L"-c", L"--ctx-size", nullptr, Group::Basic, false, true, false, false, L"8192",
         L"8192",
         L"Context window in tokens. Larger uses more VRAM.",
         L"上下文长度（token）。越大占用显存越多。"},

        {L"-n", L"--predict", nullptr, Group::Basic, false, true, false, false, L"-1", L"-1",
         L"Tokens to predict; -1 means until the model stops.",
         L"生成的最大 token 数；-1 表示直到模型自行结束。"},

        {L"--keep", nullptr, nullptr, Group::Basic, false, true, false, false, L"", L"0",
         L"Tokens kept from the initial prompt when context shifts.",
         L"上下文滚动时保留的初始提示 token 数。"},

        {L"-np", L"--parallel", nullptr, Group::Server, false, true, false, false, L"", L"1",
         L"Number of parallel request slots. Each slot needs its own context.",
         L"并行请求槽位数。每个槽位各占一份上下文。"},

        // ------------------------------------------------------------------ sampling
        {L"--temp", L"--temperature", nullptr, Group::Sampling, false, true, false, false, L"0.8",
         L"0.8",
         L"Sampling temperature. Lower is more deterministic.",
         L"采样温度。越低越确定。"},

        {L"--top-k", nullptr, nullptr, Group::Sampling, false, true, false, false, L"40", L"40",
         L"Keep only the K most likely tokens.",
         L"仅保留概率最高的 K 个 token。"},

        {L"--top-p", nullptr, nullptr, Group::Sampling, false, true, false, false, L"0.95", L"0.95",
         L"Nucleus sampling threshold.",
         L"核采样阈值。"},

        {L"--min-p", nullptr, nullptr, Group::Sampling, false, true, false, false, L"0.05", L"0.05",
         L"Drop tokens below this probability relative to the best one.",
         L"丢弃概率低于最优 token 该比例的候选。"},

        {L"--repeat-penalty", nullptr, nullptr, Group::Sampling, false, true, false, false, L"1.1",
         L"1.1",
         L"Penalty for repeated tokens; 1.0 disables it.",
         L"重复惩罚；1.0 表示关闭。"},

        {L"-s", L"--seed", nullptr, Group::Sampling, false, true, false, false, L"-1", L"-1",
         L"RNG seed; -1 picks a random one each run.",
         L"随机种子；-1 表示每次随机。"},

        // -------------------------------------------------------------------- server
        {L"--host", nullptr, nullptr, Group::Server, false, false, false, false, L"127.0.0.1",
         L"127.0.0.1",
         L"Interface the HTTP server binds to.",
         L"HTTP 服务绑定的地址。"},

        {L"--port", nullptr, nullptr, Group::Server, false, true, false, false, L"8080", L"8080",
         L"HTTP port. The chat page is served here.",
         L"HTTP 端口，对话页面由此提供。"},

        {L"-a", L"--alias", nullptr, Group::Server, false, false, false, false, L"", L"my-model",
         L"Name reported by the server to API clients.",
         L"服务对外暴露的模型名称。"},

        {L"-cb", L"--cont-batching", nullptr, Group::Server, true, false, false, false, L"on", L"",
         L"Continuous batching for better throughput with parallel slots.",
         L"连续批处理，多槽位时吞吐更高。"},

        {L"--api-key", nullptr, nullptr, Group::Server, false, false, false, false, L"", L"secret",
         L"Require this key in the Authorization header.",
         L"要求请求头携带此密钥。"},

        {L"--timeout", nullptr, nullptr, Group::Server, false, true, false, false, L"600", L"600",
         L"Idle timeout in seconds for a slot.",
         L"单个槽位的空闲超时（秒）。"},

        // --------------------------------------------------------------- performance
        {L"-t", L"--threads", nullptr, Group::Performance, false, true, false, false, L"6", L"6",
         L"CPU threads for generation. Use your physical core count.",
         L"生成阶段的 CPU 线程数，建议取物理核心数。"},

        {L"-tb", L"--threads-batch", nullptr, Group::Performance, false, true, false, false, L"",
         L"6",
         L"CPU threads for prompt processing.",
         L"提示词处理阶段的 CPU 线程数。"},

        {L"-b", L"--batch-size", nullptr, Group::Performance, false, true, false, false, L"2048",
         L"2048",
         L"Logical batch size for prompt processing.",
         L"提示词处理的逻辑批大小。"},

        {L"-ub", L"--ubatch-size", nullptr, Group::Performance, false, true, false, false, L"512",
         L"512",
         L"Physical batch size.", L"物理批大小。"},

        // Not a pure switch: llama.cpp's --flash-attn takes a value
        // (on|off|auto), and emitting it bare made the next flag on the line
        // (" -ngl") its value - "unknown value for --flash-attn: '-ngl'".
        {L"-fa", L"--flash-attn", nullptr, Group::Performance, false, false, false, false, L"on",
         L"on|off|auto",
         L"Flash attention. Faster and lighter on memory.",
         L"Flash Attention，速度更快且更省内存。"},

        // --mlock / --no-mmap are gone from llama.cpp; their replacement is a
        // single tri-state-plus switch covering the whole loading strategy.
        {L"-lm", L"--load-mode", nullptr, Group::Performance, false, false, false, false, L"",
         L"auto|mmap|mlock|mmap+mlock|dio|none",
         L"Model loading mode (replaces the old --mlock / --no-mmap).",
         L"模型加载模式（替代旧的 --mlock / --no-mmap）。"},

        // ------------------------------------------------------------------ GPU
        {L"-ngl", L"--n-gpu-layers", nullptr, Group::Gpu, false, true, false, false, L"99", L"99",
         L"Layers offloaded to the GPU. 99 means all of them.",
         L"卸载到 GPU 的层数，99 表示全部。"},

        {L"-sm", L"--split-mode", nullptr, Group::Gpu, false, false, false, false, L"layer",
         L"none|layer|row",
         L"How to split the model across multiple GPUs.",
         L"多卡时的模型切分方式。"},

        {L"-ts", L"--tensor-split", nullptr, Group::Gpu, false, false, false, false, L"", L"3,1",
         L"Proportion of the model per GPU, comma separated.",
         L"各 GPU 的模型分配比例，逗号分隔。"},

        // KV cache quantization - the q8_0 / q4_0 knobs. Quantizing the cache
        // trades a little quality for a lot of context memory, which matters
        // on 8-16GB cards running long contexts.
        {L"-ctk", L"--cache-type-k", nullptr, Group::Gpu, false, false, false, false, L"",
         L"f16|q8_0|q4_0",
         L"KV cache K quantization type (f16, q8_0, q4_0, ...).",
         L"KV 缓存 K 量化类型（f16、q8_0、q4_0 等）。"},
        {L"-ctv", L"--cache-type-v", nullptr, Group::Gpu, false, false, false, false, L"",
         L"f16|q8_0|q4_0",
         L"KV cache V quantization type (f16, q8_0, q4_0, ...).",
         L"KV 缓存 V 量化类型（f16、q8_0、q4_0 等）。"},

        {L"-mg", L"--main-gpu", nullptr, Group::Gpu, false, true, false, false, L"", L"0",
         L"Index of the primary GPU.", L"主 GPU 的编号。"},

        // ------------------------------------------------------------------ chat
        {L"--chat-template", nullptr, nullptr, Group::Chat, false, false, false, false, L"",
         L"chatml", L"Built-in chat template to use.",
         L"使用内置的对话模板名称。"},

        {L"--jinja", nullptr, nullptr, Group::Chat, true, false, false, false, L"on", L"",
         L"Honour the chat template embedded in the GGUF metadata.",
         L"使用模型内置的对话模板。"},

        {L"--chat-template-file", L"-ctf", nullptr, Group::Chat, false, false, true, false, L"",
         L"template.jinja", L"Load a custom chat template from a file.",
         L"从文件加载自定义对话模板。"},

        {L"--reasoning-format", nullptr, nullptr, Group::Chat, false, false, false, false, L"",
         L"none|auto|deepseek", L"How reasoning output is separated from the answer.",
         L"推理内容与回答的分离方式。"},
    };
    return table;
}

const std::vector<Spec>& specs() {
    static const std::vector<Spec> table = buildTable();
    return table;
}

const Spec* find(const std::wstring& flag) {
    std::wstring want = util::lower(util::trim(flag));
    if (want.empty()) return nullptr;
    for (const Spec& s : specs()) {
        if (util::lower(s.flag) == want) return &s;
        if (s.shortName && util::iequals(s.shortName, want)) return &s;
        if (s.aliases) {
            // `aliases` may hold several comma separated forms.
            for (const auto& alt : util::split(util::lower(s.aliases), L','))
                if (util::trim(alt) == want) return &s;
        }
    }
    return nullptr;
}

std::wstring label(const Spec& spec) {
    std::wstring out = spec.flag;
    if (spec.aliases && *spec.aliases) out += util::format(L"  (%s)", spec.aliases);
    return out;
}

const wchar_t* groupName(Group g) {
    switch (g) {
        case Group::Basic:       return T(Str::GroupBasic);
        case Group::Sampling:    return T(Str::GroupSampling);
        case Group::Server:      return T(Str::GroupServer);
        case Group::Performance: return T(Str::GroupPerformance);
        case Group::Gpu:         return T(Str::GroupGpu);
        case Group::Chat:        return T(Str::GroupChat);
        default:                 return T(Str::GroupCustom);
    }
}

const wchar_t* groupKey(Group g) {
    switch (g) {
        case Group::Basic:       return L"basic";
        case Group::Sampling:    return L"sampling";
        case Group::Server:      return L"server";
        case Group::Performance: return L"performance";
        case Group::Gpu:         return L"gpu";
        case Group::Chat:        return L"chat";
        default:                 return L"custom";
    }
}

bool groupFromKey(const std::wstring& key, Group& out) {
    for (int i = 0; i < (int)Group::Count; ++i) {
        if (util::iequals(key, groupKey((Group)i))) {
            out = (Group)i;
            return true;
        }
    }
    return false;
}

std::vector<ParamValue> defaultParams() {
    std::vector<ParamValue> out;
    out.reserve(specs().size());
    for (const Spec& s : specs()) {
        ParamValue pv;
        pv.flag = s.flag;
        pv.value = s.defaultVal ? s.defaultVal : L"";
        pv.group = groupKey(s.group);
        pv.custom = false;
        out.push_back(std::move(pv));
    }
    return out;
}

bool toggleOn(const std::wstring& value) {
    std::wstring v = util::lower(util::trim(value));
    return v == L"on" || v == L"true" || v == L"yes" || v == L"1";
}

}  // namespace catalog