// Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co.,Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shottoolwidget.h"
#include "tooltips.h"
#include "../utils/configsettings.h"
#include "../utils/baseutils.h"
#include "../utils.h"
#include "../utils/log.h"
#include "../accessibility/acTextDefine.h"
#include "../main_window.h"
#include "slider.h"

#include <DSlider>
#include <DLineEdit>
#include <DMenu>
#include <DFrame>
#include <DBlurEffectWidget>
#include <DPalette>

#include <QAction>
#include <QButtonGroup>
#include <QStyleFactory>
#include <QLine>
#include <QDebug>
#include <QPainter>

DGUI_USE_NAMESPACE
DWIDGET_USE_NAMESPACE

namespace {
//const int TOOLBAR_HEIGHT = 223;
//const int TOOLBAR_WIDTH = 40;
const QSize TOOL_ICON_SIZE = QSize(30, 30);
const QSize TOOL_BUTTON_SIZE = QSize(36, 36);
const int MEASURE_BUTTON_SPACING = 6;
const QMargins MEASURE_LAYOUT_MARGINS = QMargins(8, 0, 8, 0);
const int STEP_NUMBER_BUTTON_SPACING = 6;
const QMargins STEP_NUMBER_LAYOUT_MARGINS = QMargins(8, 0, 8, 0);

QIcon stepNumberColorIcon(const QColor &color, bool numberPart)
{
    QPixmap pixmap(30, 30);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QRectF markerRect(5, 6, 20, 18);
    painter.setPen(QPen(QColor(17, 24, 39, 80), 1));
    painter.setBrush(numberPart ? QColor(255, 255, 255, 235) : color);
    painter.drawRoundedRect(markerRect, 7, 7);
    painter.setPen(numberPart ? color : QColor(255, 255, 255));
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(15);
    painter.setFont(font);
    painter.drawText(markerRect, Qt::AlignCenter, QStringLiteral("1"));

    return QIcon(pixmap);
}
}

ShotToolWidget::ShotToolWidget(MainWindow *pmainwindow, DWidget *parent) :
    DStackedWidget(parent),
    m_thicknessBtnGroup(nullptr),
    m_pMainWindow(pmainwindow)
{
    qCDebug(dsrApp) << "ShotToolWidget constructor called.";
    initWidget();
}

void ShotToolWidget::initWidget()
{
    qCDebug(dsrApp) << "ShotToolWidget::initWidget called.";
//    this->setAttribute(Qt::WA_StyledBackground, true);
//    this->setStyleSheet("background-color: rgb(0,255, 0)");
//    setMinimumSize(TOOLBAR_WIDTH, TOOLBAR_HEIGHT);
    hintFilter = new HintFilter(this);
    
    // 初始化各个组件
    initEffectLabel();
    addWidget(m_effectSubTool);
    
    // 初始化标志
    m_textInitFlag = false;
    m_thicknessInitFlag = false;
    m_measureInitFlag = false;
    
    // 初始化指针
    m_thicknessLabel = nullptr;
    m_textSubTool = nullptr;
    m_measureSubTool = nullptr;
    m_stepNumberSubTool = nullptr;
    
    qCDebug(dsrApp) << "ShotToolWidget::initWidget completed.";
}

void ShotToolWidget::installTipHint(QWidget *w, const QString &hintstr)
{
    qCDebug(dsrApp) << "Installing tip hint for widget:" << w << "with hint:" << hintstr;
    // TODO: parent must be mainframe
    auto hintWidget = new ToolTips("", m_pMainWindow);
    hintWidget->hide();
    hintWidget->setText(hintstr);
//    hintWidget->setFixedHeight(32);
    w->setProperty("HintWidget", QVariant::fromValue<QWidget *>(hintWidget));
    w->installEventFilter(hintFilter);

}

