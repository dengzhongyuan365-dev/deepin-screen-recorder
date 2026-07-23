# deepin-screen-recorder 架构诊断与渐进式重构方案

## 1. 背景

当前项目是一个以 qmake 为主的 Qt/DTK Linux 桌面应用，覆盖截图、录屏、滚动截图、OCR、贴图、dock 插件、X11/Wayland 平台适配等能力。项目的主要问题已经不是“代码量大”，而是功能在长期叠加后形成了高耦合、弱边界、强依赖的结构，新需求会自然扩散到 `MainWindow`、`Utils`、`ShapesWidget`、`SubToolWidget`、`RecordProcess` 等多个模块，导致修改面过宽、回归风险上升、复用困难。

本方案的目标不是一次性重写，而是在保持现有功能稳定的前提下，逐步建立清晰的模块边界，使后续新增能力，例如序号标注、圆角区域、区域再调整、图片后处理、Treeland 适配、录屏后端扩展等，都可以落在明确、可测试、可替换的模块内。

## 2. 架构目标

### 2.1 目标

- 保持现有用户功能、交互语义、保存行为和 DBus 对外接口尽可能不变。
- 将 UI、业务流程、标注工具、效果处理、截图区域、保存、后处理、平台适配拆成独立模块。
- 降低 `MainWindow`、`Utils`、`ShapesWidget`、`RecordProcess` 的职责密度。
- 让新增工具、新增后处理、新增平台适配、新增录制后端成为局部改动，而不是跨多个大类同步修改。
- 让测试可以围绕稳定接口和可替换实现展开，而不是依赖主窗口的大量隐式状态。
- 继续尊重 qmake 主构建系统，迁移期间不强制切换 CMake。

### 2.2 非目标

- 不在第一阶段重写 UI。
- 不在第一阶段迁移到 CMake。
- 不在第一阶段删除 qmake 功能宏。
- 不一次性替换所有 `Utils` 全局状态。
- 不一次性重构 `MainWindow`。

### 2.3 分层原则

- 先抽边界，再搬代码。
- 先定义语义，再定义目录。
- 先建立兼容层，再替换旧调用。
- 单次提交只处理一类责任，不同时混入重命名、迁移和业务变化。
- 新功能优先接入新边界，不再向旧的“大类”继续堆逻辑。
- 界面层只负责组合和展示，业务决策下沉到 controller/service。
- 纯数据结构不依赖 UI、DBus、X11、Wayland。

## 3. 当前架构主要问题

### 3.1 `MainWindow` 职责过重

`MainWindow` 当前同时承担：

- 主截图窗口 UI。
- 截图区域创建、移动、缩放。
- toolbar/side bar 位置管理。
- 截图、滚动截图、OCR、贴图、录屏模式切换。
- X11/Wayland 输入事件处理。
- 快捷键分发。
- 截图保存、通知、剪贴板、DBus Done 信号。
- 录屏启动、停止、状态变化。
- Wayland 窗口枚举和特殊处理。

这使得任何新功能都容易修改 `MainWindow`。

### 3.2 工具系统没有独立模型

当前工具系统依赖字符串命令：

- `"rectangle"`
- `"oval"`
- `"line"`
- `"arrow"`
- `"pen"`
- `"text"`
- `"effect"`
- `"ocr"`
- `"scrollShot"`
- `"pinScreenshots"`
- `"record"`

这些字符串在 toolbar、sidebar、快捷键、`MainWindow`、`ShapesWidget` 之间传递。新增工具时，需要同时修改多个 if/else 分支。

更关键的是，这些字符串同时承载了“用户入口”“当前模式”“具体工具行为”三类语义，本身已经成为隐式协议。协议没有类型约束，也没有统一注册点，因此工具数量越多，分支越散，后续改名或扩展就越脆弱。

toolbar 相关问题更明显：`ToolBar`、`ToolBarWidget`、`SubToolWidget` 既负责按钮和菜单展示，又持有 `MainWindow` 指针，还处理录屏/截图模式、OCR/滚动截图/贴图启用状态、保存方式菜单、摄像头/键盘/音频开关等业务状态。信号也以 `QString` 和多个特定 signal 扩散，例如工具切换、模式切换、保存方式变化、设备开关变化分别走不同路径。结果是新增一个工具或一个 toolbar 控件时，通常需要同时理解 toolbar、sub toolbar、sidebar、`MainWindow`、`ShapesWidget` 的交互链路。

这个问题不能只靠移动文件解决，需要先把 toolbar 降级为 command view：它只展示控件和发出 `ToolCommand`，业务含义交给 `ToolController` 和对应 service 解释。

### 3.3 `ShapesWidget` 同时是画布、模型和工具执行器

`ShapesWidget` 当前承担：

- 当前工具状态。
- 图形列表。
- 鼠标事件。
- 命中检测。
- 拖拽、旋转、缩放。
- 文本编辑框管理。
- undo/delete。
- 图形渲染。
- blur/mosaic 效果绘制。

这使得序号标注、圆角矩形、箭头样式、模糊/马赛克效果等扩展都必须继续膨胀 `ShapesWidget`。从架构上看，它已经不是一个纯 canvas，而是同时扮演了 document、controller、renderer、selection manager 和 effect host。

### 3.4 截图区域和标注工具耦合

截图区域的坐标、拖拽、resize、选区状态和标注工具使用相同窗口状态维护。后续如果要做区域再调整、区域圆角、区域样式，需要独立的 `CaptureRegion` 模型和 controller。

截图区域本质上是“截图会话的输入边界”，而标注工具本质上是“对已获得图像的编辑行为”。这两类对象的生命周期、坐标系、修改方式都不一样，继续共用一套状态只会让边界越来越难收敛。

### 3.5 标注、效果、后处理三类能力被混写

当前代码里，标注、效果和后处理的职责存在交叉：

