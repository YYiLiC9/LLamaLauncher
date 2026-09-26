// monitor.h - lightweight CPU / GPU / memory sampling for the running view.
//
// Everything here uses APIs that ship with Windows, so there is no dependency
// to fetch:
//   * CPU, whole-machine memory, GPU engine utilisation and dedicated VRAM
//     usage -> PDH performance counters
//   * adapter name and total VRAM -> DXGI
//   * the server's own CPU time and working set -> GetProcessTimes / PSAPI
#pragma once

#include <windows.h>

#include <deque>
#include <map>
#include <string>
#include <vector>

namespace monitor {

struct MemoryInfo {
    uint64_t total = 0;
    uint64_t used = 0;
    uint32_t percent = 0;
    bool valid = false;
};

struct GpuInfo {
    std::wstring name;
    uint64_t vramTotal = 0;        // dedicated video memory reported by DXGI
    uint64_t vramUsed = 0;         // system-wide dedicated usage from PDH
    uint32_t vramPercent = 0;
    bool vramValid = false;
    bool hasEngineCounter = false;
    std::wstring adapterKey;       // "luid_0x00000000_0x0000XXXX"
};

class Monitor {
public:
    Monitor();
    ~Monitor();

    // Samples everything. `pid` is the llama-server process (0 to skip).
    void sample(DWORD pid);

    int cpuPercent() const { return cpuPercent_; }
    int gpuPercent() const { return gpuPercent_; }
    const MemoryInfo& memory() const { return memory_; }
    const GpuInfo& gpu() const { return gpu_; }
    bool hasGpu() const { return !gpu_.name.empty(); }

    // llama-server's own footprint, which is the number that actually matters.
    uint64_t processWorkingSet() const { return processWorkingSet_; }
    int processCpuPercent() const { return processCpuPercent_; }
    uint64_t processVramBytes() const { return processVramBytes_; }
    bool processVramValid() const { return processVramValid_; }
    // Private commit of the server process (KV cache + activations + runtime);
    // with mmap'd weights this excludes the model file pages.
    uint64_t processPrivateCommit() const { return processVramBytes_; }
    // Bytes of committed file-mapped regions backed by .gguf files (the model
    // weights, lazily faulted in under the default mmap load mode). Rescanned
    // every ~5th sample, not every tick.
    uint64_t mappedModelBytes() const { return mappedModelBytes_; }

    // Rolling history for the charts, newest last.
    const std::deque<float>& cpuHistory() const { return cpuHistory_; }
    const std::deque<float>& gpuHistory() const { return gpuHistory_; }
    const std::deque<float>& memHistory() const { return memHistory_; }

    static constexpr size_t kHistoryLength = 90;

private:
    void initPdh();
    void shutdownPdh();
    void initDxgi();
    void shutdownDxgi();
    void freeGpuCounters();
    bool addGpuCounters();
    // Sums every wildcard instance whose name matches the target adapter.
    bool sumCounterArray(void* counter, uint64_t& total, bool percentMode, int& instanceCount,
                         const std::wstring& mustContain = std::wstring());

    void push(std::deque<float>& d, float v);

    // ---- PDH ----
    void* query_ = nullptr;
    void* cpuCounter_ = nullptr;
    void* memCounter_ = nullptr;
    void* gpuEngineCounter_ = nullptr;     // \GPU Engine(*)\Utilization Percentage
    void* gpuVramCounter_ = nullptr;       // \GPU Adapter Memory(*)\Dedicated Usage
    bool countersOk_ = false;

    // ---- process sampling ----
    uint64_t prevProcTime_ = 0;
    uint64_t prevProcSample_ = 0;

    // ---- DXGI ----
    void* dxgiFactory_ = nullptr;
    bool dxgiOk_ = false;

    // ---- values ----
    int cpuPercent_ = 0;
    int gpuPercent_ = 0;
    MemoryInfo memory_;
    GpuInfo gpu_;
    uint64_t processWorkingSet_ = 0;
    int processCpuPercent_ = 0;
    uint64_t processVramBytes_ = 0;
    bool processVramValid_ = false;
    uint64_t mappedModelBytes_ = 0;
    int mappedWalkTick_ = 0;

    std::deque<float> cpuHistory_;
    std::deque<float> gpuHistory_;
    std::deque<float> memHistory_;
};

}  // namespace monitor