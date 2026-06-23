// Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co.,Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../utils/calculaterect.h"
#include "../utils/configsettings.h"
#include "../utils/tempfile.h"
#include "../utils/shapesutils.h"
#include "shapeswidget.h"
#include "../utils.h"
#include "../utils/log.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QGestureEvent>
#include <QClipboard>
#include <QPolygonF>
#include <QMenu>
#include <QtMath>

#include <cmath>

#define LINEWIDTH(index) (index*2+3)

const int DRAG_BOUND_RADIUS = 8;
const int SPACING = 12;
const int RESIZEPOINT_WIDTH = 15;
const QSize ROTATE_ICON_SIZE = QSize(30, 30);
const qreal RULER_MIN_LENGTH = 160;
const qreal RULER_MARK_RADIUS = 6;
const qreal RULER_RESIZE_HANDLE_RADIUS = 7;
const qreal RULER_ROTATE_HANDLE_DISTANCE = 42;
const qreal RULER_HIT_PADDING = 10;
const qreal RULER_EDGE_MARGIN = 24;
const qreal RULER_KEY_MOVE_STEP = 1;
const qreal RULER_KEY_FAST_MOVE_STEP = 10;
const qreal RULER_KEY_RESIZE_STEP = 10;
const qreal RULER_KEY_FAST_RESIZE_STEP = 50;
const qreal RULER_ROTATE_SNAP_DEGREES = 45;
const qreal RULER_LABEL_MARGIN = 6;
const qreal RULER_JAW_WIDTH = 1.0;
const qreal MEASURE_POINT_HIT_RADIUS = 8;
const qreal MEASUREMENT_SNAP_DISTANCE = 6;
const int MEASUREMENT_HISTORY_LIMIT = 5;

static QPainterPath spotlightPathFromPoints(FourPoints rectFPoints)
{
    if (rectFPoints.length() < 4) {
        return QPainterPath();
    }

    QPolygonF targetPolygon;
    targetPolygon << rectFPoints[0] << rectFPoints[1] << rectFPoints[3] << rectFPoints[2];
    const QRectF targetBounds = targetPolygon.boundingRect();
    if (targetBounds.width() < 2 || targetBounds.height() < 2) {
        return QPainterPath();
    }

    QPainterPath targetPath;
    targetPath.addPolygon(targetPolygon);
    targetPath.closeSubpath();
    return targetPath;
}

ShapesWidget::ShapesWidget(DWidget *parent)
    : DFrame(parent),
      m_lastAngle(0),
      m_isMoving(false),
      m_isSelected(false),
      m_isShiftPressed(false),
      m_editing(false),
      m_shapesIndex(-1),
      m_selectedIndex(-1),
      m_selectedOrder(-1),
      m_menuController(new MenuController)
{
    //订阅手势事件
    QList<Qt::GestureType> gestures;
    //    gestures << Qt::PanGesture;
    gestures << Qt::PinchGesture;
    //    gestures << Qt::SwipeGesture;
    gestures << Qt::TapGesture;
    //    gestures << Qt::TapGesture;
    //    gestures << Qt::TapAndHoldGesture;
    //    gestures << Qt::CustomGesture;
    //    gestures << Qt::LastGestureType;
    foreach (Qt::GestureType gesture, gestures)
        grabGesture(gesture);


    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);

    m_penColor = BaseUtils::colorIndexOf(ConfigSettings::instance()->getValue("pen", "color_index").toInt());

    connect(m_menuController, &MenuController::shapePressed,
            this, &ShapesWidget::shapePressed);
    //    connect(m_menuController, &MenuController::saveBtnPressed,
    //            this, &ShapesWidget::saveBtnPressed);
    connect(m_menuController, &MenuController::saveAction,
            this, &ShapesWidget::menuSaveSlot);
    connect(m_menuController, &MenuController::closeAction,
            this, &ShapesWidget::menuCloseSlot);
    connect(m_menuController, &MenuController::unDoAction,
            this, &ShapesWidget::undoDrawShapes);
    connect(m_menuController, &MenuController::menuNoFocus,
            this, &ShapesWidget::menuNoFocus);
    connect(this, &ShapesWidget::setShapesUndo,
            m_menuController, &MenuController::setUndoEnable);
    connect(ConfigSettings::instance(), &ConfigSettings::shapeConfigChanged, this, &ShapesWidget::updateSelectedShape);
    //m_sideBar = new SideBar(this);
    //m_sideBar->hide();
    m_currentCursor = QCursor().pos();
}

ShapesWidget::~ShapesWidget()
{
    if (m_menuController) {
        delete m_menuController;
        m_menuController = nullptr;
    }
}
//更新选中的形状
void ShapesWidget::updateSelectedShape(const QString &group,
                                       const QString &key, int index)
{
    qCDebug(dsrApp) << "Updating selected shape - group:" << group << "key:" << key << "index:" << index;
    
    if (m_isSelectedText) {
        qCDebug(dsrApp) << "Skipping update for selected text";
        m_isSelectedText = false;
        return;
    }
    
    if (group == m_currentShape.type && key == "color_index") {
        m_penColor = BaseUtils::colorIndexOf(index);
        qCDebug(dsrApp) << "Updated pen color to:" << m_penColor;
    }

    if (m_selectedIndex != -1 && m_selectedOrder != -1 && m_selectedOrder < m_shapes.length()) {
        qCDebug(dsrApp) << "Updating shape properties for selected shape at order:" << m_selectedOrder;
        
        if ((m_selectedShape.type == "arrow" || m_selectedShape.type == "line") && key != "color_index") {
            m_selectedShape.lineWidth = LINEWIDTH(index);
        } else if (m_selectedShape.type == group && key == "line_width") {
            m_selectedShape.lineWidth = LINEWIDTH(index);
        } else if (group == "text" && m_selectedShape.type == group && key == "color_index") {
            int tmpIndex = m_shapes[m_selectedOrder].index;
            if (m_editMap.contains(tmpIndex)) {
                m_selectedShape.colorIndex = index;
                m_editMap.value(tmpIndex)->setColor(BaseUtils::colorIndexOf(index));
                m_editMap.value(tmpIndex)->update();
            }

        } else if (group == "text" && m_selectedShape.type == group && key == "fontsize")  {
            qDebug() << "change font size";
            int tmpIndex = m_shapes[m_selectedOrder].index;
            if (m_editMap.contains(tmpIndex)) {
                m_editMap.value(tmpIndex)->setFontSize(index);
                m_editMap.value(tmpIndex)->update();
            }
        } else if (group != "text" && m_selectedShape.type == group && key == "color_index") {
            m_selectedShape.colorIndex = index;
        } else if (group == "effect" && m_selectedShape.type == group &&
                   key == "radius" && (m_selectedShape.isOval == 0 || m_selectedShape.isOval == 1)) {

            m_selectedShape.radius = index * 3 + 10;
            if (m_selectedShape.isBlur) {
                emit reloadEffectImg("blur", m_selectedShape.radius);
            } else {
                emit reloadEffectImg("mosaic", m_selectedShape.radius);
            }
        } else if (group == "step-number" && m_selectedShape.type == group && key == "style") {
            const StepNumberStyle style = stepNumberStyleFromValue(index);
            const QPointF anchor = stepNumberAnchor(m_selectedShape);
            const QRectF shapeRect = stepNumberRect(anchor, style);

            m_selectedShape.stepNumberStyle = static_cast<int>(style);
            if (m_selectedShape.points.isEmpty()) {
                m_selectedShape.points.append(anchor);
            } else {
                m_selectedShape.points[0] = anchor;
            }
            m_selectedShape.mainPoints[0] = shapeRect.topLeft();
            m_selectedShape.mainPoints[1] = shapeRect.bottomLeft();
            m_selectedShape.mainPoints[2] = shapeRect.topRight();
            m_selectedShape.mainPoints[3] = shapeRect.bottomRight();
            m_hoveredShape = m_selectedShape;
        } else if (group == "step-number" && m_selectedShape.type == group && key == "color_index") {
            m_selectedShape.colorIndex = index;
            m_hoveredShape = m_selectedShape;
        } else if (group == "step-number" && m_selectedShape.type == group && key == "text_color_index") {
            m_selectedShape.stepNumberTextColorIndex = index;
            m_hoveredShape = m_selectedShape;
        }

        if (m_selectedOrder < m_shapes.length()) {
            m_shapes[m_selectedOrder] = m_selectedShape;
            qCDebug(dsrApp) << "Shape updated at order:" << m_selectedOrder;
        }
        update();
    }
    //qDebug() << ">>>>> function: " << __func__ << ", line: " << __LINE__ << ", m_selectedShape.type: " << m_selectedShape.type;
}
/*
 * never used
void ShapesWidget::updatePenColor()
{
    setPenColor(colorIndexOf(ConfigSettings::instance()->value(
                                 "common", "color_index").toInt()));
}
*/
void ShapesWidget::setCurrentShape(QString shapeType)
{
    qDebug() << __FUNCTION__ << __LINE__ << "type: " << shapeType;
    m_currentType = shapeType;
    if (shapeType == "text") {
        resetForTextToolSwitch();
    } else {
        setAllTextEditReadOnly();
    }

    if (shapeType == "ruler") {
        clearSelected();
        initRulerIfNeeded();
        updateCursorShape();
        update();
    } else if (shapeType == "measure-line") {
        clearSelected();
        resetMeasureLine();
        updateCursorShape();
        update();
    } else if (shapeType == "color-picker") {
        clearSelected();
        m_colorPickerHasColor = false;
        updateCursorShape();
        update();
    }
}

void ShapesWidget::resetForTextToolSwitch()
{
    // 切换到 text 时仍保留上一个图形的选中态，
    // 导致第一次点击先“清选中”，第二次点击才进入文本创建。
    // 这里在工具切换时清理选中态，保证首次点击直接进入文本输入。
    clearSelected();
    m_isRotated = false;
    m_isResize = false;
    m_clickedKey = Unknow;
}

void ShapesWidget::clearSelected()
{
    qCDebug(dsrApp) << "Clearing selected shape";

    for (int j = 0; j < m_selectedShape.mainPoints.length(); j++) {
        m_selectedShape.mainPoints[j] = QPointF(0, 0);
    }
    for (int j = 0; j < m_hoveredShape.mainPoints.length(); j++) {
        m_hoveredShape.mainPoints[j] = QPointF(0, 0);
    }

    //qDebug() << "clear selected!!!";
    m_isSelected = false;
    m_selectedIndex = -1;
    m_hoveredIndex = -1;
    m_selectedOrder = -1;
    m_selectedShape.points.clear();
    m_hoveredShape.points.clear();
    updateCursorShape();
}

void ShapesWidget::setAllTextEditReadOnly()
{
    qCDebug(dsrApp) << "Setting all text edits to read-only";
    
    QMap<int, TextEdit *>::iterator i = m_editMap.begin();
    while (i != m_editMap.end()) {
        i.value()->setReadOnly(true);
        i.value()->releaseKeyboard();
        i.value()->setEditing(false);

        QTextCursor textCursor =  i.value()->textCursor();
        textCursor.clearSelection();
        i.value()->setTextCursor(textCursor);
        ++i;
    }

    setNoChangedTextEditRemove();
    update();
}

void ShapesWidget::setNoChangedTextEditRemove()
{
    qCDebug(dsrApp) << "Removing unchanged text edits";
    
    if (m_shapes.length() == 0) {
        return;
    }
    for (int i = 0; i < m_shapes.length(); i++) {
        if (m_shapes[i].type == "text") {
            int t_tempIndex = m_shapes[i].index;
            if (m_editMap.value(t_tempIndex)->document()->toPlainText() == QString(tr("Input text here"))
                    || m_editMap.value(t_tempIndex)->document()->toPlainText().isEmpty()) {
                qCDebug(dsrApp) << "Removing empty text edit at index:" << t_tempIndex;
                m_shapes.removeAt(i);
                m_editMap.value(t_tempIndex)->clear();
                m_editMap.remove(t_tempIndex);

                break;
            }

        }

    }
    if (m_shapes.length() == 0) {
        qCDebug(dsrApp) << "No shapes remaining, disabling undo";
        emit setShapesUndo(false);
    }

    update();
}

void ShapesWidget::saveActionTriggered()
{
    qInfo() << __FUNCTION__ << __LINE__ << "正在执行清除图形编辑界面...";
    setAllTextEditReadOnly();
    clearSelected();
    m_clearAllTextBorder = true;
    qInfo() << __FUNCTION__ << __LINE__ << "已清楚图形编辑界面";
}

//点击某个形状
bool ShapesWidget::clickedOnShapes(QPointF pos)
{
    qCDebug(dsrApp) << "Checking for shape click at position:" << pos;
    bool onShapes = false;
    m_selectedOrder = -1;
    //    qDebug() << ">>>>> function: " << __func__ << ", line: " << __LINE__
    //             << ", pos: " << pos
    //             << ", m_shapes.length(): " << m_shapes.length();
    for (int i = 0; i < m_shapes.length(); i++) {
        //当前是否有形状被选中
        bool currentOnShape = false;
        if (m_shapes[i].type == "rectangle") {
            if (clickedOnRect(m_shapes[i].mainPoints, pos, false)) {
                qCDebug(dsrApp) << "Clicked on rectangle at index:" << i;
                currentOnShape = true;
                emit shapeClicked("rect");
            }
        }
        if (m_shapes[i].type == "oval") {
            if (clickedOnEllipse(m_shapes[i].mainPoints, pos, false)) {
                qCDebug(dsrApp) << "Clicked on oval at index:" << i;
                currentOnShape = true;
                emit shapeClicked("circ");
            }
        }
        if (m_shapes[i].type == "effect") {
            if (m_shapes[i].isOval == 0) {
                if (clickedOnEllipse(m_shapes[i].mainPoints, pos, true)) {
                    currentOnShape = true;
                    emit shapeClicked("effect");
                }
            } else if (m_shapes[i].isOval == 1) {
                if (clickedOnRect(m_shapes[i].mainPoints, pos, true)) {
                    currentOnShape = true;
                    emit shapeClicked("effect");
                }
            } else {
                if (clickedOnLine(m_shapes[i].mainPoints, m_shapes[i].points, pos)) {
                    continue;
                }
            }

        }
        if (m_shapes[i].type == "arrow") {
            if (clickedOnArrow(m_shapes[i].points, pos)) {
                currentOnShape = true;
                emit shapeClicked("arrow");
            }
        }
        if (m_shapes[i].type == "line") {
            if (clickedOnArrow(m_shapes[i].points, pos)) {
                currentOnShape = true;
                emit shapeClicked("line");
            }
        }
        if (m_shapes[i].type == "pen") {
            if (clickedOnLine(m_shapes[i].mainPoints, m_shapes[i].points, pos)) {
                currentOnShape = true;
                emit shapeClicked("pen");
            }
        }

        if (m_shapes[i].type == "text") {
            if (clickedOnText(m_shapes[i].mainPoints, pos)) {
                currentOnShape = true;
                emit shapeClicked("text");
            }
        }
        if (m_shapes[i].type == "step-number") {
            if (clickedOnRect(m_shapes[i].mainPoints, pos, true)) {
                currentOnShape = true;
            }
        }
        if (m_shapes[i].type == "spotlight") {
            if (clickedOnRect(m_shapes[i].mainPoints, pos, true)) {
                currentOnShape = true;
                emit shapeClicked("spotlight");
            }
        }

        if (currentOnShape) {
            m_selectedShape = m_shapes[i];
            m_selectedIndex = m_shapes[i].index;
            m_selectedOrder = i;
            if (m_selectedShape.type == "step-number") {
                ConfigSettings::instance()->setValue("step-number", "style", m_selectedShape.stepNumberStyle);
                ConfigSettings::instance()->setValue("step-number", "color_index", m_selectedShape.colorIndex);
                ConfigSettings::instance()->setValue("step-number", "text_color_index", m_selectedShape.stepNumberTextColorIndex);
                emit shapeClicked("step-number");
            }
            onShapes = true;
            break;
        } else {
            m_selectedIndex = -1;
            m_selectedOrder = -1;
            update();
            continue;
        }
    }
    return onShapes;
}

bool ShapesWidget::clickedOnStepNumber(QPointF pos)
{
    m_selectedIndex = -1;
    m_selectedOrder = -1;

    for (int i = m_shapes.length() - 1; i >= 0; --i) {
        if (m_shapes[i].type != "step-number") {
            continue;
        }

        const FourPoints mainPoints = m_shapes[i].mainPoints;
        const FourPoints otherPoints = getAnotherFPoints(mainPoints);
        const QRectF stepRect = QRectF(mainPoints[0], mainPoints[3]).normalized();

        bool hitStepNumber = true;
        m_isResize = false;
        m_isRotated = false;
        m_resizeDirection = Moving;
        if (pointClickIn(mainPoints[0], pos)) {
            m_isResize = true;
            m_clickedKey = First;
            m_resizeDirection = TopLeft;
        } else if (pointClickIn(mainPoints[1], pos)) {
            m_isResize = true;
            m_clickedKey = Second;
            m_resizeDirection = BottomLeft;
        } else if (pointClickIn(mainPoints[2], pos)) {
            m_isResize = true;
            m_clickedKey = Third;
            m_resizeDirection = TopRight;
        } else if (pointClickIn(mainPoints[3], pos)) {
            m_isResize = true;
            m_clickedKey = Fourth;
            m_resizeDirection = BottomRight;
        } else if (pointClickIn(otherPoints[0], pos)) {
            m_isResize = true;
            m_clickedKey = Fifth;
            m_resizeDirection = Left;
        } else if (pointClickIn(otherPoints[1], pos)) {
            m_isResize = true;
            m_clickedKey = Sixth;
            m_resizeDirection = Top;
        } else if (pointClickIn(otherPoints[2], pos)) {
            m_isResize = true;
            m_clickedKey = Seventh;
            m_resizeDirection = Right;
        } else if (pointClickIn(otherPoints[3], pos)) {
            m_isResize = true;
            m_clickedKey = Eighth;
            m_resizeDirection = Bottom;
        } else if (stepRect.contains(pos)) {
            m_isResize = false;
            m_resizeDirection = Moving;
        } else {
            hitStepNumber = false;
        }

        if (!hitStepNumber) {
            continue;
        }

        m_selectedShape = m_shapes[i];
        m_hoveredShape = m_shapes[i];
        m_selectedIndex = m_shapes[i].index;
        m_selectedOrder = i;
        m_hoveredIndex = m_shapes[i].index;
        m_isSelected = true;
        m_pressedPoint = pos;
        ConfigSettings::instance()->setValue("step-number", "style", m_selectedShape.stepNumberStyle);
        ConfigSettings::instance()->setValue("step-number", "color_index", m_selectedShape.colorIndex);
        ConfigSettings::instance()->setValue("step-number", "text_color_index", m_selectedShape.stepNumberTextColorIndex);
        emit shapeClicked("step-number");
        return true;
    }

    return false;
}

//判断是否选中图形,不是真实鼠标事件会触发
bool ShapesWidget::clickedShapes(QPointF pos)
{
    for (int i = 0; i < m_shapes.length(); i++) {
        if (m_shapes[i].type == "rectangle") {
            if (clickedOnRect(m_shapes[i].mainPoints, pos, false)) {
                return true;
            }
        }
        if (m_shapes[i].type == "oval") {
            if (clickedOnEllipse(m_shapes[i].mainPoints, pos, false)) {
                return true;
            }
        }

        if (m_shapes[i].type == "effect") {
            if (m_shapes[i].isOval == 0) {
                if (clickedOnEllipse(m_shapes[i].mainPoints, pos, true)) {
                    return true;
                }
            } else if (m_shapes[i].isOval == 1) {
                if (clickedOnRect(m_shapes[i].mainPoints, pos, true)) {
                    return true;
                }
            } else {
                if (clickedOnLine(m_shapes[i].mainPoints, m_shapes[i].points, pos)) {
                    return true;
                }
            }
        }

        if (m_shapes[i].type == "arrow" || m_shapes[i].type == "line") {
            if (clickedOnArrow(m_shapes[i].points, pos)) {
                return true;
            }
        }
        if (m_shapes[i].type == "pen") {
            if (clickedOnLine(m_shapes[i].mainPoints, m_shapes[i].points, pos)) {
                return true;
            }
        }

        if (m_shapes[i].type == "text") {
            if (clickedOnText(m_shapes[i].mainPoints, pos)) {
                return true;
            }
        }
        if (m_shapes[i].type == "step-number") {
            if (clickedOnRect(m_shapes[i].mainPoints, pos, true)) {
                return true;
            }
        }
        if (m_shapes[i].type == "spotlight") {
            if (clickedOnRect(m_shapes[i].mainPoints, pos, true)) {
                return true;
            }
        }
    }
    return false;
}

//TODO: selectUnique
//矩形是否被点击
bool ShapesWidget::clickedOnRect(FourPoints rectPoints, QPointF pos, bool isBlurMosaic)
{
    m_isSelected = false;
    m_isResize = false;
    m_isRotated = false;

    QPointF point1 = rectPoints[0];
    QPointF point2 = rectPoints[1];
    QPointF point3 = rectPoints[2];
    QPointF point4 = rectPoints[3];

    FourPoints otherFPoints = getAnotherFPoints(rectPoints);
    if (pointClickIn(point1, pos)) {
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = First;
        m_resizeDirection = TopLeft;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(point2, pos)) {
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Second;
        m_resizeDirection = BottomLeft;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(point3, pos)) {
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Third;
        m_resizeDirection = TopRight;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(point4, pos)) {
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Fourth;
        m_resizeDirection = BottomRight;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(otherFPoints[0], pos)) {
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Fifth;
        m_resizeDirection = Left;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(otherFPoints[1], pos)) {
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Sixth;
        m_resizeDirection = Top;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(otherFPoints[2], pos)) {
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Seventh;
        m_resizeDirection = Right;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(otherFPoints[3], pos)) {
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Eighth;
        m_resizeDirection = Bottom;
        m_pressedPoint = pos;
        return true;
    } else if (rotateOnPoint(rectPoints, pos)) {
        m_isSelected = true;
        m_isRotated = true;
        m_isResize = false;
        m_resizeDirection = Rotate;
        m_pressedPoint = pos;
        return true;
    } else if (pointOnLine(rectPoints[0], rectPoints[1], pos) ||
               pointOnLine(rectPoints[1], rectPoints[3], pos) ||
               pointOnLine(rectPoints[3], rectPoints[2], pos) ||
               pointOnLine(rectPoints[2], rectPoints[0], pos)) {
        m_isSelected = true;
        m_isResize = false;
        m_resizeDirection = Moving;
        m_pressedPoint = pos;
        return true;
    } else if (isBlurMosaic && pointInRect(rectPoints, pos)) {
        m_isSelected = true;
        m_isResize = false;
        m_resizeDirection = Moving;
        m_pressedPoint = pos;
        return true;
    }  else {
        m_isSelected = false;
        m_isResize = false;
        m_isRotated = false;
    }

    return false;
}

