#include "renderer/core/MemoryBudget.hpp"

#include <algorithm>
#include <limits>

namespace dashcore::renderer::core {
namespace {

constexpr std::uint64_t kBytesPerKiB = 1024ull;
constexpr std::uint64_t kMiB = 1024ull * 1024ull;
constexpr std::uint64_t kMinimumSystemReserve = 256ull * kMiB;
constexpr std::uint64_t kMinimumUsableBudget = 8ull * kMiB;
constexpr std::uint64_t kMaximumSingleBuffer = 16ull * kMiB;
constexpr std::uint64_t kCgroupV1UnlimitedThreshold = 1ull << 60u;

bool parseUnsigned(std::string_view text, std::uint64_t& value) noexcept {
    value = 0;
    std::size_t cursor = 0;
    while (cursor < text.size() && (text[cursor] == ' ' || text[cursor] == '\t')) ++cursor;
    auto const start = cursor;
    while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
        auto const digit = static_cast<std::uint64_t>(text[cursor] - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10ull) return false;
        value = value * 10ull + digit;
        ++cursor;
    }
    return cursor > start;
}

bool parseStrictUnsigned(std::string_view text, std::uint64_t& value) noexcept {
    value = 0;
    std::size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' ||
            text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    if (begin == end) return false;

    for (std::size_t cursor = begin; cursor < end; ++cursor) {
        auto const character = text[cursor];
        if (character < '0' || character > '9') return false;
        auto const digit = static_cast<std::uint64_t>(character - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10ull) return false;
        value = value * 10ull + digit;
    }
    return true;
}

bool parseCgroupLimit(
    std::string_view text,
    bool isV2,
    std::uint64_t& value,
    bool& unlimited
) noexcept {
    value = 0;
    unlimited = false;
    std::size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' ||
            text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    if (begin == end) return false;

    if (isV2 && text.substr(begin, end - begin) == "max") {
        unlimited = true;
        return true;
    }
    if (!parseStrictUnsigned(text.substr(begin, end - begin), value)) return false;

    // cgroup v1 commonly represents "unlimited" with a near-SIZE_MAX sentinel.
    if (!isV2 && value >= kCgroupV1UnlimitedThreshold) {
        value = 0;
        unlimited = true;
    }
    return true;
}

bool findKilobyteField(std::string_view text, std::string_view field, std::uint64_t& bytes) noexcept {
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        auto const lineEnd = text.find('\n', cursor);
        auto const end = lineEnd == std::string_view::npos ? text.size() : lineEnd;
        auto const line = text.substr(cursor, end - cursor);
        if (line.size() >= field.size() && line.substr(0, field.size()) == field) {
            auto const colon = line.find(':');
            if (colon == std::string_view::npos) return false;
            std::uint64_t value = 0;
            if (!parseUnsigned(line.substr(colon + 1), value) ||
                value > std::numeric_limits<std::uint64_t>::max() / kBytesPerKiB) return false;
            bytes = value * kBytesPerKiB;
            return true;
        }
        if (lineEnd == std::string_view::npos) break;
        cursor = lineEnd + 1;
    }
    return false;
}

} // namespace

ProcessMemorySnapshot parseProcessMemorySnapshot(
    std::string_view memInfo,
    std::string_view processStatus,
    std::string_view smapsRollup,
    std::string_view cgroupV2Limit,
    std::string_view cgroupV2Current,
    std::string_view cgroupV1Limit,
    std::string_view cgroupV1Usage
) noexcept {
    ProcessMemorySnapshot result{};

    std::uint64_t totalBytes = 0;
    std::uint64_t availableBytes = 0;
    if (findKilobyteField(memInfo, "MemTotal:", totalBytes) &&
        findKilobyteField(memInfo, "MemAvailable:", availableBytes) &&
        totalBytes > 0 && availableBytes <= totalBytes) {
        result.systemTotalBytes = totalBytes;
        result.systemAvailableBytes = availableBytes;
        result.hasSystemMemory = true;
    }

    result.hasProcessRss = findKilobyteField(processStatus, "VmRSS:", result.processRssBytes);
    result.hasProcessPeakRss = findKilobyteField(processStatus, "VmHWM:", result.processPeakRssBytes);
    result.hasProcessVirtual = findKilobyteField(processStatus, "VmSize:", result.processVirtualBytes);
    result.hasProcessPss = findKilobyteField(smapsRollup, "Pss:", result.processPssBytes);

    std::uint64_t limitBytes = 0;
    std::uint64_t currentBytes = 0;
    bool limitUnlimited = false;
    if (parseCgroupLimit(cgroupV2Limit, true, limitBytes, limitUnlimited) &&
        parseStrictUnsigned(cgroupV2Current, currentBytes)) {
        result.hasCgroupMemoryLimit = true;
        result.cgroupMemoryLimitUnlimited = limitUnlimited;
        result.cgroupMemoryLimitBytes = limitBytes;
        result.hasCgroupMemoryCurrent = true;
        result.cgroupMemoryCurrentBytes = currentBytes;
    } else if (parseCgroupLimit(cgroupV1Limit, false, limitBytes, limitUnlimited) &&
               parseStrictUnsigned(cgroupV1Usage, currentBytes)) {
        result.hasCgroupMemoryLimit = true;
        result.cgroupMemoryLimitUnlimited = limitUnlimited;
        result.cgroupMemoryLimitBytes = limitBytes;
        result.hasCgroupMemoryCurrent = true;
        result.cgroupMemoryCurrentBytes = currentBytes;
    }

    return result;
}

RendererMemoryBudget calculateRendererMemoryBudget(
    ProcessMemorySnapshot const& snapshot,
    std::uint64_t maximumBudgetBytes
) noexcept {
    RendererMemoryBudget result{};
    if (!snapshot.hasSystemMemory || snapshot.systemTotalBytes == 0 ||
        snapshot.systemAvailableBytes > snapshot.systemTotalBytes || maximumBudgetBytes == 0) return result;

    result.systemSafetyReserveBytes = std::max<std::uint64_t>(
        kMinimumSystemReserve, snapshot.systemTotalBytes / 10ull
    );
    if (snapshot.systemAvailableBytes <= result.systemSafetyReserveBytes) return result;

    result.availableHeadroomBytes = snapshot.systemAvailableBytes - result.systemSafetyReserveBytes;
    // Only one eighth of remaining system headroom is available to the renderer; this is planning only.
    result.budgetBytes = std::min<std::uint64_t>(result.availableHeadroomBytes / 8ull, maximumBudgetBytes);
    result.canAllocateBuffers = result.budgetBytes >= kMinimumUsableBudget;
    if (result.canAllocateBuffers) {
        result.maxSingleBufferBytes = std::min<std::uint64_t>(result.budgetBytes / 2ull, kMaximumSingleBuffer);
    }
    return result;
}

} // namespace dashcore::renderer::core