//初始化模糊功能工具栏
void ShotToolWidget::initEffectLabel()
{
    qCDebug(dsrApp) << "ShotToolWidget::initEffectLabel called.";
    m_effectSubTool = new DLabel(this);
    DBlurEffectWidget *t_blurArea = new DBlurEffectWidget(this);
    t_blurArea->setBlurRectXRadius(7);
    t_blurArea->setBlurRectYRadius(7);
    t_blurArea->setRadius(15);
    t_blurArea->setMode(DBlurEffectWidget::GaussianBlur);
    t_blurArea->setBlurEnabled(true);
    t_blurArea->setBlendMode(DBlurEffectWidget::InWidgetBlend);
    t_blurArea->setMaskColor(QColor(255, 255, 255, 0));

    QHBoxLayout *t_blurAreaLayout = new QHBoxLayout(this);
    t_blurAreaLayout->setContentsMargins(0, 0, 0, 0);

    //分割线
    DVerticalLine *t_seperator = new DVerticalLine(this);
    t_seperator->setFixedSize(3, 30);
    DVerticalLine *t_seperator0 = new DVerticalLine(this);
    t_seperator0->setFixedSize(3, 30);
    DVerticalLine *t_seperator1 = new DVerticalLine(this);
    t_seperator1->setFixedSize(3, 30);
    t_seperator1->hide();

    QButtonGroup *effectTypeBtnGroup = new QButtonGroup(this);
    effectTypeBtnGroup->setExclusive(true);

    ToolButton *mosaicBut = new ToolButton(this);
    mosaicBut->setFixedSize(TOOL_BUTTON_SIZE);
    mosaicBut->setIconSize(TOOL_ICON_SIZE);
    mosaicBut->setIcon(QIcon::fromTheme(QString("Mosaic_normal")));
    effectTypeBtnGroup->addButton(mosaicBut);
    ToolButton *blurBut = new ToolButton(this);
    blurBut->setFixedSize(TOOL_BUTTON_SIZE);
    blurBut->setIconSize(TOOL_ICON_SIZE);
    blurBut->setIcon(QIcon::fromTheme(QString("vague_normal")));
    effectTypeBtnGroup->addButton(blurBut);


    QButtonGroup *shapBtnGroup = new QButtonGroup(this);
    shapBtnGroup->setExclusive(true);

    // 创建矩形按钮
    ToolButton *rectBut = new ToolButton(this);
    rectBut->setFixedSize(TOOL_BUTTON_SIZE);
    rectBut->setIconSize(TOOL_ICON_SIZE);
    rectBut->setIcon(QIcon::fromTheme(QString("rectangle-normal")));
    installTipHint(rectBut, tr("Rectangle\nPress and hold Shift to draw a square"));
    shapBtnGroup->addButton(rectBut);
    
    // 创建椭圆按钮
    ToolButton *ovalBut = new ToolButton(this);
    ovalBut->setFixedSize(TOOL_BUTTON_SIZE);
    ovalBut->setIconSize(TOOL_ICON_SIZE);
    ovalBut->setIcon(QIcon::fromTheme(QString("oval-normal")));
    installTipHint(ovalBut, tr("Ellipse\nPress and hold Shift to draw a circle"));
    shapBtnGroup->addButton(ovalBut);
    
    ToolButton *penBut = new ToolButton(this);
    penBut->setFixedSize(TOOL_BUTTON_SIZE);
    penBut->setIconSize(TOOL_ICON_SIZE);
    penBut->setIcon(QIcon::fromTheme(QString("effect_pen")));
    installTipHint(penBut, tr("Brush\nPress and hold Shift to draw a straight line"));
    shapBtnGroup->addButton(penBut);

    //模糊强度滑动条
    Slider *t_radiusSize = new Slider(Qt::Horizontal, this);
    t_radiusSize->slider()->setTickInterval(1);
    t_radiusSize->setMinimum(0);
    t_radiusSize->setMaximum(10);
    t_radiusSize->setLeftIcon(QIcon::fromTheme(QString("mosaic1")));
    t_radiusSize->setRightIcon(QIcon::fromTheme(QString("mosaic2")));
    t_radiusSize->setIconSize(QSize(16, 16));
    installTipHint(t_radiusSize, tr("Adjust blur strength (Scroll to adjust it)"));
    int t_fontSize = ConfigSettings::instance()->getValue("effect", "radius").toInt();
    t_radiusSize->setValue(t_fontSize);
    t_radiusSize->setMouseWheelEnabled(true);

    //笔刷大小滑动条
    Slider *t_lineWidthSize = new Slider(Qt::Horizontal, this);
    //t_lineWidthSize->setStyleSheet("border-image: url(:/icons/deepin/builtin/texts/vague_normal_32px.svg)");
    t_lineWidthSize->slider()->setTickInterval(1);
    t_lineWidthSize->setMinimum(0);
    t_lineWidthSize->setMaximum(10);
    t_lineWidthSize->slider()->setSingleStep(2);
    t_lineWidthSize->setLeftIcon(QIcon::fromTheme(QString("effect_pen_w")));
    //t_lineWidthSize->setRightIcon(QIcon::fromTheme(QString("effect_pen_w")));
    t_lineWidthSize->setIconSize(QSize(16, 16));
    installTipHint(t_lineWidthSize, tr("Adjust brush size (Scroll to adjust it)"));
    int t_width = ConfigSettings::instance()->getValue("effect", "line_width").toInt();
    t_lineWidthSize->setValue(t_width);
    t_lineWidthSize->setMouseWheelEnabled(true);

    DLabel *penWidth = new DLabel(this);
    penWidth->setElideMode(Qt::TextElideMode::ElideLeft);
    penWidth->setFixedSize(24, 24);
    DFontSizeManager::instance()->bind(penWidth, DFontSizeManager::T7);
    penWidth->setText(QString("%1").arg((t_width + 1) * 2));

    QHBoxLayout *widthSizeLayout = new QHBoxLayout(this);
    widthSizeLayout->setContentsMargins(0, 0, 0, 0);
    widthSizeLayout->addWidget(t_lineWidthSize);
    widthSizeLayout->addWidget(penWidth);

    penWidth->hide();
    t_lineWidthSize->hide();

    t_blurAreaLayout->addWidget(mosaicBut);
    t_blurAreaLayout->addWidget(blurBut);
    t_blurAreaLayout->addWidget(t_seperator);
    t_blurAreaLayout->addWidget(rectBut);
    t_blurAreaLayout->addWidget(ovalBut);
    t_blurAreaLayout->addWidget(penBut);
    t_blurAreaLayout->addWidget(t_seperator0);
    t_blurAreaLayout->addWidget(t_radiusSize);
    t_blurAreaLayout->addWidget(t_seperator1);
    t_blurAreaLayout->addLayout(widthSizeLayout);
    t_blurArea->setLayout(t_blurAreaLayout);

    bool isBlur = ConfigSettings::instance()->getValue("effect", "isBlur").toBool();
    if (isBlur) {
        qCDebug(dsrApp) << "Blur effect is enabled.";
        blurBut->setChecked(true);
        t_radiusSize->setLeftIcon(QIcon::fromTheme(QString("effect_pen1")));
        t_radiusSize->setRightIcon(QIcon::fromTheme(QString("effect_pen2")));
    } else {
        qCDebug(dsrApp) << "Mosaic effect is enabled.";
        mosaicBut->setChecked(true);
        t_radiusSize->setLeftIcon(QIcon::fromTheme(QString("mosaic1")));
        t_radiusSize->setRightIcon(QIcon::fromTheme(QString("mosaic2")));
    }

    int isOvalBut = ConfigSettings::instance()->getValue("effect", "isOval").toInt();

    if (isOvalBut == 0) {
        qCDebug(dsrApp) << "Oval tool is selected.";
        ovalBut->setChecked(true);
    } else if (isOvalBut == 1) {
        qCDebug(dsrApp) << "Rectangle tool is selected.";
        rectBut->setChecked(true);
    } else if (isOvalBut == 2) {
        qCDebug(dsrApp) << "Pen tool is selected, showing width controls.";
        penBut->setChecked(true);
        t_lineWidthSize->show();
        penWidth->show();
        t_seperator1->show();
    } else {
        // 默认选择矩形
        qCDebug(dsrApp) << "Default to rectangle tool.";
        rectBut->setChecked(true);
    }

    connect(effectTypeBtnGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
    [ = ] {
        qCDebug(dsrApp) << "Effect type button clicked.";
        ConfigSettings::instance()->setValue("effect", "isBlur", blurBut->isChecked());
        if (blurBut->isChecked()) {
            qCDebug(dsrApp) << "Blur button checked, setting blur icons.";
            t_radiusSize->setLeftIcon(QIcon::fromTheme(QString("effect_pen1")));
            t_radiusSize->setRightIcon(QIcon::fromTheme(QString("effect_pen2")));
        } else {
            qCDebug(dsrApp) << "Mosaic button checked, setting mosaic icons.";
            t_radiusSize->setLeftIcon(QIcon::fromTheme(QString("mosaic1")));
            t_radiusSize->setRightIcon(QIcon::fromTheme(QString("mosaic2")));
        }
    });


    connect(shapBtnGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
    [ = ] {
        qCDebug(dsrApp) << "Shape type button clicked.";
        int value = 2;
        if (rectBut->isChecked()) {
            value = 1;
        } else if (ovalBut->isChecked()) {
            value = 0;
        }

        qCDebug(dsrApp) << "Shape type changed to:" << value;

        if (penBut->isChecked()) {
            qCDebug(dsrApp) << "Pen tool selected, showing width controls";
            t_lineWidthSize->show();
            penWidth->show();
            t_seperator1->show();
        } else {
            qCDebug(dsrApp) << "Shape tool selected, hiding width controls";
            t_lineWidthSize->hide();
            penWidth->hide();
            t_seperator1->hide();
        }
        ConfigSettings::instance()->setValue("effect", "isOval", value);
    });


    connect(t_radiusSize, &DSlider::valueChanged, this, [ = ] {
        int t_value = t_radiusSize->value();
        qCDebug(dsrApp) << "Effect radius changed to:" << t_value;
        ConfigSettings::instance()->setValue("effect", "radius", t_value);
    });

    connect(t_lineWidthSize, &DSlider::valueChanged, this, [ = ] {
        int t_value = t_lineWidthSize->value();
        qCDebug(dsrApp) << "Line width changed to:" << t_value;
        penWidth->setText(QString("%1").arg((t_value + 1) * 2));
        ConfigSettings::instance()->setValue("effect", "line_width", t_value);
    });

    QHBoxLayout *rectLayout = new QHBoxLayout(this);
    rectLayout->setContentsMargins(3, 3, 3, 3);

    rectLayout->addWidget(t_blurArea);
    m_effectSubTool->setLayout(rectLayout);

    addWidget(m_effectSubTool);
}

