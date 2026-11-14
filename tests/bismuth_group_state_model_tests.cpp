#include "bismuth_bridge/BismuthGroupStateModel.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>
using namespace dashcore::bismuth_bridge;

namespace {
bool near(float a, float b, float e = 0.0001f) { return std::fabs(a - b) <= e; }
GroupCombinationKey key(std::initializer_list<std::int16_t> ids) {
    GroupCombinationKey value{};
    for (auto id : ids) { assert(value.count < value.ids.size()); value.ids[value.count++] = id; }
    return value;
}
std::uint32_t findCombination(BismuthGroupStateModel const& model, std::initializer_list<std::int16_t> ids) {
    auto wanted = key(ids);
    auto const& values = model.combinations();
    for (std::uint32_t i = 0; i < values.size(); ++i) {
        if (values[i].count != wanted.count) continue;
        bool equal = true;
        for (std::uint8_t j = 0; j < wanted.count; ++j) if (values[i].ids[j] != wanted.ids[j]) equal = false;
        if (equal) return i;
    }
    assert(false && "group combination not found");
    return 0;
}
void testCombinationNormalizationAndMove() {
    BismuthGroupStateModel model;
    assert(model.initialize({key({5, 1}), key({1}), key({8}), key({1, 5})}, {}));
    assert(model.combinationCount() == 3);
    auto both = findCombination(model, {1, 5});
    auto one = findCombination(model, {1});
    auto eight = findCombination(model, {8});
    model.moveGroup(1, 10.f, -2.f);
    auto const& states = model.states();
    assert(near(states[both].offsetX, 10.f) && near(states[both].offsetY, -2.f));
    assert(near(states[one].offsetX, 10.f));
    assert(near(states[eight].offsetX, 0.f));
}
void testRotationAndLock() {
    BismuthGroupStateModel model;
    assert(model.initialize({key({2}), key({3})}, {}));
    auto groupTwo = findCombination(model, {2});
    model.moveGroup(2, 10.f, -2.f);
    model.rotateGroup(2, 90.f, false, true, 0.f, 0.f);
    auto const& rotated = model.states()[groupTwo];
    assert(near(rotated.offsetX, -2.f) && near(rotated.offsetY, -10.f));
    assert(near(rotated.localTransform.m00, 0.f));
    assert(near(rotated.localTransform.m01, 1.f));
    assert(near(rotated.localTransform.m10, -1.f));
    assert(near(rotated.localTransform.m11, 0.f));
    auto local = rotated.localTransform;
    model.rotateGroup(2, 30.f, true, true, 0.f, 0.f);
    auto const& locked = model.states()[groupTwo];
    assert(near(locked.localTransform.m00, local.m00));
    assert(near(locked.localTransform.m01, local.m01));
    assert(!near(locked.positionalTransform.m00, 0.f) || !near(locked.positionalTransform.m01, 1.f));
}
void testOpacityToggleAndCheckpoint() {
    BismuthGroupStateModel model;
    assert(model.initialize({key({1, 5}), key({1}), key({8})}, {1, 5}));
    auto both = findCombination(model, {1, 5});
    auto one = findCombination(model, {1});
    auto eight = findCombination(model, {8});
    std::vector<float> opacity(9, 1.f);
    opacity[1] = 0.5f; opacity[5] = 0.7f;
    model.updateOpacities(opacity);
    assert(near(model.states()[both].opacity, 0.35f));
    assert(near(model.states()[one].opacity, 0.5f));
    assert(near(model.states()[eight].opacity, 1.f));
    model.setPulseIndex(both, 4);
    auto checkpoint = model.storeCheckpoint();
    model.toggleGroup(5, false);
    model.updateOpacities(opacity);
    assert(near(model.states()[both].opacity, 0.f));
    assert(model.restoreCheckpoint(checkpoint));
    assert(near(model.states()[both].opacity, 0.35f));
    assert(model.states()[both].pulseIndex == 4);
    model.toggleGroup(1, false);
    model.updateOpacities(opacity);
    assert(near(model.states()[both].opacity, 0.f));
    model.reset();
    assert(near(model.states()[both].offsetX, 0.f));
    assert(near(model.states()[both].opacity, 1.f));
}
void testGpuRecordLayoutAndSerialization() {
    static_assert(sizeof(GpuGroupStateRecord) == 64);
    static_assert(alignof(GpuGroupStateRecord) == 16);
    BismuthGroupStateModel model;
    assert(model.initialize({key({4})}, {4}));
    model.moveGroup(4, 12.f, 13.f);
    model.setPulseIndex(0, 9);
    std::vector<float> opacity(5, 1.f); opacity[4] = 0.25f;
    model.updateOpacities(opacity);
    auto records = model.makeGpuRecords();
    assert(records.size() == 1);
    assert(near(records[0].offsetOpacity[0], 12.f));
    assert(near(records[0].offsetOpacity[1], 13.f));
    assert(near(records[0].offsetOpacity[2], 0.25f));
    assert(records[0].pulseIndex == 9);
}
}
int main() {
    testCombinationNormalizationAndMove();
    testRotationAndLock();
    testOpacityToggleAndCheckpoint();
    testGpuRecordLayoutAndSerialization();
    return 0;
}
