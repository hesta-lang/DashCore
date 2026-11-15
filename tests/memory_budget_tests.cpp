#include "renderer/core/MemoryBudget.hpp"

#include <cassert>
#include <cstdint>
#include <string_view>

using namespace dashcore::renderer::core;

int main() {
    constexpr std::uint64_t KiB = 1024ull;
    constexpr std::uint64_t MiB = 1024ull * KiB;
    constexpr std::uint64_t GiB = 1024ull * MiB;

    constexpr std::string_view memInfo =
        "MemTotal:        5833728 kB\nMemFree: 112000 kB\nMemAvailable: 827392 kB\n";
    constexpr std::string_view status =
        "Name:\tGeometryDash\nVmSize:\t1258291 kB\nVmHWM:\t520192 kB\nVmRSS:\t501760 kB\n";
    constexpr std::string_view smaps = "Rss: 501760 kB\nPss: 492544 kB\n";

    auto const sample = parseProcessMemorySnapshot(memInfo, status, smaps);
    assert(sample.hasSystemMemory);
    assert(sample.systemTotalBytes == 5833728ull * KiB);
    assert(sample.systemAvailableBytes == 827392ull * KiB);
    assert(sample.hasProcessRss && sample.processRssBytes == 501760ull * KiB);
    assert(sample.hasProcessPeakRss && sample.processPeakRssBytes == 520192ull * KiB);
    assert(sample.hasProcessPss && sample.processPssBytes == 492544ull * KiB);
    assert(sample.hasProcessVirtual && sample.processVirtualBytes == 1258291ull * KiB);

    auto const budget = calculateRendererMemoryBudget(sample);
    assert(budget.systemSafetyReserveBytes >= 256ull * MiB);
    assert(budget.availableHeadroomBytes == sample.systemAvailableBytes - budget.systemSafetyReserveBytes);
    assert(budget.budgetBytes <= 64ull * MiB);
    assert(budget.canAllocateBuffers);
    assert(budget.maxSingleBufferBytes > 0 && budget.maxSingleBufferBytes <= 16ull * MiB);
    assert(budget.maxSingleBufferBytes < budget.budgetBytes);

    auto const v2 = parseProcessMemorySnapshot(
        memInfo, status, smaps, "536870912\n", "268435456\n"
    );
    assert(v2.hasCgroupMemoryLimit && !v2.cgroupMemoryLimitUnlimited);
    assert(v2.cgroupMemoryLimitBytes == 512ull * MiB);
    assert(v2.hasCgroupMemoryCurrent && v2.cgroupMemoryCurrentBytes == 256ull * MiB);

    auto const v2Unlimited = parseProcessMemorySnapshot(
        memInfo, status, smaps, "max\n", "104857600\n"
    );
    assert(v2Unlimited.hasCgroupMemoryLimit && v2Unlimited.cgroupMemoryLimitUnlimited);
    assert(v2Unlimited.cgroupMemoryLimitBytes == 0);
    assert(v2Unlimited.hasCgroupMemoryCurrent);
    assert(v2Unlimited.cgroupMemoryCurrentBytes == 100ull * MiB);

    auto const v1 = parseProcessMemorySnapshot(
        memInfo, status, smaps, {}, {}, "536870912\n", "268435456\n"
    );
    assert(v1.hasCgroupMemoryLimit && !v1.cgroupMemoryLimitUnlimited);
    assert(v1.cgroupMemoryLimitBytes == 512ull * MiB);
    assert(v1.hasCgroupMemoryCurrent && v1.cgroupMemoryCurrentBytes == 256ull * MiB);

    auto const v1Unlimited = parseProcessMemorySnapshot(
        memInfo, status, smaps, {}, {}, "9223372036854771712\n", "12345\n"
    );
    assert(v1Unlimited.hasCgroupMemoryLimit && v1Unlimited.cgroupMemoryLimitUnlimited);
    assert(v1Unlimited.cgroupMemoryLimitBytes == 0);
    assert(v1Unlimited.hasCgroupMemoryCurrent && v1Unlimited.cgroupMemoryCurrentBytes == 12345ull);

    auto const invalidCgroup = parseProcessMemorySnapshot(
        memInfo, status, smaps, "not-a-limit\n", "10\n", "bad\n", "10\n"
    );
    assert(!invalidCgroup.hasCgroupMemoryLimit);
    assert(!invalidCgroup.hasCgroupMemoryCurrent);

    // cgroup and RLIMIT readings are reported independently; the system-based planning
    // budget is not inferred from limits that may be hidden or unenforced by Android.
    auto const budgetWithCgroup = calculateRendererMemoryBudget(v2);
    assert(budgetWithCgroup.budgetBytes == budget.budgetBytes);

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

    auto const limits = queryProcessResourceLimits();
#if defined(__linux__) || defined(__ANDROID__)
    assert(limits.addressSpace.available);
#endif
    return 0;
}
