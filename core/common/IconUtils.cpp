#include "IconUtils.h"

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <appmodel.h>
#include <propidl.h>
#include <propsys.h>
#include <propkey.h>
#include <shellapi.h>
#include <shobjidl_core.h>
#include <unknwn.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

struct __declspec(uuid("5b0d3235-4dba-4d44-865e-8f1d0e4fd04d")) IMemoryBufferByteAccess : ::IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** value, UINT32* capacity) = 0;
};

namespace
{

std::vector<BYTE> ReadIconMask(HBITMAP hMask, int width, int height, int& stride)
{
    stride = 0;
    if (!hMask || width <= 0 || height <= 0)
    {
        return {};
    }

    stride = ((width + 31) / 32) * 4;
    std::vector<BYTE> maskBits(static_cast<size_t>(stride) * static_cast<size_t>(height));

    struct MonochromeBitmapInfo
    {
        BITMAPINFOHEADER header{};
        RGBQUAD colors[2]{};
    } bmi{};
    bmi.header.biSize = sizeof(BITMAPINFOHEADER);
    bmi.header.biWidth = width;
    bmi.header.biHeight = -height;
    bmi.header.biPlanes = 1;
    bmi.header.biBitCount = 1;
    bmi.header.biCompression = BI_RGB;
    bmi.colors[0] = RGBQUAD{0, 0, 0, 0};
    bmi.colors[1] = RGBQUAD{255, 255, 255, 0};

    HDC hdc = GetDC(nullptr);
    if (!hdc)
    {
        stride = 0;
        return {};
    }

    const int lines = GetDIBits(
        hdc,
        hMask,
        0,
        static_cast<UINT>(height),
        maskBits.data(),
        reinterpret_cast<BITMAPINFO*>(&bmi),
        DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);

    if (lines == 0)
    {
        stride = 0;
        return {};
    }
    return maskBits;
}

bool IsMaskTransparent(const std::vector<BYTE>& maskBits, int stride, int x, int y)
{
    if (maskBits.empty() || stride <= 0)
    {
        return false;
    }

    const BYTE byte = maskBits[static_cast<size_t>(y) * static_cast<size_t>(stride) + static_cast<size_t>(x / 8)];
    return (byte & (0x80 >> (x % 8))) != 0;
}

void NormalizeIconPixels(BYTE* pixels, int width, int height, const std::vector<BYTE>& maskBits, int maskStride)
{
    bool hasAlpha = false;
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        if (pixels[i * 4 + 3] != 0)
        {
            hasAlpha = true;
            break;
        }
    }

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            BYTE* pixel = pixels + (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
            BYTE& blue = pixel[0];
            BYTE& green = pixel[1];
            BYTE& red = pixel[2];
            BYTE& alpha = pixel[3];

            if (!hasAlpha)
            {
                if (IsMaskTransparent(maskBits, maskStride, x, y))
                {
                    blue = 0;
                    green = 0;
                    red = 0;
                    alpha = 0;
                }
                else
                {
                    alpha = 255;
                }
                continue;
            }

            if (alpha == 0)
            {
                blue = 0;
                green = 0;
                red = 0;
            }
            else if (alpha < 255 && (blue > alpha || green > alpha || red > alpha))
            {
                blue = static_cast<BYTE>((static_cast<UINT>(blue) * alpha + 127) / 255);
                green = static_cast<BYTE>((static_cast<UINT>(green) * alpha + 127) / 255);
                red = static_cast<BYTE>((static_cast<UINT>(red) * alpha + 127) / 255);
            }
        }
    }
}

