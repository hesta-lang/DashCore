#pragma once

#include "BismuthGroupStateModel.hpp"

#include <cstdint>
#include <map>
#include <vector>

class CheckpointObject;
class GJBaseGameLayer;
class GJEffectManager;

namespace cocos2d {
class CCArray;
}

namespace dashcore::bismuth_bridge {

struct ColorChannelState {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t opacity = 255;
    bool valid = false;
};

struct PulseActionState {
    std::uint16_t groupId = 0;
    std::uint32_t combinationIndex = 0;
    float currentValue = 0.f;
    std::uint8_t type = 0;
    bool mainOnly = false;
    bool detailOnly = false;
    bool dynamicHsv = false;
    std::uint16_t colorIndex = 0;
    float hue = 0.f;
    float saturation = 0.f;
    float brightness = 0.f;
    bool absoluteSaturation = false;
    bool absoluteBrightness = false;
    ColorChannelState color{};
};

struct TriggerAdapterStats {
    std::uint64_t moveActions = 0;
    std::uint64_t rotationActions = 0;
    std::uint64_t followActions = 0;
    std::uint64_t toggleActions = 0;
    std::uint64_t opacityCaptures = 0;
    std::uint64_t pulseCaptures = 0;
    std::uint64_t colorChannelCaptures = 0;
};

class BismuthTriggerAdapter {
public:
    static constexpr std::uint32_t kNoCombination = UINT32_MAX;

    bool initialize(GJBaseGameLayer* layer, cocos2d::CCArray* objects) noexcept;
    void reset() noexcept;
    void resetStates() noexcept;
    void observeMoveActions(GJBaseGameLayer* layer) noexcept;
    void observeRotationActions(GJBaseGameLayer* layer) noexcept;
    void observeFollowActions(GJBaseGameLayer* layer) noexcept;
    void observeToggleGroup(std::int32_t groupId, bool enabled) noexcept;
    void captureVisualEffects(GJEffectManager* effectManager) noexcept;
    void storeCheckpoint(CheckpointObject* checkpoint) noexcept;
    bool restoreCheckpoint(CheckpointObject* checkpoint) noexcept;
    void clearCheckpoints() noexcept;

    bool initialized() const noexcept { return m_model.initialized(); }
    BismuthGroupStateModel const& model() const noexcept { return m_model; }
    std::vector<std::uint32_t> const& objectCombinationIndices() const noexcept {
        return m_objectCombinationIndices;
    }
    std::vector<ColorChannelState> const& colorChannels() const noexcept {
        return m_colorChannels;
    }
    std::vector<PulseActionState> const& pulseActions() const noexcept {
        return m_pulseActions;
    }
    std::vector<GpuGroupStateRecord> gpuGroupStateRecords() const {
        return m_model.makeGpuRecords();
    }
    TriggerAdapterStats const& stats() const noexcept { return m_stats; }

private:
    BismuthGroupStateModel m_model;
    std::vector<std::uint32_t> m_objectCombinationIndices;
    std::vector<float> m_opacityByGroupId;
    std::vector<ColorChannelState> m_colorChannels;
    std::vector<PulseActionState> m_pulseActions;
    std::map<CheckpointObject*, GroupStateCheckpoint> m_checkpoints;
    TriggerAdapterStats m_stats{};
    bool m_ready = false;
};

} // namespace dashcore::bismuth_bridge
