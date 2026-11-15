#include "renderer/core/MemoryBudget.hpp"

#if defined(__linux__) || defined(__ANDROID__)

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/resource.h>
#include <unistd.h>
#include <fcntl.h>

namespace dashcore::renderer::core {
namespace {

std::size_t readFile(char const* path, char* destination, std::size_t capacity) noexcept {
    if (!path || !destination || capacity < 2) return 0;
    auto const descriptor = ::open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) return 0;

    std::size_t total = 0;
    while (total + 1 < capacity) {
        auto const count = ::read(descriptor, destination + total, capacity - total - 1);
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            total = 0;
            break;
        }
        total += static_cast<std::size_t>(count);
    }
    ::close(descriptor);
    destination[total] = '\0';
    return total;
}

bool safeCgroupPath(std::string_view path) noexcept {
    if (path.empty() || path.front() != '/') return false;
    std::size_t segmentStart = 1;
    for (std::size_t i = 1; i <= path.size(); ++i) {
        if (i < path.size()) {
            auto const c = path[i];
            bool const allowed =
                (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '/' || c == '_' ||
                c == '-' || c == '.' || c == '@';
            if (!allowed) return false;
        }
        if (i == path.size() || path[i] == '/') {
            auto const segment = path.substr(segmentStart, i - segmentStart);
            if (segment == "." || segment == "..") return false;
            segmentStart = i + 1;
        }
    }
    return true;
}

void copyPath(std::string_view source, char* destination, std::size_t capacity) noexcept {
    if (!destination || capacity == 0 || !safeCgroupPath(source) || source.size() + 1 > capacity) return;
    std::memcpy(destination, source.data(), source.size());
    destination[source.size()] = '\0';
}

bool hasMemoryController(std::string_view controllers) noexcept {
    std::size_t cursor = 0;
    while (cursor <= controllers.size()) {
        auto const comma = controllers.find(',', cursor);
        auto const end = comma == std::string_view::npos ? controllers.size() : comma;
        if (controllers.substr(cursor, end - cursor) == "memory") return true;
        if (comma == std::string_view::npos) break;
        cursor = comma + 1;
    }
    return false;
}

void findCgroupPaths(
    std::string_view membership,
    char* unifiedPath,
    std::size_t unifiedCapacity,
    char* memoryPath,
    std::size_t memoryCapacity
) noexcept {
    std::size_t cursor = 0;
    while (cursor < membership.size()) {
        auto const lineEnd = membership.find('\n', cursor);
        auto const end = lineEnd == std::string_view::npos ? membership.size() : lineEnd;
        auto const line = membership.substr(cursor, end - cursor);
        auto const firstColon = line.find(':');
        auto const secondColon = firstColon == std::string_view::npos
            ? std::string_view::npos
            : line.find(':', firstColon + 1);
        if (firstColon != std::string_view::npos && secondColon != std::string_view::npos) {
            auto const controllers = line.substr(firstColon + 1, secondColon - firstColon - 1);
            auto const path = line.substr(secondColon + 1);
            if (controllers.empty() && unifiedPath[0] == '\0') {
                copyPath(path, unifiedPath, unifiedCapacity);
            } else if (hasMemoryController(controllers) && memoryPath[0] == '\0') {
                copyPath(path, memoryPath, memoryCapacity);
            }
        }
        if (lineEnd == std::string_view::npos) break;
        cursor = lineEnd + 1;
    }
}

bool readCgroupPair(
    char const* root,
    char const* membershipPath,
    char const* limitName,
    char const* currentName,
    char* limitText,
    std::size_t limitCapacity,
    char* currentText,
    std::size_t currentCapacity
) noexcept {
    if (!root || !limitText || !currentText) return false;
    char limitPath[640]{};
    char currentPath[640]{};
    char const* suffix = membershipPath && membershipPath[0] != '\0' &&
        std::strcmp(membershipPath, "/") != 0 ? membershipPath : "";
    auto const limitLength = std::snprintf(
        limitPath, sizeof(limitPath), "%s%s/%s", root, suffix, limitName
    );
    auto const currentLength = std::snprintf(
        currentPath, sizeof(currentPath), "%s%s/%s", root, suffix, currentName
    );
    if (limitLength <= 0 || currentLength <= 0 ||
        static_cast<std::size_t>(limitLength) >= sizeof(limitPath) ||
        static_cast<std::size_t>(currentLength) >= sizeof(currentPath)) return false;

    auto const limitSize = readFile(limitPath, limitText, limitCapacity);
    auto const currentSize = readFile(currentPath, currentText, currentCapacity);
    return limitSize != 0 && currentSize != 0;
}

void queryCgroupFiles(
    char const* membershipPath,
    char const* root,
    char const* limitName,
    char const* currentName,
    char* limitText,
    std::size_t limitCapacity,
    char* currentText,
    std::size_t currentCapacity
) noexcept {
    if (readCgroupPair(root, membershipPath, limitName, currentName,
                       limitText, limitCapacity, currentText, currentCapacity)) return;
    if (membershipPath && membershipPath[0] != '\0' &&
        std::strcmp(membershipPath, "/") != 0) {
        limitText[0] = '\0';
        currentText[0] = '\0';
        (void)readCgroupPair(root, "/", limitName, currentName,
                             limitText, limitCapacity, currentText, currentCapacity);
    }
}

void assignResourceLimit(ResourceLimitPair& destination, int resource) noexcept {
    struct rlimit limits {};
    if (::getrlimit(resource, &limits) != 0) return;

    destination.available = true;
    destination.softUnlimited = limits.rlim_cur == RLIM_INFINITY;
    destination.hardUnlimited = limits.rlim_max == RLIM_INFINITY;
    destination.softBytes = destination.softUnlimited
        ? 0
        : static_cast<std::uint64_t>(limits.rlim_cur);
    destination.hardBytes = destination.hardUnlimited
        ? 0
        : static_cast<std::uint64_t>(limits.rlim_max);
}

} // namespace