bool CopyPixelsToSoftwareBitmap(
    const BYTE* srcBits,
    int width,
    int height,
    winrt::Windows::Graphics::Imaging::SoftwareBitmap const& softwareBitmap)
{
    try
    {
        namespace wg = winrt::Windows::Graphics::Imaging;
        wg::BitmapBuffer buffer = softwareBitmap.LockBuffer(wg::BitmapBufferAccessMode::Write);
        const auto plane = buffer.GetPlaneDescription(0);
        auto reference = buffer.CreateReference();
        auto byteAccess = reference.as<::IMemoryBufferByteAccess>();

        BYTE* destBits = nullptr;
        UINT32 capacity = 0;
        if (FAILED(byteAccess->GetBuffer(&destBits, &capacity)) || !destBits)
        {
            return false;
        }

        const size_t rowBytes = static_cast<size_t>(width) * 4;
        for (int y = 0; y < height; ++y)
        {
            const int64_t destOffset = static_cast<int64_t>(plane.StartIndex) +
                                       static_cast<int64_t>(plane.Stride) * y;
            if (destOffset < 0 || static_cast<uint64_t>(destOffset) + rowBytes > capacity)
            {
                return false;
            }

            std::memcpy(
                destBits + destOffset,
                srcBits + static_cast<size_t>(y) * rowBytes,
                rowBytes);
        }
        return true;
    }
    catch (const winrt::hresult_error&)
    {
        return false;
    }
}

winrt::Windows::UI::Xaml::Media::ImageSource HBitmapToImageSource(HBITMAP hBitmap)
{
    if (!hBitmap)
    {
        return nullptr;
    }

    BITMAP bmp{};
    if (!GetObject(hBitmap, sizeof(bmp), &bmp))
    {
        return nullptr;
    }

    const int width = bmp.bmWidth;
    const int height = std::abs(bmp.bmHeight);
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    std::vector<BYTE> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(nullptr);
    if (!hdc)
    {
        return nullptr;
    }

    const int lines = GetDIBits(
        hdc,
        hBitmap,
        0,
        static_cast<UINT>(height),
        pixels.data(),
        &bmi,
        DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    if (lines == 0)
    {
        return nullptr;
    }

    NormalizeIconPixels(pixels.data(), width, height, {}, 0);

    namespace wg = winrt::Windows::Graphics::Imaging;
    wg::SoftwareBitmap softwareBitmap(wg::BitmapPixelFormat::Bgra8, width, height, wg::BitmapAlphaMode::Premultiplied);
    if (!CopyPixelsToSoftwareBitmap(pixels.data(), width, height, softwareBitmap))
    {
        return nullptr;
    }

    try
    {
        winrt::Windows::UI::Xaml::Media::Imaging::SoftwareBitmapSource source;
        source.SetBitmapAsync(softwareBitmap);
        return source;
    }
    catch (const winrt::hresult_error&)
    {
        return nullptr;
    }
}

std::wstring GetWindowPropertyAppUserModelId(HWND hwnd)
{
    IPropertyStore* rawStore = nullptr;
    if (FAILED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&rawStore))) || !rawStore)
    {
        return {};
    }

    winrt::com_ptr<IPropertyStore> propertyStore;
    propertyStore.attach(rawStore);

    PROPVARIANT value{};
    PropVariantInit(&value);

    std::wstring appUserModelId;
    if (SUCCEEDED(propertyStore->GetValue(PKEY_AppUserModel_ID, &value)) &&
        value.vt == VT_LPWSTR &&
        value.pwszVal)
    {
        appUserModelId = value.pwszVal;
    }

    PropVariantClear(&value);
    return appUserModelId;
}

struct AppUserModelIdSearchContext
{
    std::wstring appUserModelId;
};

BOOL CALLBACK EnumChildAppUserModelIdProc(HWND hwndChild, LPARAM lParam)
{
    auto* context = reinterpret_cast<AppUserModelIdSearchContext*>(lParam);
    context->appUserModelId = GetWindowPropertyAppUserModelId(hwndChild);
    return context->appUserModelId.empty() ? TRUE : FALSE;
}

std::wstring GetWindowOrChildAppUserModelId(HWND hwnd)
{
    std::wstring appUserModelId = GetWindowPropertyAppUserModelId(hwnd);
    if (!appUserModelId.empty())
    {
        return appUserModelId;
    }

    AppUserModelIdSearchContext context{};
    EnumChildWindows(hwnd, EnumChildAppUserModelIdProc, reinterpret_cast<LPARAM>(&context));
    return context.appUserModelId;
}