//圆形是否被点击
bool ShapesWidget::clickedOnEllipse(FourPoints mainPoints, QPointF pos, bool isBlurMosaic)
{
    qCDebug(dsrApp) << "clickedOnEllipse called with pos:" << pos << ", isBlurMosaic:" << isBlurMosaic;
    m_isSelected = false;
    m_isResize = false;
    m_isRotated = false;

    m_pressedPoint = pos;
    FourPoints otherFPoints = getAnotherFPoints(mainPoints);
    if (pointClickIn(mainPoints[0], pos)) {
        qCDebug(dsrApp) << "Clicked on mainPoints[0] (TopLeft).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = First;
        m_resizeDirection = TopLeft;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(mainPoints[1], pos)) {
        qCDebug(dsrApp) << "Clicked on mainPoints[1] (BottomLeft).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Second;
        m_resizeDirection = BottomLeft;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(mainPoints[2], pos)) {
        qCDebug(dsrApp) << "Clicked on mainPoints[2] (TopRight).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Third;
        m_resizeDirection = TopRight;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(mainPoints[3], pos)) {
        qCDebug(dsrApp) << "Clicked on mainPoints[3] (BottomRight).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Fourth;
        m_resizeDirection = BottomRight;
        m_pressedPoint = pos;
        return true;
    }  else if (pointClickIn(otherFPoints[0], pos)) {
        qCDebug(dsrApp) << "Clicked on otherFPoints[0] (Left).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Fifth;
        m_resizeDirection = Left;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(otherFPoints[1], pos)) {
        qCDebug(dsrApp) << "Clicked on otherFPoints[1] (Top).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Sixth;
        m_resizeDirection = Top;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(otherFPoints[2], pos)) {
        qCDebug(dsrApp) << "Clicked on otherFPoints[2] (Right).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Seventh;
        m_resizeDirection = Right;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(otherFPoints[3], pos)) {
        qCDebug(dsrApp) << "Clicked on otherFPoints[3] (Bottom).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Eighth;
        m_resizeDirection = Bottom;
        m_pressedPoint = pos;
        return true;
    } else if (rotateOnPoint(mainPoints, pos)) {
        qCDebug(dsrApp) << "Clicked on rotate point.";
        m_isSelected = true;
        m_isRotated = true;
        m_isResize = false;
        m_resizeDirection = Rotate;
        m_pressedPoint = pos;
        return true;
    }  else if (pointOnEllipse(mainPoints, pos)) {
        qCDebug(dsrApp) << "Clicked on ellipse boundary.";
        m_isSelected = true;
        m_isResize = false;

        m_resizeDirection = Moving;
        m_pressedPoint = pos;
        return true;
    } else if (isBlurMosaic && pointInRect(mainPoints, pos)) {
        qCDebug(dsrApp) << "Clicked inside blur/mosaic ellipse area.";
        m_isSelected = true;
        m_isResize = false;
        m_resizeDirection = Moving;
        m_pressedPoint = pos;
        return true;
    } else {
        qCDebug(dsrApp) << "No click detected on ellipse.";
        m_isSelected = false;
        m_isResize = false;
        m_isRotated = false;
    }

    return false;
}

//箭头是否被点击
bool ShapesWidget::clickedOnArrow(QList<QPointF> points, QPointF pos)
{
    qCDebug(dsrApp) << "clickedOnArrow called with pos:" << pos;
    if (points.length() != 2) {
        qCDebug(dsrApp) << "Arrow points length is not 2, returning false.";
        return false;
    }

    m_isSelected = false;
    m_isResize = false;
    m_isRotated = false;
    m_isArrowRotated = false;


    if (pointClickIn(points[0], pos)) {
        qCDebug(dsrApp) << "Clicked on arrow point 0 (start point).";
        m_isSelected = true;
        m_isRotated = true;
        m_isArrowRotated = false;
        m_resizeDirection = Left;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(points[1], pos)) {
        qCDebug(dsrApp) << "Clicked on arrow point 1 (end point).";
        m_isSelected = true;
        m_isRotated = true;
        m_isArrowRotated = false;
        m_resizeDirection = Right;
        m_pressedPoint = pos;
        return true;
    } else if (pointOnLine(points[0], points[1], pos)) {
        qCDebug(dsrApp) << "Clicked on arrow line.";
        m_isSelected = true;
        m_isRotated = false;
        m_resizeDirection = Moving;
        m_pressedPoint = pos;
        return true;
    } else {
        qCDebug(dsrApp) << "No click detected on arrow.";
        m_isSelected = false;
        m_isRotated = false;
        m_isResize = false;
        m_isArrowRotated = false;
        m_resizeDirection = Outting;
        m_pressedPoint = pos;
        return false;
    }

}

//画出的线是否被点击
bool ShapesWidget::clickedOnLine(FourPoints mainPoints,
                                 QList<QPointF> points,
                                 QPointF pos)
{
    qCDebug(dsrApp) << "clickedOnLine called with pos:" << pos;
    m_isSelected = false;
    m_isResize = false;
    m_isRotated = false;

    m_pressedPoint = QPoint(0, 0);
    FourPoints otherFPoints = getAnotherFPoints(mainPoints);
    if (pointClickIn(mainPoints[0], pos)) {
        qCDebug(dsrApp) << "Clicked on mainPoints[0] (TopLeft).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = First;
        m_resizeDirection = TopLeft;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(mainPoints[1], pos)) {
        qCDebug(dsrApp) << "Clicked on mainPoints[1] (BottomLeft).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Second;
        m_resizeDirection = BottomLeft;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(mainPoints[2], pos)) {
        qCDebug(dsrApp) << "Clicked on mainPoints[2] (TopRight).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Third;
        m_resizeDirection = TopRight;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(mainPoints[3], pos)) {
        qCDebug(dsrApp) << "Clicked on mainPoints[3] (BottomRight).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Fourth;
        m_resizeDirection = BottomRight;
        m_pressedPoint = pos;
        return true;
    }  else if (pointClickIn(otherFPoints[0], pos)) {
        qCDebug(dsrApp) << "Clicked on otherFPoints[0] (Left).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Fifth;
        m_resizeDirection = Left;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(otherFPoints[1], pos)) {
        qCDebug(dsrApp) << "Clicked on otherFPoints[1] (Top).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Sixth;
        m_resizeDirection = Top;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(otherFPoints[2], pos)) {
        qCDebug(dsrApp) << "Clicked on otherFPoints[2] (Right).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Seventh;
        m_resizeDirection = Right;
        m_pressedPoint = pos;
        return true;
    } else if (pointClickIn(otherFPoints[3], pos)) {
        qCDebug(dsrApp) << "Clicked on otherFPoints[3] (Bottom).";
        m_isSelected = true;
        m_isResize = true;
        m_clickedKey = Eighth;
        m_resizeDirection = Bottom;
        m_pressedPoint = pos;
        return true;
    } else if (rotateOnPoint(mainPoints, pos)) {
        qCDebug(dsrApp) << "Clicked on rotate point.";
        m_isSelected = true;
        m_isRotated = true;
        m_isResize = false;
        m_resizeDirection = Rotate;
        m_pressedPoint = pos;
        return true;
    }  else if (pointOnArLine(points, pos)) {
        qCDebug(dsrApp) << "Clicked on arrow line.";
        m_isSelected = true;
        m_isResize = false;

        m_resizeDirection = Moving;
        m_pressedPoint = pos;
        return true;
    } else {
        qCDebug(dsrApp) << "No click detected on line.";
        m_isSelected = false;
        m_isResize = false;
        m_isRotated = false;
    }

    return false;
}

//文本框是否被点击
bool ShapesWidget::clickedOnText(FourPoints mainPoints, QPointF pos)
{
    qCDebug(dsrApp) << "clickedOnText called with pos:" << pos;
    if (pointInRect(mainPoints, pos)) {
        qCDebug(dsrApp) << "Click detected inside text rectangle.";
        m_isSelected = true;
        m_isResize = false;
        m_resizeDirection = Moving;

        return true;
    } else {
        qCDebug(dsrApp) << "Click not detected inside text rectangle.";
        m_isSelected = false;
        m_isResize = false;

        return false;
    }
}

bool ShapesWidget::hoverOnRect(FourPoints rectPoints, QPointF pos, bool isTextBorder)
{
    qCDebug(dsrApp) << "hoverOnRect called with pos:" << pos << ", isTextBorder:" << isTextBorder;
    FourPoints tmpFPoints = getAnotherFPoints(rectPoints);
    if (pointClickIn(rectPoints[0], pos)) {
        qCDebug(dsrApp) << "Hovered on TopLeft resize point.";
        m_resizeDirection = TopLeft;
        return true;
    } else if (pointClickIn(rectPoints[1], pos)) {
        qCDebug(dsrApp) << "Hovered on BottomLeft resize point.";
        m_resizeDirection = BottomLeft;
        return true;
    } else if (pointClickIn(rectPoints[2], pos)) {
        qCDebug(dsrApp) << "Hovered on TopRight resize point.";
        m_resizeDirection = TopRight;
        return true;
    } else if (pointClickIn(rectPoints[3], pos)) {
        qCDebug(dsrApp) << "Hovered on BottomRight resize point.";
        m_resizeDirection = BottomRight;
        return true;
    } else if (rotateOnPoint(rectPoints, pos) && m_selectedIndex != -1
               && m_selectedIndex == m_hoveredIndex && !isTextBorder) {
        qCDebug(dsrApp) << "Hovered on rotate point.";
        m_resizeDirection = Rotate;
        return true;
    }  else if (pointClickIn(tmpFPoints[0], pos)) {
        qCDebug(dsrApp) << "Hovered on Left resize point.";
        m_resizeDirection = Left;
        return true;
    } else if (pointClickIn(tmpFPoints[1], pos)) {
        qCDebug(dsrApp) << "Hovered on Top resize point.";
        m_resizeDirection = Top;
        return true;
    }  else if (pointClickIn(tmpFPoints[2], pos)) {
        qCDebug(dsrApp) << "Hovered on Right resize point.";
        m_resizeDirection = Right;
        return true;
    } else if (pointClickIn(tmpFPoints[3], pos)) {
        qCDebug(dsrApp) << "Hovered on Bottom resize point.";
        m_resizeDirection = Bottom;
        return true;
    } else if (pointOnLine(rectPoints[0],  rectPoints[1], pos) || pointOnLine(rectPoints[1],
                                                                              rectPoints[3], pos) || pointOnLine(rectPoints[3], rectPoints[2], pos) ||
               pointOnLine(rectPoints[2], rectPoints[0], pos)) {
        qCDebug(dsrApp) << "Hovered on rectangle border.";
        m_resizeDirection = Moving;
        return true;
    } else {
        qCDebug(dsrApp) << "No hover detected on rectangle.";
        m_resizeDirection = Outting;
    }
    return false;
}

bool ShapesWidget::hoverOnEllipse(FourPoints mainPoints, QPointF pos)
{
    qCDebug(dsrApp) << "hoverOnEllipse called with pos:" << pos;
    FourPoints tmpFPoints = getAnotherFPoints(mainPoints);

    if (pointClickIn(mainPoints[0], pos)) {
        qCDebug(dsrApp) << "Hovered on TopLeft resize point.";
        m_resizeDirection = TopLeft;
        return true;
    } else if (pointClickIn(mainPoints[1], pos)) {
        qCDebug(dsrApp) << "Hovered on BottomLeft resize point.";
        m_resizeDirection = BottomLeft;
        return true;
    } else if (pointClickIn(mainPoints[2], pos)) {
        qCDebug(dsrApp) << "Hovered on TopRight resize point.";
        m_resizeDirection = TopRight;
        return true;
    } else if (pointClickIn(mainPoints[3], pos)) {
        qCDebug(dsrApp) << "Hovered on BottomRight resize point.";
        m_resizeDirection = BottomRight;
        return true;
    } else if (rotateOnPoint(mainPoints, pos) && m_selectedIndex != -1
               && m_selectedIndex == m_hoveredIndex) {
        qCDebug(dsrApp) << "Hovered on rotate point.";
        m_resizeDirection = Rotate;
        return true;
    }  else if (pointClickIn(tmpFPoints[0], pos)) {
        qCDebug(dsrApp) << "Hovered on Left resize point.";
        m_resizeDirection = Left;
        return true;
    } else if (pointClickIn(tmpFPoints[1], pos)) {
        qCDebug(dsrApp) << "Hovered on Top resize point.";
        m_resizeDirection = Top;
        return true;
    }  else if (pointClickIn(tmpFPoints[2], pos)) {
        qCDebug(dsrApp) << "Hovered on Right resize point.";
        m_resizeDirection = Right;
        return true;
    } else if (pointClickIn(tmpFPoints[3], pos)) {
        qCDebug(dsrApp) << "Hovered on Bottom resize point.";
        m_resizeDirection = Bottom;
        return true;
    }  else if (pointOnEllipse(mainPoints, pos)) {
        qCDebug(dsrApp) << "Hovered on ellipse boundary.";
        m_isSelected = true;
        m_isResize = false;

        m_resizeDirection = Moving;
        m_pressedPoint = pos;
        return true;
    } else {
        qCDebug(dsrApp) << "No hover detected on ellipse.";
        m_resizeDirection = Outting;
    }
    return false;
}

bool ShapesWidget::hoverOnArrow(QList<QPointF> points, QPointF pos)
{
    qCDebug(dsrApp) << "hoverOnArrow called with pos:" << pos;
    if (points.length() != 2) {
        qCDebug(dsrApp) << "Arrow points length is not 2, returning false.";
        return false;
    }
    bool result = false;
//    QPointF t_rotatepos;

//    qreal t_minx = qMin(points[1].x(), points[0].x());
//    qreal t_miny = qMin(points[1].y(), points[0].y());
//    int t_height = qAbs(points[1].y() - points[0].y());
//    int t_width = qAbs(points[1].x() - points[0].x());

//    t_rotatepos.setX(t_minx + t_width / 2 - 5);
//    t_rotatepos.setY(t_miny + t_height / 2 - 35);

    //第一个点的偏移量
    qreal firstPointPadX = std::abs(pos.x() - points[0].x());
    qreal firstPointPadY = std::abs(pos.y() - points[0].y());
    //第二个点的偏移量
    qreal secondPointPadX = std::abs(pos.x() - points[1].x());
    qreal secondPointPadY = std::abs(pos.y() - points[1].y());
    //端点的偏移最大值
    int pointPadding = 4;
    if (pointOnLine(points[0], points[1], pos) &&
            ((firstPointPadX > pointPadding || firstPointPadY > pointPadding)  &&
             (secondPointPadX > pointPadding  || secondPointPadY > pointPadding))) {
        qCDebug(dsrApp) << "Hovered on arrow line, not near endpoints.";
        m_resizeDirection = Moving;
        m_clickedKey = Unknow;
        result = true;
    } else if (m_selectedIndex != -1 && m_selectedIndex == m_hoveredIndex
               && pointClickIn(points[0], pos) && firstPointPadX <= pointPadding && firstPointPadY <= pointPadding) {
        qCDebug(dsrApp) << "Hovered on first arrow endpoint.";
        m_clickedKey = First;
        m_resizeDirection = Rotate;
        result = true;
    } else if (m_selectedIndex != -1 && m_selectedIndex == m_hoveredIndex
               && pointClickIn(points[1], pos) && secondPointPadX <= pointPadding && secondPointPadY <= pointPadding) {
        qCDebug(dsrApp) << "Hovered on second arrow endpoint.";
        m_clickedKey =   Second;
        m_resizeDirection = Rotate;
        result = true;
    } /*else if ( m_selectedIndex != -1 && m_selectedIndex == m_hoveredIndex
                && pointClickIn(t_rotatepos, pos)) {
        m_clickedKey =  Third;
        m_resizeDirection = Rotate;
        return true;*/
//    }
    else {
        qCDebug(dsrApp) << "No hover detected on arrow.";
        m_resizeDirection = Outting;
        result = false;
    }
    return result;
}

bool ShapesWidget::hoverOnLine(FourPoints mainPoints, QList<QPointF> points,
                               QPointF pos)
{
    qCDebug(dsrApp) << "hoverOnLine called with pos:" << pos;
    FourPoints tmpFPoints = getAnotherFPoints(mainPoints);

    if (pointClickIn(mainPoints[0], pos)) {
        qCDebug(dsrApp) << "Hovered on TopLeft resize point.";
        m_resizeDirection = TopLeft;
        return true;
    } else if (pointClickIn(mainPoints[1], pos)) {
        qCDebug(dsrApp) << "Hovered on BottomLeft resize point.";
        m_resizeDirection = BottomLeft;
        return true;
    } else if (pointClickIn(mainPoints[2], pos)) {
        qCDebug(dsrApp) << "Hovered on TopRight resize point.";
        m_resizeDirection = TopRight;
        return true;
    } else if (pointClickIn(mainPoints[3], pos)) {
        qCDebug(dsrApp) << "Hovered on BottomRight resize point.";
        m_resizeDirection = BottomRight;
        return true;
    } else if (rotateOnPoint(mainPoints, pos) && m_selectedIndex != -1
               && m_selectedIndex == m_hoveredIndex) {
        qCDebug(dsrApp) << "Hovered on rotate point.";
        m_resizeDirection = Rotate;
        return true;
    }  else if (pointClickIn(tmpFPoints[0], pos)) {
        qCDebug(dsrApp) << "Hovered on Left resize point.";
        m_resizeDirection = Left;
        return true;
    } else if (pointClickIn(tmpFPoints[1], pos)) {
        qCDebug(dsrApp) << "Hovered on Top resize point.";
        m_resizeDirection = Top;
        return true;
    }  else if (pointClickIn(tmpFPoints[2], pos)) {
        qCDebug(dsrApp) << "Hovered on Right resize point.";
        m_resizeDirection = Right;
        return true;
    } else if (pointClickIn(tmpFPoints[3], pos)) {
        qCDebug(dsrApp) << "Hovered on Bottom resize point.";
        m_resizeDirection = Bottom;
        return true;
    }  else if (pointOnArLine(points, pos)) {
        qCDebug(dsrApp) << "Hovered on line segment.";
        m_isSelected = true;
        m_isResize = false;

        m_resizeDirection = Moving;
        m_pressedPoint = pos;
        return true;
    } else {
        qCDebug(dsrApp) << "No hover detected on line.";
        m_resizeDirection = Outting;
    }
    return false;
}

bool ShapesWidget::hoverOnText(int textIndex, FourPoints mainPoints, QPointF pos)
{
    qCDebug(dsrApp) << "hoverOnText called with textIndex:" << textIndex << "pos:" << pos;
    //qDebug() << "hoverOnText:" <<  mainPoints << pos;
    if (hoverOnRect(mainPoints, pos, true) ||
            (pos.x() >= mainPoints[0].x() - 5
             && pos.x() <= mainPoints[2].x() + 5
             && pos.y() >= mainPoints[0].y() - 5
             && pos.y() <= mainPoints[2].y() + 5)) {

        if (m_editMap.contains(textIndex) && m_editMap[textIndex]->isReadOnly()) {
            m_resizeDirection = Moving;
            return true;
        }
    }

    m_resizeDirection = Outting;
    return false;
}

bool ShapesWidget::hoverOnShapes(Toolshape toolShape, QPointF pos)
{
    if (toolShape.type == "rectangle") {
        return hoverOnRect(toolShape.mainPoints, pos);
    } else if (toolShape.type == "oval") {
        return hoverOnEllipse(toolShape.mainPoints, pos);
    } else if (toolShape.type == "arrow" || toolShape.type == "line") {
        return hoverOnArrow(toolShape.points, pos);
    } else if (toolShape.type == "pen") {
        return hoverOnLine(toolShape.mainPoints, toolShape.points, pos);
    } else if (toolShape.type == "text") {
        return hoverOnText(toolShape.index, toolShape.mainPoints, pos);
    } else if (toolShape.type == "effect" && toolShape.isOval == 0) {
        return hoverOnEllipse(toolShape.mainPoints, pos);
    } else if (toolShape.type == "effect" && toolShape.isOval == 1) {
        return hoverOnRect(toolShape.mainPoints, pos);
    } else if (toolShape.type == "step-number") {
        const FourPoints otherPoints = getAnotherFPoints(toolShape.mainPoints);
        const QRectF stepRect = QRectF(toolShape.mainPoints[0], toolShape.mainPoints[3]).normalized();
        if (pointClickIn(toolShape.mainPoints[0], pos)) {
            m_resizeDirection = TopLeft;
            return true;
        } else if (pointClickIn(toolShape.mainPoints[1], pos)) {
            m_resizeDirection = BottomLeft;
            return true;
        } else if (pointClickIn(toolShape.mainPoints[2], pos)) {
            m_resizeDirection = TopRight;
            return true;
        } else if (pointClickIn(toolShape.mainPoints[3], pos)) {
            m_resizeDirection = BottomRight;
            return true;
        } else if (pointClickIn(otherPoints[0], pos)) {
            m_resizeDirection = Left;
            return true;
        } else if (pointClickIn(otherPoints[1], pos)) {
            m_resizeDirection = Top;
            return true;
        } else if (pointClickIn(otherPoints[2], pos)) {
            m_resizeDirection = Right;
            return true;
        } else if (pointClickIn(otherPoints[3], pos)) {
            m_resizeDirection = Bottom;
            return true;
        } else if (stepRect.contains(pos)) {
            m_resizeDirection = Moving;
            return true;
        }
        m_resizeDirection = Outting;
        return false;
    } else if (toolShape.type == "spotlight") {
        return hoverOnRect(toolShape.mainPoints, pos);
    }

    m_hoveredShape.type = "";
    return false;
}

bool ShapesWidget::rotateOnPoint(FourPoints mainPoints,
                                 QPointF pos)
{
    bool result = hoverOnRotatePoint(mainPoints, pos);
    return result;
}

bool ShapesWidget::hoverOnRotatePoint(FourPoints mainPoints, QPointF pos)
{
    QPointF rotatePoint = getRotatePoint(mainPoints[0], mainPoints[1],
                                         mainPoints[2], mainPoints[3]);
    rotatePoint = QPointF(rotatePoint.x() - 5, rotatePoint.y() - 5);
    bool result = false;
    if (pos.x() >= rotatePoint.x() - SPACING && pos.x() <= rotatePoint.x() + SPACING
            && pos.y() >= rotatePoint.y() - SPACING && pos.y() <= rotatePoint.y() + SPACING) {
        result = true;
    } else {
        result = false;
    }

    m_pressedPoint = rotatePoint;
    return result;
}

bool ShapesWidget::textEditIsReadOnly()
{
    qDebug() << "textEditIsReadOnly:" << m_editMap.count();

    QMap<int, TextEdit *>::iterator i = m_editMap.begin();
    while (i != m_editMap.end()) {
        if (m_editing || !i.value()->isReadOnly()) {
            setAllTextEditReadOnly();
            m_editing = false;
            m_currentShape.type = "";
            update();
            return true;
        }
        ++i;
    }

    return false;
}

