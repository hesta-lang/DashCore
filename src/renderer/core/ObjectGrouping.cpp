#include "renderer/core/ObjectGrouping.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace dashcore::renderer::core {
namespace {

using Clock = std::chrono::steady_clock;

bool hasFiniteTransform(ObjectTransformRecord const& object) noexcept {
    return std::isfinite(object.positionX) &&
        std::isfinite(object.positionY) &&
        std::isfinite(object.rotationDegrees) &&
        std::isfinite(object.scaleX) &&
        std::isfinite(object.scaleY);
}

std::uint64_t packedBytes(GroupedObjectArrays const& grouped) noexcept {
    return static_cast<std::uint64_t>(grouped.positionsXY.size()) * sizeof(float) +
        static_cast<std::uint64_t>(grouped.rotationsDegrees.size()) * sizeof(float) +
        static_cast<std::uint64_t>(grouped.scalesXY.size()) * sizeof(float) +
        static_cast<std::uint64_t>(grouped.typeIds.size()) * sizeof(std::uint32_t) +
        static_cast<std::uint64_t>(grouped.objectIds.size()) * sizeof(std::uint32_t) +
        static_cast<std::uint64_t>(grouped.stateFlags.size()) * sizeof(std::uint32_t);
}

std::uint64_t elapsedNanoseconds(Clock::time_point start) noexcept {
    auto const elapsed = Clock::now() - start;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()
    );
}

} // namespace

bool validateGroupedObjectArrays(
    GroupedObjectArrays const& grouped
) noexcept {
    auto const objectCount = grouped.objectIds.size();
    if (objectCount > kMaximumGroupedObjects ||
        grouped.positionsXY.size() != objectCount * 2u ||
        grouped.rotationsDegrees.size() != objectCount ||
        grouped.scalesXY.size() != objectCount * 2u ||
        grouped.typeIds.size() != objectCount ||
        grouped.stateFlags.size() != objectCount) {
        return false;
    }

    std::size_t expectedFirstObject = 0;
    bool hasPreviousType = false;
    std::uint32_t previousType = 0;
    for (auto const& group : grouped.groups) {
        if (group.objectCount == 0 ||
            group.firstObject != expectedFirstObject ||
            (hasPreviousType && group.typeId <= previousType)) {
            return false;
        }

        auto const groupEnd = expectedFirstObject + group.objectCount;
        if (groupEnd > objectCount) return false;
        for (auto index = expectedFirstObject; index < groupEnd; ++index) {
            if (grouped.typeIds[index] != group.typeId ||
                grouped.objectIds[index] == 0 ||
                !std::isfinite(grouped.positionsXY[index * 2u]) ||
                !std::isfinite(grouped.positionsXY[index * 2u + 1u]) ||
                !std::isfinite(grouped.rotationsDegrees[index]) ||
                !std::isfinite(grouped.scalesXY[index * 2u]) ||
                !std::isfinite(grouped.scalesXY[index * 2u + 1u])) {
                return false;
            }
        }

        expectedFirstObject = groupEnd;
        previousType = group.typeId;
        hasPreviousType = true;
    }

    if (expectedFirstObject != objectCount ||
        ((objectCount == 0) != grouped.groups.empty())) {
        return false;
    }

    try {
        auto identifiers = grouped.objectIds;
        std::sort(identifiers.begin(), identifiers.end());
        return std::adjacent_find(identifiers.begin(), identifiers.end()) ==
            identifiers.end();
    } catch (...) {
        return false;
    }
}