ProcessResourceLimits queryProcessResourceLimits() noexcept {
    ProcessResourceLimits result{};
#if defined(RLIMIT_AS)
    assignResourceLimit(result.addressSpace, RLIMIT_AS);
#endif
#if defined(RLIMIT_RSS)
    assignResourceLimit(result.residentSet, RLIMIT_RSS);
#endif
    return result;
}

ProcessMemorySnapshot queryProcessMemorySnapshot() noexcept {
    // Fixed-size stack buffers keep sampling independent of the heap.
    char memInfo[8192]{};
    char processStatus[8192]{};
    char smapsRollup[8192]{};
    char cgroupMembership[8192]{};
    char cgroupV2Path[384]{};
    char cgroupV1Path[384]{};
    char cgroupV2Limit[96]{};
    char cgroupV2Current[96]{};
    char cgroupV1Limit[96]{};
    char cgroupV1Usage[96]{};

    auto const memInfoSize = readFile("/proc/meminfo", memInfo, sizeof(memInfo));
    auto const statusSize = readFile("/proc/self/status", processStatus, sizeof(processStatus));
    auto const smapsSize = readFile("/proc/self/smaps_rollup", smapsRollup, sizeof(smapsRollup));
    auto const membershipSize = readFile("/proc/self/cgroup", cgroupMembership, sizeof(cgroupMembership));

    if (membershipSize != 0) {
        findCgroupPaths(
            std::string_view(cgroupMembership, membershipSize),
            cgroupV2Path, sizeof(cgroupV2Path),
            cgroupV1Path, sizeof(cgroupV1Path)
        );
    }

    queryCgroupFiles(
        cgroupV2Path, "/sys/fs/cgroup", "memory.max", "memory.current",
        cgroupV2Limit, sizeof(cgroupV2Limit),
        cgroupV2Current, sizeof(cgroupV2Current)
    );
    if (cgroupV2Limit[0] == '\0' || cgroupV2Current[0] == '\0') {
        queryCgroupFiles(
            cgroupV1Path, "/sys/fs/cgroup/memory", "memory.limit_in_bytes", "memory.usage_in_bytes",
            cgroupV1Limit, sizeof(cgroupV1Limit),
            cgroupV1Usage, sizeof(cgroupV1Usage)
        );
        if (cgroupV1Limit[0] == '\0' || cgroupV1Usage[0] == '\0') {
            queryCgroupFiles(
                cgroupV1Path, "/dev/memcg", "memory.limit_in_bytes", "memory.usage_in_bytes",
                cgroupV1Limit, sizeof(cgroupV1Limit),
                cgroupV1Usage, sizeof(cgroupV1Usage)
            );
        }
    }

    auto result = parseProcessMemorySnapshot(
        std::string_view(memInfo, memInfoSize),
        std::string_view(processStatus, statusSize),
        std::string_view(smapsRollup, smapsSize),
        std::string_view(cgroupV2Limit, std::strlen(cgroupV2Limit)),
        std::string_view(cgroupV2Current, std::strlen(cgroupV2Current)),
        std::string_view(cgroupV1Limit, std::strlen(cgroupV1Limit)),
        std::string_view(cgroupV1Usage, std::strlen(cgroupV1Usage))
    );
    result.resourceLimits = queryProcessResourceLimits();
    return result;
}

} // namespace dashcore::renderer::core

#else

namespace dashcore::renderer::core {
ProcessResourceLimits queryProcessResourceLimits() noexcept { return {}; }
ProcessMemorySnapshot queryProcessMemorySnapshot() noexcept { return {}; }
} // namespace dashcore::renderer::core

#endif
