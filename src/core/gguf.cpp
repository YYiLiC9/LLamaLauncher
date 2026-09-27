// gguf.cpp - see gguf.h. Reads the GGUF header + metadata table only.
#include "core/gguf.h"

#include <windows.h>

#include <cstring>
#include <vector>

#include "core/util.h"

namespace gguf {
namespace {

// gguf_metadata_value_type from ggml.
enum : uint32_t {
    kUint8 = 0,
    kInt8 = 1,
    kUint16 = 2,
    kInt16 = 3,
    kUint32 = 4,
    kInt32 = 5,
    kFloat32 = 6,
    kBool = 7,
    kString = 8,
    kArray = 9,
    kUint64 = 10,
    kInt64 = 11,
    kFloat64 = 12
};

// Enough for any metadata table seen in the wild. The tokenizer vocabulary is
// the only entry that can grow past a megabyte and it sits at the end of the
// table, so even when it overflows the buffer the fields we need are already
// parsed and readMeta still succeeds.
constexpr DWORD kHeaderBytes = 8 * 1024 * 1024;

// Size of a fixed-width metadata value; 0 for the variable-length ones.
size_t fixedSize(uint32_t type) {
    switch (type) {
        case kUint8:
        case kInt8:
        case kBool:
            return 1;
        case kUint16:
        case kInt16:
            return 2;
        case kUint32:
        case kInt32:
        case kFloat32:
            return 4;
        case kUint64:
        case kInt64:
        case kFloat64:
            return 8;
        default:
            return 0;
    }
}

class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& buf) : buf_(buf) {}

    bool left(uint64_t n) const {
        return n <= (uint64_t)buf_.size() && pos_ + n <= (uint64_t)buf_.size();
    }
    bool skip(uint64_t n) {
        if (!left(n)) return false;
        pos_ += (size_t)n;
        return true;
    }
    bool u32(uint32_t& out) {
        if (!left(4)) return false;
        out = (uint32_t)buf_[pos_] | ((uint32_t)buf_[pos_ + 1] << 8) |
              ((uint32_t)buf_[pos_ + 2] << 16) | ((uint32_t)buf_[pos_ + 3] << 24);
        pos_ += 4;
        return true;
    }
    bool u64(uint64_t& out) {
        if (!left(8)) return false;
        out = 0;
        for (int i = 0; i < 8; ++i) out |= (uint64_t)buf_[pos_ + i] << (8 * i);
        pos_ += 8;
        return true;
    }
    // GGUF string: uint64 length followed by that many bytes (not terminated).
    bool str(std::string& out) {
        uint64_t len = 0;
        if (!u64(len)) return false;
        if (!left(len)) return false;
        out.assign((const char*)&buf_[pos_], (size_t)len);
        pos_ += (size_t)len;
        return true;
    }
    // Consumes one value of `type`. Scalars land in `num`/`frac`/`text`; arrays
    // are walked element by element, the only way to skip them correctly.
    bool value(uint32_t type, uint64_t& num, double& frac, std::string& text) {
        if (type == kString) return str(text);
        if (type == kArray) {
            uint32_t sub = 0;
            uint64_t count = 0;
            if (!u32(sub) || !u64(count)) return false;
            if (count > (uint64_t)buf_.size()) return false;  // corrupt: bail out
            if (size_t fixed = fixedSize(sub)) return skip(count * (uint64_t)fixed);
            uint64_t ignoreNum = 0;
            double ignoreFrac = 0;
            std::string ignoreText;
            for (uint64_t i = 0; i < count; ++i) {
                if (!value(sub, ignoreNum, ignoreFrac, ignoreText)) return false;
            }
            return true;
        }
        size_t fixed = fixedSize(type);
        if (!fixed) return false;  // unknown type: its size is unknowable
        if (!left((uint64_t)fixed)) return false;
        uint64_t raw = 0;
        for (size_t i = 0; i < fixed; ++i) raw |= (uint64_t)buf_[pos_ + i] << (8 * i);
        pos_ += fixed;
        num = raw;
        if (type == kFloat32) {
            float f = 0;
            std::memcpy(&f, &raw, sizeof(f));
            frac = (double)f;
        } else if (type == kFloat64) {
            std::memcpy(&frac, &raw, sizeof(frac));
        }
        return true;
    }

private:
    const std::vector<uint8_t>& buf_;
    size_t pos_ = 0;
};

