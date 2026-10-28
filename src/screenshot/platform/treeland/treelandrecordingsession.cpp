// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "treelandrecordingsession.h"
#include "treelandclipboardholder.h"

#include "../../../ext-image-capture/extcapturerecorder.h"
#include "../../../utils/configsettings.h"
#include "../../../utils/log.h"
#include "../../../utils.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

namespace Dsr::Screenshot {

namespace {

constexpr int RecordingStartTimeoutMs = 10000;
constexpr int RecordingNotifyIntervalMs = 1000;

void callRecorderTray(const QString &function)
{
    QDBusInterface timeInterface(QStringLiteral("com.deepin.ScreenRecorder.time"),
                                 QStringLiteral("/com/deepin/ScreenRecorder/time"),
                                 QStringLiteral("com.deepin.ScreenRecorder.time"),
                                 QDBusConnection::sessionBus());
    timeInterface.asyncCall(function);

    QDBusInterface shotInterface(QStringLiteral("com.deepin.ShotRecorder.PanelStatus"),
                                 QStringLiteral("/com/deepin/ShotRecorder/PanelStatus"),
                                 QStringLiteral("com.deepin.ShotRecorder.PanelStatus"),
                                 QDBusConnection::sessionBus());
    shotInterface.asyncCall(function);

    QDBusInterface recorderInterface(QStringLiteral("com.deepin.ShotRecorder.Recorder.PanelStatus"),
                                     QStringLiteral("/com/deepin/ShotRecorder/Recorder/PanelStatus"),
                                     QStringLiteral("com.deepin.ShotRecorder.Recorder.PanelStatus"),
                                     QDBusConnection::sessionBus());
    recorderInterface.asyncCall(function);
}

QString recordingSaveDirectory()
{
    ConfigSettings *settings = ConfigSettings::instance();
    QString directory = settings->getValue(QStringLiteral("recorder"),
                                           QStringLiteral("save_dir"))
                                .toString();
    if (directory.isEmpty()) {
        directory = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
                + QStringLiteral("/Screen Recordings");
    } else if (settings->getValue(QStringLiteral("recorder"),
                                  QStringLiteral("save_op"))
                       .toInt()
               != 1) {
        directory += QStringLiteral("/Screen Recordings");
    }

    if (!QDir().mkpath(directory)) {
        const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        QDir().mkpath(movies);
        directory = movies;
    }
    return directory;
}

} // namespace

TreelandRecordingSession::TreelandRecordingSession(QObject *parent)
    : IRecordingSession(parent)
    , m_recordingNotifyTimer(new QTimer(this))
    , m_startTimeoutTimer(new QTimer(this))
{
    m_recordingNotifyTimer->setInterval(RecordingNotifyIntervalMs);
    connect(m_recordingNotifyTimer, &QTimer::timeout, this, [this] {
        if (m_state == State::Recording)
            callRecorderTray(QStringLiteral("onRecording"));
    });

    m_startTimeoutTimer->setSingleShot(true);
    m_startTimeoutTimer->setInterval(RecordingStartTimeoutMs);
    connect(m_startTimeoutTimer, &QTimer::timeout,
            this, &TreelandRecordingSession::onStartTimeout);
}

TreelandRecordingSession::~TreelandRecordingSession()
{
    if (m_recorder && m_recorder->state() != ExtCaptureRecorder::Stopped)
        m_recorder->stopRecording();
}

bool TreelandRecordingSession::start(const RecordingRequest &request)
{
    if (m_state == State::Starting || m_state == State::Recording
            || m_state == State::Stopping) {
        return false;
    }

    cleanupRecorder();
    m_capturePath.clear();
    m_finalPath.clear();
    m_clipboardError.clear();
    if (request.screens.isEmpty() || request.selection.isEmpty()) {
        finishFailed(QStringLiteral("录屏区域没有可用的输出屏幕"));
        return false;
    }

    m_request = request;
    m_request.selection = request.selection.normalized();
    m_pendingError.clear();
    m_stopRequested = false;
    m_startIssued = false;
    m_cancelled = false;
    m_terminal = false;

    const QString configuredPath = request.outputPath;
    m_capturePath = configuredPath.isEmpty()
            ? createOutputPath(request.format)
            : configuredPath;
    m_finalPath = m_capturePath;
    if (request.format == Utils::kGIF) {
        m_capturePath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                .filePath(QStringLiteral("deepin-screen-recorder-%1.mp4")
                                                  .arg(QDateTime::currentMSecsSinceEpoch()));
        QFile::remove(m_capturePath);
    }

    const QDir outputDir = QFileInfo(m_finalPath).dir();
    if (!outputDir.exists() && !QDir().mkpath(outputDir.path())) {
        finishFailed(QStringLiteral("无法创建录屏保存目录：%1").arg(outputDir.path()));
        return false;
    }

    m_recorder = new ExtCaptureRecorder(this);
    connect(m_recorder, &ExtCaptureRecorder::recordingStarted,
            this, &TreelandRecordingSession::onRecorderStarted);
    connect(m_recorder, &ExtCaptureRecorder::recordingStopped,
            this, &TreelandRecordingSession::onRecorderStopped);
    connect(m_recorder, &ExtCaptureRecorder::error,
            this, &TreelandRecordingSession::onRecorderError);
    connect(m_recorder, &ExtCaptureRecorder::protocolAvailable,
            this, &TreelandRecordingSession::startRecorderWhenAvailable);

    setState(State::Starting);
    m_startTimeoutTimer->start();
    QTimer::singleShot(0, this, &TreelandRecordingSession::startRecorderWhenAvailable);
    return true;
}

void TreelandRecordingSession::startRecorderWhenAvailable()
{
    if (m_state != State::Starting || m_startIssued || !m_recorder
            || !m_recorder->isAvailable()) {
        return;
    }

    m_startIssued = true;
    const bool started = m_request.screens.size() > 1
            ? m_recorder->startMultiScreenRecording(m_request.screens,
                                                    m_request.includeCursor,
                                                    m_capturePath,
                                                    m_request.frameRate,
                                                    m_request.selection)
            : m_recorder->startRecording(m_request.screen,
                                         m_request.includeCursor,
                                         m_capturePath,
                                         m_request.frameRate,
                                         m_request.selection);
    if (!started) {
        m_startTimeoutTimer->stop();
        finishFailed(QStringLiteral("启动 TreeLand 录屏失败"));
    }
}

void TreelandRecordingSession::stop()
{
    if (m_state != State::Starting && m_state != State::Recording)
        return;

    m_stopRequested = true;
    setState(State::Stopping);
    m_startTimeoutTimer->stop();
    m_recordingNotifyTimer->stop();
    callRecorderTray(QStringLiteral("onPause"));
    if (m_recorder)
        m_recorder->stopRecording();
}

void TreelandRecordingSession::cancel()
{
    if (m_terminal)
        return;
    m_cancelled = true;
    if (m_recorder && m_recorder->state() != ExtCaptureRecorder::Stopped) {
        stop();
        m_cancelled = true;
        return;
    }
    finishCancelled();
}

IRecordingSession::State TreelandRecordingSession::state() const
{
    return m_state;
}

QString TreelandRecordingSession::outputPath() const
{
    return m_finalPath;
}

void TreelandRecordingSession::onRecorderStarted()
{
    if (m_state != State::Starting)
        return;
    m_startTimeoutTimer->stop();
    setState(State::Recording);
    m_recordingNotifyTimer->start();
    callRecorderTray(QStringLiteral("onStart"));
    emit started();
}

void TreelandRecordingSession::onRecorderStopped()
{
    if (m_terminal)
        return;

    m_startTimeoutTimer->stop();
    m_recordingNotifyTimer->stop();
    callRecorderTray(QStringLiteral("onStop"));

    if (m_cancelled) {
        finishCancelled();
        return;
    }
    if (!m_pendingError.isEmpty()) {
        finishFailed(m_pendingError);
        return;
    }
    if (!m_stopRequested) {
        finishFailed(QStringLiteral("录屏会话意外结束"));
        return;
    }

    const QFileInfo captureInfo(m_capturePath);
    if (!captureInfo.exists() || captureInfo.size() <= 0) {
        finishFailed(QStringLiteral("录屏文件没有生成或内容为空"));
        return;
    }

    if (m_request.format == Utils::kGIF) {
        startGifTranscode();
        return;
    }
    finishSaved();
}

void TreelandRecordingSession::onRecorderError(const QString &reason)
{
    if (m_terminal)
        return;
    qCritical() << "TreeLand recording failed:" << reason;
    m_pendingError = reason;
    m_stopRequested = true;
    if (m_recorder && m_recorder->state() != ExtCaptureRecorder::Stopped)
        m_recorder->stopRecording();
    else
        finishFailed(reason);
}

void TreelandRecordingSession::onStartTimeout()
{
    if (m_state != State::Starting)
        return;
    m_pendingError = QStringLiteral("等待录屏启动超时");
    m_stopRequested = true;
    if (m_recorder)
        m_recorder->stopRecording();
    else
        finishFailed(m_pendingError);
}

void TreelandRecordingSession::finishSaved()
{
    if (m_terminal)
        return;
    m_terminal = true;
    setState(State::Completed);
    notifySaved(m_finalPath);
    if (!copyToClipboard(m_finalPath)) {
        qWarning() << "TreeLand recording clipboard handoff failed:"
                   << m_clipboardError;
    }
    cleanupRecorder();
    emit saved(m_finalPath);
}

void TreelandRecordingSession::finishCancelled()
{
    if (m_terminal)
        return;
    m_terminal = true;
    m_startTimeoutTimer->stop();
    m_recordingNotifyTimer->stop();
    QFile::remove(m_capturePath);
    if (m_capturePath != m_finalPath)
        QFile::remove(m_finalPath);
    setState(State::Cancelled);
    cleanupRecorder();
    emit cancelled();
}

void TreelandRecordingSession::finishFailed(const QString &reason)
{
    if (m_terminal)
        return;
    m_terminal = true;
    m_startTimeoutTimer->stop();
    m_recordingNotifyTimer->stop();
    callRecorderTray(QStringLiteral("onStop"));
    if (!m_capturePath.isEmpty())
        QFile::remove(m_capturePath);
    if (!m_finalPath.isEmpty() && m_finalPath != m_capturePath)
        QFile::remove(m_finalPath);
    setState(State::Failed);
    cleanupRecorder();
    emit failed(reason);
}

void TreelandRecordingSession::startGifTranscode()
{
    setState(State::Stopping);
    auto *process = new QProcess(this);
    const int fps = qMax(5, qMin(m_request.frameRate, 30));
    const QStringList arguments{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("warning"),
        QStringLiteral("-y"),
        QStringLiteral("-i"), m_capturePath,
        QStringLiteral("-vf"),
        QStringLiteral("fps=%1,scale=iw:-1:flags=lanczos").arg(fps),
        QStringLiteral("-loop"), QStringLiteral("0"),
        m_finalPath
    };
    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, process](int exitCode, QProcess::ExitStatus) {
                const QString stderrText = QString::fromLocal8Bit(process->readAllStandardError());
                process->deleteLater();
                if (exitCode != 0 || !QFileInfo::exists(m_finalPath)) {
                    QFile::remove(m_finalPath);
                    finishFailed(QStringLiteral("GIF 转码失败：%1").arg(stderrText.trimmed()));
                    return;
                }
                QFile::remove(m_capturePath);
                finishSaved();
            });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        if (process->state() == QProcess::NotRunning)
            finishFailed(QStringLiteral("无法启动 GIF 转码进程"));
    });
    process->start(QStringLiteral("ffmpeg"), arguments);
}