- 标注是几何或文本层面的编辑，例如矩形、箭头、文字、序号标注。
- 效果是对图像像素或局部区域的处理，例如模糊、马赛克。
- 后处理是对最终输出做收尾，例如边框、光标叠加、圆角裁切、水印。

这三类能力的实现方式不同，运行时机也不同。如果放在同一层，只会让工具扩展、保存逻辑和渲染路径持续耦合。

### 3.6 平台适配散落

X11/Wayland 判断散落在：

- `main.cpp`
- `MainWindow`
- `Utils`
- `waylandrecord`
- widgets
- dock 插件

Treeland 后续适配不能继续用散落 `if (isWayland/isTreeland)` 的方式叠加，应建立平台能力抽象。

### 3.7 保存、通知、后处理混合

截图保存流程混合：

- 保存策略。
- 路径生成。
- 文件格式。
- 文件对话框。
- 图片写入。
- 剪贴板。
- 通知。
- 三方 DBus Done 信号。
- OCR 后续处理。
- 退出应用。

这些应该拆成服务和后处理管线。

## 4. 目标架构

### 4.1 模块分层

建议目标目录结构：

```text
src/
  app/
    main.cpp
    applicationbootstrap.*
    commandlineparser.*
    singleinstanceservice.*

  ui/
    mainwindow.*
    toolbar/
    canvas/
    panels/

  application/
    screenshotcontroller.*
    toolcontroller.*
    toolregistry.*
    captureregioncontroller.*
    annotationcontroller.*
    effectcontroller.*
    recordingcontroller.*
    shortcutcommandrouter.*

  domain/
    appcontext.*
    featurecapabilities.*
    tooltype.*
    toolcommand.*
    toolstyle.*
    captureregion.*
    screenshotsession.*
    recordingsession.*

  annotation/
    annotationdocument.*
    annotationitem.*
    annotationselection.*
    annotationhistory.*

  effects/
    effectdocument.*
    effectoperation.*
    blureffectoperation.*
    mosaiceffectoperation.*

  tools/
    annotationtool.*
    effecttool.*
    rectangletool.*
    roundedrectangletool.*
    ellipsetool.*
    linetool.*
    arrowtool.*
    pentool.*
    texttool.*
    numbermarkertool.*
    blurregiontool.*
    mosaicregiontool.*

  rendering/
    annotationrenderer.*
    shaperenderer.*
    textrenderer.*
    effectpreviewrenderer.*

  screenshot/
    screenshotcaptureservice.*
    screenshotsaveservice.*
    screenshotsavepathresolver.*
    screenshotimagewriter.*

  postprocess/
    imagepostprocessor.*
    postprocesspipeline.*
    borderprocessor.*
    cursoroverlayprocessor.*
    roundedregionprocessor.*
    watermarkprocessor.*

  recording/
    recordingoptions.*
    recorderservice.*
    recorderbackend.*
    ffmpegrecorderbackend.*
    gstreamerrecorderbackend.*
    waylandrecorderbackend.*

  platform/
    desktopplatform.*
    x11platform.*
    waylandplatform.*
    treelandplatform.*
    clipboardservice.*
    notificationservice.*
    filedialogservice.*
    settingsservice.*
    windowinfoservice.*
    inputcaptureservice.*
    overlaywindowservice.*
    dockrecorderservice.*
```

迁移期间不需要一次性建立全部目录。可以先通过 `.pri` 分组表达边界，再逐步移动实现文件。目录结构是结果，不是第一步目标；第一步目标是让新代码按正确职责落位。

### 4.2 模块职责边界

| 模块 | 主要职责 | 不应该承担 |
| --- | --- | --- |
| `ui` | 组合窗口、按钮、菜单、画布控件、状态展示 | 保存策略、平台判断、工具执行逻辑 |
| `application` | 编排用例，连接 UI 命令、领域模型和服务 | 具体像素处理、具体平台 API 调用 |
| `domain` | 稳定枚举、值对象、会话状态、能力描述 | Qt Widget、DBus、X11/Wayland 细节 |
| `annotation` | 标注文档、标注项、选择状态、撤销栈 | 模糊/马赛克像素处理、保存、平台适配 |
| `effects` | 模糊、马赛克等局部像素效果的操作模型和处理逻辑 | 矩形/箭头/文本等纯标注渲染 |
| `tools` | 将鼠标、触摸、快捷键输入转换为命令或编辑操作 | 直接保存文件、直接调用 `MainWindow` |
| `rendering` | 将标注和效果预览绘制到画布或图片 | 管理工具状态、管理保存路径 |
| `screenshot` | 截图获取、裁剪、保存编排、路径解析、图片写入 | 通知/剪贴板具体实现、窗口系统判断 |
| `postprocess` | 输出图片的最终处理链，例如边框、光标、圆角、水印 | 用户交互、工具命令分发 |
| `recording` | 录屏选项、录屏服务、后端选择、录制状态 | toolbar 展示、截图标注 |
| `platform` | 平台能力抽象和 X11/Wayland/Treeland 实现 | 业务流程决策、UI 状态管理 |

### 4.3 依赖方向

依赖必须单向：

```text
ui -> application -> domain
application -> screenshot / recording / annotation / effects / tools
tools -> annotation / effects / domain
screenshot / recording / tools -> platform interface
platform implementation -> Qt / DTK / X11 / Wayland / DBus
```

禁止方向：

```text
domain -> ui
domain -> DBus/X11/Wayland
tools -> MainWindow
platform -> MainWindow
renderer -> toolbar
service -> qApp->quit()
```

### 4.4 为什么这样拆

