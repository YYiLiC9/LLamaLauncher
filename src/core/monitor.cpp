#include "core/monitor.h"

#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>

#include <algorithm>

#include "core/util.h"

namespace monitor {

namespace {

PDH_HQUERY asQuery(void* p) { return (PDH_HQUERY)p; }

// A freshly added PDH counter needs two samples before it yields a value, and
// wildcard GPU counters only start working once the adapter has real activity,
// so the sampler is polled once per UI tick and is allowed to fail quietly.
bool addCounter(PDH_HQUERY query, const wchar_t* path, void** out) {
    PDH_HCOUNTER counter = nullptr;
    if (::PdhAddEnglishCounterW(query, path, 0, &counter) != ERROR_SUCCESS) return false;
    *out = counter;
    return true;
}

}  // namespace

Monitor::Monitor() {
    initPdh();
    initDxgi();
}

Monitor::~Monitor() {
    shutdownPdh();
    shutdownDxgi();
}

// --------------------------------------------------------------------- PDH ---
void Monitor::initPdh() {
    PDH_HQUERY query = nullptr;
    if (::PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return;
    query_ = query;

    if (!addCounter(query, L"\\Processor Information(_Total)\\% Processor Utility", &cpuCounter_)) {
        // Older builds do not expose "% Processor Utility".
        addCounter(query, L"\\Processor(_Total)\\% Processor Time", &cpuCounter_);
    }
    addCounter(query, L"\\Memory\\% Committed Bytes In Use", &memCounter_);
    addCounter(query, L"\\GPU Engine(*)\\Utilization Percentage", &gpuEngineCounter_);
    addCounter(query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", &gpuVramCounter_);

    if (::PdhCollectQueryData(query) != ERROR_SUCCESS) return;
    countersOk_ = cpuCounter_ != nullptr || memCounter_ != nullptr;
}

void Monitor::shutdownPdh() {
    if (!query_) return;
    ::PdhCloseQuery(asQuery(query_));
    query_ = nullptr;
    cpuCounter_ = nullptr;
    memCounter_ = nullptr;
    gpuEngineCounter_ = nullptr;
    gpuVramCounter_ = nullptr;
}

bool Monitor::sumCounterArray(void* counter, uint64_t& total, bool percentMode, int& instanceCount) {
    if (!counter) return false;
    PDH_HCOUNTER h = (PDH_HCOUNTER)counter;

    // First call learns the buffer size. PDH_MORE_DATA is expected here.
    DWORD bufSize = 0, itemCount = 0;
    PDH_STATUS st = ::PdhGetFormattedCounterArrayW(h, percentMode ? PDH_FMT_DOUBLE : PDH_FMT_LARGE,
                                                   &bufSize, &itemCount, nullptr);
    if (st != PDH_MORE_DATA && st != ERROR_SUCCESS) return false;
    if (bufSize == 0) return false;

    std::vector<BYTE> buffer(bufSize);
    st = ::PdhGetFormattedCounterArrayW(h, percentMode ? PDH_FMT_DOUBLE : PDH_FMT_LARGE, &bufSize,
                                        &itemCount,
                                        (PDH_FMT_COUNTERVALUE_ITEM_W*)buffer.data());
    if (st != ERROR_SUCCESS) return false;

    auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buffer.data();
    uint64_t sum = 0;
    int used = 0;
    for (DWORD i = 0; i < itemCount; ++i) {
        PDH_FMT_COUNTERVALUE& v = items[i].FmtValue;
        if (v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA) continue;

        // Only count the adapter we chose in the settings view; a machine with
        // both an iGPU and a dGPU would otherwise report a blended number.
        if (!gpu_.adapterKey.empty() && items[i].szName) {
            std::wstring name = items[i].szName;
            if (name.find(gpu_.adapterKey) == std::wstring::npos) continue;
        }

        if (percentMode) {
            double d = v.doubleValue;
            if (d < 0) d = 0;
            if (d > 100) d = 100;
            sum += (uint64_t)(d + 0.5);
            ++used;
        } else {
            if (v.largeValue < 0) continue;
            sum += (uint64_t)v.largeValue;
            ++used;
        }
    }
    if (used == 0) return false;
    total = sum;
    instanceCount = used;
    return true;
}

// -------------------------------------------------------------------- DXGI ---
void Monitor::initDxgi() {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) || !factory) return;
    dxgiFactory_ = factory;

    // Pick the discrete adapter: the one with the most dedicated video memory.
    IDXGIAdapter1* best = nullptr;
    DXGI_ADAPTER_DESC1 bestDesc{};
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        if (!adapter) break;
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.DedicatedVideoMemory > bestDesc.DedicatedVideoMemory) {
            if (best) best->Release();
            best = adapter;
            bestDesc = desc;
        } else {
            adapter->Release();
        }
    }
    if (best) {
        gpu_.name = bestDesc.Description;
        gpu_.vramTotal = bestDesc.DedicatedVideoMemory;
        // PDH names the instance luid_0xHHHHHHHH_0xLLLLLLLL while DXGI stores
        // the two halves the other way round.
        gpu_.adapterKey = util::format(L"luid_0x%08X_0x%08X", bestDesc.AdapterLuid.HighPart,
                                       bestDesc.AdapterLuid.LowPart);
        best->Release();
        dxgiOk_ = true;
    }
}

