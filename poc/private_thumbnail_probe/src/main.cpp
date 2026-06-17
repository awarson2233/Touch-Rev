#include <windows.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

struct IDesktopWindowXamlSourceNative : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE AttachToWindow(HWND parentWnd) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_WindowHandle(HWND* hWnd) = 0;
};

namespace
{
constexpr GUID kIDesktopWindowXamlSourceNative = {
    0x3cbcf1bf,
    0x2f76,
    0x4e9c,
    {0x96, 0xab, 0xe8, 0x4b, 0x37, 0x97, 0x25, 0x54},
};

constexpr GUID kIDesktopThumbnailDevice = {
    0x096a23dc,
    0xa2b9,
    0x45f2,
    {0x9a, 0xd3, 0xcc, 0x6f, 0x69, 0xdd, 0xd7, 0x00},
};

constexpr GUID kIDesktopThumbnailDevice2 = {
    0x6b78fae3,
    0x94a7,
    0x563e,
    {0xb0, 0x72, 0xf8, 0x56, 0xf0, 0x40, 0xab, 0xf7},
};

constexpr GUID kIThumbnailFactory = {
    0x0a9c1f58,
    0x6aaf,
    0x4343,
    {0x8c, 0xfe, 0x10, 0x56, 0x58, 0x86, 0x16, 0x2a},
};

constexpr GUID kIDuplicateVisual = {
    0x84fa17ed,
    0x2003,
    0x44ed,
    {0xa3, 0x05, 0x5b, 0xff, 0x93, 0x22, 0xb7, 0x48},
};

constexpr std::wstring_view kDesktopThumbnailDeviceClass =
    L"Windows.Internal.Shell.Multitasking.Desktop.DesktopThumbnailDevice";

struct FoundationRect
{
    float X;
    float Y;
    float Width;
    float Height;
};

struct FoundationSize
{
    float Width;
    float Height;
};

struct ThumbnailProperties
{
    std::uint64_t value0 = 0;
    std::uint32_t value8 = 0;
};

struct IDesktopThumbnailDevice : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE CreateThumbnailFactoryForWindow(
        void* thumbnailWindow,
        IInspectable** factory) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateThumbnailFactoryForDesktopBackground(
        FoundationRect rect,
        IInspectable** factory) = 0;
};

struct IDesktopThumbnailDevice2 : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE CreateThumbnailFactoryForSnappedWindows(
        std::uint32_t count,
        const std::uint32_t* windowIds,
        FoundationRect rect,
        IInspectable** factory) = 0;
};

struct IThumbnailFactoryVisual : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_SourceSize(FoundationSize* size) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Bounds(FoundationRect* bounds) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateThumbnailVisual(
        IUnknown* compositor,
        FoundationSize size,
        ThumbnailProperties properties,
        IInspectable** duplicateVisual) = 0;
};

struct IDuplicateVisualProbe : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_DuplicateVisual(IInspectable** visual) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnknownSlot38() = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DuplicateVisualSurfaceBrush(IInspectable** brush) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Kind(std::int32_t* kind) = 0;
};

template <typename T>
class ComPtr
{
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : m_ptr(other.m_ptr)
    {
        other.m_ptr = nullptr;
    }

    ComPtr& operator=(ComPtr&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }
        return *this;
    }

    ~ComPtr()
    {
        Reset();
    }

    T* Get() const
    {
        return m_ptr;
    }

    T** Put()
    {
        Reset();
        return &m_ptr;
    }

    void Reset()
    {
        if (m_ptr != nullptr)
        {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }

    T* operator->() const
    {
        return m_ptr;
    }

    explicit operator bool() const
    {
        return m_ptr != nullptr;
    }

private:
    T* m_ptr = nullptr;
};

class HString
{
public:
    explicit HString(std::wstring_view value)
    {
        m_hr = WindowsCreateString(value.data(), static_cast<UINT32>(value.size()), &m_value);
    }

    HString(const HString&) = delete;
    HString& operator=(const HString&) = delete;

    ~HString()
    {
        if (m_value != nullptr)
        {
            WindowsDeleteString(m_value);
        }
    }

    HRESULT Result() const
    {
        return m_hr;
    }

    HSTRING Get() const
    {
        return m_value;
    }

private:
    HSTRING m_value = nullptr;
    HRESULT m_hr = E_FAIL;
};

class RoInitializeScope
{
public:
    explicit RoInitializeScope(RO_INIT_TYPE initType)
    {
        m_hr = RoInitialize(initType);
        m_shouldUninitialize = SUCCEEDED(m_hr);
    }

    RoInitializeScope(const RoInitializeScope&) = delete;
    RoInitializeScope& operator=(const RoInitializeScope&) = delete;

