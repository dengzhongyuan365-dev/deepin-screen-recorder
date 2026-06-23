# 尺子测量功能实现总结

## 背景与目标

本次需求是在截图编辑工具中增加一个“尺度测量”能力。用户期望的交互不是普通的两点测距线，而是一把可视化尺子：

- 在截图编辑区调出一把尺子。
- 尺子可以移动和旋转。
- 用户先摆放尺子位置，再在尺子上标记 A/B 两个点。
- 工具显示 A/B 两点之间的距离，第一版以像素 `px` 为单位。
- 尺子只是测量辅助层，不保存到最终截图里。
- 尺子不进入普通标注图元列表，不影响现有撤销逻辑。

## 核心设计

### 1. 尺子作为临时覆盖层

现有截图标注通过 `ShapesWidget` 管理，普通矩形、箭头、画笔、文本等图元会存入 `m_shapes`，并由 `paintImage()` 写入最终截图。

尺子不应作为普通 `Toolshape` 存储，否则会带来两个问题：

- 保存截图时尺子会被画进最终图片。
- 撤销、选择、拖拽等普通图元逻辑会把尺子当成真实标注。

因此本次实现把尺子设计为 `ShapesWidget` 内部的临时 UI 覆盖层：

- 不加入 `m_shapes`。
- 不走普通图元保存路径。
- 只在截图编辑界面绘制。
- 只在当前工具为 `"ruler"` 时显示。

### 2. 绘制路径

现有保存逻辑：

```cpp
void ShapesWidget::paintImage(QImage &image)
{
    QPainter painter(&image);
    handlePaint(painter);
}
```

因此尺子不能放进 `handlePaint()`，否则会被保存进截图。

本次改为在实时编辑绘制路径中追加尺子覆盖层：

```cpp
void ShapesWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    handlePaint(painter);
    paintRulerOverlay(painter);
    if (m_shapes.length() > 0) {
        emit setShapesUndo(true);
    }
}
```

并且 `paintRulerOverlay()` 内部会判断：

```cpp
if (!m_rulerVisible || m_currentType != "ruler") {
    return;
}
```

这样切换到其他工具后，尺子不会继续遮挡普通标注编辑。

### 3. 尺子状态模型

尺子状态保存在 `ShapesWidget` 中，不进入 `Toolshapes m_shapes`。

新增状态包括：

```cpp
bool m_rulerVisible = false;
bool m_rulerPressed = false;
bool m_rulerMarkAVisible = false;
bool m_rulerMarkBVisible = false;
RulerAction m_rulerAction = RulerAction::None;
QPointF m_rulerCenter;
qreal m_rulerLength = 400;
qreal m_rulerThickness = 48;
qreal m_rulerAngle = 0;
qreal m_rulerMarkA = 0;
qreal m_rulerMarkB = 0;
QPointF m_rulerPressedPoint;
qreal m_rulerPressedAngle = 0;
```

交互状态枚举：

```cpp
enum class RulerAction {
    None,
    Move,
    Rotate,
    DragMarkA,
    DragMarkB,
    PlaceMark,
};
```

A/B 标记不是保存为屏幕坐标，而是保存为尺子轴线上的归一化位置，范围约为 `[-0.5, 0.5]`。这样尺子移动或旋转时，标记会自然跟随尺子。

距离计算方式：

```cpp
const qreal distance = qAbs(m_rulerMarkB - m_rulerMarkA) * m_rulerLength;
```

第一版显示逻辑坐标像素，文本格式为：

```cpp
QStringLiteral("%1 px").arg(qRound(distance));
```

## 已修改文件

### `src/widgets/subtoolwidget.h`

新增截图工具栏尺子按钮成员：

```cpp
ToolButton *m_rulerButton = nullptr;
```

### `src/widgets/subtoolwidget.cpp`

在截图工具栏中新增尺子按钮，位置放在箭头之后、画笔之前：

```cpp
m_rulerButton = new ToolButton();
m_rulerButton->setIconSize(TOOL_ICON_SIZE);
installTipHint(m_rulerButton, tr("Ruler"));
m_rulerButton->setIcon(QIcon::fromTheme("ruler-normal"));
Utils::setAccessibility(m_rulerButton, AC_SUBTOOLWIDGET_RULER_BUTTON);
m_shotBtnGroup->addButton(m_rulerButton);
m_rulerButton->setFixedSize(TOOL_BUTTON_SIZE);
btnList.append(m_rulerButton);
```

在按钮组点击分发中新增：

```cpp
if (m_rulerButton->isChecked()) {
    emit changeShotToolFunc("ruler");
}
```

在程序化工具选择中支持：

```cpp
} else if (shape == "ruler") {
    m_rulerButton->click();
}
```

在工具栏位置计算中支持：

```cpp
} else if (shape == "ruler") {
    x = m_rulerButton->x();
}
```

### `src/accessibility/acTextDefine.h`

新增 accessibility 常量：

```cpp
#define AC_SUBTOOLWIDGET_RULER_BUTTON "ruler_button"// 截图 尺子按钮
```

### `assets/icons/icons.qrc`

新增尺子图标资源：

```xml
<file>texts/ruler-normal_32px.svg</file>
```

### `assets/icons/texts/ruler-normal_32px.svg`

新增尺子工具图标。

### `src/widgets/shapeswidget.h`

新增：

- `RulerAction` 状态枚举。
- 尺子几何辅助函数声明。
- 尺子绘制和鼠标交互函数声明。
- 尺子临时状态字段。