void TreelandRecordingSession::notifySaved(const QString &path)
{
    if (Utils::isRootUser)
        return;

    QDBusInterface notification(QStringLiteral("org.freedesktop.Notifications"),
                                 QStringLiteral("/org/freedesktop/Notifications"),
                                 QStringLiteral("org.freedesktop.Notifications"),
                                 QDBusConnection::sessionBus());
    const QStringList actions{QStringLiteral("_open"), tr("View"),
                              QStringLiteral("_open1"), tr("Open Folder")};
    QVariantMap hints;
    hints[QStringLiteral("x-deepin-action-_open")] = QStringList{QStringLiteral("xdg-open"), path};
    hints[QStringLiteral("x-deepin-action-_open1")] =
            QStringList{QStringLiteral("dde-file-manager"), QStringLiteral("--show-item"), path};
    notification.callWithArgumentList(
            QDBus::AutoDetect,
            QStringLiteral("Notify"),
            QList<QVariant>{Utils::appName,
                            0u,
                            QStringLiteral("deepin-screen-recorder"),
                            tr("Recording finished"),
                            tr("Saved to %1").arg(path),
                            actions,
                            hints,
                            -1});
}

bool TreelandRecordingSession::copyToClipboard(const QString &path)
{
    return launchTreelandClipboardHolder(QStringLiteral("file"),
                                         path,
                                         &m_clipboardError);
}

QString TreelandRecordingSession::createOutputPath(int format) const
{
    QString extension = QStringLiteral("mp4");
    if (format == Utils::kMKV)
        extension = QStringLiteral("mkv");
    else if (format == Utils::kGIF)
        extension = QStringLiteral("gif");

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    return QDir(recordingSaveDirectory())
            .filePath(QStringLiteral("%1_%2.%3").arg(tr("Record"), timestamp, extension));
}

void TreelandRecordingSession::setState(State state)
{
    m_state = state;
}

void TreelandRecordingSession::cleanupRecorder()
{
    if (!m_recorder)
        return;
    m_recorder->disconnect(this);
    m_recorder->deleteLater();
    m_recorder = nullptr;
}

} // namespace Dsr::Screenshot
