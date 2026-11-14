#include "BismuthTriggerAdapter.hpp"

#include <Geode/Enums.hpp>
#include <Geode/binding/CheckpointObject.hpp>
#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

using namespace geode::prelude;

namespace dashcore::bismuth_bridge {
namespace {
constexpr std::size_t kMaximumLevelObjects = 1000000;
constexpr std::size_t kMaximumPulseActions = 131072;
constexpr std::size_t kColorChannelCount = 1101;

bool validGroupId(std::int32_t value) noexcept {
    return value >= 0 &&
        value <= static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max());
}

ColorChannelState copyColor(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                            std::uint8_t opacity) noexcept {
    return {r, g, b, opacity, true};
}
}

bool BismuthTriggerAdapter::initialize(
    GJBaseGameLayer* layer,
    cocos2d::CCArray* objects
) noexcept {
    reset();
    if (!layer || !objects) return false;

    try {
        ::geode::cocos::CCArrayExt<GameObject*, false> entries(objects);
        if (entries.size() > kMaximumLevelObjects) return false;

        std::vector<GroupCombinationKey> combinations;
        std::vector<GroupCombinationKey> objectKeys;
        std::vector<std::uint8_t> objectPresent;
        std::vector<std::int16_t> opacityGroups;
        combinations.reserve(entries.size());
        objectKeys.reserve(entries.size());
        objectPresent.reserve(entries.size());

        for (auto* object : entries) {
            if (!object) {
                objectKeys.emplace_back();
                objectPresent.push_back(0u);
                continue;
            }
            if (object->m_groupCount > 0 && !object->m_groups) return false;

            GroupCombinationKey key{};
            auto const count = std::min<std::size_t>(
                static_cast<std::size_t>(object->m_groupCount),
                key.ids.size()
            );
            for (std::size_t index = 0; index < count; ++index) {
                auto const groupId = static_cast<std::int32_t>(object->m_groups->at(index));
                if (!validGroupId(groupId) || groupId == 0) continue;
                key.ids[key.count++] = static_cast<std::int16_t>(groupId);
            }
            combinations.push_back(key);
            objectKeys.push_back(key);
            objectPresent.push_back(1u);

            if (object->m_objectID == ALPHA_TRIGGER_ID ||
                object->m_objectID == TOGGLE_TRIGGER_ID) {
                auto* effectObject = static_cast<EffectGameObject*>(object);
                auto const targetGroup = static_cast<std::int32_t>(effectObject->m_targetGroupID);
                if (validGroupId(targetGroup) && targetGroup != 0)
                    opacityGroups.push_back(static_cast<std::int16_t>(targetGroup));
            }
        }

        if (!m_model.initialize(combinations, opacityGroups)) return false;
        m_objectCombinationIndices.assign(entries.size(), kNoCombination);
        for (std::size_t index = 0; index < objectKeys.size(); ++index) {
            if (objectPresent[index] == 0u) continue;
            auto combinationIndex = kNoCombination;
            if (!m_model.findCombinationIndex(objectKeys[index], combinationIndex))
                return false;
            m_objectCombinationIndices[index] = combinationIndex;
        }

        m_opacityByGroupId.assign(
            static_cast<std::size_t>(m_model.maximumGroupId()) + 1u, 1.f
        );
        m_colorChannels.assign(kColorChannelCount, {});
        m_pulseActions.clear();
        m_pulseActions.emplace_back(); // Pulse index zero means "no pulse" in Bismuth.
        m_ready = true;
        return true;
    } catch (...) {
        reset();
        return false;
    }
}

void BismuthTriggerAdapter::reset() noexcept {
    m_model = {};
    m_objectCombinationIndices.clear();
    m_opacityByGroupId.clear();
    m_colorChannels.clear();
    m_pulseActions.clear();
    m_checkpoints.clear();
    m_stats = {};
    m_ready = false;
}

void BismuthTriggerAdapter::resetStates() noexcept {
    if (!m_ready) return;
    m_model.reset();
    std::fill(m_colorChannels.begin(), m_colorChannels.end(), ColorChannelState{});
    m_pulseActions.clear();
    m_pulseActions.emplace_back();
    m_checkpoints.clear();
    m_stats = {};
}

