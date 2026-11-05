/*
 * SPDX-FileCopyrightText: 2007-2010 Peter Penz <peter.penz19@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TERMINALPANEL_H
#define TERMINALPANEL_H

#include "kiofuse_interface.h"
#include "panels/panel.h"

#include <QQueue>

class KActionCollection;
class KMessageWidget;
class QQuickWidget;
class QVBoxLayout;

namespace KIO
{
class StatJob;
}

class KJob;
/**
 * @brief Shows the terminal which is synchronized with the URL of the
 *        active view.
 */
class TerminalPanel : public Panel
{
    Q_OBJECT

public:
    explicit TerminalPanel(QWidget *parent = nullptr);
    ~TerminalPanel() override;

    /**
     * @brief This function is used to set the terminal panels's cwd to
     *        home when an unmounting request is received.
     */
    void goHome();
    bool currentWorkingDirectoryIsChildOf(const QString &path) const;
    bool isHiddenInVisibleWindow() const;
    bool terminalHasFocus() const;
    bool hasProgramRunning() const;
    QString runningProgramName() const;
    KActionCollection *actionCollection();
    /**
     * @brief Keeps what the shortcuts dialog changed in actionCollection().
     */
    void saveShortcuts();

public Q_SLOTS:
    void terminalExited();
    void dockVisibilityChanged();
    void switchSync(bool syncUrl);
    void setSwitchTerminalUrlSyncAction(QAction *urlToggle);

Q_SIGNALS:
    void hideTerminalPanel();

    /**
     * Is emitted if the an URL change is requested.
     */
    void changeUrl(const QUrl &url);

protected:
    bool urlChanged() override;

    void showEvent(QShowEvent *event) override;

private Q_SLOTS:
    void slotMostLocalUrlResult(KJob *job);
    void slotKonsolePartCurrentDirectoryChanged(const QString &dir);

private:
    enum class HistoryPolicy { AddToHistory, SkipHistory };

    void changeDir(const QUrl &url);
    void emitUrlChanged(const QUrl &url);
    void sendCdToTerminal(const QString &path, HistoryPolicy addToHistory = HistoryPolicy::AddToHistory);
    void sendCdToTerminalKIOFuse(const QUrl &url);

private:
    bool m_clearTerminal;
    bool m_creatingTerminal = false;
    bool m_syncUrl;
    QUrl m_url;
    KIO::StatJob *m_mostLocalUrlJob;

    QVBoxLayout *m_layout;
    // The root of org.sonicde.terminal.app's TerminalArea, used by name: Ecco doesn't link
    // the terminal's libraries.
    QObject* m_terminal;
    QQuickWidget* m_terminalWidget;
    KMessageWidget *m_konsolePartMissingMessage;
    QString m_konsolePartCurrentDirectory;
    QQueue<QString> m_sendCdToTerminalHistory;
    org::kde::KIOFuse::VFS m_kiofuseInterface;
    QAction *m_switchTerminalUrlSyncAction;
};

#endif // TERMINALPANEL_H