std::vector<uint8_t> readHeader(const std::wstring& path) {
    std::vector<uint8_t> out;
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return out;
    out.resize(kHeaderBytes);
    DWORD got = 0;
    if (!::ReadFile(f, out.data(), kHeaderBytes, &got, nullptr) || got == 0) {
        out.clear();
        ::CloseHandle(f);
        return out;
    }
    out.resize(got);
    ::CloseHandle(f);
    return out;
}

}  // namespace

Meta readMeta(const std::wstring& path) {
    Meta m;
    std::vector<uint8_t> buf = readHeader(path);
    if (buf.size() < 24 || std::memcmp(buf.data(), "GGUF", 4) != 0) return m;

    Reader r(buf);
    if (!r.skip(4)) return m;  // magic
    uint32_t version = 0;
    uint64_t tensorCount = 0;
    uint64_t kvCount = 0;
    if (!r.u32(version) || !r.u64(tensorCount) || !r.u64(kvCount)) return m;
    (void)version;
    (void)tensorCount;

    std::wstring arch;
    bool blockFound = false;
    for (uint64_t i = 0; i < kvCount; ++i) {
        std::string key;
        uint32_t type = 0;
        if (!r.str(key) || !r.u32(type)) break;
        uint64_t num = 0;
        double frac = 0;
        std::string text;
        if (!r.value(type, num, frac, text)) break;

        std::wstring k = util::toUtf16(key);
        if (k == L"general.architecture") {
            arch = util::toUtf16(text);
        } else if (util::endsWith(k, L".block_count")) {
            m.layers = (uint32_t)num;
            blockFound = true;
        } else if (util::endsWith(k, L".attention.head_count_kv")) {
            m.headsKv = (uint32_t)num;
        } else if (util::endsWith(k, L".attention.head_count")) {
            m.heads = (uint32_t)num;
        } else if (util::endsWith(k, L".attention.key_length")) {
            m.headDim = (uint32_t)num;
        } else if (util::endsWith(k, L".embedding_length")) {
            m.embd = (uint32_t)num;
        }
        // Tokenizer tables, rope config and the rest are skipped.
    }

    m.arch = arch;
    m.valid = blockFound && m.layers > 0 && (m.heads > 0 || m.headsKv > 0);
    return m;
}

double cacheTypeBytes(const std::wstring& type) {
    std::wstring t = util::lower(util::trim(type));
    // llama.cpp defaults the cache to f16. Quantised cache types carry a block
    // scale, so their per-element cost is fractional: the ratios below are the
    // ggml block size (scales included) over the 32 elements it covers.
    if (t.empty() || t == L"f16" || t == L"auto") return 2.0;
    if (t == L"f32") return 4.0;
    if (t == L"bf16") return 2.0;
    if (t == L"q8_0") return 34.0 / 32.0;  // 2-byte scale + 32 x int8
    if (t == L"q5_1") return 24.0 / 32.0;  // d + m + high bits + 5-bit data
    if (t == L"q5_0") return 22.0 / 32.0;
    if (t == L"q4_1") return 20.0 / 32.0;
    if (t == L"q4_0" || t == L"iq4_nl") return 18.0 / 32.0;
    return 2.0;
}

uint32_t kvHeadDim(const Meta& m) {
    if (m.headDim) return m.headDim;
    if (m.heads && m.embd) return m.embd / m.heads;
    return 0;
}

uint64_t cacheBytes(const Meta& m, uint64_t context, const std::wstring& typeK,
                    const std::wstring& typeV) {
    if (!m.valid || context == 0) return 0;
    uint32_t headDim = kvHeadDim(m);
    uint32_t headsKv = m.headsKv ? m.headsKv : m.heads;
    if (!headDim || !headsKv) return 0;
    // K and V for every layer and every context slot:
    //   n_layer * n_ctx * n_embd_kv_gqa * (bytes_k + bytes_v)
    double perToken = (double)headsKv * (double)headDim *
                      (cacheTypeBytes(typeK) + cacheTypeBytes(typeV));
    return (uint64_t)((double)m.layers * (double)context * perToken);
}

}  // namespace gguf