| 能力 | 当前容易落到哪里 | 正确归属 | 拆分原因 |
| --- | --- | --- | --- |
| 矩形、箭头、文字、序号标注 | `ShapesWidget` 分支 | `annotation` + `tools` + `rendering` | 是可编辑的语义对象，应支持选择、属性修改、撤销、独立渲染 |
| 模糊、马赛克 | `ShapesWidget` 的 shape/effect 混合逻辑 | `effects` + `tools` + `rendering` | 是像素级操作，需要对图像区域做处理，不能等同于普通图形 |
| 截图区域再调整 | `MainWindow` 鼠标事件和区域变量 | `CaptureRegionController` | 是截图输入边界，不是标注对象 |
| 截图区域圆角 | 可能误放到矩形标注 | `CaptureRegion` + `RoundedRegionProcessor` | 是输出裁剪属性，不是画到图上的矩形 |
| 光标叠加、边框、水印 | 保存流程或 `MainWindow` | `postprocess` | 是输出链路的可插拔步骤，应和 UI/标注解耦 |
| Treeland 适配 | 散落 `if` 分支 | `platform/TreelandPlatform` | 平台差异应集中到 capability 实现，避免污染业务流程 |

## 5. 核心设计

### 5.1 工具命令模型

新增工具类型枚举：

```cpp
enum class ToolType {
    None,
    Select,
    Rectangle,
    RoundedRectangle,
    Ellipse,
    Line,
    Arrow,
    Pen,
    Text,
    Blur,
    Mosaic,
    NumberMarker,
    RegionAdjust,
    Ocr,
    ScrollShot,
    PinScreenshot,
    Record
};
```

新增统一命令：

```cpp
struct ToolCommand {
    ToolType type = ToolType::None;
    QVariantMap context;
    QVariantMap payload;
};
```

作用：

- toolbar 点击、快捷键、右键菜单、DBus 启动都发 `ToolCommand`。
- 旧字符串通过兼容函数转换。
- 新增工具不再扩散字符串判断。
- `context` 用于表达命令来源、当前模式、保存动作等临时兼容信息，避免在 UI signal 中继续扩展多个参数。

新增工具注册表：

```cpp
struct ToolDescriptor {
    ToolType type = ToolType::None;
    QString legacyName;
    QString group;
    bool checkable = false;
};

class ToolRegistry {
public:
    ToolType fromLegacyName(const QString &name) const;
    QString toLegacyName(ToolType type) const;
    ToolDescriptor descriptor(ToolType type) const;
};
```

作用：

- 工具名称、分组、旧字符串兼容集中维护。
- toolbar 不再硬编码所有业务字符串。
- 新增按钮、新增快捷键、新增菜单入口时，共用同一套工具描述。

### 5.2 工具控制器

新增：

```cpp
class ToolController : public QObject {
    Q_OBJECT
public:
    void dispatch(const ToolCommand &command);
    ToolType currentTool() const;

signals:
    void currentToolChanged(ToolType type);
    void requestSave();
    void requestOcr();
    void requestScrollShot();
    void requestPinScreenshot();
    void requestRecordMode();
};
```

作用：

- 统一接收 toolbar、快捷键、菜单命令。
- `MainWindow` 不再直接理解全部工具字符串。
- toolbar 不直接调用 `MainWindow` 业务函数。
- 对保存、OCR、滚动截图、贴图、录屏模式等“功能命令”和矩形、箭头、文本等“编辑工具”做统一入口，但在 controller 内分发到不同子系统。

toolbar 的演进目标：

- `ToolBar` / `SubToolWidget` 只负责展示按钮、菜单和选中态。
- `ToolBar` / `SubToolWidget` 发出 `ToolCommand`，不直接调用 `MainWindow` 业务函数。
- `ToolController` 负责把 `ToolCommand` 转成具体用例。
- `MainWindow` 只连接 signal/slot 和托管 UI 生命周期。

### 5.3 标注文档

新增：

```cpp
class AnnotationDocument {
public:
    void addItem(std::unique_ptr<AnnotationItem> item);
    void removeSelected();
    void clear();
    bool undo();
    QList<AnnotationItem *> items() const;
};
```

迁移策略：

- 第一阶段保留 `Toolshape`。
- 新增 `AnnotationDocument` 时可以内部继续保存 `Toolshapes`。
- 后续逐步把不同 shape 拆成 `AnnotationItem` 子类。

作用：

- 将图形数据从 `ShapesWidget` 中移出。
- undo/delete/selection 有独立模型。
- 新增序号标注、圆角矩形、箭头样式时，不继续污染 `ShapesWidget`。

### 5.4 标注工具

新增：

```cpp
class IAnnotationTool {
public:
    virtual ~IAnnotationTool() = default;
    virtual void mousePress(const ToolEvent &event) = 0;
    virtual void mouseMove(const ToolEvent &event) = 0;
    virtual void mouseRelease(const ToolEvent &event) = 0;
};
```

每种工具独立实现：

- `RectangleTool`
- `RoundedRectangleTool`
- `EllipseTool`
- `ArrowTool`
- `PenTool`
- `TextTool`
- `NumberMarkerTool`

作用：

- 新增工具时新增类，而不是继续加 `ShapesWidget` 分支。
- 工具行为可单独测试。

### 5.5 效果子系统

模糊和马赛克不应作为普通标注 shape 处理。它们在交互上像工具，在结果上是图像像素处理。

新增：

```cpp
enum class EffectType {
    Blur,
    Mosaic
};

struct EffectOperation {
    EffectType type;
    QRectF area;
    int radius = 0;
    int zOrder = 0;
};

class EffectDocument {
public:
    void addOperation(const EffectOperation &operation);
    void removeSelected();
    bool undo();
    QList<EffectOperation> operations() const;
};
```

新增工具：

- `BlurRegionTool`
- `MosaicRegionTool`

作用：

- 工具只负责收集区域和参数。
- effect document 负责保存效果操作。
- effect preview renderer 负责预览。
- export pipeline 负责把效果应用到最终图像。
- 后续新增局部锐化、像素化、遮挡、局部高亮，不需要进入 `AnnotationDocument`。

设计边界：

- `AnnotationDocument` 保存可编辑的语义标注。
- `EffectDocument` 保存会改变像素的效果操作。
- `PostProcessPipeline` 处理输出收尾，不参与交互选择。