void Monitor::shutdownDxgi() {
    if (dxgiFactory_) {
        ((IDXGIFactory1*)dxgiFactory_)->Release();
        dxgiFactory_ = nullptr;
    }
}

// ------------------------------------------------------------------ sample ---
void Monitor::push(std::deque<float>& d, float v) {
    d.push_back(v);
    while (d.size() > kHistoryLength) d.pop_front();
}

void Monitor::sample(DWORD pid) {
    // ---------------------------------------------------------------- CPU ----
    if (query_ && countersOk_) {
        ::PdhCollectQueryData(asQuery(query_));

        PDH_FMT_COUNTERVALUE value{};
        if (cpuCounter_ &&
            ::PdhGetFormattedCounterValue((PDH_HCOUNTER)cpuCounter_, PDH_FMT_DOUBLE, nullptr,
                                          &value) == ERROR_SUCCESS &&
            value.CStatus == PDH_CSTATUS_VALID_DATA) {
            double pct = value.doubleValue;
            cpuPercent_ = (int)(std::clamp(pct, 0.0, 100.0) + 0.5);
        }
        if (memCounter_ &&
            ::PdhGetFormattedCounterValue((PDH_HCOUNTER)memCounter_, PDH_FMT_DOUBLE, nullptr,
                                          &value) == ERROR_SUCCESS &&
            value.CStatus == PDH_CSTATUS_VALID_DATA) {
            memory_.percent = (uint32_t)(std::clamp(value.doubleValue, 0.0, 100.0) + 0.5);
            memory_.valid = true;
        }
    }

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (::GlobalMemoryStatusEx(&ms)) {
        memory_.total = ms.ullTotalPhys;
        memory_.used = ms.ullTotalPhys - ms.ullAvailPhys;
        memory_.percent = ms.dwMemoryLoad;
        memory_.valid = true;
    }

    // ------------------------------------------------------- llama-server ----
    processWorkingSet_ = 0;
    processCpuPercent_ = 0;
    processVramBytes_ = 0;
    processVramValid_ = false;

    if (pid) {
        HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (h) {
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            pmc.cb = sizeof(pmc);
            if (::GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
                processWorkingSet_ = pmc.WorkingSetSize;
                processVramBytes_ = pmc.PrivateUsage;
                processVramValid_ = true;
            }
            FILETIME creation{}, exit{}, kernel{}, user{};
            if (::GetProcessTimes(h, &creation, &exit, &kernel, &user)) {
                ULARGE_INTEGER k{}, u{};
                k.LowPart = kernel.dwLowDateTime;
                k.HighPart = kernel.dwHighDateTime;
                u.LowPart = user.dwLowDateTime;
                u.HighPart = user.dwHighDateTime;
                uint64_t total = k.QuadPart + u.QuadPart;
                uint64_t now = ::GetTickCount64();
                if (prevProcTime_ && now > prevProcSample_) {
                    double elapsedMs = (double)(now - prevProcSample_);
                    double cpuMs = (double)(total - prevProcTime_) / 10000.0;
                    double pct = elapsedMs > 0 ? (cpuMs / elapsedMs) * 100.0 : 0.0;
                    processCpuPercent_ = (int)(std::max(0.0, pct) + 0.5);
                }
                prevProcTime_ = total;
                prevProcSample_ = now;
            }
            ::CloseHandle(h);
        }
    } else {
        prevProcTime_ = 0;
    }

    // ----------------------------------------------------------------- GPU ---
    gpuPercent_ = 0;
    {
        // Wildcard counters resolve their instances on the first successful
        // collect, so a reading may legitimately fail for the first few ticks.
        // Nothing special is needed: keep asking, it starts working on its own.
        uint64_t sum = 0;
        int instances = 0;
        if (sumCounterArray(gpuEngineCounter_, sum, true, instances)) {
            gpuPercent_ = (int)std::min<uint64_t>(sum, 100);
            gpu_.hasEngineCounter = true;
        }
        uint64_t vram = 0;
        if (sumCounterArray(gpuVramCounter_, vram, false, instances)) {
            gpu_.vramUsed = vram;
            gpu_.vramValid = true;
            uint64_t budget = gpu_.vramTotal ? gpu_.vramTotal : 1;
            double pct = (double)vram * 100.0 / (double)budget;
            gpu_.vramPercent = (uint32_t)(std::clamp(pct, 0.0, 100.0) + 0.5);
        }
    }

    // ---------------------------------------------------------------- charts --
    push(cpuHistory_, (float)cpuPercent_);
    push(gpuHistory_, (float)gpuPercent_);
    push(memHistory_, (float)memory_.percent);
}

}  // namespace monitor