bool buildObjectGrouping(
    std::span<ObjectTransformRecord const> input,
    GroupedObjectArrays& output,
    ObjectGroupingSummary& summary
) noexcept {
    auto const startedAt = Clock::now();
    output = {};
    summary = {};
    summary.inputObjects = input.size();

    auto finish = [&](ObjectGroupingStatus status, bool validated) noexcept {
        summary.status = status;
        summary.validated = validated;
        summary.elapsedNanoseconds = elapsedNanoseconds(startedAt);
        return validated;
    };

    if (input.size() > kMaximumGroupedObjects) {
        return finish(ObjectGroupingStatus::TooManyObjects, false);
    }

    try {
        std::vector<std::uint32_t> identifiers;
        std::vector<std::size_t> order;
        identifiers.reserve(input.size());
        order.resize(input.size());
        std::iota(order.begin(), order.end(), std::size_t{0});

        for (auto const& object : input) {
            if (object.objectId == 0) {
                return finish(ObjectGroupingStatus::ZeroObjectId, false);
            }
            if (!hasFiniteTransform(object)) {
                return finish(ObjectGroupingStatus::NonFiniteTransform, false);
            }
            identifiers.push_back(object.objectId);
        }

        std::sort(identifiers.begin(), identifiers.end());
        if (std::adjacent_find(identifiers.begin(), identifiers.end()) !=
            identifiers.end()) {
            return finish(ObjectGroupingStatus::DuplicateObjectId, false);
        }

        std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
            auto const leftType = input[left].typeId;
            auto const rightType = input[right].typeId;
            return leftType != rightType ? leftType < rightType : left < right;
        });

        GroupedObjectArrays next;
        auto const count = input.size();
        next.positionsXY.reserve(count * 2u);
        next.rotationsDegrees.reserve(count);
        next.scalesXY.reserve(count * 2u);
        next.typeIds.reserve(count);
        next.objectIds.reserve(count);
        next.stateFlags.reserve(count);
        next.groups.reserve(count);

        for (auto const sourceIndex : order) {
            auto const& object = input[sourceIndex];
            next.positionsXY.push_back(object.positionX);
            next.positionsXY.push_back(object.positionY);
            next.rotationsDegrees.push_back(object.rotationDegrees);
            next.scalesXY.push_back(object.scaleX);
            next.scalesXY.push_back(object.scaleY);
            next.typeIds.push_back(object.typeId);
            next.objectIds.push_back(object.objectId);
            next.stateFlags.push_back(object.stateFlags);
        }

        std::size_t firstObject = 0;
        while (firstObject < count) {
            auto const typeId = next.typeIds[firstObject];
            auto groupEnd = firstObject + 1u;
            while (groupEnd < count && next.typeIds[groupEnd] == typeId) {
                ++groupEnd;
            }
            next.groups.push_back({
                typeId,
                static_cast<std::uint32_t>(firstObject),
                static_cast<std::uint32_t>(groupEnd - firstObject)
            });
            firstObject = groupEnd;
        }

        if (!validateGroupedObjectArrays(next)) {
            return finish(ObjectGroupingStatus::InvalidOutput, false);
        }

        summary.groupedObjects = count;
        summary.groupCount = next.groups.size();
        summary.packedDataBytes = packedBytes(next);
        summary.groupRangeBytes =
            static_cast<std::uint64_t>(next.groups.size()) * sizeof(ObjectGroupRange);
        output = std::move(next);
        return finish(ObjectGroupingStatus::Passed, true);
    } catch (...) {
        output = {};
        return finish(ObjectGroupingStatus::AllocationFailed, false);
    }
}

ObjectGroupingSummary runSyntheticObjectGroupingDiagnostic() noexcept {
    std::array<ObjectTransformRecord, kSyntheticGroupingObjectCount> synthetic{};
    for (std::size_t index = 0; index < synthetic.size(); ++index) {
        auto const value = static_cast<float>(index);
        synthetic[index] = {
            static_cast<std::uint32_t>(index + 1u),
            value * 0.25f,
            -value * 0.125f,
            static_cast<float>((index * 13u) % 360u),
            0.75f + static_cast<float>(index % 5u) * 0.125f,
            0.8f + static_cast<float>(index % 7u) * 0.05f,
            static_cast<std::uint32_t>((index * 5u + 3u) % 8u),
            static_cast<std::uint32_t>(1u << (index % 8u))
        };
    }

    GroupedObjectArrays grouped;
    ObjectGroupingSummary summary{};
    buildObjectGrouping(synthetic, grouped, summary);
    return summary;
}

char const* objectGroupingStatusName(ObjectGroupingStatus status) noexcept {
    switch (status) {
        case ObjectGroupingStatus::Passed: return "passed";
        case ObjectGroupingStatus::TooManyObjects: return "too_many_objects";
        case ObjectGroupingStatus::ZeroObjectId: return "zero_object_id";
        case ObjectGroupingStatus::DuplicateObjectId: return "duplicate_object_id";
        case ObjectGroupingStatus::NonFiniteTransform: return "non_finite_transform";
        case ObjectGroupingStatus::AllocationFailed: return "allocation_failed";
        case ObjectGroupingStatus::InvalidOutput: return "invalid_output";
    }
    return "unknown";
}

} // namespace dashcore::renderer::core
