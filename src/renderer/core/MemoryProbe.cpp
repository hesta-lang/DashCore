#include "renderer/core/MemoryBudget.hpp"
#if defined(__linux__) || defined(__ANDROID__)
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <unistd.h>
namespace dashcore::renderer::core {
namespace {
std::size_t readProcFile(char const* path, char* destination, std::size_t capacity) noexcept {
    if (!destination || capacity < 2) return 0;
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
} // namespace
ProcessMemorySnapshot queryProcessMemorySnapshot() noexcept {
    // Fixed-size stack buffers keep sampling independent of the heap.
    char memInfo[8192]{}, processStatus[8192]{}, smapsRollup[8192]{};
    auto const memInfoSize = readProcFile("/proc/meminfo", memInfo, sizeof(memInfo));
    auto const statusSize = readProcFile("/proc/self/status", processStatus, sizeof(processStatus));
    auto const smapsSize = readProcFile("/proc/self/smaps_rollup", smapsRollup, sizeof(smapsRollup));
    return parseProcessMemorySnapshot(
        std::string_view(memInfo, memInfoSize),
        std::string_view(processStatus, statusSize),
        std::string_view(smapsRollup, smapsSize)
    );
}
} // namespace dashcore::renderer::core
#else
namespace dashcore::renderer::core {
ProcessMemorySnapshot queryProcessMemorySnapshot() noexcept { return {}; }
} // namespace dashcore::renderer::core
#endif