void ShapesWidget::handleDrag(QPointF oldPoint, QPointF newPoint)
{
    //qDebug() << "handleDrag:" << m_selectedIndex << m_shapes.length();

    if (m_selectedIndex == -1) {
        return;
    }

    if (m_shapes[m_selectedOrder].type == "arrow" || m_shapes[m_selectedOrder].type == "line") {
        for (int i = 0; i < m_shapes[m_selectedOrder].points.length(); i++) {
            m_shapes[m_selectedOrder].points[i] = QPointF(
                                                      m_shapes[m_selectedOrder].points[i].x() + (newPoint.x() - oldPoint.x()),
                                                      m_shapes[m_selectedOrder].points[i].y() + (newPoint.y() - oldPoint.y())
                                                  );
        }
        return;
    }

    if (m_shapes[m_selectedOrder].mainPoints.length() == 4) {
        for (int i = 0; i < m_shapes[m_selectedOrder].mainPoints.length(); i++) {
            m_shapes[m_selectedOrder].mainPoints[i] = QPointF(
                                                          m_shapes[m_selectedOrder].mainPoints[i].x() + (newPoint.x() - oldPoint.x()),
                                                          m_shapes[m_selectedOrder].mainPoints[i].y() + (newPoint.y() - oldPoint.y())
                                                      );
        }
    }
    for (int i = 0; i < m_shapes[m_selectedOrder].points.length(); i++) {
        m_shapes[m_selectedOrder].points[i] = QPointF(
                                                  m_shapes[m_selectedOrder].points[i].x() + (newPoint.x() - oldPoint.x()),
                                                  m_shapes[m_selectedOrder].points[i].y() + (newPoint.y() - oldPoint.y())
                                              );
    }
}

////////////////////TODO: perfect handleRotate..
void ShapesWidget::handleRotate(QPointF pos)
{
    qCDebug(dsrApp) << "handleRotate called with pos:" << pos;
    //qDebug() << "handleRotate:" << m_selectedIndex << m_shapes.length();

    if (m_selectedIndex == -1 || m_selectedShape.type == "text") {
        qCDebug(dsrApp) << "Rotation skipped: No shape selected or selected shape is text.";
        return;
    }

    if (m_selectedShape.type == "arrow" || m_selectedShape.type == "line") {
        qCDebug(dsrApp) << "Rotating arrow or line type shape.";
        if (m_isArrowRotated == false) {
            qCDebug(dsrApp) << "Arrow not rotated, handling point adjustment.";
            if (m_shapes[m_selectedOrder].isShiftPressed) {
                qCDebug(dsrApp) << "Shift key pressed, adjusting arrow/line to snap to axis.";
                if (static_cast<int>(m_shapes[m_selectedOrder].points[0].x()) == static_cast<int>(m_shapes[m_selectedOrder].points[1].x())) {
                    qCDebug(dsrApp) << "Arrow/line is vertical.";
                    if (m_clickedKey == First) {
                        qCDebug(dsrApp) << "Adjusting first point (vertical).";
                        m_shapes[m_selectedOrder].points[0] = QPointF(m_shapes[m_selectedOrder].points[1].x(),
                                                                      pos.y());
                    } else if (m_clickedKey == Second) {
                        qCDebug(dsrApp) << "Adjusting second point (vertical).";
                        m_shapes[m_selectedOrder].points[1] = QPointF(m_shapes[m_selectedOrder].points[0].x(),
                                                                      pos.y());
                    }
                } else {
                    qCDebug(dsrApp) << "Arrow/line is horizontal.";
                    if (m_clickedKey == First) {
                        qCDebug(dsrApp) << "Adjusting first point (horizontal).";
                        m_shapes[m_selectedOrder].points[0] = QPointF(pos.x(), m_shapes[m_selectedOrder].points[1].y());
                    } else if (m_clickedKey == Second) {
                        m_shapes[m_selectedOrder].points[1] = QPointF(pos.x(), m_shapes[m_selectedOrder].points[0].y());
                    }
                }
            } else {
                if (m_clickedKey == First) {
                    m_shapes[m_selectedOrder].points[0] = m_pressedPoint;
                } else if (m_clickedKey == Second) {
                    m_shapes[m_selectedOrder].points[1] = m_pressedPoint;
                }
            }
        } else {
            QPointF t_rotatepos;

            qreal t_minx = qMin(m_selectedShape.points[1].x(), m_selectedShape.points[0].x());
            qreal t_miny = qMin(m_selectedShape.points[1].y(), m_selectedShape.points[0].y());
            int t_height = static_cast<int>(qAbs(m_selectedShape.points[1].y() - m_selectedShape.points[0].y()));
            int t_width = static_cast<int>(qAbs(m_selectedShape.points[1].x() - m_selectedShape.points[0].x()));

            t_rotatepos.setX(t_minx + t_width / 2);
            t_rotatepos.setY(t_miny + t_height / 2);

            qreal angle = calculateAngle(m_pressedPoint, pos, t_rotatepos) / 35;
            for (int k = 0; k < m_selectedShape.points.length(); k++) {
                m_shapes[m_selectedOrder].points[k] = pointRotate(t_rotatepos,
                                                                  m_selectedShape.points[k], angle);
            }
        }

        m_selectedShape.points  =  m_shapes[m_selectedOrder].points;
        m_hoveredShape.points = m_shapes[m_selectedOrder].points;
        m_pressedPoint = pos;
        return;
    }

    QPointF centerInPoint = QPointF((m_selectedShape.mainPoints[0].x() +
                                     m_selectedShape.mainPoints[3].x()) / 2,
                                    (m_selectedShape.mainPoints[0].y() +
                                     m_selectedShape.mainPoints[3].y()) / 2);
    QLineF linePressed(centerInPoint, m_movingPoint);
    qreal pressedAngle = 360 - linePressed.angle();
    qreal angle;
    if (0 == static_cast<int>(m_lastAngle)) {
        m_lastAngle = pressedAngle;
        angle = 0;
    } else {
        angle = pressedAngle - m_lastAngle;
    }
    if (angle < -100) {
        m_lastAngle = pressedAngle - m_lastAngle + 360;
        angle = m_lastAngle;
    } else if (angle > 100) {
        m_lastAngle = pressedAngle - m_lastAngle - 360;
        angle = m_lastAngle;
    }
    qreal rotationDelta = (angle * M_PI) / 180;
    qreal centerX = (m_shapes[m_selectedOrder].mainPoints[0].x() + m_shapes[m_selectedOrder].mainPoints[3].x()) / 2;
    qreal centerY = (m_shapes[m_selectedOrder].mainPoints[0].y() + m_shapes[m_selectedOrder].mainPoints[3].y()) / 2;
    for (int i = 0; i < m_shapes[m_selectedOrder].mainPoints.size(); ++i) {
        qreal x = centerX + (m_shapes[m_selectedOrder].mainPoints[i].x() - centerX) * cos(rotationDelta) - (m_shapes[m_selectedOrder].mainPoints[i].y() - centerY) * sin(rotationDelta);
        qreal y = centerY + (m_shapes[m_selectedOrder].mainPoints[i].x() - centerX) * sin(rotationDelta) + (m_shapes[m_selectedOrder].mainPoints[i].y() - centerY) * cos(rotationDelta);
        //图形
        m_shapes[m_selectedOrder].mainPoints[i].setX(x);
        m_shapes[m_selectedOrder].mainPoints[i].setY(y);
    }
    qreal centerMainX = (m_selectedShape.mainPoints[0].x() + m_selectedShape.mainPoints[3].x()) / 2;
    qreal centerMainY = (m_selectedShape.mainPoints[0].y() + m_selectedShape.mainPoints[3].y()) / 2;
    for (int k = 0; k < m_selectedShape.points.length(); k++) {
        qreal x = centerMainX + (m_shapes[m_selectedOrder].points[k].x() - centerMainX) * cos(rotationDelta) - (m_shapes[m_selectedOrder].points[k].y() - centerMainY) * sin(rotationDelta);
        qreal y = centerMainY + (m_shapes[m_selectedOrder].points[k].x() - centerMainX) * sin(rotationDelta) + (m_shapes[m_selectedOrder].points[k].y() - centerMainY) * cos(rotationDelta);
        //线条
        m_shapes[m_selectedOrder].points[k].setX(x);
        m_shapes[m_selectedOrder].points[k].setY(y);
    }
    m_lastAngle = pressedAngle;

    m_selectedShape.mainPoints = m_shapes[m_selectedOrder].mainPoints;
    m_hoveredShape.mainPoints =  m_shapes[m_selectedOrder].mainPoints;
    m_pressedPoint = pos;
}

void ShapesWidget::handleResize(QPointF pos, int key)
{
    if (m_isResize && m_selectedIndex != -1) {
        if (m_shapes[m_selectedOrder].portion.isEmpty()) {
            for (int k = 0; k < m_shapes[m_selectedOrder].points.length(); k++) {
                m_shapes[m_selectedOrder].portion.append(relativePosition(
                                                             m_selectedShape.mainPoints, m_selectedShape.points[k]));
            }
        }

        FourPoints newResizeFPoints = resizePointPosition(
                                          m_shapes[m_selectedOrder].mainPoints[0],
                                          m_shapes[m_selectedOrder].mainPoints[1],
                                          m_shapes[m_selectedOrder].mainPoints[2],
                                          m_shapes[m_selectedOrder].mainPoints[3], pos, key,
                                          m_isShiftPressed);

        m_shapes[m_selectedOrder].mainPoints = newResizeFPoints;
        m_selectedShape.mainPoints = newResizeFPoints;
        m_hoveredShape.mainPoints = newResizeFPoints;

        for (int j = 0; j <  m_shapes[m_selectedOrder].portion.length(); j++) {
            m_shapes[m_selectedOrder].points[j] =
                getNewPosition(m_shapes[m_selectedOrder].mainPoints,
                               m_shapes[m_selectedOrder].portion[j]);
        }

        m_selectedShape.points = m_shapes[m_selectedOrder].points;
        m_hoveredShape.points = m_shapes[m_selectedOrder].points;
    }
    m_pressedPoint = pos;
}

bool ShapesWidget::event(QEvent *event)
{
    qCDebug(dsrApp) << "ShapesWidget::event called. Event type:" << event->type();
    if (QEvent::Gesture == event->type()) {
        qCDebug(dsrApp) << "Gesture event detected.";
        if (QGesture *tap = static_cast<QGestureEvent *>(event)->gesture(Qt::TapGesture)) {
            qCDebug(dsrApp) << "TapGesture detected.";
            tapTriggered(static_cast<QTapGesture *>(tap));
        }
        if (QGesture *pinch = static_cast<QGestureEvent *>(event)->gesture(Qt::PinchGesture)) {
            qCDebug(dsrApp) << "PinchGesture detected.";
            pinchTriggered(static_cast<QPinchGesture *>(pinch));
        }
        update();
    }
    qCDebug(dsrApp) << "Calling base QWidget::event.";
    return QWidget::event(event);
}

//重写鼠标按压事件
void ShapesWidget::mousePressEvent(QMouseEvent *e)
{
    qCDebug(dsrApp) << "mousePressEvent called. Mouse position:" << e->pos() << "Button:" << e->button() << "Source:" << e->source();
    m_lastAngle = 0;
    m_currentCursor = QCursor().pos();
    if ((m_currentType == "ruler" || m_currentType == "measure-line")
            && e->button() == Qt::RightButton) {
        showMeasurementMenu(mapToGlobal(e->pos()));
        DFrame::mousePressEvent(e);
        return;
    }
    if (m_currentType == "color-picker" && e->button() == Qt::LeftButton && handleColorPickerPress(e->pos())) {
        DFrame::mousePressEvent(e);
        return;
    }
    if (m_currentType == "ruler" && e->button() == Qt::LeftButton && handleRulerPress(e->pos())) {
        DFrame::mousePressEvent(e);
        return;
    }
    if (m_currentType == "measure-line" && e->button() == Qt::LeftButton && handleMeasureLinePress(e->pos())) {
        DFrame::mousePressEvent(e);
        return;
    }
    if (m_currentType == "step-number" && e->button() == Qt::LeftButton) {
        m_pressedPoint = e->pos();
        m_isRecording = false;
        m_isPressed = false;
        if (clickedOnStepNumber(m_pressedPoint)) {
            m_isPressed = true;
        } else {
            addStepNumber(m_pressedPoint);
        }
        updateCursorShape();
        update();
        DFrame::mousePressEvent(e);
        return;
    }

    //选中图形后，重新按下真实鼠标左键
    if (Qt::MouseEventSource::MouseEventSynthesizedByQt != e->source()
            && m_selectedIndex != -1) {
        qCDebug(dsrApp) << "Real mouse event detected and a shape is selected.";
        // 判空
        if (nullptr != m_editMap.value(m_lastEditMapKey)) {
            qCDebug(dsrApp) << "Text edit exists at lastEditMapKey.";
            // 点击鼠标左键时，去掉未更改的textEdit文本框
            if (m_editMap.value(m_lastEditMapKey)->toPlainText() == QString(tr("Input text here")) ||
                    m_editMap.value(m_lastEditMapKey)->toPlainText().isEmpty()) {
                qCDebug(dsrApp) << "Text edit is empty or default. Clearing selection and setting read-only.";
                clearSelected();
                setAllTextEditReadOnly();
                m_editing = false;
                m_selectedIndex = -1;
                m_selectedOrder = -1;
                m_selectedShape.type = "";
                update();
                DFrame::mousePressEvent(e);
            }
        }
    }

    //选中图形后，重新按下触摸屏左键
    if (Qt::MouseEventSource::MouseEventSynthesizedByQt == e->source()
            && m_selectedIndex != -1
            && !clickedShapes(e->pos())
            && "text" != m_currentType) {
        clearSelected();
        setAllTextEditReadOnly();
        m_editing = false;
        m_selectedIndex = -1;
        m_selectedOrder = -1;
        m_selectedShape.type = "";
        update();
        DFrame::mousePressEvent(e);

        //return;

    }

    if (m_selectedIndex != -1) {
        if (!(clickedOnShapes(e->pos()) && m_isRotated) && m_selectedIndex == -1 && "text" == m_currentType) {
            clearSelected();
            setAllTextEditReadOnly();
            m_editing = false;
            m_selectedIndex = -1;
            m_selectedOrder = -1;
            m_selectedShape.type = "";
            update();
            DFrame::mousePressEvent(e);
            return;
        }
    }

    //鼠标右键点击
    if (Qt::MouseEventSource::MouseEventSynthesizedByQt != e->source() && e->button() == Qt::RightButton) {
        m_pos1 = QPointF(0, 0); // 修复触控屏绘制矩形后，长按屏幕会出现多余矩形的问题
        qDebug() << "RightButton clicked!" << e->source();
        m_menuController->showMenu(QPoint(mapToGlobal(e->pos())));
        DFrame::mousePressEvent(e);
        return;
    }

    m_pressedPoint = e->pos();
    m_isPressed = true;
    if (!clickedOnShapes(m_pressedPoint)) {
        // AI助手不是绘图工具，不进入绘制逻辑
        if (m_currentType == "aiassistant") {
            DFrame::mousePressEvent(e);
            return;
        }

        m_isRecording = true;
        //qDebug() << "no one shape be clicked!" << m_selectedIndex << m_shapes.length();

        m_currentShape.type = m_currentType;
        m_currentShape.colorIndex = ConfigSettings::instance()->getValue(m_currentType, "color_index").toInt();
        m_currentShape.lineWidth = LINEWIDTH(ConfigSettings::instance()->getValue(m_currentType, "line_width").toInt());

        m_selectedIndex = -1;
        m_shapesIndex += 1;
        m_currentIndex = m_shapesIndex;

        if (m_pos1 == QPointF(0, 0)) {
            m_pos1 = e->pos();
            if (m_currentType == "pen") {
                m_currentShape.index = m_currentIndex;
                m_currentShape.points.append(m_pos1);
            } else if (m_currentType == "arrow") {
                m_currentShape.index = m_currentIndex;
                m_currentShape.isShiftPressed = m_isShiftPressed;
                m_currentShape.points.append(m_pos1);
                m_currentShape.lineWidth = LINEWIDTH(ConfigSettings::instance()->getValue("arrow", "line_width").toInt());
            } else if (m_currentType == "line") {
                m_currentShape.index = m_currentIndex;
                m_currentShape.isShiftPressed = m_isShiftPressed;
                m_currentShape.points.append(m_pos1);
                m_currentShape.lineWidth = LINEWIDTH(ConfigSettings::instance()->getValue("line", "line_width").toInt());
            } else if (m_currentType == "effect") {
                m_currentShape.isShiftPressed = m_isShiftPressed;
                m_currentShape.index = m_currentIndex;
                m_currentShape.isBlur = ConfigSettings::instance()->getValue("effect", "isBlur").toBool();
                m_currentShape.isOval = ConfigSettings::instance()->getValue("effect", "isOval").toInt();
                m_currentShape.radius = ConfigSettings::instance()->getValue("effect", "radius").toInt() * 3 + 10;
                m_currentShape.lineWidth = LINEWIDTH(ConfigSettings::instance()->getValue("effect", "line_width").toInt());
                if (m_currentShape.isBlur) {
                    emit reloadEffectImg("blur", m_currentShape.radius);
                } else {
                    emit reloadEffectImg("mosaic", m_currentShape.radius);
                }
                if (m_currentShape.isOval == 2) {
                    m_currentShape.points.append(m_pos1);
                }
            } else if (m_currentType == "rectangle") {
                m_currentShape.isShiftPressed = m_isShiftPressed;
                m_currentShape.index = m_currentIndex;
            } else if (m_currentType == "oval") {
                m_currentShape.isShiftPressed = m_isShiftPressed;
                m_currentShape.index = m_currentIndex;
            } else if (m_currentType == "spotlight") {
                m_currentShape.index = m_currentIndex;
            } else if (m_currentType == "text") {
                if (!m_editing) {
                    setAllTextEditReadOnly();
                    setNoChangedTextEditRemove();
                    m_currentShape.mainPoints[0] = m_pos1;
                    m_currentShape.index = m_currentIndex;
                    qDebug() << "new textedit:" << m_currentIndex;
                    TextEdit *edit = new TextEdit(m_currentIndex, this);
                    QString t_editText = QString(tr("Input text here"));
                    edit->appendPlainText(t_editText);
//                    edit->setPlainText(t_editText);
                    m_editing = true;
                    edit->setEditing(true);
                    int defaultFontSize = ConfigSettings::instance()->getValue("text", "fontsize").toInt();
                    m_currentShape.fontSize = defaultFontSize;


                    m_currentShape.mainPoints[0] = m_pos1;
                    m_currentShape.mainPoints[1] = QPointF(m_pos1.x(), m_pos1.y() + edit->height());
                    m_currentShape.mainPoints[2] = QPointF(m_pos1.x() + edit->width(), m_pos1.y());
                    m_currentShape.mainPoints[3] = QPointF(m_pos1.x() + edit->width(),
                                                           m_pos1.y() + edit->height());
                    m_editMap.insert(m_currentIndex, edit);
                    m_selectedIndex = m_currentIndex;
                    m_selectedShape = m_currentShape;
                    m_lastEditMapKey = m_currentIndex;


                    connect(edit, &TextEdit::repaintTextRect, this, &ShapesWidget::updateTextRect);
                    connect(edit, &TextEdit::clickToEditing, this, [ = ](int index) {
                        //                        setAllTextEditReadOnly();
                        for (int k = 0; k < m_shapes.length(); k++) {
                            if (m_shapes[k].type == "text" && m_shapes[k].index == index) {
                                m_selectedIndex = index;
                                m_selectedShape = m_shapes[k];
                                m_selectedOrder = k;
                                m_currentShape = m_selectedShape;
                                break;
                            }
                        }
                        QMap<int, TextEdit *>::iterator i = m_editMap.begin();
                        while (i != m_editMap.end()) {
                            if (i.key() != index) {
                                i.value()->setReadOnly(true);
                                i.value()->setEditing(false);
                            } else {
                                i.value()->setEditing(true);
                            }
                            QTextCursor textCursor =  i.value()->textCursor();
                            textCursor.clearSelection();
                            i.value()->setTextCursor(textCursor);
                            ++i;
                        }
                        m_editing = true;
                        m_isSelectedText = false;
                        emit shapeClicked("text");

                    });

                    connect(edit, &TextEdit::textEditSelected, this, [ = ](int index) {
                        //                        setAllTextEditReadOnly();
                        if (m_selectedIndex != index) {
                            m_editing = false;
                            m_currentShape.type = "";
                            for (int i = 0; i < m_currentShape.mainPoints.length(); i++) {
                                m_currentShape.mainPoints[i] = QPointF(0, 0);
                            }
                        }
                        for (int k = 0; k < m_shapes.length(); k++) {
                            if (m_shapes[k].type == "text" && m_shapes[k].index == index) {
                                m_selectedIndex = index;
                                m_selectedShape = m_shapes[k];
                                m_selectedOrder = k;
                                break;
                            }
                        }
                        QMap<int, TextEdit *>::iterator i = m_editMap.begin();
                        while (i != m_editMap.end()) {
                            if (i.key() != index) {
                                i.value()->setReadOnly(true);
                                i.value()->setEditing(false);
                            }

                            QTextCursor textCursor =  i.value()->textCursor();
                            textCursor.clearSelection();
                            i.value()->setTextCursor(textCursor);
                            ++i;
                        }
                        m_isSelectedText = true;
                        emit shapeClicked("text");
                    });

                    connect(edit, &TextEdit::textEditFinish, this, [ = ](int index) {
                        //                        setAllTextEditReadOnly();
                        Q_UNUSED(index);
                        setAllTextEditReadOnly();
                    });

                    edit->setSelecting(true);
                    edit->setFocus();
                    edit->move(static_cast<int>(m_pos1.x()), static_cast<int>(m_pos1.y()));
                    edit->show();
                    QTextCursor cs = edit->textCursor();
                    edit->moveCursor(QTextCursor::Start);
                    cs.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, t_editText.length());
                    edit->setTextCursor(cs);
                    edit->selectAll();
                    m_shapes.append(m_currentShape);


                    for (int k = 0; k < m_shapes.length(); k++) {
                        if (m_shapes[k].type == "text" && m_shapes[k].index == m_currentIndex) {
                            m_selectedOrder = k;
                            break;
                        }
                    }

                    qDebug() << "Insert text shape:" << m_shapes.size() << m_currentShape.type << m_currentShape.index;
                } else {
                    m_editing = false;
                    setAllTextEditReadOnly();

                }
            }
            update();
        }
    } else {
        m_isRecording = false;
        //qDebug() << "some on shape be clicked!";
        if (m_editing && m_editMap.contains(m_shapes[m_selectedOrder].index)) {
            m_editMap.value(m_shapes[m_selectedOrder].index)->setReadOnly(true);
            m_editMap.value(m_shapes[m_selectedOrder].index)->setCursorVisible(false);
            m_editMap.value(m_shapes[m_selectedOrder].index)->setFocusPolicy(Qt::NoFocus);
            qCDebug(dsrApp) << "Editing and editMap contains selected shape index. Setting text edit read-only.";
        }
        update();
    }
    DFrame::mousePressEvent(e);

}

