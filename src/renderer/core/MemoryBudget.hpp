#pragma once

#include <cstdint>
#include <string_view>

namespace dashcore::renderer::core {

struct ResourceLimitPair {
    std::uint64_t softBytes = 0;
    std::uint64_t hardBytes = 0;
    bool available = false;
    bool softUnlimited = false;
    bool hardUnlimited = false;
};

struct ProcessResourceLimits {
    ResourceLimitPair addressSpace{};
    ResourceLimitPair residentSet{};
};

struct ProcessMemorySnapshot {
    std::uint64_t systemTotalBytes = 0;
    std::uint64_t systemAvailableBytes = 0;
    std::uint64_t processRssBytes = 0;
    std::uint64_t processPssBytes = 0;
    std::uint64_t processPeakRssBytes = 0;
    std::uint64_t processVirtualBytes = 0;
    std::uint64_t cgroupMemoryLimitBytes = 0;
    std::uint64_t cgroupMemoryCurrentBytes = 0;
    bool hasSystemMemory = false;
    bool hasProcessRss = false;
    bool hasProcessPss = false;
    bool hasProcessPeakRss = false;
    bool hasProcessVirtual = false;
    bool hasCgroupMemoryLimit = false;
    bool cgroupMemoryLimitUnlimited = false;
    bool hasCgroupMemoryCurrent = false;
    ProcessResourceLimits resourceLimits{};
};

struct RendererMemoryBudget {
    std::uint64_t systemSafetyReserveBytes = 0;
    std::uint64_t availableHeadroomBytes = 0;
    std::uint64_t budgetBytes = 0;
    std::uint64_t maxSingleBufferBytes = 0;
    bool canAllocateBuffers = false;
};

ProcessMemorySnapshot parseProcessMemorySnapshot(
    std::string_view memInfo,
    std::string_view processStatus,
    std::string_view smapsRollup,
    std::string_view cgroupV2Limit = {},
    std::string_view cgroupV2Current = {},
    std::string_view cgroupV1Limit = {},
    std::string_view cgroupV1Usage = {}
) noexcept;

ProcessResourceLimits queryProcessResourceLimits() noexcept;
ProcessMemorySnapshot queryProcessMemorySnapshot() noexcept;

RendererMemoryBudget calculateRendererMemoryBudget(
    ProcessMemorySnapshot const& snapshot,
    std::uint64_t maximumBudgetBytes = 64ull * 1024ull * 1024ull
) noexcept;

} // namespace dashcore::renderer::core
