#pragma once
#include <cstdint>
#include <string_view>
namespace dashcore::renderer::core {
struct ProcessMemorySnapshot {
    std::uint64_t systemTotalBytes = 0, systemAvailableBytes = 0;
    std::uint64_t processRssBytes = 0, processPssBytes = 0, processPeakRssBytes = 0;
    bool hasSystemMemory = false, hasProcessRss = false, hasProcessPss = false, hasProcessPeakRss = false;
};
struct RendererMemoryBudget {
    std::uint64_t systemSafetyReserveBytes = 0, availableHeadroomBytes = 0;
    std::uint64_t budgetBytes = 0, maxSingleBufferBytes = 0;
    bool canAllocateBuffers = false;
};
ProcessMemorySnapshot parseProcessMemorySnapshot(
    std::string_view memInfo, std::string_view processStatus, std::string_view smapsRollup
) noexcept;
ProcessMemorySnapshot queryProcessMemorySnapshot() noexcept;
RendererMemoryBudget calculateRendererMemoryBudget(
    ProcessMemorySnapshot const& snapshot,
    std::uint64_t maximumBudgetBytes = 64ull * 1024ull * 1024ull
) noexcept;
} // namespace dashcore::renderer::core
