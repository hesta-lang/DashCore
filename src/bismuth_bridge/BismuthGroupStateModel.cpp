#include "BismuthGroupStateModel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dashcore::bismuth_bridge {
namespace {
Matrix2x2 multiply(Matrix2x2 const& a, Matrix2x2 const& b) noexcept {
    return {
        a.m00 * b.m00 + a.m01 * b.m10,
        a.m00 * b.m01 + a.m01 * b.m11,
        a.m10 * b.m00 + a.m11 * b.m10,
        a.m10 * b.m01 + a.m11 * b.m11
    };
}
bool validGroupId(std::int32_t id) noexcept {
    return id >= 0 && id <= static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max());
}
}

bool GroupCombinationKey::operator<(GroupCombinationKey const& other) const noexcept {
    if (count != other.count) return count < other.count;
    for (std::uint8_t i = 0; i < count; ++i) {
        if (ids[i] != other.ids[i]) return ids[i] < other.ids[i];
    }
    return false;
}

GroupCombinationKey BismuthGroupStateModel::normalize(GroupCombinationKey key) noexcept {
    key.count = std::min<std::uint8_t>(key.count, 10);
    std::sort(key.ids.begin(), key.ids.begin() + key.count);
    auto const end = std::unique(key.ids.begin(), key.ids.begin() + key.count);
    key.count = static_cast<std::uint8_t>(std::distance(key.ids.begin(), end));
    std::fill(key.ids.begin() + key.count, key.ids.end(), 0);
    return key;
}

bool BismuthGroupStateModel::initialize(
    std::vector<GroupCombinationKey> const& combinations,
    std::vector<std::int16_t> const& opacityGroups
) {
    m_combinations.clear();
    m_states.clear();
    m_combinationIndices.clear();
    m_combinationsPerGroup.clear();
    m_opacityGroups.clear();
    m_groupEnabled.clear();
    m_maximumGroupId = 0;
    m_initialized = false;

    for (auto id : opacityGroups) {
        if (id >= 0) m_opacityGroups.insert(id);
        m_maximumGroupId = std::max(
            m_maximumGroupId,
            static_cast<std::uint32_t>(std::max<std::int16_t>(id, 0))
        );
    }

    for (auto key : combinations) {
        key = normalize(key);
        auto const [it, inserted] = m_combinationIndices.emplace(
            key, static_cast<std::uint32_t>(m_combinations.size())
        );
        if (!inserted) continue;

        auto const index = it->second;
        m_combinations.push_back(key);
        m_states.emplace_back();
        for (std::uint8_t item = 0; item < key.count; ++item) {
            auto const id = key.ids[item];
            if (id < 0) continue;
            m_maximumGroupId = std::max(m_maximumGroupId, static_cast<std::uint32_t>(id));
            m_combinationsPerGroup[id].push_back(index);
        }
    }

    if (m_combinations.empty()) {
        GroupCombinationKey empty{};
        m_combinations.push_back(empty);
        m_combinationIndices.emplace(empty, 0);
        m_states.emplace_back();
    }

    if (!validGroupId(static_cast<std::int32_t>(m_maximumGroupId))) return false;
    m_groupEnabled.assign(static_cast<std::size_t>(m_maximumGroupId) + 1u, 1u);
    m_initialized = true;
    return true;
}

bool BismuthGroupStateModel::findCombinationIndex(
    GroupCombinationKey key,
    std::uint32_t& index
) const noexcept {
    if (!m_initialized) return false;
    key = normalize(key);
    auto const found = m_combinationIndices.find(key);
    if (found == m_combinationIndices.end()) return false;
    index = found->second;
    return true;
}

void BismuthGroupStateModel::reset() noexcept {
    for (auto& state : m_states) state = {};
    std::fill(m_groupEnabled.begin(), m_groupEnabled.end(), 1u);
}

void BismuthGroupStateModel::moveGroup(std::int32_t id, float dx, float dy) noexcept {
    if (!m_initialized || !validGroupId(id) || !std::isfinite(dx) || !std::isfinite(dy)) return;
    auto const found = m_combinationsPerGroup.find(static_cast<std::int16_t>(id));
    if (found == m_combinationsPerGroup.end()) return;
    for (auto index : found->second) {
        if (index >= m_states.size()) continue;
        m_states[index].offsetX += dx;
        m_states[index].offsetY += dy;
    }
}