### 5.6 截图区域模型

新增：

```cpp
struct CaptureRegion {
    QRectF rect;
    qreal cornerRadius = 0;
};
```

新增：

```cpp
class CaptureRegionController {
public:
    void beginCreate(QPointF pos);
    void updateCreate(QPointF pos);
    void beginMove(QPointF pos);
    void updateMove(QPointF pos);
    void beginResize(QPointF pos);
    void updateResize(QPointF pos);
    CaptureRegion region() const;
};
```

作用：

- 截图区域创建、移动、resize、区域再调整独立出来。
- 区域圆角是 `CaptureRegion` 属性，不是标注 shape。
- 多屏缩放坐标换算后续可集中处理。

### 5.7 渲染、合成与后处理管线

建议输出流程：

```text
Screen image
  -> crop by CaptureRegion
  -> apply EffectDocument
  -> render AnnotationDocument
  -> run PostProcessPipeline
  -> save
```

说明：

- effect 默认作用于底图区域，标注默认绘制在最终图像上层。
- 如果需要保留历史行为中的复杂层级关系，应增加显式 z-order 或 composition list，而不是继续依赖 `ShapesWidget` 分支顺序。
- 圆角区域、边框、光标、水印属于最终输出处理，不应回灌到标注文档。

新增：

```cpp
class ImagePostProcessor {
public:
    virtual ~ImagePostProcessor() = default;
    virtual QImage process(const QImage &image, const PostProcessContext &context) = 0;
};
```

处理器：

- `CursorOverlayProcessor`
- `BorderProcessor`
- `RoundedRegionProcessor`
- `WatermarkProcessor`

作用：

- 图片后处理功能模块化。
- 新增后处理不改保存流程，不改 `MainWindow`。
- 保存服务只关心“输入图片 + 输出参数”，不关心每个处理器内部实现。

### 5.8 平台能力抽象

新增：

```cpp
enum class PlatformType {
    X11,
    Wayland,
    Treeland
};
```

新增：

```cpp
class DesktopPlatform {
public:
    virtual ~DesktopPlatform() = default;
    virtual PlatformType type() const = 0;
    virtual WindowInfoService *windowInfo() = 0;
    virtual InputCaptureService *inputCapture() = 0;
    virtual ScreenCaptureService *screenCapture() = 0;
    virtual OverlayWindowService *overlayWindow() = 0;
};
```

实现：

- `X11Platform`
- `WaylandPlatform`
- `TreelandPlatform`

作用：

- Treeland 适配集中到 `TreelandPlatform`。
- `MainWindow` 不继续散落 `if (Utils::isWaylandMode)`。
- 输入捕获、窗口枚举、截图抓取、overlay 设置都有明确平台实现。
- 上层优先依赖 capability，不直接依赖平台名称。例如“是否支持窗口枚举”“是否支持全局输入捕获”“是否需要 portal/DBus 协议”。

## 6. 分阶段迁移计划

### 阶段 0：基线与边界治理

目标：不改业务行为，建立迁移基准。

原子任务：

1. 记录当前截图/录屏/滚动截图/OCR/贴图/保存/通知/快捷键功能矩阵。
2. 确认主工程和测试工程的 qmake 构建方式。
3. 新增 `.pri` 分组文件，但不移动实现：
   - `domain/domain.pri`
   - `screenshot/screenshot.pri`
   - `annotation/annotation.pri`
   - `effects/effects.pri`
   - `tools/tools.pri`
   - `postprocess/postprocess.pri`
   - `platform/platform.pri`

验收：

- 功能无变化。
- qmake 构建清单更清晰。
- 测试工程可复用分组清单。

### 阶段 1：统一工具命令

目标：控制工具字符串扩散。

原子任务：

1. 新增 `ToolType`。
2. 新增 `ToolCommand`。
3. 新增 `ToolTypeMapper`，提供旧字符串到 `ToolType` 的转换。
4. 快捷键内部先转 `ToolCommand`，再兼容调用旧逻辑。
5. toolbar signal 增加 `ToolCommand` 新信号，旧 `QString` 信号暂时保留。

验收：

- 所有旧工具点击行为不变。
- 新工具类型有唯一注册位置。
- 不再新增新的散落字符串。

### 阶段 2：引入 ToolController

目标：统一 toolbar、快捷键、菜单命令入口，先治理 toolbar/control/event 满天飞的问题。

原子任务：

1. 新增 `ToolController`。
2. `MainWindow::changeShotToolEvent()` 逐步变成 controller 调用。
3. 快捷键触发 `ToolController::dispatch()`。
4. toolbar 触发 `ToolController::dispatch()`。
5. `ToolBar` / `SubToolWidget` 增加 `ToolCommand` 信号，旧 `QString` 信号暂时保留。
6. 右键菜单保存/撤销/退出仍保留旧行为，后续再迁移。

验收：

- toolbar、快捷键行为一致。
- `MainWindow` 中工具字符串判断减少。
- 新增工具可以先接入 controller。
- toolbar 不再新增直接面向 `MainWindow` 的业务调用。

### 阶段 3：建立 AnnotationDocument

目标：从 `ShapesWidget` 中抽出图形数据模型。

原子任务：

1. 新增 `AnnotationDocument`，内部暂时封装 `Toolshapes`。
2. `ShapesWidget` 的 `m_shapes` 改由 `AnnotationDocument` 持有。
3. undo/delete/clear 逐步迁到 document。
4. selected/hovered 状态迁到 `AnnotationSelection`。

验收：

- 现有图形绘制、选中、删除、撤销行为不变。
- `ShapesWidget` 不再直接拥有完整图形列表。
- 文档模型可单独测试。

### 阶段 4：抽标注工具

目标：让新增标注工具不修改 `ShapesWidget` 大分支。

迁移顺序：

