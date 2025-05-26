// Copyright (C) 2020 ~ 2021 Uniontech Software Technology Co.,Ltd.
// SPDX-FileCopyrightText: 2023 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcut.h"
#include "log.h"
#include <QDBusReply>
#include <QDBusInterface>
#include <QDebug>
Shortcut::Shortcut(QObject *parent) : QObject(parent)
{
    qCDebug(dsrApp) << "Initializing Shortcut";
    ShortcutGroup screenshotGroup;
    ShortcutGroup exitGroup;
    ShortcutGroup toolsGroup;
    ShortcutGroup recordGroup;
    ShortcutGroup sizeGroup;
    ShortcutGroup setGroup;

    screenshotGroup.groupName = tr("Start/Screenshot");
    exitGroup.groupName = tr("Exit/Save");
    toolsGroup.groupName = tr("Tools");
    recordGroup.groupName = tr("Start/Recording");
    sizeGroup.groupName = tr("Size Adjustment");
    setGroup.groupName = tr("Settings");

    qCDebug(dsrApp) << "Setting up screenshot group shortcuts";
    screenshotGroup.groupItems << ShortcutItem(tr("Quick start"), getSysShortcuts("screenshot"))
                               << ShortcutItem(tr("Window screenshot"), getSysShortcuts("screenshot-window"))
                               << ShortcutItem(tr("Delay screenshot"),  getSysShortcuts("screenshot-delayed"))
                               << ShortcutItem(tr("Full screenshot"),  getSysShortcuts("screenshot-fullscreen"))
                               << ShortcutItem(tr("Start scrollshot"),  getSysShortcuts("screenshot-scroll"))
                               << ShortcutItem(tr("Start OCR"),  getSysShortcuts("screenshot-ocr"));

    qCDebug(dsrApp) << "Setting up exit group shortcuts";
    exitGroup.groupItems << ShortcutItem(tr("Exit"), "Esc")
                         << ShortcutItem(tr("Save"), "Ctrl+S");
#ifdef OCR_SCROLL_FLAGE_ON
    toolsGroup.groupItems << ShortcutItem(tr("Scrollshot"), "Alt+I");
#endif
    toolsGroup.groupItems << ShortcutItem(tr("Pin screenshots"), "Alt+P")
                          << ShortcutItem(tr("Rectangle"), "R")
                          << ShortcutItem(tr("Ellipse"), "O")
                          << ShortcutItem(tr("Line"), "L")
                          << ShortcutItem(tr("Arrow"), "X")
                          << ShortcutItem(tr("Pencil"), "P")
                          << ShortcutItem(tr("Text"), "T");
#ifdef OCR_SCROLL_FLAGE_ON
    toolsGroup.groupItems << ShortcutItem(tr("Extract text"), "Alt+O");
#endif
    toolsGroup.groupItems << ShortcutItem(tr("Delete"), "Delete")
                          << ShortcutItem(tr("Undo"), "Ctrl+Z")
                          << ShortcutItem(tr("Options"), "F3");

    qCDebug(dsrApp) << "Setting up recording group shortcuts";
    recordGroup.groupItems << ShortcutItem(tr("Start recording"), getSysShortcuts("deepin-screen-recorder"))
                           << ShortcutItem(tr("Sound"), "S")
                           << ShortcutItem(tr("Keystroke"), "K")
                           << ShortcutItem(tr("Webcam"), "C")
                           << ShortcutItem(tr("Mouse"), "M")
                           << ShortcutItem(tr("Options"), "F3")
                           << ShortcutItem(" ", " ");

    qCDebug(dsrApp) << "Setting up size adjustment group shortcuts";
    sizeGroup.groupItems << ShortcutItem(tr("Increase height up"), "Ctrl+Up")
                         << ShortcutItem(tr("Increase height down"), "Ctrl+Down")
                         << ShortcutItem(tr("Increase width left"), "Ctrl+Left")
                         << ShortcutItem(tr("Increase width right"), "Ctrl+Right")
                         << ShortcutItem(tr("Decrease height up"), "Ctrl+Shift+Up")
                         << ShortcutItem(tr("Decrease height down"), "Ctrl+Shift+Down")
                         << ShortcutItem(tr("Decrease width left"), "Ctrl+Shift+Left")
                         << ShortcutItem(tr("Decrease width right"), "Ctrl+Shift+Right");

    qCDebug(dsrApp) << "Setting up settings group shortcuts";
    setGroup.groupItems << ShortcutItem(tr("Help"), "F1")
                        << ShortcutItem(tr("Display shortcuts"), "Ctrl+Shift+?");

    m_shortcutGroups << screenshotGroup <<  recordGroup << toolsGroup <<  exitGroup << sizeGroup << setGroup;

    qCDebug(dsrApp) << "Converting shortcuts to JSON format";
    //convert to json object
    QJsonArray jsonGroups;
    for (auto scg : m_shortcutGroups) {
        QJsonObject jsonGroup;
        jsonGroup.insert("groupName", scg.groupName);
        QJsonArray jsonGroupItems;
        for (auto sci : scg.groupItems) {
            QJsonObject jsonItem;
            jsonItem.insert("name", sci.name);
            jsonItem.insert("value", sci.value);
            jsonGroupItems.append(jsonItem);
        }
        jsonGroup.insert("groupItems", jsonGroupItems);
        jsonGroups.append(jsonGroup);
    }
    m_shortcutObj.insert("shortcut", jsonGroups);
    qCDebug(dsrApp) << "Shortcut initialization completed";
}
QString Shortcut::toStr()
{
    qCDebug(dsrApp) << "Converting shortcuts to JSON string";
    QJsonDocument doc(m_shortcutObj);
    return doc.toJson().data();
}
QString Shortcut::getSysShortcuts(const QString type)
{
    qCDebug(dsrApp) << "Getting system shortcut for type:" << type;
    QDBusInterface shortcuts("org.deepin.dde.Keybinding1", "/org/deepin/dde/Keybinding1", "org.deepin.dde.Keybinding1");
    if (!shortcuts.isValid()) {
        qCWarning(dsrApp) << "DBus interface is not valid, using default shortcut for type:" << type;
        return getDefaultValue(type);
    }

    QDBusReply<QString> shortLists = shortcuts.call(QStringLiteral("ListAllShortcuts"));
    QJsonDocument doc = QJsonDocument::fromJson(shortLists.value().toUtf8());
    QJsonArray shorts = doc.array();
    QMap<QString, QString> shortcutsMap;

    for (QJsonValue shortcut : shorts) {
        const QString Id = shortcut["Id"].toString();
        if (Id == type) {
            QJsonArray Accels = shortcut["Accels"].toArray();
            QString AccelsString;
            for (QJsonValue accel : Accels)  {
                AccelsString += accel.toString();
            }
            AccelsString.remove('<');
            AccelsString.replace('>', '+');
            AccelsString.replace("Control", "Ctrl");
            AccelsString.replace("Print", "PrintScreen");
            qCDebug(dsrApp) << "Found system shortcut for" << type << ":" << AccelsString;
            return AccelsString;
        }
    }
    qCDebug(dsrApp) << "System shortcut not found for" << type << ", using default value";
    return getDefaultValue(type);
}

QString Shortcut::getDefaultValue(const QString type)
{
    qCDebug(dsrApp) << "Getting default shortcut value for type:" << type;
    QString retShortcut;
    if (type == "screenshot") {
        retShortcut = "Ctrl+Alt+A";
    } else if (type == "deepin-screen-recorder") {
        retShortcut = "Ctrl+Alt+R";
    } else if (type == "screenshot-window") {
        retShortcut = "Alt+PrintScreen";
    } else if (type == "screenshot-delayed") {
        retShortcut = "Ctrl+PrintScreen";
    } else if (type == "screenshot-fullscreen") {
        retShortcut = "PrintScreen";
    } else {
        qCWarning(dsrApp) << "Unknown shortcut type:" << type;
        return retShortcut;
    }
    qCDebug(dsrApp) << "Default shortcut for" << type << ":" << retShortcut;
    return retShortcut;
}