//初始化文字工具工具栏
void ShotToolWidget::initTextLabel()
{
    qCDebug(dsrApp) << "ShotToolWidget::initTextLabel called.";
    m_textSubTool = new DLabel(this);

    DBlurEffectWidget *t_blurArea = new DBlurEffectWidget(this);
    t_blurArea->setBlurRectXRadius(7);
    t_blurArea->setBlurRectYRadius(7);
    t_blurArea->setRadius(15);
    t_blurArea->setMode(DBlurEffectWidget::GaussianBlur);
    t_blurArea->setBlurEnabled(true);
    t_blurArea->setBlendMode(DBlurEffectWidget::InWidgetBlend);
    t_blurArea->setMaskColor(QColor(255, 255, 255, 0));
    //t_blurArea->setFixedSize(TOOL_SLIDERBlUR_SIZE);

    QHBoxLayout *t_blurAreaLayout = new QHBoxLayout(this);
    Slider *t_textFontSize = new Slider(Qt::Horizontal, this);
    t_textFontSize->slider()->setTickInterval(1);
    //t_textFontSize->setFixedSize(TOOL_SLIDER_SIZE);
    t_textFontSize->setMinimum(0);
    t_textFontSize->setMaximum(10);
    t_textFontSize->setLeftIcon(QIcon::fromTheme("Aa small_normal"));
    t_textFontSize->setRightIcon(QIcon::fromTheme("Aa big_normal"));
    t_textFontSize->setIconSize(TOOL_ICON_SIZE);
    t_textFontSize->setMouseWheelEnabled(true);
    installTipHint(t_textFontSize, tr("Adjust text size (Scroll to adjust it)"));

    static const int indexTofontsize[11] = {9, 10, 12, 14, 18, 24, 36, 48, 64, 72, 96};
    int t_fontSize = ConfigSettings::instance()->getValue("text", "fontsize").toInt();
    for (int i = 0; i < 11; ++i) {
        if (indexTofontsize[i] == t_fontSize) {
            t_textFontSize->setValue(i);
            break;
        }
    }
    t_blurAreaLayout->setContentsMargins(0, 0, 0, 0);
    t_blurAreaLayout->addWidget(t_textFontSize);
    t_blurArea->setLayout(t_blurAreaLayout);

    connect(t_textFontSize, &DSlider::valueChanged, this, [ = ] {
        int t_value = t_textFontSize->value();
        ConfigSettings::instance()->setValue("text", "fontsize", indexTofontsize[t_value]);
    });

    QHBoxLayout *rectLayout = new QHBoxLayout(this);
    rectLayout->setContentsMargins(0, 0, 5, 0);

    rectLayout->addWidget(t_blurArea);

    m_textSubTool->setLayout(rectLayout);
}