    ~RoInitializeScope()
    {
        if (m_shouldUninitialize)
        {
            RoUninitialize();
        }
    }

    HRESULT Result() const
    {
        return m_hr;
    }

private:
    HRESULT m_hr = E_FAIL;
    bool m_shouldUninitialize = false;
};

std::wstring FormatHResult(HRESULT hr)
{
    std::wstringstream stream;
    stream << L"0x" << std::uppercase << std::hex << std::setw(8) << std::setfill(L'0')
           << static_cast<unsigned long>(hr);
    return stream.str();
}

std::wstring FormatPointer(const void* ptr)
{
    std::wstringstream stream;
    stream << L"0x" << std::uppercase << std::hex << std::setw(sizeof(void*) * 2)
           << std::setfill(L'0') << reinterpret_cast<std::uintptr_t>(ptr);
    return stream.str();
}

std::wstring ModuleRvaForAddress(const void* address)
{
    HMODULE module = nullptr;
    if (address == nullptr || !GetModuleHandleExW(
                                  GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  reinterpret_cast<LPCWSTR>(address),
                                  &module))
    {
        return L"";
    }

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    const wchar_t* fileName = wcsrchr(path, L'\\');
    fileName = fileName == nullptr ? path : fileName + 1;

    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    std::wstringstream stream;
    stream << L" (" << fileName << L"+0x" << std::uppercase << std::hex << (value - base) << L")";
    return stream.str();
}

std::wstring HResultMessage(HRESULT hr)
{
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(
        flags,
        nullptr,
        static_cast<DWORD>(hr),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring message;
    if (length != 0 && buffer != nullptr)
    {
        message.assign(buffer, length);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n'))
        {
            message.pop_back();
        }
    }

    if (buffer != nullptr)
    {
        LocalFree(buffer);
    }

    return message.empty() ? L"Unknown error" : message;
}

void PrintHr(std::wstring_view operation, HRESULT hr)
{
    std::wcout << L"[" << (SUCCEEDED(hr) ? L"OK" : L"FAIL") << L"] " << operation << L" => "
               << FormatHResult(hr);
    if (FAILED(hr))
    {
        std::wcout << L" " << HResultMessage(hr);
    }
    std::wcout << std::endl;
    std::wcout.clear();
}

std::wstring HStringToWString(HSTRING value)
{
    if (value == nullptr)
    {
        return L"";
    }

    UINT32 length = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(value, &length);
    return raw == nullptr ? L"" : std::wstring(raw, length);
}

std::wstring RuntimeClassName(IInspectable* object)
{
    if (object == nullptr)
    {
        return L"<null>";
    }

    HSTRING name = nullptr;
    const HRESULT hr = object->GetRuntimeClassName(&name);
    if (FAILED(hr))
    {
        return L"<GetRuntimeClassName failed: " + FormatHResult(hr) + L">";
    }

    std::wstring result = HStringToWString(name);
    WindowsDeleteString(name);
    return result.empty() ? L"<empty>" : result;
}

void DumpInspectableIids(IInspectable* object)
{
    if (object == nullptr)
    {
        return;
    }

    ULONG count = 0;
    IID* iids = nullptr;
    const HRESULT hr = object->GetIids(&count, &iids);
    PrintHr(L"IInspectable::GetIids", hr);
    if (FAILED(hr))
    {
        return;
    }

    std::wcout << L"    IID count: " << count << L"\n";
    for (ULONG index = 0; index < count; ++index)
    {
        LPOLESTR guidString = nullptr;
        if (SUCCEEDED(StringFromIID(iids[index], &guidString)) && guidString != nullptr)
        {
            std::wcout << L"    [" << index << L"] " << guidString << L"\n";
            CoTaskMemFree(guidString);
        }
    }

    CoTaskMemFree(iids);
}

void DumpVTable(IUnknown* object, int slotCount)
{
    if (object == nullptr || slotCount <= 0)
    {
        return;
    }

    auto** vtable = *reinterpret_cast<void***>(object);
    std::wcout << L"    object : " << FormatPointer(object) << L"\n";
    std::wcout << L"    vtable : " << FormatPointer(vtable) << L"\n";
    for (int index = 0; index < slotCount; ++index)
    {
        std::wcout << L"    slot[" << std::setw(2) << index << L"] @ +0x" << std::uppercase
                   << std::hex << std::setw(2) << std::setfill(L'0') << (index * sizeof(void*))
                   << std::dec << std::setfill(L' ') << L" -> " << FormatPointer(vtable[index])
                   << ModuleRvaForAddress(vtable[index]) << L"\n";
    }
}

FoundationRect GetPrimaryWorkAreaRect()
{
    RECT rect{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &rect, 0))
    {
        rect = {0, 0, 640, 480};
    }

    return FoundationRect{
        static_cast<float>(rect.left),
        static_cast<float>(rect.top),
        static_cast<float>(rect.right - rect.left),
        static_cast<float>(rect.bottom - rect.top),
    };
}

