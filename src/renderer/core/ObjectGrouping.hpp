#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dashcore::renderer::core {

inline constexpr std::size_t kMaximumGroupedObjects = 4096;
inline constexpr std::size_t kSyntheticGroupingObjectCount = 1024;

struct ObjectTransformRecord {
    std::uint32_t objectId = 0;
    float positionX = 0.0f;
    float positionY = 0.0f;
    float rotationDegrees = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    std::uint32_t typeId = 0;
    std::uint32_t stateFlags = 0;
};

struct ObjectGroupRange {
    std::uint32_t typeId = 0;
    std::uint32_t firstObject = 0;
    std::uint32_t objectCount = 0;
};

struct GroupedObjectArrays {
    // Structure-of-arrays layout intended for later serialization to SSBOs.
    // This stage stores CPU data only and makes no OpenGL calls.
    std::vector<float> positionsXY;
    std::vector<float> rotationsDegrees;
    std::vector<float> scalesXY;
    std::vector<std::uint32_t> typeIds;
    std::vector<std::uint32_t> objectIds;
    std::vector<std::uint32_t> stateFlags;
    std::vector<ObjectGroupRange> groups;
};

enum class ObjectGroupingStatus : std::uint8_t {
    Passed,
    TooManyObjects,
    ZeroObjectId,
    DuplicateObjectId,
    NonFiniteTransform,
    AllocationFailed,
    InvalidOutput,
};

struct ObjectGroupingSummary {
    ObjectGroupingStatus status = ObjectGroupingStatus::InvalidOutput;
    std::uint64_t inputObjects = 0;
    std::uint64_t groupedObjects = 0;
    std::uint64_t groupCount = 0;
    std::uint64_t packedDataBytes = 0;
    std::uint64_t groupRangeBytes = 0;
    std::uint64_t elapsedNanoseconds = 0;
    bool validated = false;
};

bool validateGroupedObjectArrays(
    GroupedObjectArrays const& grouped
) noexcept;

bool buildObjectGrouping(
    std::span<ObjectTransformRecord const> input,
    GroupedObjectArrays& output,
    ObjectGroupingSummary& summary
) noexcept;

ObjectGroupingSummary runSyntheticObjectGroupingDiagnostic() noexcept;

char const* objectGroupingStatusName(ObjectGroupingStatus status) noexcept;

} // namespace dashcore::renderer::core