void ShotToolWidget::initMeasureLabel()
{
    qCDebug(dsrApp) << "ShotToolWidget::initMeasureLabel called.";
    m_measureSubTool = new DLabel(this);

    QButtonGroup *measureBtnGroup = new QButtonGroup(this);
    measureBtnGroup->setExclusive(true);

    m_lineMeasureButton = new ToolButton(this);
    m_lineMeasureButton->setFixedSize(TOOL_BUTTON_SIZE);
    m_lineMeasureButton->setIconSize(TOOL_ICON_SIZE);
    m_lineMeasureButton->setIcon(QIcon::fromTheme(QStringLiteral("line-normal")));
    m_lineMeasureButton->setCheckable(true);
    installTipHint(m_lineMeasureButton, tr("Point measurement"));
    Utils::setAccessibility(m_lineMeasureButton, AC_SHOTTOOLWIDGET_LINE_MEASURE_BUTTON);
    measureBtnGroup->addButton(m_lineMeasureButton);

    m_rulerMeasureButton = new ToolButton(this);
    m_rulerMeasureButton->setFixedSize(TOOL_BUTTON_SIZE);
    m_rulerMeasureButton->setIconSize(TOOL_ICON_SIZE);
    m_rulerMeasureButton->setIcon(QIcon::fromTheme(QStringLiteral("ruler-normal")));
    m_rulerMeasureButton->setCheckable(true);
    installTipHint(m_rulerMeasureButton, tr("Ruler measurement"));
    Utils::setAccessibility(m_rulerMeasureButton, AC_SHOTTOOLWIDGET_RULER_MEASURE_BUTTON);
    measureBtnGroup->addButton(m_rulerMeasureButton);

    connect(measureBtnGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this, [ = ](QAbstractButton *button) {
        if (!m_pMainWindow) {
            return;
        }

        if (button == m_lineMeasureButton) {
            m_pMainWindow->changeShotToolEvent(QStringLiteral("measure-line"));
        } else if (button == m_rulerMeasureButton) {
            m_pMainWindow->changeShotToolEvent(QStringLiteral("ruler"));
        }
    });

    QHBoxLayout *layout = new QHBoxLayout(m_measureSubTool);
    layout->setContentsMargins(MEASURE_LAYOUT_MARGINS);
    layout->setSpacing(MEASURE_BUTTON_SPACING);
    layout->addWidget(m_lineMeasureButton);
    layout->addWidget(m_rulerMeasureButton);
    m_measureSubTool->setLayout(layout);
    m_measureSubTool->setFixedWidth(TOOL_BUTTON_SIZE.width() * 2
                                    + MEASURE_BUTTON_SPACING
                                    + MEASURE_LAYOUT_MARGINS.left()
                                    + MEASURE_LAYOUT_MARGINS.right());

    m_lineMeasureButton->setChecked(true);
}