1. `RectangleTool`
2. `EllipseTool`
3. `LineTool`
4. `ArrowTool`
5. `PenTool`
6. `TextTool`
7. `NumberMarkerTool`
8. `RoundedRectangleTool`

验收：

- 每迁移一个工具，行为保持一致。
- 新工具可通过新增 tool 类接入。
- `ShapesWidget::mousePressEvent/mouseMoveEvent/mouseReleaseEvent` 逐步变薄。
- 模糊、马赛克不进入该阶段，避免继续混淆标注和像素效果。

### 阶段 5：抽效果子系统

目标：将 blur/mosaic 从标注 shape 中分离，建立局部像素效果模型。

原子任务：

1. 新增 `EffectType`、`EffectOperation`。
2. 新增 `EffectDocument`，第一阶段可继续兼容现有 `Toolshape` 中 effect 字段。
3. 新增 `BlurRegionTool`，只迁移区域选择和半径参数，不改变视觉结果。
4. 新增 `MosaicRegionTool`，只迁移区域选择和半径参数，不改变视觉结果。
5. effect 预览暂时复用现有绘制路径，渲染器在下一阶段抽出。
6. `ShapesWidget::reloadEffectImg()` 逐步变成 effect controller 的内部实现细节。

验收：

- blur/mosaic 的交互、预览、保存效果不变。
- effect 数据不再依赖 `AnnotationDocument`。
- 后续新增新的像素效果不需要修改标注工具。

### 阶段 6：抽渲染器

目标：把绘制逻辑从 `ShapesWidget` 移出。

原子任务：

1. 新增 `AnnotationRenderer`。
2. 抽 `ShapeRenderer`。
3. 抽 `TextRenderer`。
4. 抽 `EffectPreviewRenderer`。
5. 新增 `ImageCompositor`，统一组合底图、效果预览和标注层。
6. `ShapesWidget::handlePaint()` 改为调用 renderer/compositor。

验收：

- 截图标注渲染效果不变。
- blur/mosaic 渲染效果不变。
- 新增 renderer 不需要修改 canvas 主逻辑。

### 阶段 7：抽 CaptureRegionController

目标：截图区域行为独立。

原子任务：

1. 新增 `CaptureRegion`。
2. 将 `recordX/recordY/recordWidth/recordHeight` 兼容封装为 `CaptureRegion`。
3. 抽区域命中检测。
4. 抽区域移动。
5. 抽区域 resize。
6. 加入 `cornerRadius` 字段，但默认值为 0，行为不变。
7. 支持后续区域再调整。

验收：

- 区域创建、拖动、resize 行为不变。
- 多屏缩放行为不变。
- 新增区域圆角不影响标注工具。

### 阶段 8：抽保存与后处理

目标：图片输出链路模块化。

原子任务：

1. 抽 `ScreenshotImageWriter`。
2. 抽 `ClipboardService`。
3. 抽 `ScreenshotNotificationService`。
4. 抽 `ScreenshotSavePathResolver`。
5. 抽 `ScreenshotSaveService`。
6. 新增 `PostProcessPipeline`。
7. 将边框、光标叠加、区域圆角、水印作为 processor。
8. 将 effect 应用明确放在 export/compositor 阶段，不作为普通后处理混入保存服务。

验收：

- 保存到剪贴板、桌面、图片目录、指定目录、自动路径行为不变。
- 通知行为不变。
- 后处理可独立扩展。

### 阶段 9：平台抽象与 Treeland 适配

目标：平台差异集中。

原子任务：

1. 新增 `DesktopPlatformDetector`。
2. 新增 `DesktopPlatform` 接口。
3. 抽 `X11Platform`。
4. 抽 `WaylandPlatform`。
5. 新增 `TreelandPlatform` 空实现或最小实现。
6. 将窗口枚举、输入捕获、overlay 属性、截图抓取逐步迁入 platform。
7. `Utils::isWaylandMode` 逐步只作为兼容读，不再扩展使用。

验收：

- X11 行为不变。
- Wayland 行为不变。
- Treeland 新适配不污染 `MainWindow`。

### 阶段 10：录屏模块重构

目标：录屏后端可扩展。

原子任务：

1. 新增 `RecordingOptions`。
2. 抽 `DockRecorderService`。
3. 抽 `RecordingStorage`。
4. 抽 `RecordingNotificationService`。
5. 新增 `IRecorderBackend`。
6. 抽 `FfmpegRecorderBackend`。
7. 抽 `GStreamerRecorderBackend`。
8. 抽 `WaylandRecorderBackend`。
9. `RecordProcess` 退化为兼容 facade。

验收：

- FFmpeg/GStreamer/Wayland 录屏行为不变。
- 新增 PipeWire/Treeland 录制后端不改 UI。
- 底层录屏不直接退出应用。

### 阶段 11：瘦身 MainWindow

目标：MainWindow 回归 UI 组合层。

原子任务：

1. 移除保存细节。
2. 移除工具命令分支。
3. 移除 annotation 数据管理。
4. 移除区域交互细节。
5. 移除平台适配细节。
6. 移除录屏后端细节。

验收：

- `MainWindow` 只负责 UI 生命周期、信号连接和窗口显示。
- 业务逻辑由 controller/service 承担。

## 7. 新功能落点示例

### 7.1 序号标注

新增：

- `ToolType::NumberMarker`
- `NumberMarkerTool`
- `NumberMarkerItem`
- `NumberMarkerRenderer`
- `NumberMarkerOptionsView`

不应该修改：

- `MainWindow` 大逻辑。
- 保存逻辑。
- 平台适配。

原因：

- 序号标注是一个可编辑标注对象，应支持选择、移动、样式调整、撤销。
- 它不改变底图像素，也不改变截图区域。

### 7.2 圆角截图区域

新增或修改：

- `CaptureRegion::cornerRadius`
- `CaptureRegionController`
- `RoundedRegionProcessor`
- 区域参数 UI。

不应该修改：

- `AnnotationDocument`
- `ShapeRenderer`
- 录屏后端。