std::wstring RectToString(const FoundationRect& rect)
{
    std::wstringstream stream;
    stream << L"{" << rect.X << L", " << rect.Y << L", " << rect.Width << L", " << rect.Height
           << L"}";
    return stream.str();
}

std::uintptr_t ParseInteger(std::wstring_view text)
{
    std::wstring copy(text);
    wchar_t* end = nullptr;
    const unsigned long long value = std::wcstoull(copy.c_str(), &end, 0);
    if (end == copy.c_str() || (end != nullptr && *end != L'\0'))
    {
        throw std::runtime_error("invalid integer argument");
    }
    return static_cast<std::uintptr_t>(value);
}

double ParseDouble(std::wstring_view text)
{
    std::wstring copy(text);
    wchar_t* end = nullptr;
    const double value = std::wcstod(copy.c_str(), &end);
    if (end == copy.c_str() || (end != nullptr && *end != L'\0') || !std::isfinite(value))
    {
        throw std::runtime_error("invalid floating-point argument");
    }
    return value;
}

enum class ProbeMode
{
    DesktopBackground,
    Window,
    ForegroundWindow,
    XamlSmoke,
    XamlThumbnail,
};

struct Options
{
    ProbeMode mode = ProbeMode::DesktopBackground;
    HWND hwnd = nullptr;
    DWORD delayMs = 0;
    DWORD autoCloseMs = 0;
    int slotCount = 12;
    double thumbScale = 1.0;
    bool xamlThumbnail = false;
};

void PrintUsage()
{
    std::wcout << LR"(PrivateThumbnailProbe

Usage:
  PrivateThumbnailProbe.exe [--desktop]
  PrivateThumbnailProbe.exe --window 0x00123456
  PrivateThumbnailProbe.exe --foreground [--delay 3000]
  PrivateThumbnailProbe.exe --xaml-smoke
  PrivateThumbnailProbe.exe --xaml-thumbnail [--desktop | --window 0x00123456 | --foreground]

Options:
  --desktop        Create desktop background IThumbnailFactory. Default.
  --window <hwnd>  Pass a HWND-like value to CreateThumbnailFactoryForWindow.
  --foreground     Use GetForegroundWindow() after optional delay.
  --xaml-smoke     Create a Win32 + DesktopWindowXamlSource host and attach a SpriteVisual.
  --xaml-thumbnail Create the XAML host, call IThumbnailFactory::CreateThumbnailVisual, and attach if it QIs to IVisual.
  --delay <ms>     Sleep before capturing foreground window.
  --auto-close <ms> Close the XAML smoke window automatically after this delay.
  --thumb-scale <n> Multiply CreateThumbnailVisual backing size after DPI scaling. Default: 1.0.
  --slots <n>      Dump first n vtable slots for returned factory. Default: 12.
  --help           Show this text.

Exit code:
  0 = activation + factory creation + IThumbnailFactory QI succeeded.
  1 = one of those validation steps failed.
)";
}

Options ParseOptions(int argc, wchar_t** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index)
    {
        const std::wstring_view arg(argv[index]);
        if (arg == L"--help" || arg == L"-h")
        {
            PrintUsage();
            std::exit(0);
        }
        if (arg == L"--desktop")
        {
            options.mode = ProbeMode::DesktopBackground;
            continue;
        }
        if (arg == L"--foreground")
        {
            options.mode = ProbeMode::ForegroundWindow;
            continue;
        }
        if (arg == L"--xaml-smoke")
        {
            options.mode = ProbeMode::XamlSmoke;
            continue;
        }
        if (arg == L"--xaml-thumbnail")
        {
            options.xamlThumbnail = true;
            continue;
        }
        if (arg == L"--window")
        {
            if (index + 1 >= argc)
            {
                throw std::runtime_error("--window requires a HWND value");
            }
            options.mode = ProbeMode::Window;
            options.hwnd = reinterpret_cast<HWND>(ParseInteger(argv[++index]));
            continue;
        }
        if (arg == L"--delay")
        {
            if (index + 1 >= argc)
            {
                throw std::runtime_error("--delay requires milliseconds");
            }
            options.delayMs = static_cast<DWORD>(ParseInteger(argv[++index]));
            continue;
        }
        if (arg == L"--auto-close")
        {
            if (index + 1 >= argc)
            {
                throw std::runtime_error("--auto-close requires milliseconds");
            }
            options.autoCloseMs = static_cast<DWORD>(ParseInteger(argv[++index]));
            continue;
        }
        if (arg == L"--thumb-scale")
        {
            if (index + 1 >= argc)
            {
                throw std::runtime_error("--thumb-scale requires a scale value");
            }
            options.thumbScale = ParseDouble(argv[++index]);
            if (options.thumbScale < 0.25 || options.thumbScale > 4.0)
            {
                throw std::runtime_error("--thumb-scale must be between 0.25 and 4.0");
            }
            continue;
        }
        if (arg == L"--slots")
        {
            if (index + 1 >= argc)
            {
                throw std::runtime_error("--slots requires a count");
            }
            options.slotCount = static_cast<int>(ParseInteger(argv[++index]));
            if (options.slotCount < 0 || options.slotCount > 64)
            {
                throw std::runtime_error("--slots must be between 0 and 64");
            }
            continue;
        }

        throw std::runtime_error("unknown argument");
    }
    return options;
}