void ShotToolWidget::initStepNumberLabel()
{
    qCDebug(dsrApp) << "ShotToolWidget::initStepNumberLabel called.";
    m_stepNumberSubTool = new DLabel(this);
    m_stepNumberStyleBtnGroup = new QButtonGroup(this);
    m_stepNumberStyleBtnGroup->setExclusive(true);
    m_stepNumberColorBtnGroup = new QButtonGroup(this);
    m_stepNumberColorBtnGroup->setExclusive(true);

    struct StepStyleButton {
        const char *iconName;
        const char *tip;
        const char *accessibleName;
        int style;
    };

    const QList<StepStyleButton> buttons = {
        {"step-solid-normal", QT_TR_NOOP("Solid circle"), AC_SHOTTOOLWIDGET_STEP_SOLID_BUTTON, 0},
        {"step-outline-normal", QT_TR_NOOP("Outline circle"), AC_SHOTTOOLWIDGET_STEP_OUTLINE_BUTTON, 1},
        {"step-pin-normal", QT_TR_NOOP("Pin marker"), AC_SHOTTOOLWIDGET_STEP_PIN_BUTTON, 2},
        {"step-dot-normal", QT_TR_NOOP("Dot marker"), AC_SHOTTOOLWIDGET_STEP_DOT_BUTTON, 3},
        {"step-bubble-normal", QT_TR_NOOP("Bubble marker"), AC_SHOTTOOLWIDGET_STEP_BUBBLE_BUTTON, 4},
    };

    QHBoxLayout *layout = new QHBoxLayout(m_stepNumberSubTool);
    layout->setContentsMargins(STEP_NUMBER_LAYOUT_MARGINS);
    layout->setSpacing(STEP_NUMBER_BUTTON_SPACING);

    for (const StepStyleButton &buttonInfo : buttons) {
        ToolButton *button = new ToolButton(this);
        button->setFixedSize(TOOL_BUTTON_SIZE);
        button->setIconSize(TOOL_ICON_SIZE);
        button->setIcon(QIcon::fromTheme(QString::fromLatin1(buttonInfo.iconName)));
        button->setCheckable(true);
        installTipHint(button, tr(buttonInfo.tip));
        Utils::setAccessibility(button, QString::fromLatin1(buttonInfo.accessibleName));
        m_stepNumberStyleBtnGroup->addButton(button, buttonInfo.style);
        layout->addWidget(button);
    }

    DVerticalLine *separator = new DVerticalLine(this);
    separator->setFixedSize(3, 26);
    layout->addWidget(separator);

    auto updateStepNumberColorButtons = [ = ] {
        const QColor shapeColor = BaseUtils::colorIndexOf(ConfigSettings::instance()->getValue("step-number", "color_index").toInt());
        const QColor textColor = BaseUtils::colorIndexOf(ConfigSettings::instance()->getValue("step-number", "text_color_index").toInt());
        if (m_stepNumberShapeColorButton) {
            m_stepNumberShapeColorButton->setIcon(stepNumberColorIcon(shapeColor, false));
        }
        if (m_stepNumberTextColorButton) {
            m_stepNumberTextColorButton->setIcon(stepNumberColorIcon(textColor, true));
        }
    };

    m_stepNumberShapeColorButton = new ToolButton(this);
    m_stepNumberShapeColorButton->setFixedSize(TOOL_BUTTON_SIZE);
    m_stepNumberShapeColorButton->setIconSize(QSize(30, 30));
    m_stepNumberShapeColorButton->setCheckable(true);
    installTipHint(m_stepNumberShapeColorButton, tr("Marker color"));
    Utils::setAccessibility(m_stepNumberShapeColorButton, AC_SHOTTOOLWIDGET_STEP_MARKER_COLOR_BUTTON);
    m_stepNumberColorBtnGroup->addButton(m_stepNumberShapeColorButton);
    layout->addWidget(m_stepNumberShapeColorButton);

    m_stepNumberTextColorButton = new ToolButton(this);
    m_stepNumberTextColorButton->setFixedSize(TOOL_BUTTON_SIZE);
    m_stepNumberTextColorButton->setIconSize(QSize(30, 30));
    m_stepNumberTextColorButton->setCheckable(true);
    installTipHint(m_stepNumberTextColorButton, tr("Number color"));
    Utils::setAccessibility(m_stepNumberTextColorButton, AC_SHOTTOOLWIDGET_STEP_TEXT_COLOR_BUTTON);
    m_stepNumberColorBtnGroup->addButton(m_stepNumberTextColorButton);
    layout->addWidget(m_stepNumberTextColorButton);

    updateStepNumberColorButtons();
    m_stepNumberShapeColorButton->setChecked(true);

    connect(m_stepNumberColorBtnGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this, [ = ](QAbstractButton *button) {
        m_stepNumberColorKey = (button == m_stepNumberTextColorButton)
                               ? QStringLiteral("text_color_index")
                               : QStringLiteral("color_index");
        emit stepNumberColorTargetChanged(m_stepNumberColorKey);
    });

    connect(ConfigSettings::instance(), &ConfigSettings::shapeConfigChanged,
            this, [ = ](const QString &group, const QString &key, int index) {
        if (group != "step-number") {
            return;
        }
        if (key == "color_index" && m_stepNumberShapeColorButton) {
            m_stepNumberShapeColorButton->setIcon(stepNumberColorIcon(BaseUtils::colorIndexOf(qBound(0, index, 11)), false));
        } else if (key == "text_color_index" && m_stepNumberTextColorButton) {
            m_stepNumberTextColorButton->setIcon(stepNumberColorIcon(BaseUtils::colorIndexOf(qBound(0, index, 11)), true));
        }
    });

    const int currentStyle = ConfigSettings::instance()->getValue("step-number", "style").toInt();
    if (QAbstractButton *button = m_stepNumberStyleBtnGroup->button(currentStyle)) {
        button->setChecked(true);
    } else if (QAbstractButton *button = m_stepNumberStyleBtnGroup->button(0)) {
        button->setChecked(true);
    }

    connect(m_stepNumberStyleBtnGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this, [ = ](QAbstractButton *button) {
        const int style = m_stepNumberStyleBtnGroup->id(button);
        if (style >= 0) {
            ConfigSettings::instance()->setValue("step-number", "style", style);
        }
    });

    m_stepNumberSubTool->setLayout(layout);
    const int buttonCount = buttons.size() + 2;
    m_stepNumberSubTool->setFixedWidth(TOOL_BUTTON_SIZE.width() * buttonCount
                                       + STEP_NUMBER_BUTTON_SPACING * buttonCount
                                       + separator->width()
                                       + STEP_NUMBER_LAYOUT_MARGINS.left()
                                       + STEP_NUMBER_LAYOUT_MARGINS.right());
}

