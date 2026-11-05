/*
 * SPDX-FileCopyrightText: 2007-2010 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "terminalpanel.h"

#include <KActionCollection>
#include <KIO/DesktopExecParser>
#include <KIO/StatJob>
#include <KJobWidgets>
#include <KLocalizedString>
#include <KMessageWidget>
#include <KMountPoint>
#include <KProtocolInfo>
#include <KShell>

#include <QAction>
#include <QDesktopServices>
#include <QDir>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString callString(QObject* terminal, const char* method)
{
    QString result;
    QMetaObject::invokeMethod(terminal, method, Q_RETURN_ARG(QString, result));
    return result;
}

void sendInputTo(QObject* terminal, const QString& text)
{
    QMetaObject::invokeMethod(terminal, "sendInput", Q_ARG(QString, text));
}
}

TerminalPanel::TerminalPanel(QWidget *parent)
    : Panel(parent)
    , m_clearTerminal(true)
    , m_syncUrl(true)
    , m_mostLocalUrlJob(nullptr)
    , m_layout(nullptr)
    , m_terminal(nullptr)
    , m_terminalWidget(nullptr)
    , m_konsolePartMissingMessage(nullptr)
    , m_konsolePartCurrentDirectory()
    , m_sendCdToTerminalHistory()
    , m_kiofuseInterface(QStringLiteral("org.kde.KIOFuse"), QStringLiteral("/org/kde/KIOFuse"), QDBusConnection::sessionBus())
    , m_switchTerminalUrlSyncAction(nullptr)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
}

TerminalPanel::~TerminalPanel() = default;

void TerminalPanel::goHome()
{
    sendCdToTerminal(QDir::homePath(), HistoryPolicy::SkipHistory);
}

bool TerminalPanel::currentWorkingDirectoryIsChildOf(const QString &path) const
{
    if (m_terminal) {
        return callString(m_terminal, "currentWorkingDirectory").startsWith(path);
    }
    return false;
}

void TerminalPanel::terminalExited()
{
    m_terminal = nullptr;
    setFocusProxy(nullptr);
    m_layout->removeWidget(m_terminalWidget);
    // It is emitting this.
    m_terminalWidget->deleteLater();
    m_terminalWidget = nullptr;
    Q_EMIT hideTerminalPanel();
}

bool TerminalPanel::isHiddenInVisibleWindow() const
{
    return parentWidget() && parentWidget()->isHidden();
}

void TerminalPanel::dockVisibilityChanged()
{
    // Only react when the DockWidget itself (not some parent) is hidden. This way we don't
    // respond when e.g. Dolphin is minimized.
    if (isHiddenInVisibleWindow()) {
        if (m_konsolePartMissingMessage) {
            m_konsolePartMissingMessage->hide();
        } else if (m_terminal && !hasProgramRunning()) {
            // Make sure that the following "cd /" command will not affect the view.
            disconnect(m_terminal, SIGNAL(currentDirectoryChanged(QString)), this, SLOT(slotKonsolePartCurrentDirectoryChanged(QString)));

            // Make sure this terminal does not prevent unmounting any removable drives
            changeDir(QUrl::fromLocalFile(QStringLiteral("/")));

            // Because we have disconnected from the part's currentDirectoryChanged()
            // signal, we have to update m_konsolePartCurrentDirectory manually. If this
            // was not done, showing the panel again might not set the part's working
            // directory correctly.
            m_konsolePartCurrentDirectory = '/';
        }
    }
}

void TerminalPanel::switchSync(bool syncUrl)
{
    m_syncUrl = syncUrl;
}

void TerminalPanel::setSwitchTerminalUrlSyncAction(QAction *urlToggle)
{
    m_switchTerminalUrlSyncAction = urlToggle;
}

QString TerminalPanel::runningProgramName() const
{
    return m_terminal ? callString(m_terminal, "foregroundProcessName") : QString();
}

KActionCollection *TerminalPanel::actionCollection()
{
    if (!m_terminal) {
        return nullptr;
    }
    QObject* collection = nullptr;
    QMetaObject::invokeMethod(m_terminal, "sessionActionCollection", Q_RETURN_ARG(QObject*, collection));
    return qobject_cast<KActionCollection*>(collection);
}

void TerminalPanel::saveShortcuts()
{
    if (m_terminal) {
        QMetaObject::invokeMethod(m_terminal, "saveShortcuts");
    }
}

bool TerminalPanel::hasProgramRunning() const
{
    if (!m_terminal) {
        return false;
    }
    int processId = -1;
    QMetaObject::invokeMethod(m_terminal, "foregroundProcessId", Q_RETURN_ARG(int, processId));
    return processId != -1;
}

bool TerminalPanel::urlChanged()
{
    if (!url().isValid()) {
        return false;
    }

    const bool sendInput = m_terminal && !hasProgramRunning() && isVisible() && m_syncUrl && url() != m_url;
    if (sendInput) {
        changeDir(url());
        m_url = url();
    }

    return true;
}

void TerminalPanel::showEvent(QShowEvent *event)
{
    // A QQuickWidget that uses the GPU makes Qt recreate the window, which shows this again.
    if (event->spontaneous() || m_creatingTerminal) {
        Panel::showEvent(event);
        return;
    }

    if (!m_terminal) {
        m_creatingTerminal = true;
        m_clearTerminal = true;
        // Qt Quick settings must precede the first QML.
        if (QQuickStyle::name().isEmpty()) {
            QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
        }
        QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
        // Drawn with the window's other widgets: on the GPU, every Ecco window would pay for it.
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        auto* terminalWidget = new QQuickWidget(this);
        terminalWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
        terminalWidget->setFocusPolicy(Qt::StrongFocus);
        terminalWidget->loadFromModule("org.sonicde.terminal.app", "TerminalArea");
        if (terminalWidget->status() == QQuickWidget::Ready && terminalWidget->rootObject()) {
            m_terminalWidget = terminalWidget;
            m_terminal = terminalWidget->rootObject();
            connect(m_terminal, SIGNAL(finished()), this, SLOT(terminalExited()));
            setFocusProxy(m_terminalWidget);
            m_layout->addWidget(m_terminalWidget);
            if (m_konsolePartMissingMessage) {
                m_layout->removeWidget(m_konsolePartMissingMessage);
            }

            const QList<QAction *> additional = {m_switchTerminalUrlSyncAction};

            QMetaObject::invokeMethod(m_terminal, "setContextMenuAdditionalActions", Q_ARG(QList<QAction*>, additional));

        } else {
            delete terminalWidget;
            if (!m_konsolePartMissingMessage) {
                const auto konsoleInstallUrl = QUrl("appstream://org.kde.konsole.desktop");
                const auto konsoleNotInstalledText = i18n(
                    "Terminal cannot be shown because Konsole is not installed. "
                    "Please install it and then reopen the panel.");
                m_konsolePartMissingMessage = new KMessageWidget(konsoleNotInstalledText, this);
                m_konsolePartMissingMessage->setPosition(KMessageWidget::Footer);
                m_konsolePartMissingMessage->setCloseButtonVisible(false);
                m_konsolePartMissingMessage->hide();
                if (KIO::DesktopExecParser::hasSchemeHandler(konsoleInstallUrl)) {
                    auto installKonsoleAction = new QAction(i18n("Install Konsole"), this);
                    connect(installKonsoleAction, &QAction::triggered, [konsoleInstallUrl]() {
                        QDesktopServices::openUrl(konsoleInstallUrl);
                    });
                    m_konsolePartMissingMessage->addAction(installKonsoleAction);
                }
                m_layout->addWidget(m_konsolePartMissingMessage);
                m_layout->setSizeConstraint(QLayout::SetMaximumSize);
            }

            m_konsolePartMissingMessage->animatedShow();
        }
        m_creatingTerminal = false;
    }
    if (m_terminal) {
        QMetaObject::invokeMethod(m_terminal, "showShellInDir", Q_ARG(QString, url().toLocalFile()));
        if (!hasProgramRunning()) {
            changeDir(url());
        }
        m_terminalWidget->setFocus();
        connect(m_terminal, SIGNAL(currentDirectoryChanged(QString)), this, SLOT(slotKonsolePartCurrentDirectoryChanged(QString)));
    }

    Panel::showEvent(event);
}

void TerminalPanel::changeDir(const QUrl &url)
{
    delete m_mostLocalUrlJob;
    m_mostLocalUrlJob = nullptr;

    if (url.isEmpty()) {
        return;
    }

    if (url.isLocalFile()) {
        sendCdToTerminal(url.toLocalFile());
        return;
    }

    // Try stat'ing the url; note that mostLocalUrl only works with ":local" protocols
    if (KProtocolInfo::protocolClass(url.scheme()) == QLatin1String(":local")) {
        m_mostLocalUrlJob = KIO::mostLocalUrl(url, KIO::HideProgressInfo);
        if (m_mostLocalUrlJob->uiDelegate()) {
            KJobWidgets::setWindow(m_mostLocalUrlJob, this);
        }
        connect(m_mostLocalUrlJob, &KIO::StatJob::result, this, &TerminalPanel::slotMostLocalUrlResult);
        return;
    }

    // Last chance, try KIOFuse
    sendCdToTerminalKIOFuse(url);
}

void TerminalPanel::emitUrlChanged(const QUrl &url)
{
    if (m_syncUrl) {
        Q_EMIT changeUrl(url);
    }
}

void TerminalPanel::sendCdToTerminal(const QString &dir, HistoryPolicy addToHistory)
{
    if (dir == m_konsolePartCurrentDirectory // We are already there
        && m_sendCdToTerminalHistory.isEmpty() // …and that is not because the terminal couldn't keep up
    ) {
        m_clearTerminal = false;
        return;
    }

    // Send prior Ctrl-E, Ctrl-U to ensure the line is empty. This is
    // mandatory, otherwise sending a 'cd x\n' to a prompt with 'rm -rf *'
    // would result in data loss.
    sendInputTo(m_terminal, QStringLiteral("\x05\x15"));

    // We want to ignore the currentDirectoryChanged(QString) signal, which we will receive after
    // the directory change, because this directory change is not caused by a "cd" command that the
    // user entered in the panel. Therefore, we have to remember 'dir'. Note that it could also be
    // a symbolic link -> remember the 'canonical' path.
    if (addToHistory == HistoryPolicy::AddToHistory)
        m_sendCdToTerminalHistory.enqueue(QDir(dir).canonicalPath());

    sendInputTo(m_terminal, " cd " + KShell::quoteArg(dir) + '\r');

    if (m_clearTerminal) {
        sendInputTo(m_terminal, QStringLiteral(" clear\r"));
        m_clearTerminal = false;
    }
}

void TerminalPanel::sendCdToTerminalKIOFuse(const QUrl &url)
{
    // URL isn't local, only hope for the terminal to be in sync with the
    // DolphinView is to mount the remote URL in KIOFuse and point to it.
    // If we can't do that for any reason, silently fail.
    auto reply = m_kiofuseInterface.mountUrl(url.toString());
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [=, this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        if (!reply.isError()) {
            // Successfully mounted, point to the KIOFuse equivalent path.
            sendCdToTerminal(reply.value());
        }
    });
}

void TerminalPanel::slotMostLocalUrlResult(KJob *job)
{
    KIO::StatJob *statJob = static_cast<KIO::StatJob *>(job);
    const QUrl url = statJob->mostLocalUrl();
    if (url.isLocalFile()) {
        sendCdToTerminal(url.toLocalFile());
    } else {
        sendCdToTerminalKIOFuse(url);
    }

    m_mostLocalUrlJob = nullptr;
}

void TerminalPanel::slotKonsolePartCurrentDirectoryChanged(const QString &dir)
{
    m_konsolePartCurrentDirectory = QDir(dir).canonicalPath();

    // Only emit a changeUrl signal if the directory change was caused by the user inside the
    // terminal, and not by sendCdToTerminal(QString).
    while (!m_sendCdToTerminalHistory.empty()) {
        if (m_konsolePartCurrentDirectory == m_sendCdToTerminalHistory.dequeue()) {
            return;
        }
    }

    // User may potentially be browsing inside a KIOFuse mount.
    // If so lets try and change the DolphinView to point to the remote URL equivalent.
    // instead of into the KIOFuse mount itself (which can cause performance issues!)
    const QUrl url(QUrl::fromLocalFile(dir));

    KMountPoint::Ptr mountPoint = KMountPoint::currentMountPoints().findByPath(m_konsolePartCurrentDirectory);
    if (mountPoint && mountPoint->mountType() != QStringLiteral("fuse.kio-fuse")) {
        // Not in KIOFUse mount, so just switch to the corresponding URL.
        emitUrlChanged(url);
        return;
    }

    auto reply = m_kiofuseInterface.remoteUrl(m_konsolePartCurrentDirectory);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(reply, this);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this, [=, this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        if (reply.isError()) {
            // KIOFuse errored out... just show the normal URL
            emitUrlChanged(url);
        } else {
            // Our location happens to be in a KIOFuse mount and is mounted.
            // Let's change the DolphinView to point to the remote URL equivalent.
            emitUrlChanged(QUrl::fromUserInput(reply.value()));
        }
    });
}

bool TerminalPanel::terminalHasFocus() const
{
    if (m_terminalWidget) {
        return m_terminalWidget->hasFocus();
    }

    return hasFocus();
}

#include "moc_terminalpanel.cpp"