HRESULT ActivateDesktopThumbnailDevice(ComPtr<IInspectable>& device);
HRESULT QueryDevice(IInspectable* device, ComPtr<IDesktopThumbnailDevice>& thumbnailDevice);
HRESULT QueryThumbnailFactory(IInspectable* factory, ComPtr<IInspectable>& typedFactory);
HRESULT CreateFactory(const Options& options, IDesktopThumbnailDevice* device, ComPtr<IInspectable>& factory);

class XamlSmokeWindow
{
public:
    bool Initialize(HINSTANCE instance, const Options& options)
    {
        m_options = options;
        const wchar_t* className = L"PrivateThumbnailProbe.XamlSmokeWindow";
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &XamlSmokeWindow::WndProc;
        wc.lpszClassName = className;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

        if (RegisterClassExW(&wc) == 0)
        {
            PrintHr(L"RegisterClassExW", HRESULT_FROM_WIN32(GetLastError()));
            return false;
        }

        m_hwnd = CreateWindowExW(
            0,
            className,
            L"PrivateThumbnailProbe - XAML smoke",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            900,
            560,
            nullptr,
            nullptr,
            instance,
            this);
        if (m_hwnd == nullptr)
        {
            PrintHr(L"CreateWindowExW", HRESULT_FROM_WIN32(GetLastError()));
            return false;
        }

        try
        {
            InitializeXamlIsland();
            AttachSmokeVisual();
            if (m_options.xamlThumbnail)
            {
                AttachThumbnailVisual();
            }
        }
        catch (const winrt::hresult_error& ex)
        {
            PrintHr(L"Initialize XAML smoke", ex.code());
            std::wcout << L"[FAIL] " << ex.message().c_str() << std::endl;
            return false;
        }

        if (m_options.autoCloseMs != 0)
        {
            SetTimer(m_hwnd, 1, m_options.autoCloseMs, nullptr);
            std::wcout << L"[INFO] Auto-close timer set to " << m_options.autoCloseMs << L" ms." << std::endl;
        }

        ShowWindow(m_hwnd, SW_SHOWDEFAULT);
        UpdateWindow(m_hwnd);
        return true;
    }

    int Run()
    {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<XamlSmokeWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = reinterpret_cast<XamlSmokeWindow*>(create->lpCreateParams);
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }

        if (self != nullptr)
        {
            return self->HandleMessage(message, wParam, lParam);
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_SIZE:
            Resize(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_TIMER:
            if (wParam == 1)
            {
                KillTimer(m_hwnd, 1);
                DestroyWindow(m_hwnd);
                return 0;
            }
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(m_hwnd, message, wParam, lParam);
        }

        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }

    void InitializeXamlIsland()
    {
        using namespace winrt::Windows::UI::Xaml;
        using namespace winrt::Windows::UI::Xaml::Controls;
        using namespace winrt::Windows::UI::Xaml::Hosting;
        using namespace winrt::Windows::UI::Xaml::Media;

        m_xamlManager = WindowsXamlManager::InitializeForCurrentThread();
        m_xamlSource = DesktopWindowXamlSource();

        winrt::com_ptr<IDesktopWindowXamlSourceNative> nativeSource;
        auto* xamlSourceAbi = reinterpret_cast<IUnknown*>(winrt::get_abi(m_xamlSource));
        winrt::check_hresult(xamlSourceAbi->QueryInterface(
            kIDesktopWindowXamlSourceNative,
            nativeSource.put_void()));
        winrt::check_hresult(nativeSource->AttachToWindow(m_hwnd));
        winrt::check_hresult(nativeSource->get_WindowHandle(&m_xamlHwnd));

        RECT client{};
        GetClientRect(m_hwnd, &client);
        SetWindowPos(
            m_xamlHwnd,
            nullptr,
            0,
            0,
            client.right - client.left,
            client.bottom - client.top,
            SWP_SHOWWINDOW);

        Grid root;
        root.Background(SolidColorBrush(winrt::Windows::UI::Color{255, 18, 18, 18}));
        m_root = root;
        m_xamlSource.Content(root);

        std::wcout << L"[OK] DesktopWindowXamlSource attached: child HWND = "
                   << FormatPointer(m_xamlHwnd) << std::endl;
    }

