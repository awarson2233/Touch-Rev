#pragma once

#include <windows.h>
#include <objbase.h>
#include <uianimation.h>
#include <wrl/client.h>

class AnimationSystem
{
public:
    HRESULT Initialize();
    void Shutdown();

    HRESULT Start(float startX, float startY, float targetX, float targetY, double durationSeconds = 0.18);
    void Stop();
    bool Update(float& x, float& y);

    bool IsAvailable() const { return manager_ && timer_ && transitionLibrary_; }
    bool IsActive() const { return active_; }

private:
    Microsoft::WRL::ComPtr<IUIAnimationManager> manager_;
    Microsoft::WRL::ComPtr<IUIAnimationTimer> timer_;
    Microsoft::WRL::ComPtr<IUIAnimationTransitionLibrary> transitionLibrary_;
    Microsoft::WRL::ComPtr<IUIAnimationStoryboard> storyboard_;
    Microsoft::WRL::ComPtr<IUIAnimationVariable> xVariable_;
    Microsoft::WRL::ComPtr<IUIAnimationVariable> yVariable_;
    bool active_ = false;
};