std::wstring GetProcessAppUserModelId(DWORD pid)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
    {
        return {};
    }

    UINT32 length = 0;
    LONG result = GetApplicationUserModelId(process, &length, nullptr);
    if (result != ERROR_INSUFFICIENT_BUFFER || length == 0)
    {
        CloseHandle(process);
        return {};
    }

    std::wstring appUserModelId(length, L'\0');
    result = GetApplicationUserModelId(process, &length, appUserModelId.data());
    CloseHandle(process);

    if (result != ERROR_SUCCESS || length == 0)
    {
        return {};
    }

    if (!appUserModelId.empty() && appUserModelId.back() == L'\0')
    {
        appUserModelId.pop_back();
    }
    return appUserModelId;
}

std::wstring GetAppUserModelIdForWindow(HWND hwnd)
{
    std::wstring appUserModelId = GetWindowOrChildAppUserModelId(hwnd);
    if (!appUserModelId.empty())
    {
        return appUserModelId;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0)
    {
        appUserModelId = GetProcessAppUserModelId(pid);
        if (!appUserModelId.empty())
        {
            return appUserModelId;
        }
    }

    return {};
}

winrt::Windows::UI::Xaml::Media::ImageSource GetAppUserModelIdImageSource(const std::wstring& appUserModelId)
{
    if (appUserModelId.empty())
    {
        return nullptr;
    }

    const std::wstring parsingName = L"shell:AppsFolder\\" + appUserModelId;

    IShellItemImageFactory* rawFactory = nullptr;
    if (FAILED(SHCreateItemFromParsingName(
            parsingName.c_str(),
            nullptr,
            IID_PPV_ARGS(&rawFactory))) ||
        !rawFactory)
    {
        return nullptr;
    }

    winrt::com_ptr<IShellItemImageFactory> factory;
    factory.attach(rawFactory);

    HBITMAP hBitmap = nullptr;
    constexpr SIZE iconSize{32, 32};
    const HRESULT hr = factory->GetImage(
        iconSize,
        SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK,
        &hBitmap);
    if (FAILED(hr) || !hBitmap)
    {
        return nullptr;
    }

    auto source = HBitmapToImageSource(hBitmap);
    DeleteObject(hBitmap);
    return source;
}

std::wstring GetProcessPath(DWORD pid)
{
    std::wstring path(MAX_PATH, L'\0');
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess)
    {
        DWORD size = static_cast<DWORD>(path.size());
        if (QueryFullProcessImageNameW(hProcess, 0, path.data(), &size))
        {
            path.resize(size);
        }
        else
        {
            path.clear();
        }
        CloseHandle(hProcess);
    }
    return path;
}

HICON ExtractIconFromExe(const std::wstring& exePath)
{
    if (exePath.empty()) return nullptr;
    
    HICON hIcon = nullptr;
    UINT iconCount = PrivateExtractIconsW(exePath.c_str(), 0, 32, 32, &hIcon, nullptr, 1, LR_DEFAULTCOLOR);
    if (iconCount > 0 && hIcon)
    {
        return hIcon;
    }
    return nullptr;
}

}

namespace touchrev::common::icon
{
winrt::Windows::UI::Xaml::Media::ImageSource HIconToImageSource(HICON hIcon)
{
    if (!hIcon) return nullptr;

    ICONINFO iconInfo{};
    if (!GetIconInfo(hIcon, &iconInfo))
    {
        return nullptr;
    }

    auto releaseIconInfo = [&]() noexcept {
        if (iconInfo.hbmColor)
        {
            DeleteObject(iconInfo.hbmColor);
            iconInfo.hbmColor = nullptr;
        }
        if (iconInfo.hbmMask)
        {
            DeleteObject(iconInfo.hbmMask);
            iconInfo.hbmMask = nullptr;
        }
    };

    BITMAP bmp{};
    int width = 32;
    int height = 32;
    if (iconInfo.hbmColor && GetObject(iconInfo.hbmColor, sizeof(bmp), &bmp))
    {
        width = bmp.bmWidth;
        height = bmp.bmHeight;
    }
    else if (iconInfo.hbmMask && GetObject(iconInfo.hbmMask, sizeof(bmp), &bmp))
    {
        width = bmp.bmWidth;
        height = std::max(1, static_cast<int>(bmp.bmHeight / 2));
    }

    if (width <= 0 || height <= 0)
    {
        releaseIconInfo();
        return nullptr;
    }

    int maskStride = 0;
    const std::vector<BYTE> maskBits = ReadIconMask(iconInfo.hbmMask, width, height, maskStride);

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem)
    {
        if (hdcScreen)
        {
            ReleaseDC(nullptr, hdcScreen);
        }
        releaseIconInfo();
        return nullptr;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hbmpMem = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!hbmpMem || !pBits)
    {
        DeleteDC(hdcMem);
        if (hdcScreen)
        {
            ReleaseDC(nullptr, hdcScreen);
        }
        releaseIconInfo();
        return nullptr;
    }

