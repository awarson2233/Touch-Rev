# Gesture System Comparison

## 结论

当前 `Touch-Rev-GUI` 的手势识别稳定性差，主因不是阈值本身，而是状态模型缺少 `Touch-Logic` 已验证的三层结构：

```text
RawTouchInput frame
  -> ContactTracker：维护触点生命周期、起点、上一帧、速度、消失补偿
  -> GestureSession：Pending 稳定窗口、Peak/Locked finger count、空间聚类、掉指容忍
  -> Recognizer：LongPress / Tap 等独立识别器
```

当前实现把这些逻辑都压在 `ThreeFingerGestureRecognizer::ProcessFrame()` 一个状态机内，导致三类不稳定：

- 手指落下不同步时立即按“正好 3 指”判定，容易错过候选期。
- 手势激活后只做 ID 匹配容错，没有 `LockedFingerCount` 与 quorum 规则。
- Tap 与 LongPress 共用同一候选状态，结束时依赖最后一帧数据，和 `Touch-Logic` 的 session finalization 差异较大。

## 当前项目实际路径

### 输入来源

`Touch-Rev-GUI` 使用 Raw Input HID 注册触摸屏和触摸板：

- [RawTouchInput.cpp:50-64](src/input/raw/RawTouchInput.cpp#L50-L64)
- [RawTouchInput.cpp:82-117](src/input/raw/RawTouchInput.cpp#L82-L117)

Raw frame 构造逻辑会把 HID contact 转成 `RawTouchInput::TouchPoint`，并在 frame sync 时补释放事件：

- [RawTouchInput.cpp:163-250](src/input/raw/RawTouchInput.cpp#L163-L250)
- [RawTouchInput.cpp:445-517](src/input/raw/RawTouchInput.cpp#L445-L517)

### 手势识别入口

`InputController` 每个 `WM_INPUT` 调用 `ThreeFingerGestureRecognizer::ProcessFrame()`，定时器调用 `Tick()` 推进静止长按：

- [InputController.cpp:303-320](src/input/InputController.cpp#L303-L320)
- [MainWindow.cpp:259-279](src/ui/MainWindow.cpp#L259-L279)

手势结果再映射到 app switcher 行为：

- `DoubleTap` -> `ShowSwitcher`
- `LongPressStarted` -> `LongPressBegin`
- `LongPressMoved` -> `LongPressMove`
- `LongPressEnded` -> `LongPressEnd`

源码位置：

- [InputController.cpp:323-471](src/input/InputController.cpp#L323-L471)
- [MainWindow.cpp:694-738](src/ui/MainWindow.cpp#L694-L738)

### 当前识别器模型

当前 `ThreeFingerGestureRecognizer` 只有 4 个状态：`Idle / Candidate / Tracking / LongPressActive`：

- [ThreeFingerGestureRecognizer.h:59-67](src/input/gesture/ThreeFingerGestureRecognizer.h#L59-L67)
- [ThreeFingerGestureRecognizer.cpp:24-220](src/input/gesture/ThreeFingerGestureRecognizer.cpp#L24-L220)

它的核心限制是：

1. `TryExtractThreeFingers()` 要求当前活动手指数必须正好等于 3。
   - [ThreeFingerGestureRecognizer.cpp:396-405](src/input/gesture/ThreeFingerGestureRecognizer.cpp#L396-L405)
2. Candidate 直接绑定初始 3 个 ID，后续主要按 ID 匹配。
   - [ThreeFingerGestureRecognizer.cpp:128-167](src/input/gesture/ThreeFingerGestureRecognizer.cpp#L128-L167)
   - [ThreeFingerGestureRecognizer.cpp:407-448](src/input/gesture/ThreeFingerGestureRecognizer.cpp#L407-L448)
3. LongPressActive 掉指时用临时降级模式继续跟踪，但这是结果层容错，不是 session 层 quorum。
   - [ThreeFingerGestureRecognizer.cpp:49-98](src/input/gesture/ThreeFingerGestureRecognizer.cpp#L49-L98)
4. Tap 在 Candidate 结束时完成，缺少稳定窗口和 session finalization。
   - [ThreeFingerGestureRecognizer.cpp:538-573](src/input/gesture/ThreeFingerGestureRecognizer.cpp#L538-L573)

## Touch-Logic 已验证路径

### ContactTracker：先建立触点生命周期

`Touch-Logic` 不让 recognizer 直接消费原始 frame，而是先由 `ContactTracker` 输出 `ContactStarted / ContactUpdated / ContactEnded`：

- [ContactTracker.cs:89-214](../Touch-Rev/Touch-Engine/Input/ContactTracker.cs#L89-L214)

每个 `TouchContact` 保存：

- start/current/previous position
- frame delta
- total delta
- velocity
- timestamp

源码位置：

- [TouchContact.cs:33-230](../Touch-Rev/Touch-Engine/Core/TouchContact.cs#L33-L230)

这让 recognizer 不需要猜“这一帧有没有完整收到所有手指”，而是面向完整生命周期状态。

### GestureSession：先锁定手指数，再识别

`GestureSession` 的 Pending -> Active 激活条件不是“当前正好 3 指”，而是组合条件：

- Peak finger count 达到最小值。
- 指数稳定窗口达到阈值。
- 全部抬起时为 tap fast-track。
- 快速滑动超过阈值时 early lock。
- 3 指纵向移动额外等待，避免抢 4 指。
- 空间聚类必须有效。

源码位置：

- [GestureSession.cs:248-339](../Touch-Rev/Touch-Logic/Gesture/GestureSession.cs#L248-L339)
- [GestureSession.cs:545-570](../Touch-Rev/Touch-Logic/Gesture/GestureSession.cs#L545-L570)

激活后 session 会锁定 `LockedFingerCount`，并记录 anchor：

- [GestureSession.cs:344-354](../Touch-Rev/Touch-Logic/Gesture/GestureSession.cs#L344-L354)

掉指容忍不是“凑够 2 个 ID 继续算”，而是按 `LockedFingerCount` / quorum 和时间窗口判定：

- [GestureSession.cs:385-417](../Touch-Rev/Touch-Logic/Gesture/GestureSession.cs#L385-L417)
- [GestureEngine.cs:620-634](../Touch-Rev/Touch-Logic/Gesture/GestureEngine.cs#L620-L634)

### GestureContext：统一派生数据

Recognizer 只读 `GestureContext`，里面已有中心点、平均位移、frame delta、速度、方向和 anchor delta：

- [GestureContext.cs:164-239](../Touch-Rev/Touch-Logic/Gesture/GestureContext.cs#L164-L239)

这直接替代当前 `ThreeFingerGestureRecognizer` 内散落的 `Center()`、`TryAverageDeltaById()`、`CalculateDistances()`。

### Recognizer：LongPress 和 Tap 分离

三指长按 recognizer 只做长按判定：

- [ThreeFingerLongPressRecognizer.cs:43-107](../Touch-Rev/Touch-Logic/Gesture/Recognizers/ThreeFingerLongPressRecognizer.cs#L43-L107)
- [ThreeFingerLongPressRecognizer.cs:122-133](../Touch-Rev/Touch-Logic/Gesture/Recognizers/ThreeFingerLongPressRecognizer.cs#L122-L133)

关键差异：

- 用 `DeltaFromAnchor` 判断启动漂移，而不是候选开始时手动算平均 ID 位移。
- hard cancel 是 `MaxStartMovement * 2.4`。
- trigger limit 是随时间推进的 `1.15 -> 1.5` 倍，而当前项目固定用 `1.5`。
- 触发后每帧发 Update，End 在 session finalize 时发出。

Tap recognizer 与 long press 分离。`Touch-Logic` 当前配置中 3 指/4 指默认是 single tap；本项目因屏蔽策略不同，实施时继续保留三指 double tap：

- [MultiFingerTapRecognizer.cs:48-145](../Touch-Rev/Touch-Logic/Gesture/Recognizers/MultiFingerTapRecognizer.cs#L48-L145)
- [Program.cs:512-529](../Touch-Rev/Touch-Logic.SelfTests/Program.cs#L512-L529)

## 差异表

| 维度 | Touch-Rev-GUI 当前实现 | Touch-Logic 实现 | 对稳定性的影响 |
| --- | --- | --- | --- |
| 输入抽象 | `RawTouchInput::Frame` 直接喂给单个 recognizer | `ContactTracker` 先维护触点生命周期 | 当前容易受 frame sync、ID 抖动、漏 release 影响 |
| 手指数 | 必须当前正好 3 指 | Pending 内记录 `PeakFingerCount`，Active 后 `LockedFingerCount` | 当前容易在落指不同步时重置或丢候选 |
| 激活窗口 | 立即进入 Candidate | 稳定窗口、快速移动 early lock、tap lift-off fast-track | 当前缺少“等待手指数稳定”的缓冲 |
| 空间校验 | pairwise max distance `<= 800` | centroid radius `<= MaxClusterRadius`，4/5 指可乘系数 | 当前判断较粗，容易把分散触摸当同手 |
| 长按判定 | 固定 400ms + 固定漂移阈值 | real-time + progressive movement tolerance | 当前对触摸板/触摸屏抖动更敏感 |
| 掉指处理 | LongPressActive 内部降级到 2 指 | session quorum + tolerance window | 当前掉指逻辑只服务长按，不服务完整生命周期 |
| Tap | Candidate 结束时直接判断 double tap | Tap recognizer 在 OnEnd 中判断；`Touch-Logic` 默认 3/4 指 single tap，本项目实施保持三指 double tap | 当前 tap 与 long press 状态互相耦合 |
| 静止长按 | 依赖 `Tick()` 补帧 | Watchdog pump active recognizer | 当前有 Tick，但不具备 session timeout/finalize |

## 建议复刻范围

不考虑多线程时，建议保留当前 Raw Input 输入源和 UI action 输出，只把 `Touch-Logic` 的核心手势层按 C++ 移植到 `src/input/gesture/`：

```text
RawTouchInput::Frame
  -> GestureContactTracker
  -> GestureSession
  -> ThreeFingerLongPressRecognizer + MultiFingerTapRecognizer
  -> ThreeFingerGestureRecognizer facade
  -> InputController::MapGestureEvent
```

其中 `ThreeFingerGestureRecognizer` 继续作为外部 facade，减少 `InputController`、`RawTouchInputViewer` 的改动范围。内部替换为 session/recognizer 模型。

## 行为映射建议

当前 GUI 只有三指 app switcher 行为，因此复刻时建议先实现：

- 三指 long press：映射到现有 `LongPressStarted / LongPressMoved / LongPressEnded`。
- 三指 tap：按本项目现有屏蔽策略继续使用 double tap，facade 保留 `EventType::DoubleTap` 并映射到 `ShowSwitcher`。
- 三指 swipe：`Touch-Logic` 有完整 recognizer，但当前 GUI 未消费 `ThreeFingerSwipe*` action，本轮不默认接入，避免引入无 UI 消费者的行为。

## 小结

可操作结论：

1. 稳定性修复重点是复刻 `ContactTracker + GestureSession + GestureContext + recognizer 分离`，不是单纯调大阈值。
2. 当前项目应保留 Raw Input 与 `InputController::RawInputResult`，避免牵动窗口消息和 app switcher UI。
3. 第一轮复刻只覆盖当前 GUI 已消费的三指 long press 与 tap；多指 swipe、4/5 指和多线程 watchdog 不进入本轮。