void ShotToolWidget::initThicknessLabel()
{
    qCDebug(dsrApp) << "ShotToolWidget::initThicknessLabel called.";
    m_thicknessLabel = new DLabel(this);
    //粗细按钮组
    m_thicknessBtnGroup = new QButtonGroup(this);
    m_thicknessBtnGroup->setExclusive(true);

    //水平布局
    QHBoxLayout *hBox = new QHBoxLayout(m_thicknessLabel);
    hBox->setContentsMargins(0, 0, 0, 0);

    //根据三个粗细按钮名称创建对应的控件
    QStringList thicknessBtnName;
    thicknessBtnName << "small" << "medium" << "big";
    for (int i = 0; i < thicknessBtnName.size(); i++) {
        ToolButton *thicknessBtn = new ToolButton(this);
        Utils::setAccessibility(thicknessBtn, QString("%1_thicknessBtn").arg(thicknessBtnName[i]));
        thicknessBtn->setFixedSize(TOOL_BUTTON_SIZE);
        thicknessBtn->setIconSize(TOOL_ICON_SIZE);
        thicknessBtn->setIcon(QIcon::fromTheme(QString("brush %1_normal").arg(thicknessBtnName[i])));
        m_thicknessBtnGroup->addButton(thicknessBtn);
        m_thicknessBtnGroup->setId(thicknessBtn, i);
        hBox->addWidget(thicknessBtn);
    }

    //点击粗细按钮组中对应的按钮触发写到配置文件的操作
    connect(m_thicknessBtnGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
    [ = ](QAbstractButton * button) {
        ToolButton *thicknessBtn = static_cast<ToolButton *>(button) ;
        if (thicknessBtn->isChecked()) {
            thicknessBtn->update();
            ConfigSettings::instance()->setValue(m_shapeName, "line_width", m_thicknessBtnGroup->id(thicknessBtn));
        }
    });
    m_thicknessLabel->setLayout(hBox);
    addWidget(m_thicknessLabel);
}