void BismuthTriggerAdapter::observeMoveActions(GJBaseGameLayer* layer) noexcept {
    if (!m_ready || !layer || !layer->m_effectManager) return;
    for (auto node : layer->m_effectManager->m_unkVector6c0) {
        if (!node || node->m_unk0d0) continue;
        m_model.moveGroup(node->getTag(), node->m_unk038, node->m_unk040);
        ++m_stats.moveActions;
    }
}

void BismuthTriggerAdapter::observeRotationActions(GJBaseGameLayer* layer) noexcept {
    if (!m_ready || !layer || !layer->m_effectManager) return;
    auto* effectManager = layer->m_effectManager;

    for (auto command : effectManager->m_unkVector5b0) {
        if (!command || command->m_someInterpValue1RelatedFalse) continue;

        auto const targetGroup = static_cast<std::int32_t>(command->m_targetGroupID);
        auto const centerGroup = static_cast<std::int32_t>(command->m_centerGroupID);
        auto* staticGroup = layer->getStaticGroup(targetGroup);

        float rotation = 0.f;
        if (staticGroup && staticGroup->count() != 0) {
            rotation = command->m_someInterpValue1RelatedOne -
                       command->m_someInterpValue1RelatedZero;
        } else {
            rotation = command->m_someInterpValue2RelatedOne -
                       command->m_someInterpValue2RelatedZero;
        }
        if (rotation == 0.f && !command->m_finishRelated) continue;

        auto* centerObject = layer->tryGetMainObject(centerGroup);
        if (!centerObject) {
            m_model.rotateGroup(targetGroup, rotation, command->m_lockObjectRotation,
                                false, 0.f, 0.f);
        } else {
            auto const center = centerObject->getUnmodifiedPosition();
            m_model.rotateGroup(targetGroup, rotation, command->m_lockObjectRotation,
                                true, center.x, center.y);

            auto const extraMove = effectManager->m_unkMap618.find(targetGroup);
            if (extraMove != effectManager->m_unkMap618.end() && extraMove->second) {
                float moveX = 0.f;
                float moveY = 0.f;
                if (staticGroup && staticGroup->count() != 0) {
                    moveX = extraMove->second->m_unk090;
                    moveY = extraMove->second->m_unk098;
                } else {
                    moveX = extraMove->second->m_unk038;
                    moveY = extraMove->second->m_unk040;
                }
                m_model.moveGroup(targetGroup, moveX, moveY);
            }
        }
        ++m_stats.rotationActions;
    }
}

void BismuthTriggerAdapter::observeFollowActions(GJBaseGameLayer* layer) noexcept {
    if (!m_ready || !layer || !layer->m_effectManager) return;
    for (auto node : layer->m_effectManager->m_unkVector6d8) {
        if (!node) continue;
        auto const targetGroup = static_cast<std::int32_t>(node->getTag());
        auto const followGroup = static_cast<std::int32_t>(node->m_unk074);
        auto* followObject = layer->tryGetMainObject(followGroup);
        if (!followObject) continue;

        float moveX = 0.f;
        float moveY = 0.f;
        if (followObject->m_unk4C4 == layer->m_gameState.m_commandIndex) {
            moveX = (followObject->m_positionX - followObject->m_lastPosition.x) * node->m_unk080;
            moveY = (followObject->m_positionY - followObject->m_lastPosition.y) * node->m_unk088;
        }
        m_model.moveGroup(targetGroup, moveX, moveY);
        ++m_stats.followActions;
    }
}

void BismuthTriggerAdapter::observeToggleGroup(std::int32_t groupId, bool enabled) noexcept {
    if (!m_ready) return;
    m_model.toggleGroup(groupId, enabled);
    ++m_stats.toggleActions;
}

