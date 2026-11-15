#include "renderer/core/MemoryBudget.hpp"
#include <cassert>
#include <cstdint>
#include <string_view>
using namespace dashcore::renderer::core;
int main() {
    constexpr std::uint64_t KiB = 1024ull, MiB = 1024ull * KiB, GiB = 1024ull * MiB;
    constexpr std::string_view memInfo =
        "MemTotal:        5833728 kB\nMemFree: 112000 kB\nMemAvailable: 827392 kB\n";
    constexpr std::string_view status =
        "Name:\tGeometryDash\nVmHWM:\t520192 kB\nVmRSS:\t501760 kB\n";
    constexpr std::string_view smaps = "Rss: 501760 kB\nPss: 492544 kB\n";
    auto const sample = parseProcessMemorySnapshot(memInfo, status, smaps);
    assert(sample.hasSystemMemory);
    assert(sample.systemTotalBytes == 5833728ull * KiB);
    assert(sample.systemAvailableBytes == 827392ull * KiB);
    assert(sample.hasProcessRss && sample.processRssBytes == 501760ull * KiB);
    assert(sample.hasProcessPeakRss && sample.processPeakRssBytes == 520192ull * KiB);
    assert(sample.hasProcessPss && sample.processPssBytes == 492544ull * KiB);
    auto const budget = calculateRendererMemoryBudget(sample);
    assert(budget.systemSafetyReserveBytes >= 256ull * MiB);
    assert(budget.availableHeadroomBytes == sample.systemAvailableBytes - budget.systemSafetyReserveBytes);
    assert(budget.budgetBytes <= 64ull * MiB);
    assert(budget.canAllocateBuffers);
    assert(budget.maxSingleBufferBytes > 0 && budget.maxSingleBufferBytes <= 16ull * MiB);
    assert(budget.maxSingleBufferBytes < budget.budgetBytes);
    auto pressured = sample;
    pressured.systemAvailableBytes = 180ull * MiB;
    auto const noBudget = calculateRendererMemoryBudget(pressured);
    assert(noBudget.budgetBytes == 0);
    assert(!noBudget.canAllocateBuffers && noBudget.maxSingleBufferBytes == 0);
    ProcessMemorySnapshot incomplete{};
    incomplete.systemTotalBytes = 4ull * GiB;
    incomplete.systemAvailableBytes = 2ull * GiB;
    auto const failClosed = calculateRendererMemoryBudget(incomplete);
    assert(failClosed.budgetBytes == 0 && !failClosed.canAllocateBuffers);
    auto const noAvailable = parseProcessMemorySnapshot(
        "MemTotal: 4096000 kB\nMemFree: 500000 kB\n",
        "VmRSS: 100000 kB\n", "Pss: 90000 kB\n"
    );
    assert(!noAvailable.hasSystemMemory);
    assert(noAvailable.hasProcessRss && noAvailable.hasProcessPss);
    assert(calculateRendererMemoryBudget(noAvailable).budgetBytes == 0);
    auto const invalidAvailable = parseProcessMemorySnapshot(
        "MemTotal: 1024 kB\nMemAvailable: 2048 kB\n", "", ""
    );
    assert(!invalidAvailable.hasSystemMemory);
    assert(calculateRendererMemoryBudget(sample, 12ull * MiB).budgetBytes <= 12ull * MiB);
    auto const zeroCap = calculateRendererMemoryBudget(sample, 0);
    assert(zeroCap.budgetBytes == 0 && !zeroCap.canAllocateBuffers);
    return 0;
}