void ShapesWidget::mouseReleaseEvent(QMouseEvent *e)
{
    qCDebug(dsrApp) << "mouseReleaseEvent called. Mouse position:" << e->pos();
    m_isPressed = false;
    m_isMoving = false;

    if (m_currentType == "ruler") {
        handleRulerRelease();
        DFrame::mouseReleaseEvent(e);
        return;
    }
    if (m_currentType == "measure-line") {
        handleMeasureLineRelease();
        DFrame::mouseReleaseEvent(e);
        return;
    }

    if (Qt::MouseEventSource::MouseEventSynthesizedByQt == e->source()
            && -1 != m_selectedIndex
            && "text" != m_currentType) {
        qCDebug(dsrApp) << "Synthesized mouse event, selected shape exists, and not text type. Returning.";
        return;
    }

    if (!m_isRotated && m_selectedIndex == -1 && "text" != m_currentType) {
        qCDebug(dsrApp) << "Not rotated, no shape selected, and not text type. Clearing selection.";
        clearSelected();
        setAllTextEditReadOnly();
        m_editing = false;
        m_selectedIndex = -1;
        m_selectedOrder = -1;
        m_selectedShape.type = "";
        update();
        DFrame::mouseReleaseEvent(e);
    }

    //qDebug() << m_isRecording << m_isSelected << m_pos2;
    if (m_isRecording && !m_isSelected && m_pos2 != QPointF(0, 0)) {
        qCDebug(dsrApp) << "Recording active, no shape selected, and pos2 is not (0,0). Finalizing shape.";
        if (m_currentType == "arrow" || m_currentType == "line") {
            qCDebug(dsrApp) << "Finalizing arrow or line.";
            if (m_currentShape.points.length() == 2) {
                qCDebug(dsrApp) << "Arrow/line has 2 points.";
                if (m_isShiftPressed) {
                    qCDebug(dsrApp) << "Shift pressed, snapping arrow/line to axis.";
                    if (std::atan2(std::abs(m_pos2.y() - m_pos1.y()), std::abs(m_pos2.x() - m_pos1.x()))
                            * 180 / M_PI < 45) {
                        m_pos2 = QPointF(m_pos2.x(), m_pos1.y());
                        qCDebug(dsrApp) << "Snapped to horizontal.";
                    } else {
                        m_pos2 = QPointF(m_pos1.x(), m_pos2.y());
                        qCDebug(dsrApp) << "Snapped to vertical.";
                    }
                }

                m_currentShape.points[1] = m_pos2;
                m_currentShape.mainPoints = getMainPoints(m_currentShape.points[0], m_currentShape.points[1]);
                qCDebug(dsrApp) << "Set second point and main points for arrow/line.";

                m_shapes.append(m_currentShape);
            }
        } else if (m_currentType == "pen") {
            qCDebug(dsrApp) << "Finalizing pen shape.";
            FourPoints lineFPoints = fourPointsOfLine(m_currentShape.points);
            m_currentShape.mainPoints = lineFPoints;
            m_shapes.append(m_currentShape);
            qCDebug(dsrApp) << "Appended current pen shape to shapes list.";
        } else if (m_currentType != "text") {
            qCDebug(dsrApp) << "Finalizing non-text shape (rectangle/oval/effect).";
            FourPoints rectFPoints = getMainPoints(m_pos1, m_pos2, m_isShiftPressed);
            m_currentShape.mainPoints = rectFPoints;
            m_shapes.append(m_currentShape);
            qCDebug(dsrApp) << "Appended current shape to shapes list.";
        }

        //qDebug() << "ShapesWidget num:" << m_shapes.length();
        clearSelected();
        //选中当前绘制的图形
        if (m_currentShape.type != "effect" || m_currentShape.isOval != 2) {
            qCDebug(dsrApp) << "Selecting newly drawn shape.";
            m_selectedIndex = m_currentIndex;
            m_selectedShape = m_currentShape;
            m_selectedOrder = m_shapes.length() - 1;
            m_isSelected = true;
        }
    }

    m_isRecording = false;
    if (m_currentShape.type != "text") {
        qCDebug(dsrApp) << "Clearing main points of current shape (if not text).";
        for (int i = 0; i < m_currentShape.mainPoints.length(); i++) {
            m_currentShape.mainPoints[i] = QPointF(0, 0);
        }
    }

    m_currentShape.points.clear();
    m_pos1 = QPointF(0, 0);
    m_pos2 = QPointF(0, 0);

    update();
    DFrame::mouseReleaseEvent(e);
    qCDebug(dsrApp) << "mouseReleaseEvent finished.";
}

void ShapesWidget::mouseMoveEvent(QMouseEvent *e)
{
    qCDebug(dsrApp) << "mouseMoveEvent called. Mouse position:" << e->pos();
    m_isMoving = true;
    m_movingPoint = e->pos();
    m_currentCursor = QCursor().pos();

    if (handleColorPickerMove(e->pos())) {
        DFrame::mouseMoveEvent(e);
        return;
    }
    if (handleRulerMove(e->pos())) {
        DFrame::mouseMoveEvent(e);
        return;
    }
    if (handleMeasureLineMove(e->pos())) {
        DFrame::mouseMoveEvent(e);
        return;
    }

    if (m_isRecording && m_isPressed) {
        qCDebug(dsrApp) << "Recording is active and mouse is pressed.";
        m_pos2 = e->pos();
        updateCursorShape();

        if (m_currentShape.type == "arrow" || m_currentShape.type == "line") {
            qCDebug(dsrApp) << "Moving arrow or line shape.";
            if (m_currentShape.points.length() <= 1) {
                qCDebug(dsrApp) << "Adding second point to arrow/line.";
                if (m_isShiftPressed) {
                    qCDebug(dsrApp) << "Shift pressed, snapping arrow/line to axis.";
                    if (std::atan2(std::abs(m_pos2.y() - m_pos1.y()),
                                   std::abs(m_pos2.x() - m_pos1.x())) * 180 / M_PI < 45) {
                        m_currentShape.points.append(QPointF(m_pos2.x(), m_pos1.y()));
                        qCDebug(dsrApp) << "Snapped to horizontal.";
                    } else {
                        m_currentShape.points.append(QPointF(m_pos1.x(), m_pos2.y()));
                        qCDebug(dsrApp) << "Snapped to vertical.";
                    }
                } else {
                    m_currentShape.points.append(m_pos2);
                    qCDebug(dsrApp) << "Appended pos2 to arrow/line points.";
                }
            } else {
                qCDebug(dsrApp) << "Updating second point of arrow/line.";
                if (m_isShiftPressed) {
                    qCDebug(dsrApp) << "Shift pressed, snapping arrow/line to axis.";
                    if (std::atan2(std::abs(m_pos2.y() - m_pos1.y()),
                                   std::abs(m_pos2.x() - m_pos1.x())) * 180 / M_PI < 45) {
                        m_currentShape.points[1] = QPointF(m_pos2.x(), m_pos1.y());
                        qCDebug(dsrApp) << "Snapped to horizontal.";
                    } else {
                        m_currentShape.points[1] = QPointF(m_pos1.x(), m_pos2.y());
                        qCDebug(dsrApp) << "Snapped to vertical.";
                    }
                } else {
                    m_currentShape.points[1] = m_pos2;
                    qCDebug(dsrApp) << "Appended pos2 to arrow/line points.";
                }
            }
        }
        if (m_currentShape.type == "pen") {
            if (getDistance(m_currentShape.points[m_currentShape.points.length() - 1], m_pos2) > 3) {
                m_currentShape.points.append(m_pos2);
                qCDebug(dsrApp) << "Pen type: distance > 3, appending pos2.";
            }
        }
        // 模糊笔
        if (m_currentShape.type == "effect" && m_currentShape.isOval == 2) {
            double distance = getDistance(m_currentShape.points[m_currentShape.points.length() - 1], m_pos2);
            if (distance > 14) {
                QList<QPointF> interpolationPoints = getInterpolationPoints(m_currentShape.points[m_currentShape.points.length() - 1], m_pos2,
                                                                            (distance / 7));
                for (int i = 0; i < interpolationPoints.size(); ++i) {
                    m_currentShape.points.append(interpolationPoints[i]);
                }
                m_currentShape.points.append(m_pos2);
                qCDebug(dsrApp) << "Effect type: distance > 14, appending interpolated points and pos2.";
            } else if (distance > 7) {
                m_currentShape.points.append(m_pos2);
                qCDebug(dsrApp) << "Effect type: distance > 7, appending pos2.";
            }
        }
        update();
    } else if (!m_isRecording && m_isPressed) {
        if (m_isRotated && m_isPressed) {
            handleRotate(e->pos());
            update();
            qCDebug(dsrApp) << "Not recording but pressed and rotated, calling handleRotate.";
        }

        if (m_isResize && m_isPressed) {
            // resize function
            handleResize(QPointF(e->pos()), m_clickedKey);
            update();
            DFrame::mouseMoveEvent(e);
            qCDebug(dsrApp) << "Not recording but pressed and resizing, calling handleResize.";
            return;
        }

        if (m_isSelected && m_isPressed && m_selectedIndex != -1) {
            handleDrag(m_pressedPoint, m_movingPoint);
            //qDebug() << "move m_selectedOrder:" << m_selectedOrder;
            m_selectedShape = m_shapes[m_selectedOrder];
            m_hoveredShape = m_shapes[m_selectedOrder];

            if (m_selectedShape.type == "text") {
                m_editMap.value(m_selectedIndex)->move(static_cast<int>(m_selectedShape.mainPoints[0].x()),
                                                       static_cast<int>(m_selectedShape.mainPoints[0].y()));
                qCDebug(dsrApp) << "Selected shape is text, moving text edit.";
            }

            m_pressedPoint = m_movingPoint;
            update();
            qCDebug(dsrApp) << "Selected, pressed, and index not -1, calling handleDrag.";
        }

    } else {
        qCDebug(dsrApp) << "Not recording or not pressed.";
        if (!m_isRecording) {
            m_isHovered = false;
            for (int i = 0; i < m_shapes.length(); i++) {
                m_hoveredIndex = m_shapes[i].index;

                if (hoverOnShapes(m_shapes[i],  e->pos())) {
                    m_isHovered = true;
                    m_hoveredShape = m_shapes[i];
                    //悬停状态时，根据悬停的位置不同，光标的形状也不同
                    if (m_resizeDirection == Left) {
                        if (m_isSelected || m_isRotated) {
                            qApp->setOverrideCursor(Qt::SizeHorCursor);
                        } else {
                            qApp->setOverrideCursor(Qt::ClosedHandCursor);
                        }
                    } else if (m_resizeDirection == Top) {
                        if (m_isSelected || m_isRotated) {
                            qApp->setOverrideCursor(Qt::SizeVerCursor);
                        } else {
                            qApp->setOverrideCursor(Qt::ClosedHandCursor);
                        }
                    } else if (m_resizeDirection == Right) {
                        if (m_isSelected || m_isRotated) {
                            qApp->setOverrideCursor(Qt::SizeHorCursor);
                        } else {
                            qApp->setOverrideCursor(Qt::ClosedHandCursor);
                        }
                    } else if (m_resizeDirection == Bottom) {
                        if (m_isSelected || m_isRotated) {
                            qApp->setOverrideCursor(Qt::SizeVerCursor);
                        } else {
                            qApp->setOverrideCursor(Qt::ClosedHandCursor);
                        }
                    } else if (m_resizeDirection == TopLeft) {
                        if (m_isSelected || m_isRotated) {
                            qApp->setOverrideCursor(Qt::SizeFDiagCursor);
                        } else {
                            qApp->setOverrideCursor(Qt::ClosedHandCursor);
                        }
                    } else if (m_resizeDirection == BottomLeft) {
                        if (m_isSelected || m_isRotated) {
                            qApp->setOverrideCursor(Qt::SizeBDiagCursor);
                        } else {
                            qApp->setOverrideCursor(Qt::ClosedHandCursor);
                        }
                    } else if (m_resizeDirection == TopRight) {
                        if (m_isSelected || m_isRotated) {
                            qApp->setOverrideCursor(Qt::SizeBDiagCursor);
                        } else {
                            qApp->setOverrideCursor(Qt::ClosedHandCursor);
                        }
                    } else if (m_resizeDirection == BottomRight) {
                        if (m_isSelected || m_isRotated) {
                            qApp->setOverrideCursor(Qt::SizeFDiagCursor);
                        } else {
                            qApp->setOverrideCursor(Qt::ClosedHandCursor);
                        }
                    } else if (m_resizeDirection == Rotate) {
                        qApp->setOverrideCursor(BaseUtils::setCursorShape("rotate"));
                    } else if (m_resizeDirection == Moving) {
                        qApp->setOverrideCursor(Qt::ClosedHandCursor);
                    } else {
                        updateCursorShape();
                    }
                    //update();
                    break;
                } else {
                    updateCursorShape();
                    //update();
                }
            }
            if (!m_isHovered) {
                for (int j = 0; j < m_hoveredShape.mainPoints.length(); j++) {
                    m_hoveredShape.mainPoints[j] = QPointF(0, 0);
                }
                m_hoveredShape.type = "";
                //update();
            }
            if (m_shapes.length() == 0) {
                updateCursorShape();
            }
        } else {
            //TODO text
        }
    }
    update();
    DFrame::mouseMoveEvent(e);
}

//鼠标选中图形时，可以通过w/a/s/d及小键盘方向键控制光标移动
void ShapesWidget::keyPressEvent(QKeyEvent *keyEvent)
{
    if (m_isPressed) {
        Utils::cursorMove(m_currentCursor, keyEvent);
    }

    if (m_currentType == "ruler") {
        const qreal moveStep = keyEvent->modifiers() & Qt::AltModifier ? RULER_KEY_FAST_MOVE_STEP : RULER_KEY_MOVE_STEP;
        const qreal resizeStep = keyEvent->modifiers() & Qt::AltModifier ? RULER_KEY_FAST_RESIZE_STEP : RULER_KEY_RESIZE_STEP;

        if ((keyEvent->modifiers() & Qt::ControlModifier) && keyEvent->key() == Qt::Key_C) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                if (copyCurrentMeasurementDetailText()) {
                    return;
                }
            }
            if (copyCurrentMeasurementText()) {
                return;
            }
        }

        if (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace) {
            if (m_rulerMarkBVisible) {
                m_rulerMarkBVisible = false;
                if (m_rulerActiveMark == RulerAction::DragMarkB) {
                    m_rulerActiveMark = m_rulerMarkAVisible ? RulerAction::DragMarkA : RulerAction::None;
                }
            } else if (m_rulerMarkAVisible) {
                m_rulerMarkAVisible = false;
                m_rulerActiveMark = RulerAction::None;
            } else {
                m_rulerVisible = false;
                m_rulerPressed = false;
                m_rulerAction = RulerAction::None;
                m_rulerActiveMark = RulerAction::None;
            }
            updateCursorShape();
            update();
            return;
        }

        if (keyEvent->key() == Qt::Key_Escape) {
            m_rulerVisible = false;
            m_rulerPressed = false;
            m_rulerAction = RulerAction::None;
            m_rulerActiveMark = RulerAction::None;
            updateCursorShape();
            update();
            return;
        }

        if (!m_rulerVisible) {
            initRulerIfNeeded();
        }

        if (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_A) {
            if (moveActiveRulerMark(QPointF(-moveStep, 0))) {
                addMeasurementHistory();
                update();
                return;
            }
            m_rulerCenter += QPointF(-moveStep, 0);
            update();
            return;
        }

        if (keyEvent->key() == Qt::Key_Right || keyEvent->key() == Qt::Key_D) {
            if (moveActiveRulerMark(QPointF(moveStep, 0))) {
                addMeasurementHistory();
                update();
                return;
            }
            m_rulerCenter += QPointF(moveStep, 0);
            update();
            return;
        }

        if (keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_W) {
            if (moveActiveRulerMark(QPointF(0, -moveStep))) {
                addMeasurementHistory();
                update();
                return;
            }
            m_rulerCenter += QPointF(0, -moveStep);
            update();
            return;
        }

        if (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_S) {
            if (moveActiveRulerMark(QPointF(0, moveStep))) {
                addMeasurementHistory();
                update();
                return;
            }
            m_rulerCenter += QPointF(0, moveStep);
            update();
            return;
        }

        if (keyEvent->key() == Qt::Key_Minus || keyEvent->key() == Qt::Key_Underscore
                || keyEvent->key() == Qt::Key_BracketLeft) {
            resizeRulerCentered(-resizeStep);
            updateCursorShape();
            update();
            return;
        }

        if (keyEvent->key() == Qt::Key_Plus || keyEvent->key() == Qt::Key_Equal
                || keyEvent->key() == Qt::Key_BracketRight) {
            resizeRulerCentered(resizeStep);
            updateCursorShape();
            update();
            return;
        }

        if (keyEvent->key() == Qt::Key_R) {
            m_rulerAngle = 0;
            updateCursorShape();
            update();
            return;
        }

        if (keyEvent->key() == Qt::Key_M) {
            cycleRulerMeasureMode();
            addMeasurementHistory();
            update();
            return;
        }

        qDebug() << "ShapeWidget Exist keyEvent:" << keyEvent->text();
        return;
    }

    if (m_currentType == "measure-line") {
        if ((keyEvent->modifiers() & Qt::ControlModifier) && keyEvent->key() == Qt::Key_C) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                if (copyCurrentMeasurementDetailText()) {
                    return;
                }
            }
            if (copyCurrentMeasurementText()) {
                return;
            }
        }

        if (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace
                || keyEvent->key() == Qt::Key_Escape) {
            resetMeasureLine();
            updateCursorShape();
            update();
            return;
        }

        QPointF delta;
        switch (keyEvent->key()) {
        case Qt::Key_Left:
        case Qt::Key_A:
            delta = QPointF(-1, 0);
            break;
        case Qt::Key_Right:
        case Qt::Key_D:
            delta = QPointF(1, 0);
            break;
        case Qt::Key_Up:
        case Qt::Key_W:
            delta = QPointF(0, -1);
            break;
        case Qt::Key_Down:
        case Qt::Key_S:
            delta = QPointF(0, 1);
            break;
        default:
            break;
        }

        if (!delta.isNull()) {
            if (keyEvent->modifiers() & Qt::AltModifier) {
                delta *= 10;
            }
            if (moveActiveMeasureLinePoint(delta, keyEvent->modifiers())) {
                if (m_measureLinePointAVisible && m_measureLinePointBVisible) {
                    addMeasurementHistory();
                }
                updateCursorShape();
                update();
            }
            return;
        }

        if ((keyEvent->key() == Qt::Key_Shift || keyEvent->key() == Qt::Key_Control)
                && m_measureLinePointAVisible && !m_measureLinePointBVisible) {
            m_measureLineHoverPoint = constrainedMeasureLinePoint(mapFromGlobal(QCursor::pos()));
            syncMeasureLineCursorToPoint(m_measureLineHoverPoint);
            update();
            return;
        }

        qDebug() << "ShapeWidget Exist keyEvent:" << keyEvent->text();
        return;
    }

    // 处理删除键
    if (keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace) {
        deleteCurrentShape();
        return;
    }
    
    // 处理选中图形后的键盘调整
    if (m_selectedIndex == -1 || m_selectedOrder >= m_shapes.length()) {
        qDebug() << "ShapeWidget Exist keyEvent:" << keyEvent->text();
        return;
    }
    

    static const QHash<int, QString> modifierMap = {
        {Qt::ShiftModifier | Qt::ControlModifier, "Ctrl+Shift"},
        {Qt::ControlModifier, "Ctrl"},
        {Qt::NoModifier, ""}
    };
    
    QString direction;
    int modifiers = keyEvent->modifiers();
    
    if (modifierMap.contains(modifiers)) {
        QString prefix = modifierMap[modifiers];
        
        switch (keyEvent->key()) {
            case Qt::Key_Left:
            case Qt::Key_A:
                direction = prefix.isEmpty() ? Direction::LEFT : prefix + "+" + Direction::LEFT;
                break;
            case Qt::Key_Right:
            case Qt::Key_D:
                direction = prefix.isEmpty() ? Direction::RIGHT : prefix + "+" + Direction::RIGHT;
                break;
            case Qt::Key_Up:
            case Qt::Key_W:
                direction = prefix.isEmpty() ? Direction::UP : prefix + "+" + Direction::UP;
                break;
            case Qt::Key_Down:
            case Qt::Key_S:
                direction = prefix.isEmpty() ? Direction::DOWN : prefix + "+" + Direction::DOWN;
                break;
            default:
                break;
        }
    }
    
    // 如果找到了有效的方向，调用microAdjust
    if (!direction.isEmpty()) {
        microAdjust(direction);
    } else {
        qDebug() << "ShapeWidget Exist keyEvent:" << keyEvent->text();
    }
}

void ShapesWidget::updateTextRect(TextEdit *edit, QRectF newRect)
{
    int index = edit->getIndex();
    //    qDebug() << "updateTextRect:" << newRect << index;
    for (int j = 0; j < m_shapes.length(); j++) {
        //        qDebug() << "updateTextRect  updating:" << j << m_shapes[j].index << index;
        if (m_shapes[j].type == "text" && m_shapes[j].index == index) {
            m_shapes[j].mainPoints[0] = QPointF(newRect.x(), newRect.y());
            m_shapes[j].mainPoints[1] = QPointF(newRect.x(), newRect.y() + newRect.height());
            m_shapes[j].mainPoints[2] = QPointF(newRect.x() + newRect.width(), newRect.y());
            m_shapes[j].mainPoints[3] = QPointF(newRect.x() + newRect.width(),
                                                newRect.y() + newRect.height());
            m_currentShape = m_shapes[j];
            m_selectedShape = m_shapes[j];
            m_selectedIndex = m_shapes[j].index;
            m_selectedOrder = j;
        }
    }
    update();
}

void ShapesWidget::paintImgPoint(QPainter &painter, QPointF pos, QPixmap img, bool isResize)
{
    if (isResize) {
        painter.drawPixmap(QPointF(pos.x() - DRAG_BOUND_RADIUS,
                                   pos.y() - DRAG_BOUND_RADIUS), img);
    } else {
        painter.drawPixmap(QPointF(pos.x() - 17,
                                   pos.y() - 10), img);
    }
}

void ShapesWidget::paintRect(QPainter &painter, FourPoints rectFPoints, int index,
                             ShapeBlurStatus rectStatus, bool isBlur, bool isMosaic, int radius)
{
    QPainterPath rectPath;
    if (rectStatus  == Hovered || ((isBlur || isMosaic) && index == m_selectedIndex)) {
        painter.setPen(QColor("#01bdff"));
    } else if (rectStatus == Drawing || ((isBlur | isMosaic) && index != m_selectedIndex)) {
        painter.setPen(Qt::transparent);
    }

    rectPath.moveTo(rectFPoints[0].x(), rectFPoints[0].y());
    rectPath.lineTo(rectFPoints[1].x(), rectFPoints[1].y());
    rectPath.lineTo(rectFPoints[3].x(), rectFPoints[3].y());
    rectPath.lineTo(rectFPoints[2].x(), rectFPoints[2].y());
    rectPath.lineTo(rectFPoints[0].x(), rectFPoints[0].y());
    painter.drawPath(rectPath);

    //    using namespace utils;
    if (isBlur) {
        painter.setClipPath(rectPath);
        painter.drawPixmap(0, 0,  width(), height(),  TempFile::instance()->getBlurPixmap(radius));
        painter.drawPath(rectPath);
    }
    if (isMosaic) {
        painter.setClipPath(rectPath);
        painter.drawPixmap(0, 0,  width(), height(),  TempFile::instance()->getMosaicPixmap(radius));
        painter.drawPath(rectPath);
    }
    painter.setClipping(false);
}

