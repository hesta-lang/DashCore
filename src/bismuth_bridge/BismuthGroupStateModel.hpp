#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace dashcore::bismuth_bridge {

struct GroupCombinationKey {
    std::array<std::int16_t, 10> ids{};
    std::uint8_t count = 0;
    bool operator<(GroupCombinationKey const& other) const noexcept;
};

struct Matrix2x2 {
    float m00 = 1.f;
    float m01 = 0.f;
    float m10 = 0.f;
    float m11 = 1.f;
};

struct GroupCombinationState {
    Matrix2x2 localTransform{};
    Matrix2x2 positionalTransform{};
    float offsetX = 0.f;
    float offsetY = 0.f;
    float opacity = 1.f;
    std::uint32_t pulseIndex = 0;
};

struct alignas(16) GpuGroupStateRecord {
    float localTransform[4]{1.f, 0.f, 0.f, 1.f};
    float positionalTransform[4]{1.f, 0.f, 0.f, 1.f};
    float offsetOpacity[4]{0.f, 0.f, 1.f, 0.f};
    std::uint32_t pulseIndex = 0;
    std::uint32_t flags = 0;
    std::uint32_t reserved0 = 0;
    std::uint32_t reserved1 = 0;
};

static_assert(sizeof(GpuGroupStateRecord) == 64);
static_assert(alignof(GpuGroupStateRecord) == 16);

struct GroupStateCheckpoint {
    std::vector<GroupCombinationState> states;
    std::vector<std::uint8_t> groupEnabled;
};

class BismuthGroupStateModel {
public:
    bool initialize(
        std::vector<GroupCombinationKey> const& combinations,
        std::vector<std::int16_t> const& opacityGroups
    );
    void reset() noexcept;
    void moveGroup(std::int32_t groupId, float deltaX, float deltaY) noexcept;
    void rotateGroup(
        std::int32_t groupId,
        float angleDegrees,
        bool lockObjectRotation,
        bool hasCenter,
        float centerX,
        float centerY
    ) noexcept;
    void toggleGroup(std::int32_t groupId, bool enabled) noexcept;
    void updateOpacities(std::vector<float> const& opacityByGroupId) noexcept;
    void setPulseIndex(std::uint32_t combinationIndex, std::uint32_t pulseIndex) noexcept;
    bool findCombinationIndex(
        GroupCombinationKey key,
        std::uint32_t& index
    ) const noexcept;

    GroupStateCheckpoint storeCheckpoint() const;
    bool restoreCheckpoint(GroupStateCheckpoint const& checkpoint) noexcept;
    std::vector<GpuGroupStateRecord> makeGpuRecords() const;

    std::vector<GroupCombinationKey> const& combinations() const noexcept {
        return m_combinations;
    }
    std::vector<GroupCombinationState> const& states() const noexcept {
        return m_states;
    }
    std::uint32_t maximumGroupId() const noexcept {
        return m_maximumGroupId;
    }
    std::uint32_t combinationCount() const noexcept {
        return static_cast<std::uint32_t>(m_combinations.size());
    }
    bool initialized() const noexcept {
        return m_initialized;
    }

private:
    static GroupCombinationKey normalize(GroupCombinationKey key) noexcept;

    std::vector<GroupCombinationKey> m_combinations;
    std::vector<GroupCombinationState> m_states;
    std::map<GroupCombinationKey, std::uint32_t> m_combinationIndices;
    std::map<std::int16_t, std::vector<std::uint32_t>> m_combinationsPerGroup;
    std::set<std::int16_t> m_opacityGroups;
    std::vector<std::uint8_t> m_groupEnabled;
    std::uint32_t m_maximumGroupId = 0;
    bool m_initialized = false;
};

} // namespace dashcore::bismuth_bridge