    void AttachSmokeVisual()
    {
        using namespace winrt::Windows::Foundation::Numerics;
        using namespace winrt::Windows::UI;
        using namespace winrt::Windows::UI::Composition;
        using namespace winrt::Windows::UI::Xaml::Hosting;

        Visual rootVisual = ElementCompositionPreview::GetElementVisual(m_root);
        m_compositor = rootVisual.Compositor();
        m_smokeVisual = m_compositor.CreateSpriteVisual();
        m_smokeVisual.Brush(m_compositor.CreateColorBrush(Color{255, 0, 122, 204}));
        ElementCompositionPreview::SetElementChildVisual(m_root, m_smokeVisual);

        RECT client{};
        GetClientRect(m_hwnd, &client);
        Resize(client.right - client.left, client.bottom - client.top);

        std::wcout << L"[OK] SpriteVisual attached through ElementCompositionPreview." << std::endl;
        std::wcout << L"[INFO] Close the window to end --xaml-smoke." << std::endl;
    }

    void AttachThumbnailVisual()
    {
        using namespace winrt::Windows::UI::Composition;
        using namespace winrt::Windows::UI::Xaml::Hosting;

        std::wcout << L"[STEP] Creating private thumbnail visual..." << std::endl;

        ComPtr<IInspectable> deviceInspectable;
        HRESULT hr = ActivateDesktopThumbnailDevice(deviceInspectable);
        PrintHr(L"RoActivateInstance(DesktopThumbnailDevice)", hr);
        if (FAILED(hr))
        {
            return;
        }

        ComPtr<IDesktopThumbnailDevice> device;
        hr = QueryDevice(deviceInspectable.Get(), device);
        PrintHr(L"QueryInterface(IDesktopThumbnailDevice)", hr);
        if (FAILED(hr))
        {
            return;
        }

        Options sourceOptions = m_options;
        if (sourceOptions.mode == ProbeMode::XamlSmoke || sourceOptions.mode == ProbeMode::XamlThumbnail)
        {
            sourceOptions.mode = ProbeMode::DesktopBackground;
        }

        ComPtr<IInspectable> factory;
        hr = CreateFactory(sourceOptions, device.Get(), factory);
        PrintHr(L"Create thumbnail factory", hr);
        if (FAILED(hr))
        {
            return;
        }

        std::wcout << L"[INFO] Factory runtime class: " << RuntimeClassName(factory.Get()) << std::endl;

        ComPtr<IThumbnailFactoryVisual> visualFactory;
        hr = factory->QueryInterface(kIThumbnailFactory, reinterpret_cast<void**>(visualFactory.Put()));
        PrintHr(L"QueryInterface(IThumbnailFactory visual ABI)", hr);
        if (FAILED(hr))
        {
            return;
        }

        FoundationSize sourceSize{};
        hr = visualFactory->get_SourceSize(&sourceSize);
        PrintHr(L"IThumbnailFactory::get_SourceSize", hr);
        if (SUCCEEDED(hr))
        {
            std::wcout << L"[INFO] Source size = " << sourceSize.Width << L"x" << sourceSize.Height << std::endl;
        }

        FoundationRect sourceBounds{};
        hr = visualFactory->get_Bounds(&sourceBounds);
        PrintHr(L"IThumbnailFactory::get_Bounds", hr);
        if (SUCCEEDED(hr))
        {
            std::wcout << L"[INFO] Source bounds = " << RectToString(sourceBounds) << std::endl;
        }

        RECT client{};
        GetClientRect(m_hwnd, &client);
        constexpr float inset = 36.0f;
        const float visualWidth = static_cast<float>(client.right - client.left) - inset * 2.0f;
        const float visualHeight = static_cast<float>(client.bottom - client.top) - inset * 2.0f;
        const FoundationSize displaySize{
            visualWidth > 0.0f ? visualWidth : 1.0f,
            visualHeight > 0.0f ? visualHeight : 1.0f};
        const double dpiScale = static_cast<double>(GetDpiForWindow(m_hwnd)) / 96.0;
        const FoundationSize createSize{
            static_cast<float>(static_cast<double>(displaySize.Width) * dpiScale * m_options.thumbScale),
            static_cast<float>(static_cast<double>(displaySize.Height) * dpiScale * m_options.thumbScale)};
        std::wcout << L"[INFO] Thumbnail display size = " << displaySize.Width << L"x" << displaySize.Height << std::endl;
        std::wcout << L"[INFO] DPI scale = " << dpiScale << L", thumb scale = " << m_options.thumbScale << std::endl;
        std::wcout << L"[INFO] Requested thumbnail create size = " << createSize.Width << L"x" << createSize.Height << std::endl;

        ThumbnailProperties properties{};
        properties.value8 = 0x101;
        std::wcout << L"[INFO] ThumbnailProperties.value8 = 0x" << std::hex << properties.value8 << std::dec << std::endl;

        m_duplicateVisual.Reset();
        auto* compositorAbi = reinterpret_cast<IUnknown*>(winrt::get_abi(m_compositor));
        hr = visualFactory->CreateThumbnailVisual(compositorAbi, createSize, properties, m_duplicateVisual.Put());
        PrintHr(L"IThumbnailFactory::CreateThumbnailVisual", hr);
        if (FAILED(hr))
        {
            return;
        }

        if (!m_duplicateVisual)
        {
            std::wcout << L"[WARN] CreateThumbnailVisual returned S_OK but produced a null output pointer. "
                       << L"The local ABI declaration is still incomplete." << std::endl;
            return;
        }

        std::wcout << L"[INFO] Duplicate visual runtime class: "
                   << RuntimeClassName(m_duplicateVisual.Get()) << std::endl;
        DumpInspectableIids(m_duplicateVisual.Get());
        DumpVTable(m_duplicateVisual.Get(), 16);

        ComPtr<IDuplicateVisualProbe> m_duplicateVisualProbe;
        hr = m_duplicateVisual->QueryInterface(kIDuplicateVisual, reinterpret_cast<void**>(m_duplicateVisualProbe.Put()));
        PrintHr(L"QueryInterface(IDuplicateVisual)", hr);
        if (SUCCEEDED(hr))
        {
            std::int32_t kind = -1;
            hr = m_duplicateVisualProbe->get_Kind(&kind);
            PrintHr(L"IDuplicateVisual::get_Kind", hr);
            if (SUCCEEDED(hr))
            {
                std::wcout << L"[INFO] DuplicateVisual kind = " << kind << std::endl;
            }

            ComPtr<IInspectable> surfaceBrush;
            hr = m_duplicateVisualProbe->get_DuplicateVisualSurfaceBrush(surfaceBrush.Put());
            PrintHr(L"IDuplicateVisual::get_DuplicateVisualSurfaceBrush", hr);
            if (SUCCEEDED(hr) && surfaceBrush)
            {
                std::wcout << L"[INFO] Surface brush runtime class: "
                           << RuntimeClassName(surfaceBrush.Get()) << std::endl;
                DumpInspectableIids(surfaceBrush.Get());

                void* rawBrush = nullptr;
                hr = surfaceBrush->QueryInterface(
                    winrt::guid_of<winrt::Windows::UI::Composition::ICompositionBrush>(),
                    &rawBrush);
                PrintHr(L"Surface brush QueryInterface(Windows.UI.Composition.ICompositionBrush)", hr);
                if (SUCCEEDED(hr))
                {
                    CompositionBrush brush{nullptr};
                    winrt::attach_abi(brush, rawBrush);

                    SpriteVisual brushVisual = m_compositor.CreateSpriteVisual();
                    brushVisual.Brush(brush);
                    ElementCompositionPreview::SetElementChildVisual(m_root, brushVisual);
                    m_thumbnailBrushVisual = brushVisual;

                    RECT client{};
                    GetClientRect(m_hwnd, &client);
                    Resize(client.right - client.left, client.bottom - client.top);

                    std::wcout << L"[OK] Surface brush SpriteVisual attached to XAML root." << std::endl;
                    return;
                }
            }

            ComPtr<IInspectable> innerVisual;
            hr = m_duplicateVisualProbe->get_DuplicateVisual(innerVisual.Put());
            PrintHr(L"IDuplicateVisual::get_DuplicateVisual", hr);
            if (SUCCEEDED(hr) && innerVisual)
            {
                std::wcout << L"[INFO] Inner visual runtime class: "
                           << RuntimeClassName(innerVisual.Get()) << std::endl;
                DumpInspectableIids(innerVisual.Get());

                void* rawInnerVisual = nullptr;
                hr = innerVisual->QueryInterface(
                    winrt::guid_of<winrt::Windows::UI::Composition::IVisual>(),
                    &rawInnerVisual);
                PrintHr(L"Inner QueryInterface(Windows.UI.Composition.IVisual)", hr);
                if (SUCCEEDED(hr))
                {
                    Visual thumbnailVisual{nullptr};
                    winrt::attach_abi(thumbnailVisual, rawInnerVisual);
                    ElementCompositionPreview::SetElementChildVisual(m_root, thumbnailVisual);
                    m_thumbnailVisual = thumbnailVisual;

                    RECT client{};
                    GetClientRect(m_hwnd, &client);
                    Resize(client.right - client.left, client.bottom - client.top);

                    std::wcout << L"[OK] Inner IVisual attached to XAML root." << std::endl;
                    return;
                }
            }
        }

        void* rawVisual = nullptr;
        hr = m_duplicateVisual->QueryInterface(
            winrt::guid_of<winrt::Windows::UI::Composition::IVisual>(),
            &rawVisual);
        PrintHr(L"QueryInterface(Windows.UI.Composition.IVisual)", hr);
        if (FAILED(hr))
        {
            std::wcout << L"[WARN] Returned object is not directly attachable as IVisual." << std::endl;
            return;
        }

        Visual thumbnailVisual{nullptr};
        winrt::attach_abi(thumbnailVisual, rawVisual);
        ElementCompositionPreview::SetElementChildVisual(m_root, thumbnailVisual);
        m_thumbnailVisual = thumbnailVisual;

        Resize(client.right - client.left, client.bottom - client.top);

        std::wcout << L"[OK] Thumbnail IVisual attached to XAML root." << std::endl;
    }