原因：

- 圆角截图区域是输出裁剪属性，不是用户画在图片上的圆角矩形。
- 它应该影响最终导出的 alpha/裁剪结果，而不是进入标注文档。

### 7.3 区域再调整

新增或修改：

- `ToolType::RegionAdjust`
- `CaptureRegionController`
- 区域控制点/边缘命中检测。
- `CaptureRegionView` 或 canvas 中的 region overlay。

不应该修改：

- `AnnotationDocument`
- `EffectDocument`
- 保存路径逻辑。

原因：

- 区域再调整是截图会话状态变化，不是图像编辑结果。
- 它需要稳定处理多屏、高 DPI、边界限制和最小尺寸约束，应集中在 capture region 层。

### 7.4 模糊和马赛克

新增或修改：

- `ToolType::Blur`
- `ToolType::Mosaic`
- `BlurRegionTool`
- `MosaicRegionTool`
- `EffectDocument`
- `EffectPreviewRenderer`

不应该修改：

- `AnnotationDocument`
- `ShapeRenderer`
- 保存路径和通知逻辑。

原因：

- 模糊/马赛克是局部像素效果，不是普通图形标注。
- 它需要对图像区域取样、处理和重绘，参数变化会触发效果图重算。

### 7.5 图片后处理

新增：

- `ImagePostProcessor` 子类。
- 在 `PostProcessPipeline` 注册。

不应该修改：

- `MainWindow::saveAction()`。
- `ScreenshotImageWriter`。

原因：

- 后处理作用于最终输出结果，应以 pipeline 形式组合。
- 保存服务只负责任务编排和写入，不关心具体处理器。

### 7.6 Treeland 适配

新增：

- `TreelandPlatform`
- Treeland 截图后端。
- Treeland overlay/input/window info 实现。

不应该修改：

- toolbar。
- annotation tools。
- save service。

原因：

- Treeland 是平台能力差异，不是 UI 差异。
- 上层应通过 capability 询问能力，而不是直接判断平台名称后分支。

## 8. 典型需求切片：可调整圆角截图区域与方向提示面板

### 8.1 需求描述

假设新增需求：

- 用户通过鼠标框选截图区域后，仍可以继续调整该区域。
- 区域可以通过鼠标拖拽边缘和控制点调整大小。
- 区域可以通过键盘方向键移动或缩放。
- 区域支持圆角，且圆角半径可调整。
- 新增一个类似上下左右快捷键提示的窗口或区域，用于展示当前区域调整相关操作。
- 功能行为要兼容截图、录屏、滚动截图等既有模式，不破坏现有快捷键和 toolbar 行为。

这个需求非常适合作为架构迁移入口，因为它同时触及：

- `MainWindow` 中的 `recordX/recordY/recordWidth/recordHeight` 区域状态。
- `MainWindow::getAction()` / `resizeTop()` / `resizeBottom()` / `resizeLeft()` / `resizeRight()` 等区域交互逻辑。
- `keyPressEF()` 中方向键移动和缩放逻辑。
- `updateToolBarPos()` / `updateSideBarPos()` / `m_sizeTips->updateTips()` 等区域变化后的 UI 联动。
- `ShapesWidget::setGlobalRect()` 与标注画布区域同步。
- 最终截图保存时的裁剪、圆角输出、光标叠加、边框处理。

如果继续在 `MainWindow` 里直接叠加圆角、调整面板、快捷键提示和区域再调整，短期能做出来，但后续需求会更难维护。因此这个需求应该作为 `CaptureRegion` 子系统的第一批迁移目标。

### 8.2 当前代码暴露的问题

当前区域能力主要散落在 `MainWindow`：

- 区域数据是四个裸字段：`recordX`、`recordY`、`recordWidth`、`recordHeight`。
- 区域动作是若干 `int ACTION_*` 常量。
- 鼠标命中检测、resize、移动、键盘微调分别在不同函数中实现。
- 截图和录屏有不同最小尺寸，但判断散落在分支中。
- 区域变化后，需要手动同步 size tips、toolbar、sidebar、camera widget、shapes widget。
- 圆角没有模型位置，容易被误放到标注 shape 或保存逻辑中。

现有 `KeyButtonWidget` 和 `RecorderRegionShow` 可以作为 UI 风格参考，但不应该直接承载这个新需求的业务逻辑。它们当前更偏录屏过程中的按键显示，和“区域调整提示面板”的生命周期、位置策略、命令说明不同。

### 8.3 架构归属

这个需求应拆成五个子能力：

| 子能力 | 归属模块 | 原因 |
| --- | --- | --- |
| 区域矩形、圆角、约束 | `domain/CaptureRegion` | 是截图会话的稳定数据，不依赖 UI |
| 鼠标/键盘调整区域 | `application/CaptureRegionController` | 是区域交互规则，不应散落在主窗口 |
| 区域边框、控制点、圆角预览 | `ui/canvas/CaptureRegionOverlay` 或 `rendering/CaptureRegionRenderer` | 是显示层，不负责修改规则 |
| 上下左右快捷键提示面板 | `ui/panels/RegionShortcutHintPanel` | 是 UI 提示，不参与区域计算 |
| 最终圆角裁切输出 | `postprocess/RoundedRegionProcessor` | 是最终输出处理，不是标注内容 |

关键判断：

- 圆角截图区域不是 `RoundedRectangleTool`。
- 圆角截图区域不是 `AnnotationItem`。
- 圆角截图区域不应该画进 `ShapesWidget` 的标注文档。
- 圆角截图区域是 `CaptureRegion` 的一个输出属性，最终由 `RoundedRegionProcessor` 影响导出图片。

### 8.4 建议对象模型

新增或演进：

