# PrivateThumbnailProbe

本目录是独立 PoC，不挂接根目录 `CMakeLists.txt`，也不依赖现有 `TouchRevGUI` target。

## 目标

验证当前系统上是否可以从普通本地进程走通私有 DWM / Composition 缩略图链路：

```text
RoActivateInstance("Windows.Internal.Shell.Multitasking.Desktop.DesktopThumbnailDevice")
  -> QueryInterface(IDesktopThumbnailDevice)
  -> CreateThumbnailFactoryForDesktopBackground / CreateThumbnailFactoryForWindow
  -> IThumbnailFactory
  -> IThumbnailFactory::CreateThumbnailVisual
  -> IDuplicateVisual::get_DuplicateVisual
  -> Windows.UI.Composition.IVisual
  -> ElementCompositionPreview.SetElementChildVisual(XAML root, visual)
```

程序不会注入 Explorer，也不会附加到现有主项目。

## 构建

```powershell
cmake -S . -B .\build\ninja-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build .\build\ninja-debug
```

## 运行

默认验证 desktop background factory 创建端：

```powershell
.\build\ninja-debug\PrivateThumbnailProbe.exe
```

验证指定窗口句柄创建端：

```powershell
.\build\ninja-debug\PrivateThumbnailProbe.exe --window 0x00123456
```

延迟后取前台窗口句柄：

```powershell
.\build\ninja-debug\PrivateThumbnailProbe.exe --foreground --delay 3000
```

验证 XAML Island + 普通 Composition visual：

```powershell
.\build\ninja-debug\PrivateThumbnailProbe.exe --xaml-smoke
```

验证 desktop background thumbnail visual 嵌入 XAML：

```powershell
.\build\ninja-debug\PrivateThumbnailProbe.exe --xaml-thumbnail
```

验证 foreground window thumbnail visual 嵌入 XAML：

```powershell
.\build\ninja-debug\PrivateThumbnailProbe.exe --xaml-thumbnail --foreground --delay 1000
```

自动关闭窗口，适合脚本验证：

```powershell
.\build\ninja-debug\PrivateThumbnailProbe.exe --xaml-thumbnail --auto-close 1000
```

提高 thumbnail 创建 backing size，用于验证 DPI / 清晰度影响：

```powershell
.\build\ninja-debug\PrivateThumbnailProbe.exe --xaml-thumbnail --window 0x00123456 --thumb-scale 2.0
```

`--thumb-scale` 不改变 XAML 中的显示尺寸，只把传给 `IThumbnailFactory::CreateThumbnailVisual` 的创建尺寸按 `displaySize * dpiScale * thumbScale` 放大。

## 已确认结果

当前机器上已确认：

- `DesktopThumbnailDevice` 可以从普通本地进程 activation。
- `IDesktopThumbnailDevice` / `IDesktopThumbnailDevice2` 可以 QI。
- `CreateThumbnailFactoryForDesktopBackground` 可以返回 `DesktopBackgroundThumbnailFactory`。
- `CreateThumbnailFactoryForWindow` 可以对普通 foreground HWND 返回 `WindowThumbnailFactory`。
- `IThumbnailFactory` 的 `CreateThumbnailVisual` 在当前 build 位于 vtable `+0x40`。
- `CreateThumbnailVisual` 返回 `IDuplicateVisual`。
- `IDuplicateVisual::get_DuplicateVisual` 返回 `Windows.UI.Composition.Visual`。
- 返回的 inner visual 可以通过 `ElementCompositionPreview.SetElementChildVisual` 挂到自定义 XAML root。

## 关键 ABI 记录

```text
IThumbnailFactory
  +0x30 unknown
  +0x38 unknown
  +0x40 CreateThumbnailVisual(ICompositor*, Size, ThumbnailProperties, IDuplicateVisual**)

IDuplicateVisual
  +0x30 get_DuplicateVisual(IInspectable**)
  +0x38 unknown
  +0x40 get_DuplicateVisualSurfaceBrush(IInspectable**)
  +0x48 get_Kind(int32_t*)
```

注意：`IDuplicateVisual::get_Kind` 当前对返回对象给出 `E_NOTIMPL`，但不影响 `get_DuplicateVisual` 路径。

## 当前边界

这个 PoC 已证明“自定义 XAML root 嵌入私有 thumbnail composition visual”可行。还没有处理：

- 缩略图 resize 后的重新布局。
- `ThumbnailProperties` 的完整字段语义。
- `KindChanged` / source 更新事件。
- 非对称圆角和 clip。
- 跨 Windows build 的 ABI 兼容。