    void Resize(int width, int height)
    {
        if (m_xamlHwnd != nullptr)
        {
            SetWindowPos(m_xamlHwnd, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_SHOWWINDOW);
        }

        const float inset = 36.0f;
        const float visualWidth = static_cast<float>(width) - inset * 2.0f;
        const float visualHeight = static_cast<float>(height) - inset * 2.0f;
        const winrt::Windows::Foundation::Numerics::float3 offset{inset, inset, 0.0f};
        const winrt::Windows::Foundation::Numerics::float2 size{
            visualWidth > 0.0f ? visualWidth : 0.0f,
            visualHeight > 0.0f ? visualHeight : 0.0f};

        if (m_smokeVisual != nullptr)
        {
            m_smokeVisual.Offset(offset);
            m_smokeVisual.Size(size);
        }

        if (m_thumbnailVisual != nullptr)
        {
            m_thumbnailVisual.Offset(offset);
            m_thumbnailVisual.Size(size);
        }

        if (m_thumbnailBrushVisual != nullptr)
        {
            m_thumbnailBrushVisual.Offset(offset);
            m_thumbnailBrushVisual.Size(size);
        }
    }

    HWND m_hwnd = nullptr;
    HWND m_xamlHwnd = nullptr;
    winrt::Windows::UI::Xaml::Hosting::WindowsXamlManager m_xamlManager{nullptr};
    winrt::Windows::UI::Xaml::Hosting::DesktopWindowXamlSource m_xamlSource{nullptr};
    winrt::Windows::UI::Xaml::Controls::Grid m_root{nullptr};
    winrt::Windows::UI::Composition::Compositor m_compositor{nullptr};
    winrt::Windows::UI::Composition::SpriteVisual m_smokeVisual{nullptr};
    winrt::Windows::UI::Composition::Visual m_thumbnailVisual{nullptr};
    winrt::Windows::UI::Composition::SpriteVisual m_thumbnailBrushVisual{nullptr};
    ComPtr<IInspectable> m_duplicateVisual;
    Options m_options{};
};