void BismuthTriggerAdapter::captureVisualEffects(GJEffectManager* effectManager) noexcept {
    if (!m_ready || !effectManager) return;
    try {
        std::fill(m_opacityByGroupId.begin(), m_opacityByGroupId.end(), 1.f);
        for (std::size_t id = 1; id < m_opacityByGroupId.size(); ++id) {
            m_opacityByGroupId[id] = effectManager->opacityModForGroup(static_cast<int>(id));
        }
        m_model.updateOpacities(m_opacityByGroupId);
        ++m_stats.opacityCaptures;

        std::fill(m_colorChannels.begin(), m_colorChannels.end(), ColorChannelState{});
        for (auto* channel : effectManager->m_colorActionSpriteVector) {
            if (!channel) continue;
            auto const id = static_cast<std::int32_t>(channel->m_colorID);
            if (id < 0 || static_cast<std::size_t>(id) >= m_colorChannels.size()) continue;
            auto const opacity = static_cast<std::uint8_t>(
                std::clamp(static_cast<int>(channel->m_opacity), 0, 255)
            );
            m_colorChannels[static_cast<std::size_t>(id)] = copyColor(
                channel->m_copyColor.r, channel->m_copyColor.g,
                channel->m_copyColor.b, opacity
            );
            ++m_stats.colorChannelCaptures;
        }

        m_pulseActions.clear();
        m_pulseActions.emplace_back();
        auto const& combinations = m_model.combinations();
        for (std::size_t combinationIndex = 0;
             combinationIndex < combinations.size() &&
             m_pulseActions.size() < kMaximumPulseActions;
             ++combinationIndex) {
            auto const pulseStart = m_pulseActions.size();
            auto const& combination = combinations[combinationIndex];

            for (std::uint8_t item = 0;
                 item < combination.count &&
                 m_pulseActions.size() < kMaximumPulseActions;
                 ++item) {
                auto const groupId = static_cast<std::int32_t>(combination.ids[item]);
                if (groupId <= 0 ||
                    static_cast<std::size_t>(groupId) >= effectManager->m_unkVector2f0.size()) continue;
                if (!effectManager->m_unkVector2f0[static_cast<std::size_t>(groupId)]) continue;

                auto const& actions = effectManager->m_pulseEffectMap[static_cast<std::size_t>(groupId)];
                for (auto const& action : actions) {
                    if (action.m_pulseEffectType != PulseEffectType::Hsv &&
                        action.m_pulseEffectType != PulseEffectType::Color) continue;
                    if (m_pulseActions.size() >= kMaximumPulseActions) break;

                    PulseActionState state{};
                    state.groupId = static_cast<std::uint16_t>(groupId);
                    state.combinationIndex = static_cast<std::uint32_t>(combinationIndex);
                    state.currentValue = action.m_currentValue;
                    state.type = static_cast<std::uint8_t>(action.m_pulseEffectType);
                    state.mainOnly = action.m_mainOnly;
                    state.detailOnly = action.m_detailOnly;
                    state.dynamicHsv = action.m_isDynamicHsv;
                    state.colorIndex = static_cast<std::uint16_t>(
                        std::clamp(static_cast<int>(action.m_colorIndex), 0, 65535)
                    );
                    state.hue = action.m_hsv.h;
                    state.saturation = action.m_hsv.s;
                    state.brightness = action.m_hsv.v;
                    state.absoluteSaturation = action.m_hsv.absoluteSaturation;
                    state.absoluteBrightness = action.m_hsv.absoluteBrightness;
                    state.color = copyColor(action.m_color.r, action.m_color.g, action.m_color.b, 255);
                    m_pulseActions.push_back(state);
                    ++m_stats.pulseCaptures;
                }
            }

            if (m_pulseActions.size() == pulseStart) {
                m_model.setPulseIndex(static_cast<std::uint32_t>(combinationIndex), 0);
            } else {
                m_model.setPulseIndex(
                    static_cast<std::uint32_t>(combinationIndex),
                    static_cast<std::uint32_t>(pulseStart)
                );
                if (m_pulseActions.size() < kMaximumPulseActions) m_pulseActions.emplace_back();
            }
        }
    } catch (...) {
        // The mirror is optional; a capture failure must never interrupt gameplay.
        m_pulseActions.clear();
        m_pulseActions.emplace_back();
    }
}

void BismuthTriggerAdapter::storeCheckpoint(CheckpointObject* checkpoint) noexcept {
    if (!m_ready || !checkpoint) return;
    try {
        m_checkpoints[checkpoint] = m_model.storeCheckpoint();
    } catch (...) {
        // Never interfere with vanilla checkpoint creation.
    }
}

bool BismuthTriggerAdapter::restoreCheckpoint(CheckpointObject* checkpoint) noexcept {
    if (!m_ready || !checkpoint) return false;
    auto const found = m_checkpoints.find(checkpoint);
    if (found == m_checkpoints.end()) return false;
    return m_model.restoreCheckpoint(found->second);
}

void BismuthTriggerAdapter::clearCheckpoints() noexcept {
    m_checkpoints.clear();
}
} // namespace dashcore::bismuth_bridge
