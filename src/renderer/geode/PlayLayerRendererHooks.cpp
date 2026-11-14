#include "renderer/geode/RendererCoordinator.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>

#if defined(GEODE_IS_ANDROID)
#include <Geode/cocos/platform/android/jni/JniHelper.h>
#include <jni.h>
#endif

#include <chrono>
#include <cstdint>

using namespace geode::prelude;

namespace {

#if defined(GEODE_IS_ANDROID)
void notifyLauncherLevelStarted() noexcept {
    cocos2d::JniMethodInfo method{};
    auto const found = cocos2d::JniHelper::getStaticMethodInfo(
        method,
        "com/geode/launcher/LauncherDiagnostics",
        "onLevelStarted",
        "(Ljava/lang/String;Ljava/lang/String;)V"
    );

    if (!found) {
        auto vm = cocos2d::JniHelper::getJavaVM();
        JNIEnv* env = nullptr;
        if (vm && vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK &&
            env && env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return;
    }

    // Level identity is intentionally omitted in this first bridge. The launcher
    // still receives exact PlayLayer enter/exit boundaries for level-only frames.
    method.env->CallStaticVoidMethod(
        method.classID,
        method.methodID,
        static_cast<jstring>(nullptr),
        static_cast<jstring>(nullptr)
    );
    if (method.env->ExceptionCheck()) method.env->ExceptionClear();
    method.env->DeleteLocalRef(method.classID);
}

void notifyLauncherLevelEnded() noexcept {
    cocos2d::JniMethodInfo method{};
    auto const found = cocos2d::JniHelper::getStaticMethodInfo(
        method,
        "com/geode/launcher/LauncherDiagnostics",
        "onLevelEnded",
        "()V"
    );

    if (!found) {
        auto vm = cocos2d::JniHelper::getJavaVM();
        JNIEnv* env = nullptr;
        if (vm && vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK &&
            env && env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return;
    }

    method.env->CallStaticVoidMethod(method.classID, method.methodID);
    if (method.env->ExceptionCheck()) method.env->ExceptionClear();
    method.env->DeleteLocalRef(method.classID);
}
#else
void notifyLauncherLevelStarted() noexcept {}
void notifyLauncherLevelEnded() noexcept {}
#endif

} // namespace

class $modify(DashCoreExperimentalPlayLayer, PlayLayer) {
    void onEnterTransitionDidFinish() {
        PlayLayer::onEnterTransitionDidFinish();
        dashcore::renderer::geode::coordinator().onLevelInit();
        notifyLauncherLevelStarted();
    }

    void postUpdate(float dt) {
        auto const updateStart = std::chrono::steady_clock::now();
        PlayLayer::postUpdate(dt);
        auto const updateElapsed = std::chrono::steady_clock::now() - updateStart;
        auto const updateNanoseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(updateElapsed).count()
        );
        dashcore::renderer::geode::coordinator().onFrame(this, updateNanoseconds);
    }

    void onExit() {
        notifyLauncherLevelEnded();
        dashcore::renderer::geode::coordinator().onLevelExit();
        PlayLayer::onExit();
    }
};