```cpp
enum class CaptureMode {
    Screenshot,
    Recording,
    ScrollScreenshot
};

enum class RegionHandle {
    None,
    Move,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    CornerRadius
};

struct CaptureRegion {
    QRectF rect;
    qreal cornerRadius = 0.0;
};

struct CaptureRegionConstraints {
    QRectF bounds;
    QSizeF minSize;
    QSizeF maxSize;
    qreal minCornerRadius = 0.0;
    qreal maxCornerRadius = 0.0;
    bool keepInsideBounds = true;
};

struct CaptureRegionState {
    CaptureMode mode = CaptureMode::Screenshot;
    CaptureRegion region;
    CaptureRegionConstraints constraints;
    RegionHandle activeHandle = RegionHandle::None;
    bool editing = false;
};
```

新增 controller：

```cpp
class CaptureRegionController : public QObject {
    Q_OBJECT
public:
    void setState(const CaptureRegionState &state);
    CaptureRegionState state() const;

    RegionHandle hitTest(const QPointF &pos) const;

    void beginInteraction(RegionHandle handle, const QPointF &pos);
    void updateInteraction(const QPointF &pos);
    void endInteraction();

    void moveBy(const QPointF &delta);
    void resizeBy(RegionHandle handle, const QPointF &delta);
    void adjustCornerRadius(qreal delta);
    void setCornerRadius(qreal radius);

signals:
    void regionChanged(const CaptureRegion &region);
    void interactionChanged(RegionHandle handle);
};
```

作用：

- 替代 `MainWindow::getAction()` 的命中检测。
- 替代 `resizeTop()` / `resizeBottom()` / `resizeLeft()` / `resizeRight()` 的尺寸调整。
- 收敛方向键移动、Ctrl+方向键扩展、Ctrl+Shift+方向键收缩等规则。
- 统一截图和录屏的最小尺寸差异，通过 `CaptureRegionConstraints` 注入。
- 后续 Treeland、多屏、高 DPI 的区域边界问题可以集中治理。

### 8.5 交互设计

区域调整应有明确状态机：

```text
Idle
  -> Creating
  -> Selected
Selected
  -> Moving
  -> Resizing
  -> AdjustingCornerRadius
  -> Annotating
  -> Confirming
```

建议规则：

- 鼠标框选完成后进入 `Selected`。
- 点击区域内部进入 `Moving`。
- 拖拽边缘或角点进入 `Resizing`。
- 拖拽圆角控制点，或通过 toolbar/slider 调整圆角，进入 `AdjustingCornerRadius`。
- 选择矩形、箭头、文字等标注工具后进入 `Annotating`，但区域模型仍保留。
- 如果允许“标注后再调整截图区域”，需要明确标注坐标是否随区域平移或裁剪。这一项必须产品确认，建议第一阶段只支持标注前调整，避免引入坐标迁移风险。

方向键策略建议：

- 方向键或 WASD：移动区域 1px。
- Shift + 方向键：移动区域 10px。
- Ctrl + 方向键：向对应方向扩展 1px。
- Ctrl + Shift + 方向键：向对应方向收缩 1px。
- Alt + 方向键，或圆角 slider：调整圆角半径。

为了兼容现有行为，第一阶段不建议改变既有快捷键语义。可以先把现有 `keyPressEF()` 的行为迁入 controller，再在 controller 内增加圆角调整命令。

### 8.6 圆角输出设计

圆角有两种不同含义，必须区分：

- 预览圆角：截图选择框在 UI 上显示圆角边界。
- 输出圆角：保存的图片真正带 alpha 圆角裁切。

建议实现方式：

```text
CaptureRegion.cornerRadius
  -> CaptureRegionOverlay 负责预览
  -> RoundedRegionProcessor 负责导出
```

输出链路：

```text
Screen image
  -> crop by CaptureRegion.rect
  -> apply EffectDocument
  -> render AnnotationDocument
  -> RoundedRegionProcessor(cornerRadius)
  -> BorderProcessor
  -> CursorOverlayProcessor
  -> save
```

注意：

- 如果输出格式是 PNG，可以保留 alpha 圆角。
- 如果输出格式是 JPEG，应定义背景填充颜色，因为 JPEG 不支持 alpha。
- 如果存在边框，必须定义边框在圆角裁切前还是后。建议圆角裁切先执行，边框再按圆角路径绘制。
- 录屏区域圆角通常不应影响录制内容，除非产品明确要求录屏输出也裁圆角。第一阶段建议圆角只作用于截图输出。

### 8.7 区域快捷键提示面板设计

新增 `RegionShortcutHintPanel`：

```cpp
struct RegionShortcutHint {
    QString key;
    QString description;
    bool active = false;
};

class RegionShortcutHintPanel : public DWidget {
    Q_OBJECT
public:
    void setHints(const QList<RegionShortcutHint> &hints);
    void followRegion(const CaptureRegion &region, const QRectF &screenBounds);
};
```

职责：

- 显示当前区域调整相关快捷键。
- 跟随截图区域，但不遮挡 toolbar、sidebar 和尺寸提示。
- 根据当前状态显示不同内容，例如创建中、选中、移动、调整圆角。
- 只读展示，不直接修改区域。

位置策略：

- 优先放在区域下方居中。
- 下方空间不足时放到区域上方。
- 上下空间都不足时放到区域内部底部。
- 与 toolbar/sidebar/size tips 的避让策略由 `OverlayLayoutManager` 统一计算。

不建议复用 `KeyButtonWidget` 作为业务组件，但可以复用其视觉样式或抽出通用的 `ShortcutKeyChip`。原因是 `KeyButtonWidget` 当前表示“用户按下过的键”，而新面板表示“可用操作提示”，语义不同。

### 8.8 UI 布局协调

区域变化后，现在需要手动更新多个 UI：

- size tips
- toolbar
- sidebar
- camera widget
- shapes widget
- key button widgets

建议引入：

```cpp
class OverlayLayoutManager {
public:
    OverlayLayoutResult layout(const CaptureRegion &region,
                               const QRectF &screenBounds,
                               const OverlayLayoutInput &input) const;
};
```