void BismuthGroupStateModel::rotateGroup(
    std::int32_t id, float angleDegrees, bool lockObjectRotation,
    bool hasCenter, float centerX, float centerY
) noexcept {
    if (!m_initialized || !validGroupId(id) || !std::isfinite(angleDegrees) ||
        (hasCenter && (!std::isfinite(centerX) || !std::isfinite(centerY)))) return;
    auto const found = m_combinationsPerGroup.find(static_cast<std::int16_t>(id));
    if (found == m_combinationsPerGroup.end()) return;

    auto const radians = angleDegrees * 0.01745329251994329577f;
    auto const c = std::cos(radians);
    auto const s = std::sin(radians);
    // Row-major representation of the GLM matrix used by upstream Bismuth.
    Matrix2x2 const rotation{c, s, -s, c};

    for (auto index : found->second) {
        if (index >= m_states.size()) continue;
        auto& state = m_states[index];
        if (!lockObjectRotation) state.localTransform = multiply(state.localTransform, rotation);
        if (hasCenter) {
            state.positionalTransform = multiply(state.positionalTransform, rotation);
            auto const x = state.offsetX - centerX;
            auto const y = state.offsetY - centerY;
            state.offsetX = rotation.m00 * x + rotation.m01 * y + centerX;
            state.offsetY = rotation.m10 * x + rotation.m11 * y + centerY;
        }
    }
}

void BismuthGroupStateModel::toggleGroup(std::int32_t id, bool enabled) noexcept {
    if (!m_initialized || !validGroupId(id) || static_cast<std::size_t>(id) >= m_groupEnabled.size()) return;
    m_groupEnabled[static_cast<std::size_t>(id)] = enabled ? 1u : 0u;
}

void BismuthGroupStateModel::updateOpacities(std::vector<float> const& opacityByGroupId) noexcept {
    if (!m_initialized) return;
    for (std::size_t combinationIndex = 0; combinationIndex < m_combinations.size(); ++combinationIndex) {
        float opacity = 1.f;
        auto const& key = m_combinations[combinationIndex];
        for (std::uint8_t item = 0; item < key.count; ++item) {
            auto const id = key.ids[item];
            if (!m_opacityGroups.contains(id)) continue;
            auto const groupIndex = static_cast<std::size_t>(id);
            if (groupIndex >= m_groupEnabled.size() || m_groupEnabled[groupIndex] == 0u) {
                opacity = 0.f;
                break;
            }
            if (groupIndex < opacityByGroupId.size()) {
                auto const value = opacityByGroupId[groupIndex];
                opacity *= std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
            }
        }
        m_states[combinationIndex].opacity = opacity;
    }
}

void BismuthGroupStateModel::setPulseIndex(std::uint32_t index, std::uint32_t pulseIndex) noexcept {
    if (!m_initialized || index >= m_states.size()) return;
    m_states[index].pulseIndex = pulseIndex;
}

GroupStateCheckpoint BismuthGroupStateModel::storeCheckpoint() const {
    return {m_states, m_groupEnabled};
}

bool BismuthGroupStateModel::restoreCheckpoint(GroupStateCheckpoint const& checkpoint) noexcept {
    if (!m_initialized || checkpoint.states.size() != m_states.size() ||
        checkpoint.groupEnabled.size() != m_groupEnabled.size()) return false;
    std::copy(checkpoint.states.begin(), checkpoint.states.end(), m_states.begin());
    std::copy(checkpoint.groupEnabled.begin(), checkpoint.groupEnabled.end(), m_groupEnabled.begin());
    return true;
}

std::vector<GpuGroupStateRecord> BismuthGroupStateModel::makeGpuRecords() const {
    std::vector<GpuGroupStateRecord> result;
    result.reserve(m_states.size());
    for (auto const& state : m_states) {
        GpuGroupStateRecord record{};
        record.localTransform[0] = state.localTransform.m00;
        record.localTransform[1] = state.localTransform.m01;
        record.localTransform[2] = state.localTransform.m10;
        record.localTransform[3] = state.localTransform.m11;
        record.positionalTransform[0] = state.positionalTransform.m00;
        record.positionalTransform[1] = state.positionalTransform.m01;
        record.positionalTransform[2] = state.positionalTransform.m10;
        record.positionalTransform[3] = state.positionalTransform.m11;
        record.offsetOpacity[0] = state.offsetX;
        record.offsetOpacity[1] = state.offsetY;
        record.offsetOpacity[2] = state.opacity;
        record.offsetOpacity[3] = 0.f;
        record.pulseIndex = state.pulseIndex;
        result.push_back(record);
    }
    return result;
}
} // namespace dashcore::bismuth_bridge
