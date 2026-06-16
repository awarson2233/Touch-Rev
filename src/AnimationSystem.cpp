#include "AnimationSystem.h"

#include "Win32Error.h"

#include <cmath>

HRESULT AnimationSystem::Initialize()
{
    Shutdown();

    HRESULT hr = CoCreateInstance(
        CLSID_UIAnimationManager,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(manager_.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        DebugLogHResult(L"CoCreateInstance(CLSID_UIAnimationManager)", hr);
        Shutdown();
        return hr;
    }

    hr = CoCreateInstance(
        CLSID_UIAnimationTimer,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(timer_.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        DebugLogHResult(L"CoCreateInstance(CLSID_UIAnimationTimer)", hr);
        Shutdown();
        return hr;
    }

    hr = CoCreateInstance(
        CLSID_UIAnimationTransitionLibrary,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(transitionLibrary_.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        DebugLogHResult(L"CoCreateInstance(CLSID_UIAnimationTransitionLibrary)", hr);
        Shutdown();
        return hr;
    }

    return S_OK;
}

void AnimationSystem::Shutdown()
{
    Stop();
    yVariable_.Reset();
    xVariable_.Reset();
    transitionLibrary_.Reset();
    timer_.Reset();
    manager_.Reset();
}

HRESULT AnimationSystem::Start(
    float startX,
    float startY,
    float targetX,
    float targetY,
    double durationSeconds)
{
    if (!IsAvailable())
    {
        return E_FAIL;
    }

    if (std::abs(startX - targetX) < 0.01f && std::abs(startY - targetY) < 0.01f)
    {
        active_ = false;
        return S_FALSE;
    }

    Stop();

    HRESULT hr = manager_->CreateAnimationVariable(startX, xVariable_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        DebugLogHResult(L"IUIAnimationManager::CreateAnimationVariable(x)", hr);
        return hr;
    }

    hr = manager_->CreateAnimationVariable(startY, yVariable_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        DebugLogHResult(L"IUIAnimationManager::CreateAnimationVariable(y)", hr);
        return hr;
    }

    hr = manager_->CreateStoryboard(storyboard_.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        DebugLogHResult(L"IUIAnimationManager::CreateStoryboard", hr);
        return hr;
    }

    Microsoft::WRL::ComPtr<IUIAnimationTransition> xTransition;
    hr = transitionLibrary_->CreateAccelerateDecelerateTransition(
        durationSeconds,
        targetX,
        0.35,
        0.65,
        xTransition.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        DebugLogHResult(L"IUIAnimationTransitionLibrary::CreateAccelerateDecelerateTransition(x)", hr);
        return hr;
    }

    Microsoft::WRL::ComPtr<IUIAnimationTransition> yTransition;
    hr = transitionLibrary_->CreateAccelerateDecelerateTransition(
        durationSeconds,
        targetY,
        0.35,
        0.65,
        yTransition.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        DebugLogHResult(L"IUIAnimationTransitionLibrary::CreateAccelerateDecelerateTransition(y)", hr);
        return hr;
    }

    hr = storyboard_->AddTransition(xVariable_.Get(), xTransition.Get());
    if (FAILED(hr))
    {
        DebugLogHResult(L"IUIAnimationStoryboard::AddTransition(x)", hr);
        return hr;
    }

    hr = storyboard_->AddTransition(yVariable_.Get(), yTransition.Get());
    if (FAILED(hr))
    {
        DebugLogHResult(L"IUIAnimationStoryboard::AddTransition(y)", hr);
        return hr;
    }

    UI_ANIMATION_SECONDS now = 0.0;
    hr = timer_->GetTime(&now);
    if (FAILED(hr))
    {
        DebugLogHResult(L"IUIAnimationTimer::GetTime", hr);
        return hr;
    }

    hr = storyboard_->Schedule(now);
    if (FAILED(hr))
    {
        DebugLogHResult(L"IUIAnimationStoryboard::Schedule", hr);
        return hr;
    }

    active_ = true;
    return S_OK;
}

void AnimationSystem::Stop()
{
    if (manager_)
    {
        manager_->AbandonAllStoryboards();
    }
    storyboard_.Reset();
    active_ = false;
}

bool AnimationSystem::Update(float& x, float& y)
{
    if (!active_ || !IsAvailable() || !xVariable_ || !yVariable_)
    {
        return false;
    }

    UI_ANIMATION_SECONDS now = 0.0;
    HRESULT hr = timer_->GetTime(&now);
    if (FAILED(hr))
    {
        DebugLogHResult(L"IUIAnimationTimer::GetTime(update)", hr);
        active_ = false;
        return false;
    }

    hr = manager_->Update(now);
    if (FAILED(hr))
    {
        DebugLogHResult(L"IUIAnimationManager::Update", hr);
        active_ = false;
        return false;
    }

    DOUBLE currentX = 0.0;
    DOUBLE currentY = 0.0;
    hr = xVariable_->GetValue(&currentX);
    if (FAILED(hr))
    {
        DebugLogHResult(L"IUIAnimationVariable::GetValue(x)", hr);
        active_ = false;
        return false;
    }

    hr = yVariable_->GetValue(&currentY);
    if (FAILED(hr))
    {
        DebugLogHResult(L"IUIAnimationVariable::GetValue(y)", hr);
        active_ = false;
        return false;
    }

    x = static_cast<float>(currentX);
    y = static_cast<float>(currentY);

    UI_ANIMATION_MANAGER_STATUS status = UI_ANIMATION_MANAGER_IDLE;
    hr = manager_->GetStatus(&status);
    if (SUCCEEDED(hr) && status == UI_ANIMATION_MANAGER_IDLE)
    {
        active_ = false;
        storyboard_.Reset();
    }

    return true;
}
