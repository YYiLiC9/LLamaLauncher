// gguf.h - reads just enough of a GGUF file to size the KV cache.
//
// llama.cpp stopped printing buffer sizes in its startup log, and the KV cache
// cannot be measured from outside the process (it lives inside one allocation
// together with the compute buffers). What we *can* do is read the model's own
// metadata: the GGUF header lists the layer count, the attention head counts
// and the head dimension, which is everything the KV size formula needs.
//
// Only the header and the metadata table are read - never the tensor table and
// never any tensor data - so opening a 20 GB model costs one 1 MB read.
#pragma once

#include <cstdint>
#include <string>

namespace gguf {

struct Meta {
    bool valid = false;         // header parsed and a layer count was found
    std::wstring arch;          // "qwen2", "llama", ... (metadata key prefix)
    uint32_t layers = 0;        // <arch>.block_count
    uint32_t heads = 0;         // <arch>.attention.head_count
    uint32_t headsKv = 0;       // <arch>.attention.head_count_kv (GQA/MQA)
    uint32_t headDim = 0;       // <arch>.attention.key_length, or embd / heads
    uint32_t embd = 0;          // <arch>.embedding_length
};

// Parses the metadata table of `path`. Returns Meta{} (valid=false) when the
// file is not a GGUF or cannot be read.
Meta readMeta(const std::wstring& path);

// Bytes one KV element occupies for a --cache-type-k / --cache-type-v value.
// Quantised cache types carry a block scale, so the size is not an integer;
// an empty or unknown value falls back to f16, which is llama.cpp's default.
double cacheTypeBytes(const std::wstring& type);

// Head dimension actually used for the KV cache (n_embd_head_kv).
uint32_t kvHeadDim(const Meta& m);

// Total KV cache size in bytes: K and V for every layer and every context slot.
// Returns 0 when the metadata or the context size is unknown.
uint64_t cacheBytes(const Meta& m, uint64_t context, const std::wstring& typeK,
                    const std::wstring& typeV);

}  // namespace gguf
