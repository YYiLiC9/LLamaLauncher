// tools/gguf_probe.cpp - prints a GGUF file's metadata and the KV cache size
// derived from it. Used to verify core/gguf.cpp against real models.
//
//   gguf_probe.exe <model.gguf> [ctx] [cache-type-k] [cache-type-v]
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/gguf.h"
#include "core/util.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: gguf_probe.exe <model.gguf> [ctx] [cache-type-k] [cache-type-v]\n");
        return 2;
    }
    std::wstring path = argv[1];
    uint64_t ctx = argc > 2 ? ::_wcstoui64(argv[2], nullptr, 10) : 8192;
    std::wstring typeK = argc > 3 ? argv[3] : L"";
    std::wstring typeV = argc > 4 ? argv[4] : L"";
    if (ctx == 0) ctx = 8192;

    gguf::Meta m = gguf::readMeta(path);
    std::printf("file      : %ls\n", path.c_str());
    std::printf("disk bytes: %llu\n", (unsigned long long)util::modelFileBytes(path));
    std::printf("valid     : %d\n", m.valid ? 1 : 0);
    std::printf("arch      : %ls\n", m.arch.c_str());
    std::printf("layers    : %u\n", m.layers);
    std::printf("heads     : %u\n", m.heads);
    std::printf("heads_kv  : %u\n", m.headsKv);
    std::printf("head_dim  : %u\n", m.headDim);
    std::printf("embd      : %u\n", m.embd);
    std::printf("kv_dim    : %u\n", gguf::kvHeadDim(m));

    uint64_t kv = gguf::cacheBytes(m, ctx, typeK, typeV);
    std::printf("ctx       : %llu\n", (unsigned long long)ctx);
    std::printf("kv bytes  : %llu\n", (unsigned long long)kv);
    if (kv) {
        std::printf("kv        : %.2f GiB / %.2f GB\n", (double)kv / 1073741824.0,
                    (double)kv / 1000000000.0);
    }
    return m.valid ? 0 : 1;
}