    HGDIOBJ hOldBmp = SelectObject(hdcMem, hbmpMem);
    if (!hOldBmp)
    {
        DeleteObject(hbmpMem);
        DeleteDC(hdcMem);
        if (hdcScreen)
        {
            ReleaseDC(nullptr, hdcScreen);
        }
        releaseIconInfo();
        return nullptr;
    }

    const size_t dibBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    std::memset(pBits, 0, dibBytes);

    const BOOL drawn = DrawIconEx(hdcMem, 0, 0, hIcon, width, height, 0, nullptr, DI_NORMAL);
    if (drawn)
    {
        NormalizeIconPixels(static_cast<BYTE*>(pBits), width, height, maskBits, maskStride);
    }

    namespace wg = winrt::Windows::Graphics::Imaging;
    wg::SoftwareBitmap softwareBitmap(wg::BitmapPixelFormat::Bgra8, width, height, wg::BitmapAlphaMode::Premultiplied);
    const bool copied = drawn && CopyPixelsToSoftwareBitmap(static_cast<const BYTE*>(pBits), width, height, softwareBitmap);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hbmpMem);
    DeleteDC(hdcMem);
    if (hdcScreen)
    {
        ReleaseDC(nullptr, hdcScreen);
    }
    releaseIconInfo();

    if (!copied)
    {
        return nullptr;
    }

    try
    {
        winrt::Windows::UI::Xaml::Media::Imaging::SoftwareBitmapSource source;
        source.SetBitmapAsync(softwareBitmap);
        return source;
    }
    catch (const winrt::hresult_error&)
    {
        return nullptr;
    }
}

winrt::Windows::UI::Xaml::Media::ImageSource GetWindowIcon(HWND hwnd)
{
    if (!hwnd)
    {
        return nullptr;
    }

    const std::wstring appUserModelId = GetAppUserModelIdForWindow(hwnd);
    if (!appUserModelId.empty())
    {
        auto source = GetAppUserModelIdImageSource(appUserModelId);
        if (source)
        {
            return source;
        }
    }

    HICON hIcon = nullptr;
    bool shouldDestroy = false;

    // 使用窗口公开的 HICON；部分 Win32 程序会在这里提供更准确的运行时图标。
    SendMessageTimeoutW(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 100, reinterpret_cast<PDWORD_PTR>(&hIcon));
    if (!hIcon)
    {
        SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG, 100, reinterpret_cast<PDWORD_PTR>(&hIcon));
    }
    if (!hIcon)
    {
        SendMessageTimeoutW(hwnd, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 100, reinterpret_cast<PDWORD_PTR>(&hIcon));
    }
    if (!hIcon)
    {
        hIcon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON));
    }
    if (!hIcon)
    {
        hIcon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM));
    }

    // 窗口没有公开图标时，退回到进程 exe 资源图标。
    if (!hIcon)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != 0)
        {
            std::wstring exePath = GetProcessPath(pid);
            if (!exePath.empty())
            {
                hIcon = ExtractIconFromExe(exePath);
                if (hIcon)
                {
                    shouldDestroy = true;
                }
            }
        }
    }

    if (!hIcon)
    {
        return nullptr;
    }

    auto source = HIconToImageSource(hIcon);

    if (shouldDestroy)
    {
        DestroyIcon(hIcon);
    }
    return source;
}
}
