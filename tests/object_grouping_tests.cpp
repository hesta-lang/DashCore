#include "renderer/core/ObjectGrouping.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <string_view>
#include <vector>

using namespace dashcore::renderer::core;

static ObjectTransformRecord makeObject(
    std::uint32_t id,
    std::uint32_t type,
    float x
) {
    return {id, x, x + 1.0f, x + 2.0f, 1.0f, 1.5f, type, id & 0x0fu};
}

int main() {
    std::vector<ObjectTransformRecord> input{
        makeObject(10, 2, 10.0f),
        makeObject(11, 1, 11.0f),
        makeObject(12, 2, 12.0f),
        makeObject(13, 0, 13.0f),
        makeObject(14, 1, 14.0f),
    };

    GroupedObjectArrays grouped;
    ObjectGroupingSummary summary{};
    assert(buildObjectGrouping(input, grouped, summary));
    assert(summary.status == ObjectGroupingStatus::Passed);
    assert(summary.validated);
    assert(summary.inputObjects == 5);
    assert(summary.groupedObjects == 5);
    assert(summary.groupCount == 3);
    assert(summary.packedDataBytes == 5u * 32u);
    assert(summary.groupRangeBytes == 3u * sizeof(ObjectGroupRange));
    assert(validateGroupedObjectArrays(grouped));

    assert((grouped.typeIds == std::vector<std::uint32_t>{0, 1, 1, 2, 2}));
    assert((grouped.objectIds == std::vector<std::uint32_t>{13, 11, 14, 10, 12}));
    assert(grouped.groups.size() == 3);
    assert(grouped.groups[0].typeId == 0);
    assert(grouped.groups[0].firstObject == 0);
    assert(grouped.groups[0].objectCount == 1);
    assert(grouped.groups[1].typeId == 1);
    assert(grouped.groups[1].firstObject == 1);
    assert(grouped.groups[1].objectCount == 2);
    assert(grouped.groups[2].typeId == 2);
    assert(grouped.groups[2].firstObject == 3);
    assert(grouped.groups[2].objectCount == 2);

    // Stable ordering within a type preserves the original record order.
    assert(grouped.positionsXY[2] == 11.0f);
    assert(grouped.positionsXY[4] == 14.0f);
    assert(grouped.stateFlags[1] == (11u & 0x0fu));

    std::vector<ObjectTransformRecord> empty;
    assert(buildObjectGrouping(empty, grouped, summary));
    assert(summary.validated);
    assert(summary.groupedObjects == 0);
    assert(summary.groupCount == 0);
    assert(grouped.objectIds.empty());
    assert(grouped.groups.empty());

    auto duplicate = input;
    duplicate[4].objectId = duplicate[0].objectId;
    assert(!buildObjectGrouping(duplicate, grouped, summary));
    assert(summary.status == ObjectGroupingStatus::DuplicateObjectId);
    assert(!summary.validated);
    assert(grouped.objectIds.empty());

    auto zeroId = input;
    zeroId[0].objectId = 0;
    assert(!buildObjectGrouping(zeroId, grouped, summary));
    assert(summary.status == ObjectGroupingStatus::ZeroObjectId);

    auto nonFinite = input;
    nonFinite[2].rotationDegrees = std::numeric_limits<float>::quiet_NaN();
    assert(!buildObjectGrouping(nonFinite, grouped, summary));
    assert(summary.status == ObjectGroupingStatus::NonFiniteTransform);

    std::vector<ObjectTransformRecord> oversized(kMaximumGroupedObjects + 1u);
    assert(!buildObjectGrouping(oversized, grouped, summary));
    assert(summary.status == ObjectGroupingStatus::TooManyObjects);

    auto invalidGrouped = GroupedObjectArrays{};
    invalidGrouped.positionsXY = {0.0f};
    assert(!validateGroupedObjectArrays(invalidGrouped));

    auto synthetic = runSyntheticObjectGroupingDiagnostic();
    assert(synthetic.status == ObjectGroupingStatus::Passed);
    assert(synthetic.validated);
    assert(synthetic.inputObjects == kSyntheticGroupingObjectCount);
    assert(synthetic.groupedObjects == kSyntheticGroupingObjectCount);
    assert(synthetic.groupCount == 8);
    assert(synthetic.packedDataBytes == kSyntheticGroupingObjectCount * 32u);
    assert(synthetic.groupRangeBytes == 8u * sizeof(ObjectGroupRange));
    assert(objectGroupingStatusName(synthetic.status) == std::string_view("passed"));
    return 0;
}