它负责输出：

- toolbar 位置。
- sidebar 位置。
- size tips 位置。
- region shortcut hint panel 位置。
- camera widget 推荐位置。

这样区域变化时，上层只需要：

```text
CaptureRegionController::regionChanged
  -> OverlayLayoutManager::layout
  -> MainWindow 更新各 UI geometry
```

这一步能明显降低 `MainWindow` 中 `updateToolBarPos()`、`updateSideBarPos()`、`updateCameraWidgetPos()` 互相感知的复杂度。

### 8.9 平滑迁移步骤

这个需求建议拆成 7 个原子提交：

1. 新增 `CaptureRegion`、`RegionHandle`、`CaptureRegionConstraints`，不接入业务。
2. 新增 `CaptureRegionController`，用单元测试覆盖 hit test、move、resize、边界约束、最小尺寸。
3. 在 `MainWindow` 中增加兼容同步层：`recordX/Y/Width/Height <-> CaptureRegion`，行为不变。
4. 将 `getAction()` 和鼠标 resize 调整迁入 controller，旧函数保留为薄 wrapper。
5. 将方向键移动/缩放迁入 controller，保持现有快捷键语义不变。
6. 新增 `RegionShortcutHintPanel`，只显示提示，不改变业务。
7. 新增 `cornerRadius` UI 和 `RoundedRegionProcessor`，默认半径为 0，开启后仅影响截图输出。

每一步验收：

- 当前截图框选、拖拽、resize 行为不变。
- 方向键/WASD/Ctrl/Ctrl+Shift 现有行为不变。
- toolbar/sidebar/size tips 位置不回退。
- 标注工具仍能正常进入和保存。
- 默认 `cornerRadius = 0` 时导出图片字节级或视觉上等价。

### 8.10 这个需求对总架构的价值

这个需求不是孤立功能，而是一次低风险的架构切入点：

- 它能把截图区域从 `MainWindow` 中抽出来。
- 它能建立区域模型和区域 controller，为后续区域再调整、固定比例、吸附窗口、多屏边界治理打基础。
- 它能把“区域样式”和“标注图形”分开，避免圆角区域被误做成标注工具。
- 它能推动 overlay 布局统一，减少 toolbar、sidebar、tips、快捷键提示各自计算位置。
- 它能提前验证后处理 pipeline，因为圆角输出天然需要 `RoundedRegionProcessor`。

推荐把它放在总体迁移的阶段 7 和阶段 8 之间实施：先有 `CaptureRegionController`，再做 `RegionShortcutHintPanel` 和 `RoundedRegionProcessor`。如果要更早支撑产品需求，也可以先做兼容式小闭环，但必须保留 controller 边界，不要直接往 `MainWindow` 继续加字段和分支。

## 9. 稳定性要求

每个原子提交必须满足：

- 不改变用户可见行为。
- 不改变 DBus 对外接口。
- 不改变默认保存路径和文件名格式。
- 不改变快捷键语义。
- 不改变默认配置。
- 能单独编译。
- 能单独回滚。
- 测试工程同步更新。
- 新代码不得新增对 `MainWindow` 的反向依赖。
- 新 service 不得直接调用 `qApp->quit()` 或 `_Exit()`。

## 10. 测试策略

### 10.1 单元测试

优先覆盖：

- `ToolTypeMapper`
- `ToolRegistry`
- `ToolController`
- `AnnotationDocument`
- `EffectDocument`
- `CaptureRegionController`
- `ScreenshotSavePathResolver`
- `PostProcessPipeline`
- `DesktopPlatformDetector`

### 10.2 集成测试

覆盖功能矩阵：

- 区域截图。
- 全屏截图。
- 顶层窗口截图。
- 滚动截图。
- OCR。
- 贴图。
- 标注工具切换。
- 标注撤销/删除/样式修改。
- blur/mosaic 预览与保存。
- 截图区域移动和 resize。
- 保存到剪贴板。
- 保存到文件。
- 指定路径保存。
- no notification。
- Wayland 快捷键。
- X11 全局事件。
- 多屏缩放。

### 10.3 平台验证

至少覆盖：

- X11。
- Wayland。
- Treeland。
- 多屏。
- 高 DPI。
- 2D/3D 窗管。
- 锁屏状态。

### 10.4 架构回归检查

每次提交需要检查：

- 新增 UI 代码是否直接调用平台实现。
- 新增工具是否注册到 `ToolRegistry`。
- 新增标注是否进入 `AnnotationDocument`。
- 新增像素效果是否进入 `EffectDocument`。
- 新增输出处理是否进入 `PostProcessPipeline`。
- 新增平台分支是否进入 `platform` 实现，而不是散落到 `MainWindow`。

## 11. 实施管理建议

- 每次只做一个小目标。
- 每个阶段都先建立兼容层，再迁移调用点。
- 不删除旧路径，直到新路径稳定。
- 不在同一提交里同时做重命名、移动文件和逻辑变化。
- 每个新模块必须有明确 owner 和测试点。
- 大类先瘦身，不追求一次性删除。
- qmake 清单和测试 qmake 清单必须同步维护。

## 12. 推荐第一批落地任务

第一批建议只做架构铺垫，不改业务行为：

1. 新增 `ToolType`、`ToolCommand`、`ToolTypeMapper`。
2. 新增 `ToolRegistry`，集中维护旧字符串到新工具类型的映射。
3. 新增 `.pri` 分组。
4. 快捷键和 toolbar 内部开始通过 `ToolCommand` 兼容分发。
5. 新增 `AnnotationDocument`，但内部暂时兼容 `Toolshapes`。
6. 新增 `EffectDocument`，但内部暂时兼容现有 effect 字段。
7. 新增 `CaptureRegion`，但仍从现有 `recordX/recordY/recordWidth/recordHeight` 同步。

这批完成后，项目就有了后续扩展的基本骨架，同时不会影响现有功能。