void ShapesWidget::paintEllipse(QPainter &painter, FourPoints ellipseFPoints, int index,
                                ShapeBlurStatus  ovalStatus, bool isBlur, bool isMosaic, int radius)
{
    if (ovalStatus  == Hovered || ((isBlur || isMosaic) && index == m_selectedIndex)) {
        painter.setPen(QColor("#01bdff"));
    } else if (ovalStatus == Drawing || ((isBlur | isMosaic) && index != m_selectedIndex)) {
        painter.setPen(Qt::transparent);
    }

    FourPoints minorPoints = getAnotherFPoints(ellipseFPoints);
    QList<QPointF> eightControlPoints = getEightControlPoint(ellipseFPoints);
    QPainterPath ellipsePath;
    QPainterPath rectPath;
    //    qDebug() << "here" << ellipseFPoints[0].y() - ellipseFPoints[2].y();

    if (qAbs(ellipseFPoints[0].y() - ellipseFPoints[2].y()) <= 0.1
            && qAbs(ellipseFPoints[1].y() - ellipseFPoints[3].y()) <= 0.1) {
        QRect t_rect;
        t_rect.setX(static_cast<int>(ellipseFPoints[0].x()));
        t_rect.setY(static_cast<int>(ellipseFPoints[0].y()));
        t_rect.setWidth(static_cast<int>(ellipseFPoints[3].x() - ellipseFPoints[0].x()));
        t_rect.setHeight(static_cast<int>(ellipseFPoints[3].y() - ellipseFPoints[0].y()));

        ellipsePath.addEllipse(t_rect);
        painter.drawPath(ellipsePath);
    }

    else {
        ellipsePath.moveTo(minorPoints[0].x(), minorPoints[0].y());
        ellipsePath.cubicTo(eightControlPoints[0], eightControlPoints[1], minorPoints[1]);
        ellipsePath.cubicTo(eightControlPoints[4], eightControlPoints[5], minorPoints[2]);
        ellipsePath.cubicTo(eightControlPoints[6], eightControlPoints[7], minorPoints[3]);
        ellipsePath.cubicTo(eightControlPoints[3], eightControlPoints[2], minorPoints[0]);
        painter.drawPath(ellipsePath);
    }


    //    using namespace utils;
    if (isBlur) {
        painter.setClipPath(ellipsePath);
        painter.drawPixmap(0, 0,  width(), height(),  TempFile::instance()->getBlurPixmap(radius));
        painter.drawPath(ellipsePath);
    }
    if (isMosaic) {
        painter.setClipPath(ellipsePath);
        painter.drawPixmap(0, 0,  width(), height(),  TempFile::instance()->getMosaicPixmap(radius));
        painter.drawPath(ellipsePath);
    }
    painter.setClipping(false);
}

void ShapesWidget::paintArrow(QPainter &painter, QList<QPointF> lineFPoints,
                              int lineWidth, bool isStraight)
{
    if (lineFPoints.length() == 2) {
        if (!isStraight) {
            QList<QPointF> arrowPoints = pointOfArrow(lineFPoints[0],
                                                      lineFPoints[1], 8 + (lineWidth - 1) * 2);
            QPainterPath path;
            const QPen oldPen = painter.pen();
            if (arrowPoints.length() >= 3) {
                painter.drawLine(lineFPoints[0], lineFPoints[1]);
                path.moveTo(arrowPoints[2].x(), arrowPoints[2].y());
                path.lineTo(arrowPoints[0].x(), arrowPoints[0].y());
                path.lineTo(arrowPoints[1].x(), arrowPoints[1].y());
                path.lineTo(arrowPoints[2].x(), arrowPoints[2].y());
            }
            painter.setPen(Qt :: NoPen);
            painter.fillPath(path, QBrush(oldPen.color()));
        } else {
            painter.drawLine(lineFPoints[0], lineFPoints[1]);
        }
    }
}

void ShapesWidget::paintLine(QPainter &painter, QList<QPointF> lineFPoints)
{
    QPainterPath linePaths;
    if (lineFPoints.length() >= 1)
        linePaths.moveTo(lineFPoints[0]);
    else
        return;

    for (int k = 1; k < lineFPoints.length() - 2; k++) {
        linePaths.quadTo(lineFPoints[k], lineFPoints[k + 1]);
    }
    painter.drawPath(linePaths);
}

void ShapesWidget::paintEffectLine(QPainter &painter, QList<QPointF> lineFPoints, bool isMosaic, int radius, int lineWidth)
{
    QPainterPath linePaths;
    if (lineFPoints.length() >= 1)
        linePaths.moveTo(lineFPoints[0]);
    else
        return;

    for (int k = 1; k < lineFPoints.length() - 2; k++) {
        FourPoints rectFoints = getRectPoints(lineFPoints[k - 1], lineFPoints[k + 1], lineWidth);
        paintRect(painter, rectFoints, 0, Drawing, isMosaic, !isMosaic, radius);
        //paintRect(painter, rectFoints, 0, Hovered, false, false, radius);
    }
}

void ShapesWidget::paintText(QPainter &painter, FourPoints rectFPoints)
{
    QPen textPen;
    textPen.setStyle(Qt::DashLine);
    textPen.setColor("#01bdff");
    painter.setPen(textPen);

    if (rectFPoints.length() >= 4) {
        painter.drawLine(rectFPoints[0], rectFPoints[1]);
        painter.drawLine(rectFPoints[1], rectFPoints[3]);
        painter.drawLine(rectFPoints[3], rectFPoints[2]);
        painter.drawLine(rectFPoints[2], rectFPoints[0]);
    }
}

void ShapesWidget::paintText(QPainter &painter, FourPoints rectFPoints, const QString &text, int fontsize)
{
    qDebug() << "fontsize: " << fontsize;
    QFont font = painter.font() ;
    font.setPixelSize(fontsize);
    painter.setFont(font);
    QRect rect(static_cast<int>(rectFPoints[0].x()),
               static_cast<int>(rectFPoints[0].y()),
               static_cast<int>(rectFPoints[3].x() - rectFPoints[0].x()),
               static_cast<int>(rectFPoints[3].y() - rectFPoints[0].y()));
    painter.drawText(rect, Qt::AlignLeft, text);
}

void ShapesWidget::paintStepNumber(QPainter &painter, const Toolshape &shape)
{
    const QRectF shapeRect = QRectF(shape.mainPoints[0], shape.mainPoints[3]).normalized();
    if (!shapeRect.isValid()) {
        return;
    }

    const QPointF center = shapeRect.center();
    const StepNumberStyle style = stepNumberStyleFromValue(shape.stepNumberStyle);
    const QString numberText = QString::number(shape.fontSize);
    QSizeF baseSize(28, 28);
    if (style == StepNumberStyle::Pin) {
        baseSize = QSizeF(30, 48);
    } else if (style == StepNumberStyle::DotLabel) {
        baseSize = QSizeF(50, 30);
    } else if (style == StepNumberStyle::Bubble) {
        baseSize = QSizeF(48, 36);
    }
    const qreal scale = qBound<qreal>(0.55,
                                      qMin(shapeRect.width() / baseSize.width(),
                                           shapeRect.height() / baseSize.height()),
                                      4.0);
    auto stepColor = [](int index, int fallback) {
        return BaseUtils::colorIndexOf(qBound(0, index < 0 ? fallback : index, 11));
    };
    const QColor markerColor = stepColor(shape.colorIndex, 3);
    const QColor numberColor = stepColor(shape.stepNumberTextColorIndex, 2);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QFont numberFont = painter.font();
    numberFont.setBold(true);
    numberFont.setPixelSize(qBound(10, qRound(17 * scale), 48));
    painter.setFont(numberFont);

    if (style == StepNumberStyle::OutlineCircle) {
        painter.setPen(QPen(markerColor, 3));
        painter.setBrush(QColor(255, 255, 255, 220));
        painter.drawEllipse(shapeRect.adjusted(2, 2, -2, -2));
        painter.setPen(numberColor);
        painter.drawText(shapeRect, Qt::AlignCenter, numberText);
    } else if (style == StepNumberStyle::Pin) {
        const qreal top = shapeRect.top() + 2 * scale;
        const qreal tipY = shapeRect.bottom() - 2 * scale;
        const qreal pinHalfWidth = qMax<qreal>(6, shapeRect.width() / 2 - scale);
        const qreal pinHeight = qMax<qreal>(24, tipY - top);

        QPainterPath pinPath;
        pinPath.moveTo(center.x(), tipY);
        pinPath.cubicTo(center.x() - pinHalfWidth * 0.55, top + pinHeight * 0.86,
                        center.x() - pinHalfWidth, top + pinHeight * 0.64,
                        center.x() - pinHalfWidth, top + pinHeight * 0.38);
        pinPath.cubicTo(center.x() - pinHalfWidth, top + pinHeight * 0.16,
                        center.x() - pinHalfWidth * 0.62, top,
                        center.x(), top);
        pinPath.cubicTo(center.x() + pinHalfWidth * 0.62, top,
                        center.x() + pinHalfWidth, top + pinHeight * 0.16,
                        center.x() + pinHalfWidth, top + pinHeight * 0.38);
        pinPath.cubicTo(center.x() + pinHalfWidth, top + pinHeight * 0.64,
                        center.x() + pinHalfWidth * 0.55, top + pinHeight * 0.86,
                        center.x(), tipY);
        pinPath.closeSubpath();

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 28));
        painter.drawEllipse(QRectF(center.x() - 8 * scale, tipY - scale,
                                   16 * scale, 5 * scale));

        QLinearGradient pinGradient(QPointF(shapeRect.left(), top),
                                    QPointF(shapeRect.right(), tipY));
        QColor lightMarker = markerColor.lighter(125);
        QColor darkMarker = markerColor.darker(118);
        lightMarker.setAlpha(250);
        darkMarker.setAlpha(250);
        pinGradient.setColorAt(0.0, lightMarker);
        pinGradient.setColorAt(0.45, markerColor);
        pinGradient.setColorAt(1.0, darkMarker);
        painter.setBrush(pinGradient);
        painter.drawPath(pinPath);

        QColor edgeColor = markerColor.darker(118);
        edgeColor.setAlpha(150);
        painter.setPen(QPen(edgeColor, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(pinPath);

        const qreal plateSize = qBound<qreal>(12, 19 * scale, shapeRect.width() - 6 * scale);
        const QRectF numberPlate(center.x() - plateSize / 2,
                                 top + 8 * scale,
                                 plateSize, plateSize);
        QColor plateColor = numberColor.lightness() > 190
                            ? QColor(17, 24, 39, 92)
                            : QColor(255, 255, 255, 232);
        painter.setPen(QPen(QColor(255, 255, 255, 120), 1));
        painter.setBrush(plateColor);
        painter.drawEllipse(numberPlate);

        painter.setPen(QPen(QColor(255, 255, 255, 180), 1.4 * scale, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(QRectF(center.x() + 3 * scale, top + 5 * scale,
                               8 * scale, 8 * scale), 20 * 16, 70 * 16);

        QRectF numberRect = numberPlate.adjusted(1, 0, -1, 0);
        painter.setPen(numberColor);
        painter.drawText(numberRect, Qt::AlignCenter, numberText);
    } else if (style == StepNumberStyle::DotLabel) {
        const QPointF anchor = shape.points.isEmpty()
                               ? QPointF(shapeRect.left() + 5, shapeRect.center().y())
                               : shape.points.first();
        QRectF labelRect(anchor.x() + 11 * scale, anchor.y() - 12 * scale,
                         32 * scale, 24 * scale);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 32));
        painter.drawRoundedRect(labelRect.translated(0, scale), 12 * scale, 12 * scale);
        painter.setPen(QPen(markerColor, 1.6 * scale, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(anchor.x() + 4 * scale, anchor.y()),
                         QPointF(labelRect.left() + scale, anchor.y()));
        painter.setPen(Qt::NoPen);
        painter.setBrush(markerColor);
        painter.drawEllipse(anchor, 4 * scale, 4 * scale);
        painter.setBrush(markerColor);
        painter.drawRoundedRect(labelRect, 12 * scale, 12 * scale);
        painter.setPen(numberColor);
        painter.drawText(labelRect, Qt::AlignCenter, numberText);
    } else if (style == StepNumberStyle::Bubble) {
        QRectF bubbleRect = shapeRect.adjusted(4 * scale, 2 * scale, -4 * scale, -10 * scale);
        const QPointF tailTip(bubbleRect.center().x(), shapeRect.bottom() - 2 * scale);
        QPainterPath bubblePath;
        bubblePath.addRoundedRect(bubbleRect, 11 * scale, 11 * scale);
        QPainterPath tailPath;
        tailPath.moveTo(bubbleRect.center().x() - 5 * scale, bubbleRect.bottom() - 3 * scale);
        tailPath.quadTo(bubbleRect.center().x() - 2 * scale, bubbleRect.bottom() + 3 * scale,
                        tailTip.x(), tailTip.y());
        tailPath.quadTo(bubbleRect.center().x() + 2 * scale, bubbleRect.bottom() + 3 * scale,
                        bubbleRect.center().x() + 5 * scale, bubbleRect.bottom() - 3 * scale);
        tailPath.closeSubpath();
        bubblePath = bubblePath.united(tailPath);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 30));
        painter.drawPath(bubblePath.translated(0, 1.2 * scale));

        QLinearGradient bubbleGradient(bubbleRect.topLeft(), bubbleRect.bottomRight());
        QColor lightMarker = markerColor.lighter(118);
        QColor darkMarker = markerColor.darker(112);
        lightMarker.setAlpha(248);
        darkMarker.setAlpha(248);
        bubbleGradient.setColorAt(0.0, lightMarker);
        bubbleGradient.setColorAt(0.55, markerColor);
        bubbleGradient.setColorAt(1.0, darkMarker);
        painter.setBrush(bubbleGradient);
        painter.drawPath(bubblePath);

        QColor edgeColor = markerColor.darker(120);
        edgeColor.setAlpha(130);
        painter.setPen(QPen(edgeColor, scale));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(bubblePath);

        painter.setPen(QPen(QColor(255, 255, 255, 145), 1.2 * scale, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(bubbleRect.left() + 10 * scale, bubbleRect.top() + 5 * scale),
                         QPointF(bubbleRect.right() - 10 * scale, bubbleRect.top() + 5 * scale));

        painter.setPen(numberColor);
        painter.drawText(bubbleRect, Qt::AlignCenter, numberText);
    } else {
        QColor rimColor = markerColor.darker(115);
        rimColor.setAlpha(210);
        painter.setPen(QPen(rimColor, 2));
        painter.setBrush(markerColor);
        painter.drawEllipse(shapeRect.adjusted(1, 1, -1, -1));
        painter.setPen(numberColor);
        painter.drawText(shapeRect, Qt::AlignCenter, numberText);
    }
    painter.restore();
}

