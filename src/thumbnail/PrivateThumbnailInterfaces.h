#pragma once

#include <inspectable.h>
#include <unknwn.h>
#include <winrt/base.h>

#include <cstdint>
#include <string_view>

namespace touchrev::thumbnail
{
constexpr GUID kIDesktopThumbnailDevice = {
    0x096a23dc,
    0xa2b9,
    0x45f2,
    {0x9a, 0xd3, 0xcc, 0x6f, 0x69, 0xdd, 0xd7, 0x00},
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
}