int RunXamlSmokeProbe(const Options& options)
{
    HINSTANCE instance = GetModuleHandleW(nullptr);
    XamlSmokeWindow window;
    if (!window.Initialize(instance, options))
    {
        return 1;
    }
    return window.Run();
}

HRESULT ActivateDesktopThumbnailDevice(ComPtr<IInspectable>& device)
{
    HString className(kDesktopThumbnailDeviceClass);
    if (FAILED(className.Result()))
    {
        return className.Result();
    }

    return RoActivateInstance(className.Get(), device.Put());
}

HRESULT QueryDevice(IInspectable* device, ComPtr<IDesktopThumbnailDevice>& thumbnailDevice)
{
    if (device == nullptr)
    {
        return E_POINTER;
    }

    return device->QueryInterface(
        kIDesktopThumbnailDevice,
        reinterpret_cast<void**>(thumbnailDevice.Put()));
}

HRESULT QueryDevice2(IInspectable* device, ComPtr<IDesktopThumbnailDevice2>& thumbnailDevice2)
{
    if (device == nullptr)
    {
        return E_POINTER;
    }

    return device->QueryInterface(
        kIDesktopThumbnailDevice2,
        reinterpret_cast<void**>(thumbnailDevice2.Put()));
}

HRESULT QueryThumbnailFactory(IInspectable* factory, ComPtr<IInspectable>& typedFactory)
{
    if (factory == nullptr)
    {
        return E_POINTER;
    }

    return factory->QueryInterface(kIThumbnailFactory, reinterpret_cast<void**>(typedFactory.Put()));
}