void ShapesWidget::paintSpotlightMask(QPainter &painter, const QList<FourPoints> &spotlights)
{
    QPainterPath targetPath;
    for (const FourPoints &spotlight : spotlights) {
        const QPainterPath path = spotlightPathFromPoints(spotlight);
        if (!path.isEmpty()) {
            targetPath = targetPath.united(path);
        }
    }

    if (targetPath.isEmpty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath outsidePath;
    outsidePath.addRect(rect());
    painter.fillPath(outsidePath.subtracted(targetPath), QColor(0, 0, 0, 115));

    for (const FourPoints &spotlight : spotlights) {
        paintSpotlightFrame(painter, spotlight);
    }

    painter.restore();
}

void ShapesWidget::paintSpotlightFrame(QPainter &painter, FourPoints rectFPoints)
{
    const QPainterPath targetPath = spotlightPathFromPoints(rectFPoints);
    if (targetPath.isEmpty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(QPen(QColor(255, 255, 255, 210), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(targetPath);
    painter.setPen(QPen(QColor(255, 196, 0, 220), 1));
    painter.drawPath(targetPath);
    painter.restore();
}

void ShapesWidget::addStepNumber(QPointF pos)
{
    Toolshape shape;
    shape.type = QStringLiteral("step-number");
    shape.index = ++m_shapesIndex;
    shape.fontSize = nextStepNumber();
    shape.stepNumberStyle = static_cast<int>(currentStepNumberStyle());
    shape.colorIndex = ConfigSettings::instance()->getValue("step-number", "color_index").toInt();
    shape.stepNumberTextColorIndex = ConfigSettings::instance()->getValue("step-number", "text_color_index").toInt();
    shape.points.append(pos);

    const QRectF shapeRect = stepNumberRect(pos, currentStepNumberStyle());
    shape.mainPoints[0] = shapeRect.topLeft();
    shape.mainPoints[1] = shapeRect.bottomLeft();
    shape.mainPoints[2] = shapeRect.topRight();
    shape.mainPoints[3] = shapeRect.bottomRight();

    m_shapes.append(shape);
    m_selectedShape = shape;
    m_hoveredShape = shape;
    m_selectedIndex = shape.index;
    m_selectedOrder = m_shapes.length() - 1;
    m_hoveredIndex = shape.index;
    m_isSelected = true;
    emit setShapesUndo(true);
}

QPointF ShapesWidget::stepNumberAnchor(const Toolshape &shape) const
{
    if (!shape.points.isEmpty()) {
        return shape.points.first();
    }

    const QRectF shapeRect = QRectF(shape.mainPoints[0], shape.mainPoints[3]).normalized();
    const StepNumberStyle style = stepNumberStyleFromValue(shape.stepNumberStyle);
    if (style == StepNumberStyle::Pin || style == StepNumberStyle::Bubble) {
        return QPointF(shapeRect.center().x(), shapeRect.bottom());
    }
    if (style == StepNumberStyle::DotLabel) {
        return QPointF(shapeRect.left() + 6, shapeRect.center().y());
    }
    return shapeRect.topLeft();
}

QRectF ShapesWidget::stepNumberRect(QPointF anchor, StepNumberStyle style) const
{
    if (style == StepNumberStyle::Pin) {
        return QRectF(anchor.x() - 15, anchor.y() - 48, 30, 48);
    }
    if (style == StepNumberStyle::DotLabel) {
        return QRectF(anchor.x() - 5, anchor.y() - 15, 50, 30);
    }
    if (style == StepNumberStyle::Bubble) {
        return QRectF(anchor.x() - 24, anchor.y() - 36, 48, 36);
    }
    return QRectF(anchor, QSizeF(28, 28));
}

ShapesWidget::StepNumberStyle ShapesWidget::currentStepNumberStyle() const
{
    return stepNumberStyleFromValue(ConfigSettings::instance()->getValue("step-number", "style").toInt());
}

ShapesWidget::StepNumberStyle ShapesWidget::stepNumberStyleFromValue(int value) const
{
    switch (value) {
    case static_cast<int>(StepNumberStyle::OutlineCircle):
        return StepNumberStyle::OutlineCircle;
    case static_cast<int>(StepNumberStyle::Pin):
        return StepNumberStyle::Pin;
    case static_cast<int>(StepNumberStyle::DotLabel):
        return StepNumberStyle::DotLabel;
    case static_cast<int>(StepNumberStyle::Bubble):
        return StepNumberStyle::Bubble;
    case static_cast<int>(StepNumberStyle::SolidCircle):
    default:
        return StepNumberStyle::SolidCircle;
    }
}

int ShapesWidget::nextStepNumber() const
{
    int maxStep = 0;
    for (const Toolshape &shape : m_shapes) {
        if (shape.type == "step-number") {
            maxStep = qMax(maxStep, shape.fontSize);
        }
    }
    return maxStep + 1;
}

QColor ShapesWidget::sampleColorAt(QPointF pos) const
{
    const QPixmap pixmap = TempFile::instance()->getFullscreenPixmap();
    if (pixmap.isNull()) {
        return QColor();
    }

    const QImage image = pixmap.toImage();
    const qreal ratio = pixmap.devicePixelRatioF() > 0 ? pixmap.devicePixelRatioF() : devicePixelRatioF();
    QPoint imagePoint(qRound((m_globalRect.x() + pos.x()) * ratio),
                      qRound((m_globalRect.y() + pos.y()) * ratio));
    if (!image.rect().contains(imagePoint)) {
        imagePoint = QPoint(qRound(pos.x() * ratio), qRound(pos.y() * ratio));
    }
    if (!image.rect().contains(imagePoint)) {
        return QColor();
    }

    return image.pixelColor(imagePoint);
}

QString ShapesWidget::colorPickerText(const QColor &color) const
{
    if (!color.isValid()) {
        return QString();
    }

    return QStringLiteral("#%1%2%3")
            .arg(color.red(), 2, 16, QLatin1Char('0'))
            .arg(color.green(), 2, 16, QLatin1Char('0'))
            .arg(color.blue(), 2, 16, QLatin1Char('0'))
            .toUpper();
}

bool ShapesWidget::handleColorPickerPress(QPointF pos)
{
    if (m_currentType != "color-picker") {
        return false;
    }

    m_colorPickerPos = boundedMeasureLinePoint(pos);
    m_colorPickerColor = sampleColorAt(m_colorPickerPos);
    m_colorPickerHasColor = m_colorPickerColor.isValid();

    const QString text = colorPickerText(m_colorPickerColor);
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
    update();
    return true;
}

bool ShapesWidget::handleColorPickerMove(QPointF pos)
{
    if (m_currentType != "color-picker") {
        return false;
    }

    m_colorPickerPos = boundedMeasureLinePoint(pos);
    m_colorPickerColor = sampleColorAt(m_colorPickerPos);
    m_colorPickerHasColor = m_colorPickerColor.isValid();
    updateCursorShape();
    update();
    return true;
}

void ShapesWidget::paintColorPickerOverlay(QPainter &painter)
{
    if (m_currentType != "color-picker" || !m_colorPickerHasColor) {
        return;
    }

    const QString text = colorPickerText(m_colorPickerColor);
    if (text.isEmpty()) {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QFont labelFont = painter.font();
    labelFont.setPixelSize(11);
    painter.setFont(labelFont);
    const QFontMetrics fm(labelFont);
    const QRect textRect = fm.boundingRect(text);

    QRectF panel(m_colorPickerPos + QPointF(14, 14),
                 QSizeF(textRect.width() + 42, qMax(26, textRect.height() + 12)));
    const QRectF bounds = rect().adjusted(6, 6, -6, -6);
    if (panel.right() > bounds.right()) {
        panel.moveRight(m_colorPickerPos.x() - 14);
    }
    if (panel.bottom() > bounds.bottom()) {
        panel.moveBottom(m_colorPickerPos.y() - 14);
    }
    if (panel.left() < bounds.left()) {
        panel.moveLeft(bounds.left());
    }
    if (panel.top() < bounds.top()) {
        panel.moveTop(bounds.top());
    }

    painter.setPen(QPen(QColor(0, 0, 0, 50), 1));
    painter.setBrush(QColor(36, 42, 52, 220));
    painter.drawRoundedRect(panel, 5, 5);

    const QRectF swatch(panel.left() + 6, panel.top() + 6, 16, panel.height() - 12);
    painter.setPen(QPen(QColor(255, 255, 255, 220), 1));
    painter.setBrush(m_colorPickerColor);
    painter.drawRoundedRect(swatch, 3, 3);

    painter.setPen(Qt::white);
    painter.drawText(QRectF(panel.left() + 28, panel.top(), panel.width() - 34, panel.height()),
                     Qt::AlignVCenter | Qt::AlignLeft, text);

    painter.setPen(QPen(QColor(255, 255, 255, 190), 1));
    painter.drawLine(QPointF(m_colorPickerPos.x() - 8, m_colorPickerPos.y()),
                     QPointF(m_colorPickerPos.x() + 8, m_colorPickerPos.y()));
    painter.drawLine(QPointF(m_colorPickerPos.x(), m_colorPickerPos.y() - 8),
                     QPointF(m_colorPickerPos.x(), m_colorPickerPos.y() + 8));
    painter.restore();
}

void ShapesWidget::initRulerIfNeeded()
{
    if (m_rulerVisible) {
        return;
    }

    m_rulerVisible = true;
    m_rulerPressed = false;
    m_rulerAction = RulerAction::None;
    m_rulerMarkAVisible = false;
    m_rulerMarkBVisible = false;
    m_rulerActiveMark = RulerAction::None;
    m_rulerAngle = 0;
    m_rulerLength = boundedRulerLength(qMax(width() * 0.78, RULER_MIN_LENGTH));
    m_rulerThickness = 48;
    m_rulerCenter = rect().center();
    m_rulerMarkA = 0;
    m_rulerMarkB = 0;
}

QPointF ShapesWidget::rulerAxisUnit() const
{
    const qreal radian = qDegreesToRadians(m_rulerAngle);
    return QPointF(std::cos(radian), std::sin(radian));
}

QPointF ShapesWidget::rulerNormalUnit() const
{
    const QPointF axis = rulerAxisUnit();
    return QPointF(-axis.y(), axis.x());
}

QPointF ShapesWidget::rulerPointAt(qreal t) const
{
    const qreal clamped = qBound<qreal>(-0.5, t, 0.5);
    return m_rulerCenter + rulerAxisUnit() * (clamped * m_rulerLength);
}

qreal ShapesWidget::projectPointToRuler(QPointF pos) const
{
    const QPointF delta = pos - m_rulerCenter;
    const QPointF axis = rulerAxisUnit();
    const qreal projected = QPointF::dotProduct(delta, axis) / m_rulerLength;
    return qBound<qreal>(-0.5, projected, 0.5);
}

qreal ShapesWidget::rulerMaximumLength() const
{
    const qreal diagonal = std::hypot(static_cast<qreal>(width()), static_cast<qreal>(height()));
    return qMax(RULER_MIN_LENGTH, diagonal - RULER_EDGE_MARGIN * 2);
}

qreal ShapesWidget::boundedRulerLength(qreal length) const
{
    return qBound(RULER_MIN_LENGTH, length, rulerMaximumLength());
}

qreal ShapesWidget::snappedRulerAngle(qreal angle) const
{
    return qRound(angle / RULER_ROTATE_SNAP_DEGREES) * RULER_ROTATE_SNAP_DEGREES;
}

QPolygonF ShapesWidget::rulerBodyPolygon() const
{
    const QPointF axis = rulerAxisUnit();
    const QPointF normal = rulerNormalUnit();
    const QPointF halfAxis = axis * (m_rulerLength / 2);
    const QPointF halfNormal = normal * (m_rulerThickness / 2);

    QPolygonF polygon;
    polygon << m_rulerCenter - halfAxis - halfNormal
            << m_rulerCenter + halfAxis - halfNormal
            << m_rulerCenter + halfAxis + halfNormal
            << m_rulerCenter - halfAxis + halfNormal;
    return polygon;
}

QPointF ShapesWidget::rulerRotateHandlePoint() const
{
    return m_rulerCenter + rulerNormalUnit() * (m_rulerThickness / 2 + RULER_ROTATE_HANDLE_DISTANCE);
}

QPointF ShapesWidget::rulerStartHandlePoint() const
{
    return rulerPointAt(-0.5) + rulerNormalUnit() * (m_rulerThickness / 2 + RULER_RESIZE_HANDLE_RADIUS);
}

QPointF ShapesWidget::rulerEndHandlePoint() const
{
    return rulerPointAt(0.5) + rulerNormalUnit() * (m_rulerThickness / 2 + RULER_RESIZE_HANDLE_RADIUS);
}

ShapesWidget::RulerAction ShapesWidget::hitTestRuler(QPointF pos) const
{
    if (!m_rulerVisible) {
        return RulerAction::None;
    }

    if (QLineF(pos, rulerStartHandlePoint()).length() <= RULER_RESIZE_HANDLE_RADIUS + RULER_HIT_PADDING) {
        return RulerAction::ResizeStart;
    }

    if (QLineF(pos, rulerEndHandlePoint()).length() <= RULER_RESIZE_HANDLE_RADIUS + RULER_HIT_PADDING) {
        return RulerAction::ResizeEnd;
    }

    auto hitTestMark = [&](qreal mark) {
        const QPointF delta = pos - rulerPointAt(mark);
        const qreal along = qAbs(QPointF::dotProduct(delta, rulerAxisUnit()));
        const qreal across = qAbs(QPointF::dotProduct(delta, rulerNormalUnit()));
        return along <= RULER_HIT_PADDING
                && across <= m_rulerThickness / 2 + RULER_HIT_PADDING;
    };

    if (m_rulerMarkAVisible && hitTestMark(m_rulerMarkA)) {
        return RulerAction::DragMarkA;
    }

    if (m_rulerMarkBVisible && hitTestMark(m_rulerMarkB)) {
        return RulerAction::DragMarkB;
    }

    if (QLineF(pos, rulerRotateHandlePoint()).length() <= RULER_MARK_RADIUS + RULER_HIT_PADDING) {
        return RulerAction::Rotate;
    }

    const QPointF delta = pos - m_rulerCenter;
    const qreal along = QPointF::dotProduct(delta, rulerAxisUnit());
    const qreal across = QPointF::dotProduct(delta, rulerNormalUnit());
    if (qAbs(along) <= m_rulerLength / 2 + RULER_HIT_PADDING
            && qAbs(across) <= m_rulerThickness / 2 + RULER_HIT_PADDING) {
        if (qAbs(across) <= RULER_HIT_PADDING) {
            return RulerAction::PlaceMark;
        }
        return RulerAction::Move;
    }

    return RulerAction::None;
}

void ShapesWidget::paintRulerOverlay(QPainter &painter, bool includeControls)
{
    if (!m_rulerVisible || m_currentType != "ruler") {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPointF axis = rulerAxisUnit();
    const QPointF normal = rulerNormalUnit();
    const QPolygonF body = rulerBodyPolygon();

    painter.setPen(QPen(QColor(83, 96, 118, 80), 1));
    painter.setBrush(QColor(255, 255, 255, 58));
    painter.drawPolygon(body);

    const QPointF start = rulerPointAt(-0.5);
    const QPointF end = rulerPointAt(0.5);
    const RulerAction hoverAction = includeControls
            ? (m_rulerPressed ? m_rulerAction : hitTestRuler(mapFromGlobal(QCursor::pos())))
            : RulerAction::None;
    painter.setPen(QPen(QColor(32, 42, 56, 190), 1));
    painter.drawLine(start, end);

    QFont font = painter.font();
    font.setPixelSize(10);
    painter.setFont(font);

    const int length = qRound(m_rulerLength);
    for (int px = 0; px <= length; px += 10) {
        const qreal t = -0.5 + px / m_rulerLength;
        const QPointF base = rulerPointAt(t);
        const bool major = px % 50 == 0;
        const qreal tickLength = major ? 16 : 9;
        painter.setPen(QPen(QColor(32, 42, 56, 235), major ? 1.5 : 1));
        painter.drawLine(base, base - normal * tickLength);
        if (major) {
            const QPointF labelPos = base - normal * (tickLength + 4) - axis * 8;
            painter.drawText(QRectF(labelPos.x(), labelPos.y() - 10, 34, 12), Qt::AlignCenter, QString::number(px));
        }
    }

    if (includeControls) {
        auto paintResizeHandle = [&](const QPointF &anchor, const QPointF &pos, bool active) {
            painter.setPen(QPen(QColor(1, 189, 255, active ? 220 : 100), 1));
            painter.setBrush(QColor(255, 255, 255, active ? 130 : 55));
            painter.drawLine(anchor, pos);
            painter.drawEllipse(pos, RULER_RESIZE_HANDLE_RADIUS, RULER_RESIZE_HANDLE_RADIUS);
            painter.drawLine(pos - normal * 8, pos + normal * 8);
        };
        paintResizeHandle(start, rulerStartHandlePoint(), hoverAction == RulerAction::ResizeStart);
        paintResizeHandle(end, rulerEndHandlePoint(), hoverAction == RulerAction::ResizeEnd);

        const QPointF rotateHandle = rulerRotateHandlePoint();
        const bool rotateActive = hoverAction == RulerAction::Rotate;
        painter.setPen(QPen(QColor(1, 189, 255, rotateActive ? 220 : 100), 1));
        painter.setBrush(QColor(255, 255, 255, rotateActive ? 130 : 55));
        painter.drawLine(m_rulerCenter + normal * (m_rulerThickness / 2), rotateHandle);
        painter.drawEllipse(rotateHandle, RULER_MARK_RADIUS, RULER_MARK_RADIUS);
    }

    auto drawOutlinedText = [&](const QRectF &textRect, int flags, const QString &text, const QColor &color) {
        painter.setPen(QColor(255, 255, 255, 190));
        painter.drawText(textRect.translated(-1, 0), flags, text);
        painter.drawText(textRect.translated(1, 0), flags, text);
        painter.drawText(textRect.translated(0, -1), flags, text);
        painter.drawText(textRect.translated(0, 1), flags, text);
        painter.setPen(color);
        painter.drawText(textRect, flags, text);
    };

    auto paintMark = [&](qreal mark, const QString &name, qreal direction, bool active) {
        const QPointF pos = rulerPointAt(mark);
        const QColor markColor(255, 0, 0, active ? 255 : 215);
        const qreal jawWidth = RULER_JAW_WIDTH;
        const QPointF jawTop = pos - normal * (m_rulerThickness / 2 + 4);
        const QPointF jawBottom = pos + normal * (m_rulerThickness / 2 + 4);
        const QPointF jawOuterOffset = axis * (direction * jawWidth);
        QPolygonF jaw;
        jaw << jawTop
            << jawBottom
            << jawBottom + jawOuterOffset
            << jawTop + jawOuterOffset;
        painter.setPen(Qt::NoPen);
        painter.setBrush(markColor);
        painter.drawPolygon(jaw);

        const QPointF leaderStart = pos - normal * (m_rulerThickness / 2 + 6);
        const QPointF leaderEnd = leaderStart + axis * (direction * 28);
        const QPointF arrowBack = leaderEnd - axis * (direction * 5);
        QPolygonF arrow;
        arrow << leaderEnd
              << arrowBack + normal * 3
              << arrowBack - normal * 3;

        painter.setPen(QPen(markColor, 1));
        painter.drawLine(leaderStart, leaderEnd);
        painter.setBrush(markColor);
        painter.drawPolygon(arrow);

        const QPointF textCenter = leaderEnd + axis * (direction * 12);
        painter.setPen(markColor);
        QFont markFont = painter.font();
        markFont.setBold(active);
        painter.setFont(markFont);
        drawOutlinedText(QRectF(textCenter.x() - 8, textCenter.y() - 8, 16, 16),
                         Qt::AlignCenter, name, markColor);

        if (includeControls) {
            painter.setPen(QPen(QColor(255, 0, 0, active ? 70 : 45), 1));
            painter.drawLine(pos - normal * height(), pos + normal * height());
        }
    };

    if (m_rulerMarkAVisible) {
        const qreal direction = m_rulerMarkBVisible && m_rulerMarkA > m_rulerMarkB ? 1 : -1;
        paintMark(m_rulerMarkA, QStringLiteral("A"), direction,
                  hoverAction == RulerAction::DragMarkA || m_rulerActiveMark == RulerAction::DragMarkA);
    }
    if (m_rulerMarkBVisible) {
        const qreal direction = m_rulerMarkAVisible && m_rulerMarkB < m_rulerMarkA ? -1 : 1;
        paintMark(m_rulerMarkB, QStringLiteral("B"), direction,
                  hoverAction == RulerAction::DragMarkB || m_rulerActiveMark == RulerAction::DragMarkB);
    }

    if (m_rulerMarkAVisible && m_rulerMarkBVisible) {
        const QPointF mid = rulerPointAt((m_rulerMarkA + m_rulerMarkB) / 2);
        const QString text = currentMeasurementText();
        const QColor measureColor(255, 0, 0);
        const qreal bracketOffset = m_rulerThickness / 2 + 26;
        const QRectF bounds = rect().adjusted(RULER_LABEL_MARGIN, RULER_LABEL_MARGIN,
                                              -RULER_LABEL_MARGIN, -RULER_LABEL_MARGIN);

        const qreal startMark = qMin(m_rulerMarkA, m_rulerMarkB);
        const qreal endMark = qMax(m_rulerMarkA, m_rulerMarkB);
        const QPointF markStart = rulerPointAt(startMark);
        const QPointF markEnd = rulerPointAt(endMark);

        auto bracketFits = [&](qreal side) {
            return bounds.contains(markStart + normal * (side * bracketOffset))
                    && bounds.contains(markEnd + normal * (side * bracketOffset))
                    && bounds.contains(mid + normal * (side * bracketOffset));
        };
        const qreal side = bracketFits(-1) || !bracketFits(1) ? -1 : 1;

        const QPointF markEdgeStart = markStart + normal * (side * (m_rulerThickness / 2 + 4));
        const QPointF markEdgeEnd = markEnd + normal * (side * (m_rulerThickness / 2 + 4));
        const QPointF bracketStart = markStart + normal * (side * bracketOffset);
        const QPointF bracketEnd = markEnd + normal * (side * bracketOffset);
        const QPointF bracketMid = mid + normal * (side * bracketOffset);

        QFont measureFont = painter.font();
        measureFont.setPixelSize(11);
        measureFont.setBold(false);
        painter.setFont(measureFont);
        const QFontMetrics fm(measureFont);
        const qreal textHalfWidth = fm.horizontalAdvance(text) / 2.0 + 7;
        const qreal textHalfHeight = fm.height() / 2.0;
        const qreal bracketLength = QLineF(bracketStart, bracketEnd).length();

        painter.setPen(QPen(measureColor, 1));
        painter.setBrush(measureColor);
        painter.drawLine(markEdgeStart, bracketStart);
        painter.drawLine(markEdgeEnd, bracketEnd);

        QPointF textCenter = bracketMid;
        if (bracketLength > textHalfWidth * 2 + 8) {
            painter.drawLine(bracketStart, bracketMid - axis * textHalfWidth);
            painter.drawLine(bracketMid + axis * textHalfWidth, bracketEnd);
        } else {
            painter.drawLine(bracketStart, bracketEnd);

            auto textRect = [&](const QPointF &center) {
                return QRectF(center.x() - textHalfWidth,
                              center.y() - textHalfHeight,
                              textHalfWidth * 2,
                              textHalfHeight * 2);
            };

            qreal textDirection = 1;
            const QPointF rightTextCenter = bracketEnd + axis * (textHalfWidth + 14);
            const QPointF leftTextCenter = bracketStart - axis * (textHalfWidth + 14);
            if (!bounds.contains(textRect(rightTextCenter)) && bounds.contains(textRect(leftTextCenter))) {
                textDirection = -1;
                textCenter = leftTextCenter;
            } else {
                textCenter = rightTextCenter;
            }

            const QPointF leaderStart = textDirection > 0 ? bracketEnd : bracketStart;
            const QPointF leaderEnd = textCenter - axis * (textDirection * textHalfWidth);
            painter.drawLine(leaderStart, leaderEnd);
        }

        drawOutlinedText(QRectF(textCenter.x() - textHalfWidth,
                                textCenter.y() - textHalfHeight,
                                textHalfWidth * 2,
                                textHalfHeight * 2),
                         Qt::AlignCenter, text, measureColor);

    }

    painter.restore();
}

bool ShapesWidget::handleRulerPress(QPointF pos)
{
    if (m_currentType != "ruler") {
        return false;
    }

    initRulerIfNeeded();

    m_rulerAction = hitTestRuler(pos);
    if (m_rulerAction == RulerAction::None) {
        m_rulerPressed = false;
        updateCursorShape();
        update();
        return true;
    }

    m_rulerPressed = true;
    m_rulerPressedPoint = pos;

    if (m_rulerAction == RulerAction::PlaceMark) {
        const qreal mark = snappedRulerMark(pos);
        if (!m_rulerMarkAVisible) {
            m_rulerMarkA = mark;
            m_rulerMarkAVisible = true;
            m_rulerAction = RulerAction::DragMarkA;
            m_rulerActiveMark = RulerAction::DragMarkA;
        } else {
            m_rulerMarkB = mark;
            m_rulerMarkBVisible = true;
            m_rulerAction = RulerAction::DragMarkB;
            m_rulerActiveMark = RulerAction::DragMarkB;
        }
    } else if (m_rulerAction == RulerAction::DragMarkA || m_rulerAction == RulerAction::DragMarkB) {
        m_rulerActiveMark = m_rulerAction;
    } else if (m_rulerAction == RulerAction::Move
               || m_rulerAction == RulerAction::ResizeStart
               || m_rulerAction == RulerAction::ResizeEnd) {
        m_rulerActiveMark = RulerAction::None;
    } else if (m_rulerAction == RulerAction::Rotate) {
        const QPointF delta = pos - m_rulerCenter;
        const qreal angle = qRadiansToDegrees(std::atan2(delta.y(), delta.x()));
        m_rulerPressedAngle = m_rulerAngle - angle;
        m_rulerActiveMark = RulerAction::None;
    }

    clearSelected();
    updateCursorShape();
    update();
    return true;
}

bool ShapesWidget::handleRulerMove(QPointF pos)
{
    if (!m_rulerVisible) {
        if (m_currentType == "ruler") {
            updateCursorShape();
            return true;
        }
        return false;
    }

    if (m_currentType != "ruler" && !m_rulerPressed) {
        return false;
    }

    if (m_rulerPressed) {
        if (m_rulerAction == RulerAction::Move) {
            m_rulerCenter += pos - m_rulerPressedPoint;
            m_rulerPressedPoint = pos;
        } else if (m_rulerAction == RulerAction::Rotate) {
            const QPointF delta = pos - m_rulerCenter;
            if (!qFuzzyIsNull(delta.x()) || !qFuzzyIsNull(delta.y())) {
                const qreal angle = qRadiansToDegrees(std::atan2(delta.y(), delta.x()));
                m_rulerAngle = angle + m_rulerPressedAngle;
                if (m_isShiftPressed) {
                    m_rulerAngle = snappedRulerAngle(m_rulerAngle);
                }
            }
        } else if (m_rulerAction == RulerAction::ResizeStart) {
            resizeRulerFromHandle(true, pos);
        } else if (m_rulerAction == RulerAction::ResizeEnd) {
            resizeRulerFromHandle(false, pos);
        } else if (m_rulerAction == RulerAction::DragMarkA) {
            m_rulerMarkA = snappedRulerMark(pos);
            m_rulerMarkAVisible = true;
            m_rulerActiveMark = RulerAction::DragMarkA;
        } else if (m_rulerAction == RulerAction::DragMarkB) {
            m_rulerMarkB = snappedRulerMark(pos);
            m_rulerMarkBVisible = true;
            m_rulerActiveMark = RulerAction::DragMarkB;
        }
    }

    updateCursorShape();
    update();
    return true;
}

void ShapesWidget::handleRulerRelease()
{
    if (!m_rulerPressed) {
        return;
    }

    m_rulerPressed = false;
    if (m_rulerMarkAVisible && m_rulerMarkBVisible) {
        addMeasurementHistory();
    }
    m_rulerAction = RulerAction::None;
    updateCursorShape();
    update();
}

void ShapesWidget::resizeRulerFromHandle(bool resizeStart, QPointF pos)
{
    const QPointF axis = rulerAxisUnit();
    const QPointF oldMarkAPos = rulerPointAt(m_rulerMarkA);
    const QPointF oldMarkBPos = rulerPointAt(m_rulerMarkB);

    if (resizeStart) {
        const QPointF fixedEnd = rulerPointAt(0.5);
        const qreal proposedLength = QPointF::dotProduct(fixedEnd - pos, axis);
        const qreal newLength = boundedRulerLength(proposedLength);
        m_rulerLength = newLength;
        m_rulerCenter = fixedEnd - axis * (newLength / 2);
    } else {
        const QPointF fixedStart = rulerPointAt(-0.5);
        const qreal proposedLength = QPointF::dotProduct(pos - fixedStart, axis);
        const qreal newLength = boundedRulerLength(proposedLength);
        m_rulerLength = newLength;
        m_rulerCenter = fixedStart + axis * (newLength / 2);
    }

    if (m_rulerMarkAVisible) {
        m_rulerMarkA = projectPointToRuler(oldMarkAPos);
    }
    if (m_rulerMarkBVisible) {
        m_rulerMarkB = projectPointToRuler(oldMarkBPos);
    }
}

void ShapesWidget::resizeRulerCentered(qreal deltaLength)
{
    const QPointF oldMarkAPos = rulerPointAt(m_rulerMarkA);
    const QPointF oldMarkBPos = rulerPointAt(m_rulerMarkB);

    m_rulerLength = boundedRulerLength(m_rulerLength + deltaLength);

    if (m_rulerMarkAVisible) {
        m_rulerMarkA = projectPointToRuler(oldMarkAPos);
    }
    if (m_rulerMarkBVisible) {
        m_rulerMarkB = projectPointToRuler(oldMarkBPos);
    }
}

qreal ShapesWidget::snappedRulerMark(QPointF pos) const
{
    qreal mark = projectPointToRuler(pos);

    const qreal pixel = (mark + 0.5) * m_rulerLength;
    mark = qBound<qreal>(-0.5, qRound(pixel) / m_rulerLength - 0.5, 0.5);

    const qreal edgeSnap = MEASUREMENT_SNAP_DISTANCE / m_rulerLength;
    if (qAbs(mark + 0.5) <= edgeSnap) {
        return -0.5;
    }
    if (qAbs(mark - 0.5) <= edgeSnap) {
        return 0.5;
    }

    return mark;
}

bool ShapesWidget::moveActiveRulerMark(QPointF delta)
{
    if (!m_rulerVisible || m_rulerActiveMark == RulerAction::None) {
        return false;
    }

    const qreal projectedDelta = QPointF::dotProduct(delta, rulerAxisUnit()) / m_rulerLength;
    if (qFuzzyIsNull(projectedDelta)) {
        return false;
    }

    if (m_rulerActiveMark == RulerAction::DragMarkA && m_rulerMarkAVisible) {
        m_rulerMarkA = qBound<qreal>(-0.5, m_rulerMarkA + projectedDelta, 0.5);
        return true;
    }
    if (m_rulerActiveMark == RulerAction::DragMarkB && m_rulerMarkBVisible) {
        m_rulerMarkB = qBound<qreal>(-0.5, m_rulerMarkB + projectedDelta, 0.5);
        return true;
    }

    return false;
}

qreal ShapesWidget::rulerMeasurementDistance() const
{
    if (!m_rulerMarkAVisible || !m_rulerMarkBVisible) {
        return 0;
    }

    qreal distance = qAbs(m_rulerMarkB - m_rulerMarkA) * m_rulerLength;
    if (m_rulerMeasureMode == RulerMeasureMode::Center) {
        distance += RULER_JAW_WIDTH;
    } else if (m_rulerMeasureMode == RulerMeasureMode::Outer) {
        distance += RULER_JAW_WIDTH * 2;
    }

    return qMax<qreal>(0, distance);
}

QString ShapesWidget::rulerMeasureModeText() const
{
    switch (m_rulerMeasureMode) {
    case RulerMeasureMode::Inner:
        return tr("Inner");
    case RulerMeasureMode::Center:
        return tr("Center");
    case RulerMeasureMode::Outer:
        return tr("Outer");
    }
    return QString();
}

void ShapesWidget::cycleRulerMeasureMode()
{
    if (m_rulerMeasureMode == RulerMeasureMode::Inner) {
        m_rulerMeasureMode = RulerMeasureMode::Center;
    } else if (m_rulerMeasureMode == RulerMeasureMode::Center) {
        m_rulerMeasureMode = RulerMeasureMode::Outer;
    } else {
        m_rulerMeasureMode = RulerMeasureMode::Inner;
    }
}

QString ShapesWidget::currentMeasurementText() const
{
    if (m_currentType == "ruler" && m_rulerMarkAVisible && m_rulerMarkBVisible) {
        return QStringLiteral("%1px").arg(qRound(rulerMeasurementDistance()));
    }

    if (m_currentType == "measure-line" && m_measureLinePointAVisible && m_measureLinePointBVisible) {
        const qreal distance = QLineF(m_measureLinePointA, m_measureLinePointB).length();
        return QStringLiteral("%1px").arg(qRound(distance));
    }

    return QString();
}

bool ShapesWidget::copyCurrentMeasurementText()
{
    const QString text = currentMeasurementText();
    if (text.isEmpty()) {
        return false;
    }

    QApplication::clipboard()->setText(text);
    return true;
}

QString ShapesWidget::currentMeasurementDetailText() const
{
    const QString value = currentMeasurementText();
    if (value.isEmpty()) {
        return QString();
    }

    if (m_currentType == "ruler" && m_rulerMarkAVisible && m_rulerMarkBVisible) {
        const QPointF pointA = rulerPointAt(m_rulerMarkA);
        const QPointF pointB = rulerPointAt(m_rulerMarkB);
        return QStringLiteral("%1 %2 A(%3,%4) B(%5,%6)")
                .arg(value, rulerMeasureModeText())
                .arg(qRound(pointA.x()))
                .arg(qRound(pointA.y()))
                .arg(qRound(pointB.x()))
                .arg(qRound(pointB.y()));
    }

    if (m_currentType == "measure-line" && m_measureLinePointAVisible && m_measureLinePointBVisible) {
        const QPointF delta = m_measureLinePointB - m_measureLinePointA;
        return QStringLiteral("%1 dx:%2px dy:%3px A(%4,%5) B(%6,%7)")
                .arg(value)
                .arg(qRound(qAbs(delta.x())))
                .arg(qRound(qAbs(delta.y())))
                .arg(qRound(m_measureLinePointA.x()))
                .arg(qRound(m_measureLinePointA.y()))
                .arg(qRound(m_measureLinePointB.x()))
                .arg(qRound(m_measureLinePointB.y()));
    }

    return value;
}

bool ShapesWidget::copyCurrentMeasurementDetailText()
{
    const QString text = currentMeasurementDetailText();
    if (text.isEmpty()) {
        return false;
    }

    QApplication::clipboard()->setText(text);
    return true;
}

void ShapesWidget::addMeasurementHistory()
{
    const QString text = currentMeasurementDetailText();
    if (text.isEmpty()) {
        return;
    }

    if (!m_measurementHistory.isEmpty() && m_measurementHistory.first() == text) {
        return;
    }

    m_measurementHistory.removeAll(text);
    m_measurementHistory.prepend(text);
    while (m_measurementHistory.size() > MEASUREMENT_HISTORY_LIMIT) {
        m_measurementHistory.removeLast();
    }
}

void ShapesWidget::clearMeasurementHistory()
{
    m_measurementHistory.clear();
    update();
}

void ShapesWidget::showMeasurementMenu(QPoint globalPos)
{
    QMenu menu(this);
    QAction *copyValueAction = menu.addAction(tr("Copy measurement"));
    copyValueAction->setEnabled(!currentMeasurementText().isEmpty());
    QAction *copyDetailAction = menu.addAction(tr("Copy measurement details"));
    copyDetailAction->setEnabled(!currentMeasurementDetailText().isEmpty());

    QAction *innerModeAction = nullptr;
    QAction *centerModeAction = nullptr;
    QAction *outerModeAction = nullptr;
    if (m_currentType == "ruler") {
        menu.addSeparator();
        innerModeAction = menu.addAction(tr("Inner distance"));
        centerModeAction = menu.addAction(tr("Center distance"));
        outerModeAction = menu.addAction(tr("Outer distance"));
        innerModeAction->setCheckable(true);
        centerModeAction->setCheckable(true);
        outerModeAction->setCheckable(true);
        innerModeAction->setChecked(m_rulerMeasureMode == RulerMeasureMode::Inner);
        centerModeAction->setChecked(m_rulerMeasureMode == RulerMeasureMode::Center);
        outerModeAction->setChecked(m_rulerMeasureMode == RulerMeasureMode::Outer);
    }

    menu.addSeparator();
    QAction *clearCurrentAction = menu.addAction(tr("Clear current measurement"));
    QAction *clearHistoryAction = menu.addAction(tr("Clear measurement history"));
    clearHistoryAction->setEnabled(!m_measurementHistory.isEmpty());

    QAction *selectedAction = menu.exec(globalPos);
    if (!selectedAction) {
        return;
    }

    if (selectedAction == copyValueAction) {
        copyCurrentMeasurementText();
    } else if (selectedAction == copyDetailAction) {
        copyCurrentMeasurementDetailText();
    } else if (selectedAction == innerModeAction) {
        m_rulerMeasureMode = RulerMeasureMode::Inner;
    } else if (selectedAction == centerModeAction) {
        m_rulerMeasureMode = RulerMeasureMode::Center;
    } else if (selectedAction == outerModeAction) {
        m_rulerMeasureMode = RulerMeasureMode::Outer;
    } else if (selectedAction == clearCurrentAction) {
        if (m_currentType == "ruler") {
            m_rulerMarkAVisible = false;
            m_rulerMarkBVisible = false;
            m_rulerActiveMark = RulerAction::None;
        } else if (m_currentType == "measure-line") {
            resetMeasureLine();
        }
    } else if (selectedAction == clearHistoryAction) {
        clearMeasurementHistory();
    }

    updateCursorShape();
    update();
}

QPointF ShapesWidget::snapMeasurementPoint(QPointF pos) const
{
    pos = boundedMeasureLinePoint(pos);

    auto snapValue = [](qreal value, qreal target) {
        return qAbs(value - target) <= MEASUREMENT_SNAP_DISTANCE ? target : value;
    };
    auto snapToPoint = [&](const QPointF &target) {
        if (QLineF(pos, target).length() <= MEASUREMENT_SNAP_DISTANCE) {
            pos = target;
            return;
        }
        pos.setX(snapValue(pos.x(), target.x()));
        pos.setY(snapValue(pos.y(), target.y()));
    };

    pos.setX(snapValue(pos.x(), 0));
    pos.setY(snapValue(pos.y(), 0));
    pos.setX(snapValue(pos.x(), width() - 1));
    pos.setY(snapValue(pos.y(), height() - 1));

    if (m_measureLinePointAVisible && m_measureLineAction != MeasureLineAction::DragPointA) {
        snapToPoint(m_measureLinePointA);
    }
    if (m_measureLinePointBVisible && m_measureLineAction != MeasureLineAction::DragPointB) {
        snapToPoint(m_measureLinePointB);
    }
    if (m_rulerVisible) {
        snapToPoint(rulerPointAt(-0.5));
        snapToPoint(rulerPointAt(0.5));
        if (m_rulerMarkAVisible && m_rulerAction != RulerAction::DragMarkA) {
            snapToPoint(rulerPointAt(m_rulerMarkA));
        }
        if (m_rulerMarkBVisible && m_rulerAction != RulerAction::DragMarkB) {
            snapToPoint(rulerPointAt(m_rulerMarkB));
        }
    }

    return boundedMeasureLinePoint(QPointF(qRound(pos.x()), qRound(pos.y())));
}

void ShapesWidget::resetMeasureLine()
{
    m_measureLinePointAVisible = false;
    m_measureLinePointBVisible = false;
    m_measureLinePressed = false;
    m_measureLineAction = MeasureLineAction::None;
    m_measureLineActivePoint = MeasureLineAction::None;
    m_measureLinePointA = QPointF();
    m_measureLinePointB = QPointF();
    m_measureLineHoverPoint = QPointF();
}

QPointF ShapesWidget::boundedMeasureLinePoint(QPointF pos) const
{
    if (width() <= 0 || height() <= 0) {
        return QPointF();
    }

    return QPointF(qBound<qreal>(0.0, pos.x(), static_cast<qreal>(width() - 1)),
                   qBound<qreal>(0.0, pos.y(), static_cast<qreal>(height() - 1)));
}

QPointF ShapesWidget::constrainedMeasureLinePoint(QPointF pos) const
{
    pos = snapMeasurementPoint(pos);
    if (!m_measureLinePointAVisible) {
        return pos;
    }

    if (m_isShiftPressed) {
        return QPointF(pos.x(), m_measureLinePointA.y());
    }
    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        return QPointF(m_measureLinePointA.x(), pos.y());
    }
    return pos;
}

bool ShapesWidget::moveActiveMeasureLinePoint(QPointF delta, Qt::KeyboardModifiers modifiers)
{
    if (!m_measureLinePointAVisible) {
        return false;
    }

    if (m_measureLineActivePoint == MeasureLineAction::None) {
        m_measureLineActivePoint = m_measureLinePointBVisible ? MeasureLineAction::DragPointB
                                                              : MeasureLineAction::DragPointA;
    }

    if (m_measureLineActivePoint == MeasureLineAction::DragPointA) {
        m_measureLinePointA = boundedMeasureLinePoint(m_measureLinePointA + delta);
        if (!m_measureLinePointBVisible) {
            m_measureLineHoverPoint = m_measureLinePointA;
            syncMeasureLineCursorToPoint(m_measureLinePointA);
        }
        return true;
    }

    if (m_measureLineActivePoint == MeasureLineAction::DragPointB && m_measureLinePointBVisible) {
        QPointF point = boundedMeasureLinePoint(m_measureLinePointB + delta);
        if (modifiers & Qt::ShiftModifier) {
            point.setY(m_measureLinePointA.y());
        } else if (modifiers & Qt::ControlModifier) {
            point.setX(m_measureLinePointA.x());
        }
        m_measureLinePointB = boundedMeasureLinePoint(point);
        m_measureLineHoverPoint = m_measureLinePointB;
        syncMeasureLineCursorToPoint(m_measureLinePointB);
        return true;
    }

    return false;
}

void ShapesWidget::syncMeasureLineCursorToPoint(QPointF pos)
{
    const QPoint globalPos = mapToGlobal(pos.toPoint());
    if ((QCursor::pos() - globalPos).manhattanLength() > 1) {
        QCursor::setPos(globalPos);
    }
}

ShapesWidget::MeasureLineAction ShapesWidget::hitTestMeasureLine(QPointF pos) const
{
    if (m_measureLinePointAVisible
            && QLineF(pos, m_measureLinePointA).length() <= MEASURE_POINT_HIT_RADIUS) {
        return MeasureLineAction::DragPointA;
    }
    if (m_measureLinePointBVisible
            && QLineF(pos, m_measureLinePointB).length() <= MEASURE_POINT_HIT_RADIUS) {
        return MeasureLineAction::DragPointB;
    }
    return MeasureLineAction::None;
}

bool ShapesWidget::handleMeasureLinePress(QPointF pos)
{
    if (m_currentType != "measure-line") {
        return false;
    }

    m_measureLineAction = hitTestMeasureLine(pos);
    if (m_measureLineAction != MeasureLineAction::None) {
        m_measureLinePressed = true;
        m_measureLineActivePoint = m_measureLineAction;
        m_measureLineHoverPoint = m_measureLineAction == MeasureLineAction::DragPointA ? m_measureLinePointA
                                                                                       : m_measureLinePointB;
        updateCursorShape();
        update();
        return true;
    }

    if (!m_measureLinePointAVisible || (m_measureLinePointAVisible && m_measureLinePointBVisible)) {
        m_measureLinePointA = snapMeasurementPoint(pos);
        m_measureLinePointB = QPointF();
        m_measureLinePointAVisible = true;
        m_measureLinePointBVisible = false;
        m_measureLineActivePoint = MeasureLineAction::DragPointA;
    } else {
        m_measureLinePointB = constrainedMeasureLinePoint(pos);
        m_measureLinePointBVisible = true;
        m_measureLineActivePoint = MeasureLineAction::DragPointB;
    }

    m_measureLineHoverPoint = constrainedMeasureLinePoint(pos);
    clearSelected();
    updateCursorShape();
    update();
    return true;
}

bool ShapesWidget::handleMeasureLineMove(QPointF pos)
{
    if (m_currentType != "measure-line") {
        return false;
    }

    if (m_measureLinePressed) {
        if (m_measureLineAction == MeasureLineAction::DragPointA) {
            m_measureLinePointA = snapMeasurementPoint(pos);
            m_measureLineActivePoint = MeasureLineAction::DragPointA;
            m_measureLineHoverPoint = m_measureLinePointA;
        } else if (m_measureLineAction == MeasureLineAction::DragPointB) {
            m_measureLinePointB = constrainedMeasureLinePoint(pos);
            m_measureLineActivePoint = MeasureLineAction::DragPointB;
            m_measureLineHoverPoint = m_measureLinePointB;
            syncMeasureLineCursorToPoint(m_measureLinePointB);
        } else {
            m_measureLineHoverPoint = snapMeasurementPoint(pos);
        }
    } else {
        m_measureLineHoverPoint = constrainedMeasureLinePoint(pos);
        if (m_measureLinePointAVisible && !m_measureLinePointBVisible
                && (m_isShiftPressed || (QApplication::keyboardModifiers() & Qt::ControlModifier))) {
            syncMeasureLineCursorToPoint(m_measureLineHoverPoint);
        }
    }
    updateCursorShape();
    update();
    return true;
}

void ShapesWidget::handleMeasureLineRelease()
{
    if (m_measureLinePointAVisible && m_measureLinePointBVisible) {
        addMeasurementHistory();
    }
    m_measureLinePressed = false;
    m_measureLineAction = MeasureLineAction::None;
    updateCursorShape();
}

void ShapesWidget::paintMeasurePoint(QPainter &painter, QPointF pos, const QString &text)
{
    Q_UNUSED(text);
    painter.setPen(QPen(QColor(255, 0, 0), 1));
    painter.drawPoint(pos);
}

void ShapesWidget::paintMeasureCursorGuide(QPainter &painter, QPointF pos)
{
    if (!rect().contains(pos.toPoint())) {
        return;
    }

    painter.save();
    const bool horizontalOnly = m_measureLinePointAVisible && !m_measureLinePointBVisible && m_isShiftPressed;
    const bool verticalOnly = m_measureLinePointAVisible && !m_measureLinePointBVisible
            && (QApplication::keyboardModifiers() & Qt::ControlModifier) && !m_isShiftPressed;
    painter.setPen(QPen(QColor(38, 48, 64, 120), 1));
    if (!verticalOnly) {
        painter.drawLine(QPointF(rect().left(), pos.y()), QPointF(rect().right(), pos.y()));
    }
    if (!horizontalOnly) {
        painter.drawLine(QPointF(pos.x(), rect().top()), QPointF(pos.x(), rect().bottom()));
    }
    painter.setPen(QPen(QColor(255, 0, 0), 1));
    painter.drawPoint(pos);
    painter.restore();
}

void ShapesWidget::paintMeasureLineOverlay(QPainter &painter, bool includeCursorGuide)
{
    if (m_currentType != "measure-line") {
        return;
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPointF pointB = m_measureLinePointBVisible ? m_measureLinePointB : m_measureLineHoverPoint;
    if (m_measureLinePointAVisible) {
        if (m_measureLinePointBVisible) {
            painter.setPen(QPen(QColor(1, 189, 255), 1, Qt::DashLine));
            painter.drawLine(m_measureLinePointA, pointB);
            if (includeCursorGuide) {
                painter.setPen(QPen(QColor(255, 0, 0, 45), 1));
                painter.drawLine(QPointF(m_measureLinePointA.x(), rect().top()),
                                 QPointF(m_measureLinePointA.x(), rect().bottom()));
                painter.drawLine(QPointF(pointB.x(), rect().top()),
                                 QPointF(pointB.x(), rect().bottom()));
                painter.drawLine(QPointF(rect().left(), m_measureLinePointA.y()),
                                 QPointF(rect().right(), m_measureLinePointA.y()));
                painter.drawLine(QPointF(rect().left(), pointB.y()),
                                 QPointF(rect().right(), pointB.y()));
            }
        }
        paintMeasurePoint(painter, m_measureLinePointA, QStringLiteral("A"));
    }
    if (m_measureLinePointBVisible) {
        paintMeasurePoint(painter, m_measureLinePointB, QStringLiteral("B"));
    }

    if (m_measureLinePointAVisible && m_measureLinePointBVisible) {
        const QPointF mid = (m_measureLinePointA + pointB) / 2;
        const QString text = currentMeasurementText();
        const QFontMetrics fm(painter.font());
        const QRect textBounds = fm.boundingRect(text).adjusted(-10, -6, 10, 6);
        QRectF labelRect(mid - QPointF(textBounds.width() / 2.0, textBounds.height() + 14),
                         QSizeF(textBounds.width(), textBounds.height()));
        const QRectF bounds = rect().adjusted(RULER_LABEL_MARGIN, RULER_LABEL_MARGIN,
                                              -RULER_LABEL_MARGIN, -RULER_LABEL_MARGIN);
        if (labelRect.left() < bounds.left()) {
            labelRect.moveLeft(bounds.left());
        }
        if (labelRect.right() > bounds.right()) {
            labelRect.moveRight(bounds.right());
        }
        if (labelRect.top() < bounds.top()) {
            labelRect.moveTop(bounds.top());
        }
        if (labelRect.bottom() > bounds.bottom()) {
            labelRect.moveBottom(bounds.bottom());
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(38, 48, 64, 220));
        painter.drawRoundedRect(labelRect, 6, 6);
        painter.setPen(Qt::white);
        painter.drawText(labelRect, Qt::AlignCenter, text);
    }

    if (includeCursorGuide && (!m_measureLinePointBVisible || m_measureLinePressed)) {
        paintMeasureCursorGuide(painter, m_measureLineHoverPoint);
    }
    painter.restore();
}

void ShapesWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    handlePaint(painter);
    paintRulerOverlay(painter);
    paintMeasureLineOverlay(painter);
    paintColorPickerOverlay(painter);
    if (m_shapes.length() > 0) {
        emit setShapesUndo(true);
    }
}

void ShapesWidget::handlePaint(QPainter &painter)
{
    painter.setRenderHints(QPainter::Antialiasing);
    QPen pen;

    const bool hasCurrentShape = (m_pos1 != QPointF(0, 0) && m_pos2 != QPointF(0, 0)) || m_currentShape.type == "text";
    FourPoints currentFPoint;
    if (hasCurrentShape) {
        currentFPoint = getMainPoints(m_pos1, m_pos2, m_isShiftPressed);
    }

    QList<FourPoints> spotlightShapes;
    for (int i = 0; i < m_shapes.length(); ++i) {
        if (m_shapes[i].type == "spotlight") {
            spotlightShapes.append(m_shapes[i].mainPoints);
        }
    }
    if (hasCurrentShape && m_currentType == "spotlight") {
        spotlightShapes.append(currentFPoint);
    }

    bool spotlightMaskPainted = false;
    const auto paintSpotlightMaskIfNeeded = [&]() {
        if (!spotlightMaskPainted && !spotlightShapes.isEmpty()) {
            paintSpotlightMask(painter, spotlightShapes);
            spotlightMaskPainted = true;
        }
    };

    //绘制所有图形
    for (int i = 0; i < m_shapes.length(); i++) {
        pen.setColor(BaseUtils::colorIndexOf(m_shapes[i].colorIndex));
        pen.setWidthF(m_shapes[i].lineWidth - 0.5);

        if (m_shapes[i].type == "rectangle") {
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            paintRect(painter, m_shapes[i].mainPoints, m_shapes.length(), Normal, false, false);
        } else if (m_shapes[i].type == "oval") {
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            paintEllipse(painter, m_shapes[i].mainPoints, m_shapes.length(), Normal, false, false);
        } else if (m_shapes[i].type == "effect") {
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            if (m_shapes[i].isOval == 0) {
                paintEllipse(painter, m_shapes[i].mainPoints, i, Drawing, m_shapes[i].isBlur, !m_shapes[i].isBlur, m_shapes[i].radius);
            } else if (m_shapes[i].isOval == 1) {
                paintRect(painter, m_shapes[i].mainPoints, i, Drawing, m_shapes[i].isBlur, !m_shapes[i].isBlur, m_shapes[i].radius);
            } else {
                pen.setJoinStyle(Qt::RoundJoin);
                painter.setPen(pen);
                paintEffectLine(painter, m_shapes[i].points, m_shapes[i].isBlur, m_shapes[i].radius, m_shapes[i].lineWidth);
            }
        } else if (m_shapes[i].type == "arrow") {
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            paintArrow(painter, m_shapes[i].points, pen.width(), false);
        } else if (m_shapes[i].type == "line") {
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            paintArrow(painter, m_shapes[i].points, pen.width(), true);
        } else if (m_shapes[i].type == "pen") {
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            paintLine(painter, m_shapes[i].points);
        } else if (m_shapes[i].type == "text" && !m_clearAllTextBorder) {
            //            qDebug() << "*&^" << m_shapes[i].type << m_shapes[i].index << m_selectedIndex << i;
            QMap<int, TextEdit *>::iterator m = m_editMap.begin();
            while (m != m_editMap.end()) {
                if (m.key() == m_shapes[i].index) {
                    if (!(m.value()->isReadOnly() && m_selectedIndex != i)) {
                        paintText(painter, m_shapes[i].mainPoints);
                    }
                    break;
                }
                ++m;
            }
        } else if (m_shapes[i].type == "step-number") {
            paintStepNumber(painter, m_shapes[i]);
        } else if (m_shapes[i].type == "spotlight") {
            paintSpotlightMaskIfNeeded();
        }
    }

    //绘制选中的图形
    if (hasCurrentShape) {
        pen.setColor(BaseUtils::colorIndexOf(m_currentShape.colorIndex));
        pen.setWidthF(m_currentShape.lineWidth - 0.5);

        if (m_currentType == "rectangle" && m_currentShape.type != "text") {
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            paintRect(painter, currentFPoint, m_shapes.length(), Normal, false, false);
        } else if (m_currentType == "oval" && m_currentShape.type != "text") {
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            paintEllipse(painter, currentFPoint, m_shapes.length(), Normal, false, false);

        } else if (m_currentType == "effect") {
            if (m_currentShape.isOval == 0) {
                paintEllipse(painter, currentFPoint, m_shapes.length(), Drawing, m_currentShape.isBlur, !m_currentShape.isBlur, m_currentShape.radius);
            } else if (m_currentShape.isOval == 1) {
                paintRect(painter, currentFPoint, m_shapes.length(), Drawing, m_currentShape.isBlur, !m_currentShape.isBlur, m_currentShape.radius);
            } else {
                //qDebug() << m_selectedIndex << m_currentShape.isOval << m_currentShape.points.size() << m_currentShape.radius << m_currentShape.lineWidth;
                pen.setJoinStyle(Qt::RoundJoin);
                painter.setPen(pen);
                paintEffectLine(painter, m_currentShape.points, m_currentShape.isBlur, m_currentShape.radius, m_currentShape.lineWidth);
            }
        } else if (m_currentType == "arrow" && m_currentShape.type != "text") {
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            paintArrow(painter, m_currentShape.points, pen.width(), false);
        } else if (m_currentType == "line" && m_currentShape.type != "text") {
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            paintArrow(painter, m_currentShape.points, pen.width(), true);
        } else if (m_currentType == "pen" && m_currentShape.type != "text") {
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            paintLine(painter, m_currentShape.points);
        } else if (m_currentType == "text" && !m_clearAllTextBorder) {
            if (m_editing) {
                paintText(painter, m_currentShape.mainPoints);
            }
        } else if (m_currentType == "spotlight") {
            paintSpotlightMaskIfNeeded();
        }

    }
    //绘制悬停状态的图形
    if ((m_hoveredShape.mainPoints[0] != QPointF(0, 0) ||  m_hoveredShape.points.length() != 0)
            && m_hoveredIndex != -1) {
        pen.setWidthF(0.5);
        pen.setColor("#01bdff");
        if (m_hoveredShape.type == "rectangle") {
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            paintRect(painter, m_hoveredShape.mainPoints, m_hoveredIndex,  Hovered, false, false);
        } else if (m_hoveredShape.type == "oval") {
            pen.setJoinStyle(Qt::MiterJoin);
            pen.setCapStyle(Qt::SquareCap);
            painter.setPen(pen);
            paintEllipse(painter, m_hoveredShape.mainPoints, m_hoveredIndex, Hovered, false, false);
        } else if (m_hoveredShape.type == "arrow") {
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            paintArrow(painter, m_hoveredShape.points, pen.width(), false);
        } else if (m_hoveredShape.type == "line") {
            pen.setJoinStyle(Qt::MiterJoin);
            painter.setPen(pen);
            paintArrow(painter, m_hoveredShape.points, pen.width(), true);
        } else if (m_hoveredShape.type == "pen") {
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            paintLine(painter, m_hoveredShape.points);
        } else if (m_hoveredShape.type == "effect" && m_hoveredShape.isOval == 2) {
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            paintLine(painter, m_hoveredShape.points);
        } else if (m_hoveredShape.type == "step-number") {
            painter.setPen(pen);
            paintStepNumber(painter, m_hoveredShape);
        } else if (m_hoveredShape.type == "spotlight") {
            painter.setPen(pen);
            paintRect(painter, m_hoveredShape.mainPoints, m_hoveredIndex, Hovered, false, false);
        } else {
            //        qDebug() << "hoveredShape type:" << m_hoveredShape.type;
        }
    }

    qreal ration =  this->devicePixelRatioF();
    QIcon icon(":/other/resize_handle_big.svg");
    QPixmap resizePointImg = icon.pixmap(QSize(RESIZEPOINT_WIDTH,
                                               RESIZEPOINT_WIDTH));
    resizePointImg.setDevicePixelRatio(ration);

    //只有当选中图形时m_selectedShape才会有内容
    if ((m_selectedShape.type == "arrow" ||  m_selectedShape.type == "line") && m_selectedShape.points.length() == 2) {
        paintImgPoint(painter, m_selectedShape.points[0], resizePointImg);
        paintImgPoint(painter, m_selectedShape.points[1], resizePointImg);
    } else if (m_selectedShape.type != "" && m_selectedShape.type != "text") {
        if (m_selectedShape.mainPoints[0] != QPointF(0, 0) || m_selectedShape.type == "arrow") {

            QPointF rotatePoint = getRotatePoint(m_selectedShape.mainPoints[0],
                                                 m_selectedShape.mainPoints[1],
                                                 m_selectedShape.mainPoints[2],
                                                 m_selectedShape.mainPoints[3]);

            if (m_selectedShape.type == "oval" || m_selectedShape.type == "pen" || (m_selectedShape.type == "effect" && m_selectedShape.isOval == 2)) {
                pen.setJoinStyle(Qt::MiterJoin);
                pen.setWidth(1);
                pen.setColor(QColor("#01bdff"));
                painter.setPen(pen);
                paintRect(painter,  m_selectedShape.mainPoints, -1);
            }

            QPixmap rotatePointImg;
            //            rotatePointImg = QIcon(":/resources/images/size/rotate.svg").pixmap(ROTATE_ICON_SIZE);
            rotatePointImg = QIcon(":/other/icon_rotate.svg").pixmap(ROTATE_ICON_SIZE);
            rotatePointImg.setDevicePixelRatio(this->devicePixelRatioF());
            paintImgPoint(painter, rotatePoint, rotatePointImg, false);

            //画出形状的主要骨架点
            for (int i = 0; i < m_selectedShape.mainPoints.length(); i ++) {

                paintImgPoint(painter, m_selectedShape.mainPoints[i], resizePointImg);
            }

            //画出形状的其他骨架点
            FourPoints anotherFPoints = getAnotherFPoints(m_selectedShape.mainPoints);
            for (int j = 0; j < anotherFPoints.length(); j++) {
                paintImgPoint(painter, anotherFPoints[j], resizePointImg);
            }
        }
    }
}

//将编辑的内容绘制到图片上
void ShapesWidget::paintImage(QImage &image, QPointF paintOffset)
{
    QPainter painter(&image);
    painter.translate(paintOffset);
    handlePaint(painter);
    paintRulerOverlay(painter, false);
    paintMeasureLineOverlay(painter, false);
    //    backgroundImage.save("/home/uos/Desktop/temp1.png");
}

bool ShapesWidget::isExistsText()
{
    for (int i = 0; i < m_shapes.length(); i++) {
        if (m_shapes[i].type == "text") {
            return true;
        }
    }
    return  false;
}

void ShapesWidget::enterEvent(QEvent *e)
{
    Q_UNUSED(e);
    if (m_currentType == "pen") {
        qApp->setOverrideCursor(BaseUtils::setCursorShape("pen",  BaseUtils::colorIndex(m_penColor)));
    } else if (m_currentType == "effect") {
        int isOval = ConfigSettings::instance()->getValue("effect", "isOval").toInt();
        QCursor setCursorValue;
        if (isOval == 0) {
            setCursorValue = BaseUtils::setCursorShape("oval");
        } else if (isOval == 1) {
            setCursorValue = BaseUtils::setCursorShape("rectangle");
        } else {
            setCursorValue = BaseUtils::setCursorShape("pen", 0);
        }
        qApp->setOverrideCursor(setCursorValue);
    } else if (m_currentType == "ruler") {
        updateCursorShape();
    } else if (m_currentType == "step-number"
               || m_currentType == "spotlight"
               || m_currentType == "color-picker") {
        qApp->setOverrideCursor(Qt::CrossCursor);
    } else {
        qApp->setOverrideCursor(BaseUtils::setCursorShape(m_currentType));
    }
}

void ShapesWidget::pinchTriggered(QPinchGesture *pinch)
{
    if (-1 == m_selectedIndex || "text" == m_currentType)
        return;
    QPinchGesture::ChangeFlags changeFlags = pinch->changeFlags();
    if (changeFlags & QPinchGesture::RotationAngleChanged) {
        qreal rotationDelta = (pinch->rotationAngle() - pinch->lastRotationAngle()) * 0.01;
        qreal centerX = (m_shapes[m_selectedOrder].mainPoints[0].x() + m_shapes[m_selectedOrder].mainPoints[3].x()) / 2;
        qreal centerY = (m_shapes[m_selectedOrder].mainPoints[0].y() + m_shapes[m_selectedOrder].mainPoints[3].y()) / 2;
        for (int i = 0; i < m_shapes[m_selectedOrder].mainPoints.size(); ++i) {
            qreal x = centerX + (m_shapes[m_selectedOrder].mainPoints[i].x() - centerX) * cos(rotationDelta) - (m_shapes[m_selectedOrder].mainPoints[i].y() - centerY) * sin(rotationDelta);
            qreal y = centerY + (m_shapes[m_selectedOrder].mainPoints[i].x() - centerX) * sin(rotationDelta) + (m_shapes[m_selectedOrder].mainPoints[i].y() - centerY) * cos(rotationDelta);
            //图形
            m_shapes[m_selectedOrder].mainPoints[i].setX(x);
            m_shapes[m_selectedOrder].mainPoints[i].setY(y);
        }
        qreal centerMainX = (m_selectedShape.mainPoints[0].x() + m_selectedShape.mainPoints[3].x()) / 2;
        qreal centerMainY = (m_selectedShape.mainPoints[0].y() + m_selectedShape.mainPoints[3].y()) / 2;
        for (int i = 0; i < m_selectedShape.mainPoints.length(); i ++) {
            qreal x = centerMainX + (m_selectedShape.mainPoints[i].x() - centerMainX) * cos(rotationDelta) - (m_selectedShape.mainPoints[i].y() - centerMainY) * sin(rotationDelta);
            qreal y = centerMainY + (m_selectedShape.mainPoints[i].x() - centerMainX) * sin(rotationDelta) + (m_selectedShape.mainPoints[i].y() - centerMainY) * cos(rotationDelta);
            //选中状态，点+线条
            m_selectedShape.mainPoints[i].setX(x);
            m_selectedShape.mainPoints[i].setY(y);
        }
        for (int k = 0; k < m_selectedShape.points.length(); k++) {
            qreal x = centerMainX + (m_shapes[m_selectedOrder].points[k].x() - centerMainX) * cos(rotationDelta) - (m_shapes[m_selectedOrder].points[k].y() - centerMainY) * sin(rotationDelta);
            qreal y = centerMainY + (m_shapes[m_selectedOrder].points[k].x() - centerMainX) * sin(rotationDelta) + (m_shapes[m_selectedOrder].points[k].y() - centerMainY) * cos(rotationDelta);
            //线条
            m_shapes[m_selectedOrder].points[k].setX(x);
            m_shapes[m_selectedOrder].points[k].setY(y);
        }
    }
    if (changeFlags & QPinchGesture::ScaleFactorChanged) {
        qreal scale = pinch->scaleFactor();
        qreal ascanle = (scale - 1) * 0.3 + 1;
        qreal centerX = (m_shapes[m_selectedOrder].mainPoints[0].x() + m_shapes[m_selectedOrder].mainPoints[3].x()) / 2;
        qreal centerY = (m_shapes[m_selectedOrder].mainPoints[0].y() + m_shapes[m_selectedOrder].mainPoints[3].y()) / 2;
        for (int i = 0; i < m_shapes[m_selectedOrder].mainPoints.size(); ++i) {
            qreal x = m_shapes[m_selectedOrder].mainPoints[i].x() * ascanle + centerX * (1 - ascanle);
            qreal y = m_shapes[m_selectedOrder].mainPoints[i].y() * ascanle + centerY * (1 - ascanle);
            m_shapes[m_selectedOrder].mainPoints[i].setX(x);
            m_shapes[m_selectedOrder].mainPoints[i].setY(y);
        }
        qreal centerMainX = (m_selectedShape.mainPoints[0].x() + m_selectedShape.mainPoints[3].x()) / 2;
        qreal centerMainY = (m_selectedShape.mainPoints[0].y() + m_selectedShape.mainPoints[3].y()) / 2;
        for (int i = 0; i < m_selectedShape.mainPoints.length(); i ++) {
            qreal x = m_selectedShape.mainPoints[i].x() * ascanle + centerMainX * (1 - ascanle);
            qreal y = m_selectedShape.mainPoints[i].y() * ascanle + centerMainY * (1 - ascanle);
            m_selectedShape.mainPoints[i].setX(x);
            m_selectedShape.mainPoints[i].setY(y);
        }
        for (int k = 0; k < m_selectedShape.points.length(); k++) {
            qreal x = m_shapes[m_selectedOrder].points[k].x() * ascanle + centerMainX * (1 - ascanle);
            qreal y = m_shapes[m_selectedOrder].points[k].y() * ascanle + centerMainY * (1 - ascanle);
            m_shapes[m_selectedOrder].points[k].setX(x);
            m_shapes[m_selectedOrder].points[k].setY(y);
        }
    }
}

void ShapesWidget::tapTriggered(QTapGesture *tap)
{
    if (tap->state() == Qt::GestureState::GestureFinished && !clickedOnShapes(m_movingPoint) && -1 != m_selectedIndex && "text" != m_currentType) {
        m_isPressed = false;
        m_isMoving = false;
        clearSelected();
        setAllTextEditReadOnly();
        m_editing = false;
        m_selectedIndex = -1;
        m_selectedOrder = -1;
        m_selectedShape.type = "";
    }
}

void ShapesWidget::deleteCurrentShape()
{
    qDebug() << "delete shape";
    if (m_selectedOrder >= 0 && m_selectedOrder < m_shapes.length()) {
        m_shapes.removeAt(m_selectedOrder);
    } else {
        qWarning() << "Invalid index";
    }

    if (m_selectedShape.type == "text" && m_editMap.contains(m_selectedShape.index)) {
        m_editMap.value(m_selectedShape.index)->clear();
        m_editMap.remove(m_selectedShape.index);
    }

    clearSelected();
    m_selectedShape.type = "";
    m_currentShape.type = "";
    for (int i = 0; i < m_currentShape.mainPoints.length(); i++) {
        m_currentShape.mainPoints[i] = QPointF(0, 0);
    }

    if (m_shapes.length() == 0) {
        emit setShapesUndo(false);
    }

    update();
    m_selectedIndex = -1;
    m_selectedOrder = -1;
}

void ShapesWidget::undoDrawShapes()
{
    textEditIsReadOnly();
    qDebug() << "undoDrawShapes m_selectedIndex:" << m_selectedIndex << m_shapes.length();
    if (m_selectedOrder < m_shapes.length() && m_selectedIndex != -1) {
        deleteCurrentShape();
    } else if (m_shapes.length() > 0) {
        int tmpIndex = m_shapes[m_shapes.length() - 1].index;
        if (m_shapes[m_shapes.length() - 1].type == "text" && m_editMap.contains(tmpIndex)) {
            m_editMap.value(tmpIndex)->clear();
            delete m_editMap.value(tmpIndex);
            m_editMap.remove(tmpIndex);
        }

        m_shapes.removeLast();
    }
    qDebug() << "undoDrawShapes m_selectedIndex:" << m_selectedIndex << m_shapes.length();

    if (m_shapes.length() == 0) {
        emit setShapesUndo(false);
    }
    m_isSelected = false;
    clearSelected();
    update();
}
void ShapesWidget::undoAllDrawShapes()
{
    qDebug() << "undoAllDrawShapes undoDrawShapes m_selectedIndex:" << m_selectedIndex << m_shapes.length();
    if (m_selectedOrder < m_shapes.length() && m_selectedIndex != -1) {
        deleteCurrentShape();
    } else if (m_shapes.length() > 0) {
        while (m_shapes.length() > 0) {
            int tmpIndex = m_shapes[m_shapes.length() - 1].index;
            if (m_shapes[m_shapes.length() - 1].type == "text" && m_editMap.contains(tmpIndex)) {
                m_editMap.value(tmpIndex)->clear();
                delete m_editMap.value(tmpIndex);
                m_editMap.remove(tmpIndex);
            }

            m_shapes.removeLast();
        }
    }
    qDebug() << "undoDrawShapes m_selectedIndex:" << m_selectedIndex << m_shapes.length();

    if (m_shapes.length() == 0) {
        emit setShapesUndo(false);
    }
    m_isSelected = false;
    clearSelected();
    update();
}

void ShapesWidget::isInUndoBtn(bool isUndo)
{
    //qDebug() << ">>>>>> isInUndoBtn"  << isUndo;
    m_isUnDo = isUndo;
}

void ShapesWidget::microAdjust(QString direction)
{
    if (m_selectedIndex == -1 || m_selectedOrder >= m_shapes.length()) {
        return;
    }
    
    Toolshape &currentShape = m_shapes[m_selectedOrder];
    
    if (currentShape.type == "text") {
        return;
    }

    // 保存原始的mainPoints，用于计算偏移量
    FourPoints oldMainPoints = currentShape.mainPoints;

    // 根据方向调整mainPoints
    bool isSimpleMove = direction == Direction::LEFT || direction == Direction::RIGHT || 
                        direction == Direction::UP || direction == Direction::DOWN;
                        
    bool isReduceResize = direction == Direction::CTRL_SHIFT_LEFT || direction == Direction::CTRL_SHIFT_RIGHT || 
                          direction == Direction::CTRL_SHIFT_UP || direction == Direction::CTRL_SHIFT_DOWN;

    // 调整mainPoints
    if (isSimpleMove) {
        currentShape.mainPoints = pointMoveMicro(currentShape.mainPoints, direction);
    } else if (isReduceResize) {
        currentShape.mainPoints = pointResizeMicro(currentShape.mainPoints, direction, false);
    } else {
        currentShape.mainPoints = pointResizeMicro(currentShape.mainPoints, direction, true);
    }

    if (currentShape.type == "line" || currentShape.type == "arrow") {
        if (currentShape.portion.length() == 0) {
            for (int k = 0; k < currentShape.points.length(); k++) {
                currentShape.portion.append(relativePosition(currentShape.mainPoints,
                                                                          currentShape.points[k]));
            }
        }
        for (int j = 0; j < currentShape.points.length(); j++) {
            currentShape.points[j] = getNewPosition(
                                                      currentShape.mainPoints, currentShape.portion[j]);
        }
    } 
    // 处理画笔类型的points
    else if (currentShape.type == "pen" || 
            (currentShape.type == "effect" && currentShape.isOval == 2)) {
        
        QPointF offset;
        if (isSimpleMove) {
            // 移动操作，计算偏移量
            offset = currentShape.mainPoints[0] - oldMainPoints[0];
            
            // 对所有点应用相同的偏移量
            for (int j = 0; j < currentShape.points.length(); j++) {
                currentShape.points[j] += offset;
            }
        } else {
            // 缩放操作
            QPointF oldCenter = (oldMainPoints[0] + oldMainPoints[3]) / 2;
            QPointF newCenter = (currentShape.mainPoints[0] + currentShape.mainPoints[3]) / 2;
            
            // 计算缩放比例
            QRectF oldRect = QRectF(oldMainPoints[0], oldMainPoints[3]);
            QRectF newRect = QRectF(currentShape.mainPoints[0], currentShape.mainPoints[3]);
            qreal scaleX = newRect.width() / oldRect.width();
            qreal scaleY = newRect.height() / oldRect.height();
            
            // 对每个点应用缩放和偏移
            for (int j = 0; j < currentShape.points.length(); j++) {
                QPointF point = currentShape.points[j];
                QPointF relativePos = point - oldCenter;
                relativePos.setX(relativePos.x() * scaleX);
                relativePos.setY(relativePos.y() * scaleY);
                currentShape.points[j] = newCenter + relativePos;
            }
        }
    }

    // 更新选中的形状
    m_selectedShape.mainPoints = currentShape.mainPoints;
    m_selectedShape.points = currentShape.points;
    m_hoveredShape.type = "";
    update();
}

void ShapesWidget::setShiftKeyPressed(bool isShift)
{
    m_isShiftPressed = isShift;
    if (m_currentType == "measure-line" && m_measureLinePointAVisible && !m_measureLinePointBVisible) {
        m_measureLineHoverPoint = constrainedMeasureLinePoint(m_movingPoint);
        update();
    }
}

void ShapesWidget::updateCursorShape()
{
//    qDebug() << "func" << __FUNCTION__ << "line" << __LINE__;
    if (m_currentType == "ruler") {
        QCursor setCursorValue = Qt::ArrowCursor;
        if (m_rulerPressed) {
            if (m_rulerAction == RulerAction::Move) {
                setCursorValue = Qt::ClosedHandCursor;
            } else if (m_rulerAction == RulerAction::Rotate) {
                setCursorValue = BaseUtils::setCursorShape("rotate");
            } else if (m_rulerAction == RulerAction::ResizeStart
                       || m_rulerAction == RulerAction::ResizeEnd) {
                setCursorValue = Qt::SizeHorCursor;
            } else {
                setCursorValue = Qt::CrossCursor;
            }

            if (qApp->overrideCursor()) {
                qApp->changeOverrideCursor(setCursorValue);
            } else {
                qApp->setOverrideCursor(setCursorValue);
            }
            return;
        }

        if (m_rulerVisible) {
            const RulerAction hoverAction = hitTestRuler(mapFromGlobal(QCursor::pos()));
            if (hoverAction == RulerAction::Move) {
                setCursorValue = Qt::OpenHandCursor;
            } else if (hoverAction == RulerAction::Rotate) {
                setCursorValue = BaseUtils::setCursorShape("rotate");
            } else if (hoverAction == RulerAction::ResizeStart
                       || hoverAction == RulerAction::ResizeEnd) {
                setCursorValue = Qt::SizeHorCursor;
            } else if (hoverAction == RulerAction::PlaceMark
                       || hoverAction == RulerAction::DragMarkA
                       || hoverAction == RulerAction::DragMarkB) {
                setCursorValue = Qt::CrossCursor;
            }
        }

        if (qApp->overrideCursor()) {
            qApp->changeOverrideCursor(setCursorValue);
        } else {
            qApp->setOverrideCursor(setCursorValue);
        }
        return;
    }

    if (m_currentType == "measure-line") {
        QCursor setCursorValue = Qt::CrossCursor;
        if (m_measureLinePressed || hitTestMeasureLine(mapFromGlobal(QCursor::pos())) != MeasureLineAction::None) {
            setCursorValue = m_measureLinePressed ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
        }
        if (qApp->overrideCursor()) {
            qApp->changeOverrideCursor(setCursorValue);
        } else {
            qApp->setOverrideCursor(setCursorValue);
        }
        return;
    }

    if (m_currentType == "step-number"
            || m_currentType == "spotlight"
            || m_currentType == "color-picker") {
        const QCursor setCursorValue = Qt::CrossCursor;
        if (qApp->overrideCursor()) {
            qApp->changeOverrideCursor(setCursorValue);
        } else {
            qApp->setOverrideCursor(setCursorValue);
        }
        return;
    }

    if (!m_isUnDo) {
        QCursor setCursorValue;
        if (m_currentType == "pen") {
            m_penColor = BaseUtils::colorIndexOf(ConfigSettings::instance()->getValue("pen", "color_index").toInt());
            setCursorValue = BaseUtils::setCursorShape(m_currentType, BaseUtils::colorIndex(m_penColor));
        } else if (m_currentType == "effect") {
            int isOval = ConfigSettings::instance()->getValue("effect", "isOval").toInt();
            if (isOval == 0) {
                setCursorValue = BaseUtils::setCursorShape("oval");
            } else if (isOval == 1) {
                setCursorValue = BaseUtils::setCursorShape("rectangle");
            } else {
                setCursorValue = BaseUtils::setCursorShape("pen", 0);
            }
        } else {
            setCursorValue = BaseUtils::setCursorShape(m_currentType);
        }
        // 避免相同的光标样式重复设置
        if (*qApp->overrideCursor() != setCursorValue) {
            qApp->changeOverrideCursor(setCursorValue);
        }
    }
}

void ShapesWidget::menuSaveSlot()
{
    emit saveFromMenu();
}

void ShapesWidget::menuCloseSlot()
{
    emit closeFromMenu();
}
void ShapesWidget::setGlobalRect(QRect rect)
{
    m_globalRect = rect;
}