void ShotToolWidget::switchContent(QString shapeType)
{
    qCDebug(dsrApp) << "ShotToolWidget::switchContent called with shapeType:" << shapeType;
    m_shapeName = shapeType;
    
    if (shapeType.isEmpty()) {
        qCDebug(dsrApp) << "Empty shape type, ignoring.";
        return;
    }
    
    qCDebug(dsrApp) << "Current widget:" << (currentWidget() == m_thicknessLabel ? "thickness" : 
                                          (currentWidget() == m_textSubTool ? "text" : 
                                          (currentWidget() == m_effectSubTool ? "effect" : "unknown")));
    
    // 根据形状类型切换到相应的工具栏
    if (shapeType == "rectangle" || shapeType == "oval" || shapeType == "gio" || 
        shapeType == "line" || shapeType == "arrow" || shapeType == "pen") {
        
        qCDebug(dsrApp) << "Switching to thickness panel for shape:" << shapeType;
        if (m_textSubTool)
                removeWidget(m_textSubTool);
        // 确保粗细调整面板已初始化
        if (m_thicknessInitFlag == false) {
            qCDebug(dsrApp) << "Initializing thickness panel.";
            initThicknessLabel();
            m_thicknessInitFlag = true;
        }
        
        // 设置粗细
        int linewidth_index = -1;
        if (shapeType == "gio") {
            // 对于几何图形，使用矩形的粗细设置
            linewidth_index = ConfigSettings::instance()->getValue("rectangle", "line_width").toInt();
            qCDebug(dsrApp) << "Using rectangle line width for gio:" << linewidth_index;
        } else {
            linewidth_index = ConfigSettings::instance()->getValue(shapeType, "line_width").toInt();
            qCDebug(dsrApp) << "Using line width for" << shapeType << ":" << linewidth_index;
        }
        
        if (linewidth_index != -1 && linewidth_index < m_thicknessBtnGroup->buttons().size()) {
            qCDebug(dsrApp) << "Setting line width to:" << linewidth_index;
            m_thicknessBtnGroup->button(linewidth_index)->click();
        } else {
            qCDebug(dsrApp) << "Invalid line width index:" << linewidth_index;
        }
        addWidget(m_thicknessLabel);
        // 显示粗细调整面板
        setCurrentWidget(m_thicknessLabel);
        qCDebug(dsrApp) << "Switched to thickness panel.";
    }
    // 文本会加载字体调整面板
    else if (shapeType == "text") {
        qCDebug(dsrApp) << "Switching to text panel.";
        if (m_thicknessLabel)
                removeWidget(m_thicknessLabel);
        if (m_textInitFlag == false) {
            qCDebug(dsrApp) << "Initializing text panel.";
            initTextLabel();
            m_textInitFlag = true;
        }
        addWidget(m_textSubTool);
        setCurrentWidget(m_textSubTool);
        qCDebug(dsrApp) << "Switched to text panel.";
    }
    // 模糊会加载模糊调整面板
    else if (shapeType == "effect") {
        qCDebug(dsrApp) << "Switching to effect panel.";
        setCurrentWidget(m_effectSubTool);
        qCDebug(dsrApp) << "Switched to effect panel.";
    } else if (shapeType == "measure" || shapeType == "ruler" || shapeType == "measure-line") {
        qCDebug(dsrApp) << "Switching to measure panel.";
        if (m_measureInitFlag == false) {
            initMeasureLabel();
            m_measureInitFlag = true;
        }
        if (shapeType == "ruler" && m_rulerMeasureButton) {
            m_rulerMeasureButton->setChecked(true);
        } else if (m_lineMeasureButton) {
            m_lineMeasureButton->setChecked(true);
        }
        addWidget(m_measureSubTool);
        setCurrentWidget(m_measureSubTool);
        qCDebug(dsrApp) << "Switched to measure panel.";
    } else if (shapeType == "step-number") {
        qCDebug(dsrApp) << "Switching to step number panel.";
        if (m_stepNumberInitFlag == false) {
            initStepNumberLabel();
            m_stepNumberInitFlag = true;
        }
        const int currentStyle = ConfigSettings::instance()->getValue("step-number", "style").toInt();
        if (m_stepNumberStyleBtnGroup && m_stepNumberStyleBtnGroup->button(currentStyle)) {
            m_stepNumberStyleBtnGroup->button(currentStyle)->setChecked(true);
        }
        if (m_stepNumberShapeColorButton) {
            m_stepNumberColorKey = QStringLiteral("color_index");
            m_stepNumberShapeColorButton->setChecked(true);
        }
        addWidget(m_stepNumberSubTool);
        setCurrentWidget(m_stepNumberSubTool);
        qCDebug(dsrApp) << "Switched to step number panel.";
    } else {
        qCDebug(dsrApp) << "Unknown shape type:" << shapeType;
    }
}

void ShotToolWidget::colorChecked(QString colorType)
{
    Q_UNUSED(colorType);
}

void ShotToolWidget::shapeSelected(const QString &shape)
{
    qCDebug(dsrApp) << "ShotToolWidget::shapeSelected called with shape:" << shape;
    
    if (m_pMainWindow) {
        if (shape == "rectangle") {
            m_pMainWindow->changeShotToolEvent("rectangle");
        } else if (shape == "oval") {
            m_pMainWindow->changeShotToolEvent("oval");
        }
        ConfigSettings::instance()->setValue("shape", "current", shape);
    }
}
ShotToolWidget::~ShotToolWidget()
{
    qCDebug(dsrApp) << "ShotToolWidget::~ShotToolWidget called.";
    if (nullptr != hintFilter) {
        delete hintFilter;
        hintFilter = nullptr;
    }
}