主要新增函数：

```cpp
void initRulerIfNeeded();
QPointF rulerAxisUnit() const;
QPointF rulerNormalUnit() const;
QPointF rulerPointAt(qreal t) const;
qreal projectPointToRuler(QPointF pos) const;
QPolygonF rulerBodyPolygon() const;
QPointF rulerRotateHandlePoint() const;
RulerAction hitTestRuler(QPointF pos) const;
void paintRulerOverlay(QPainter &painter);
bool handleRulerPress(QPointF pos);
bool handleRulerMove(QPointF pos);
void handleRulerRelease();
```

### `src/widgets/shapeswidget.cpp`

主要实现内容：

1. 选择 `"ruler"` 工具时初始化尺子：

```cpp
if (shapeType == "ruler") {
    clearSelected();
    initRulerIfNeeded();
    updateCursorShape();
    update();
}
```

2. 鼠标按下时优先消费尺子事件：

```cpp
if (m_currentType == "ruler" && e->button() == Qt::LeftButton && handleRulerPress(e->pos())) {
    DFrame::mousePressEvent(e);
    return;
}
```

3. 鼠标移动时优先处理尺子移动、旋转、标记拖拽：

```cpp
if (handleRulerMove(e->pos())) {
    DFrame::mouseMoveEvent(e);
    return;
}
```

4. 鼠标释放时提前结束尺子交互，避免进入普通图元落库路径：

```cpp
if (m_currentType == "ruler") {
    handleRulerRelease();
    DFrame::mouseReleaseEvent(e);
    return;
}
```

5. Delete/Backspace 在尺子模式下不删除普通标注，而是依次清除 B 点、A 点、隐藏尺子：

```cpp
if (m_currentType == "ruler") {
    if (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace) {
        if (m_rulerMarkBVisible) {
            m_rulerMarkBVisible = false;
        } else if (m_rulerMarkAVisible) {
            m_rulerMarkAVisible = false;
        } else {
            m_rulerVisible = false;
            m_rulerPressed = false;
            m_rulerAction = RulerAction::None;
        }
        updateCursorShape();
        update();
        return;
    }

    qDebug() << "ShapeWidget Exist keyEvent:" << keyEvent->text();
    return;
}
```

6. `updateCursorShape()` 对 `"ruler"` 做专门处理，避免调用不存在的 `BaseUtils::setCursorShape("ruler")`：

- 移动尺子时显示手型。
- 旋转手柄显示旋转光标。
- A/B 标记放置或拖动显示十字光标。
- 普通区域显示箭头。

### `src/widgets/sidebar.cpp`

尺子第一版不需要颜色、线宽、形状等二级配置。侧栏在 `"ruler"` 模式下显式隐藏相关面板：

```cpp
if (func == "ruler") {
    qCDebug(dsrApp) << "Ruler mode: hiding sidebar tool panels";
    m_shapeTool->hide();
    m_seperator1->hide();
    m_shotTool->hide();
    m_colorTool->hide();
    m_seperator->hide();
    m_aiAssistantTool->hide();
    setMinimumSize(TOOLBAR_WIDGET_SIZE1);
    resize(TOOLBAR_WIDGET_SIZE1);
    m_currentFunc = func;
    layout()->invalidate();
    layout()->activate();
    updateGeometry();
    update();
    return;
}
```

这样不会让 `ColorToolWidget` 或 `ShotToolWidget` 按 `"ruler"` 去读取不存在的配置。

## 当前功能行为

### 已支持

- 工具栏出现尺子按钮。
- 点击尺子按钮进入 `"ruler"` 工具。
- 尺子在截图编辑区域显示。
- 拖动尺子主体可以移动尺子。
- 拖动旋转手柄可以旋转尺子。
- 点击尺子轴线放置 A 点。
- 再次点击尺子轴线放置或更新 B 点。
- 拖动 A/B 点可以实时调整测量位置。
- A/B 都存在时显示距离，例如 `128 px`。
- Delete/Backspace 在尺子模式下清除尺子标记或隐藏尺子。
- 切换到其他标注工具后，尺子不再显示。
- 保存截图时尺子不会进入最终图片。
- 尺子不会进入 `m_shapes`，不会作为普通标注参与撤销。

### 第一版暂不支持

- 物理单位 cm/mm。
- DPI/缩放校准。
- 多把尺子。
- 将测量结果保存为普通标注。
- 快捷键切换尺子。
- 尺子颜色/样式配置。

## 验证状态

已做静态检查：

```bash
git diff --check
```

结果：未发现空白格式问题。

构建未完成，原因是环境缺少系统开发依赖，与尺子代码本身无关。用户本地构建日志显示：

```text
Project ERROR: libusb-1.0 development package not found
```

需要安装 `libusb-1.0` 开发包后才能继续进入 C++ 编译阶段。

## 注意事项

本次过程中曾尝试运行构建，构建脚本更新了多语言翻译文件 `translations/*.ts`，并产生了未跟踪的 `build_qt6/` 目录。按照用户要求，后续不再运行编译或单测，也没有自动删除或回滚这些构建副产物。

## 后续建议

如果继续完善，可以考虑：

1. 增加尺子快捷键，例如 `M`，但需要先检查快捷键冲突。
2. 增加“重置尺子位置”入口。
3. 增加“复制测量结果”或“把距离文本转成普通标注”的能力。
4. 如果产品需要，可以增加物理单位和比例尺校准，但这应作为单独功能设计。