HRESULT CreateFactory(
    const Options& options,
    IDesktopThumbnailDevice* device,
    ComPtr<IInspectable>& factory)
{
    if (device == nullptr)
    {
        return E_POINTER;
    }

    if (options.mode == ProbeMode::DesktopBackground)
    {
        const FoundationRect rect = GetPrimaryWorkAreaRect();
        std::wcout << L"[INFO] CreateThumbnailFactoryForDesktopBackground rect = "
                   << RectToString(rect) << L"\n";
        return device->CreateThumbnailFactoryForDesktopBackground(rect, factory.Put());
    }

    HWND hwnd = options.hwnd;
    if (options.mode == ProbeMode::ForegroundWindow)
    {
        if (options.delayMs != 0)
        {
            std::wcout << L"[INFO] Sleeping " << options.delayMs
                       << L" ms before GetForegroundWindow().\n";
            Sleep(options.delayMs);
        }
        hwnd = GetForegroundWindow();
    }

    std::wcout << L"[INFO] CreateThumbnailFactoryForWindow hwnd = " << FormatPointer(hwnd)
               << L"\n";
    if (hwnd == nullptr || !IsWindow(hwnd))
    {
        std::wcout << L"[WARN] HWND is null or IsWindow returned false. The private API may fail.\n";
    }

    return device->CreateThumbnailFactoryForWindow(hwnd, factory.Put());
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wcout << L"PrivateThumbnailProbe - internal DesktopThumbnailDevice validation\n";
    std::wcout << L"This uses private Windows ABI. Results are build-specific.\n\n";

    Options options;
    try
    {
        options = ParseOptions(argc, argv);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Argument error: " << ex.what() << "\n";
        PrintUsage();
        return 1;
    }

    if (options.mode == ProbeMode::XamlSmoke || options.xamlThumbnail)
    {
        RoInitializeScope ro(RO_INIT_SINGLETHREADED);
        PrintHr(L"RoInitialize(RO_INIT_SINGLETHREADED)", ro.Result());
        if (FAILED(ro.Result()))
        {
            return 1;
        }
        return RunXamlSmokeProbe(options);
    }

    RoInitializeScope ro(RO_INIT_MULTITHREADED);
    PrintHr(L"RoInitialize(RO_INIT_MULTITHREADED)", ro.Result());
    if (FAILED(ro.Result()))
    {
        return 1;
    }

    std::wcout << L"[STEP] Activating DesktopThumbnailDevice...\n";
    std::wcout.flush();

    ComPtr<IInspectable> deviceInspectable;
    HRESULT hr = ActivateDesktopThumbnailDevice(deviceInspectable);
    std::wcout << L"[STEP] Activation call returned.\n";
    PrintHr(L"RoActivateInstance(DesktopThumbnailDevice)", hr);
    if (FAILED(hr))
    {
        return 1;
    }

    std::wcout << L"[INFO] Device runtime class: "
               << RuntimeClassName(deviceInspectable.Get()) << L"\n";
    DumpInspectableIids(deviceInspectable.Get());

    ComPtr<IDesktopThumbnailDevice> device;
    hr = QueryDevice(deviceInspectable.Get(), device);
    PrintHr(L"QueryInterface(IDesktopThumbnailDevice)", hr);
    if (FAILED(hr))
    {
        return 1;
    }

    ComPtr<IDesktopThumbnailDevice2> device2;
    hr = QueryDevice2(deviceInspectable.Get(), device2);
    PrintHr(L"QueryInterface(IDesktopThumbnailDevice2)", hr);

    ComPtr<IInspectable> factory;
    hr = CreateFactory(options, device.Get(), factory);
    PrintHr(L"Create thumbnail factory", hr);
    if (FAILED(hr))
    {
        return 1;
    }

    std::wcout << L"[INFO] Factory runtime class: " << RuntimeClassName(factory.Get()) << L"\n";
    DumpInspectableIids(factory.Get());

    ComPtr<IInspectable> typedFactory;
    hr = QueryThumbnailFactory(factory.Get(), typedFactory);
    PrintHr(L"QueryInterface(IThumbnailFactory)", hr);
    if (FAILED(hr))
    {
        return 1;
    }

    std::wcout << L"[INFO] IThumbnailFactory pointer: " << FormatPointer(typedFactory.Get()) << L"\n";
    DumpVTable(typedFactory.Get(), options.slotCount);

    std::wcout << L"\n[PASS] Factory creation path is callable in this process.\n";
    std::wcout << L"[NEXT] To prove XAML embedding, add CreateThumbnailVisual + IDuplicateVisual/IVisual probe.\n";
    return 0;
}
