#pragma once

#include <QObject>
#include <QString>

#include "NotificationType.h"

class NotificationSettings;

// The runtime dispatcher every feature calls into to raise a popup or drive a progress-bar
// notification -- a single shared instance, analogous to TagManagementUseCase in spirit
// (stateless-ish, one instance for the whole app) but a QObject since it emits the signals
// NotificationOverlayWidget listens to. Constructed with a const NotificationSettings& (not the
// Qt-facing NotificationSettingsViewModel) so it can check enabled/timeout synchronously on every
// call with zero dependency on adapters/config -- see the module-level note in the plan for why
// this class doesn't live in adapters/config.
//
// Qt's signal/slot auto-connection already queues delivery across threads (the receiver's thread
// affinity decides, not the emitter's), so updateProgressBar/terminateProgressNotification are
// safe to call from a future background copy/move worker thread with no extra marshaling in this
// class itself. Untested per Architecture.md §11's existing UI/ViewModel-glue convention (same as
// TagListViewModel/WorkspaceController) -- it's a thin QObject signal hub, not business logic.
class NotificationCenter : public QObject
{
    Q_OBJECT

public:
    using ProgressId = int;

    explicit NotificationCenter(const NotificationSettings& settings, QObject* parent = nullptr);

    // No-op (never emits) if settingsFor(type).enabled is false.
    void showPopup(NotificationType type, const QString& title, const QString& body);

    // Progress-bar notifications (factory pattern): the caller asks for barCount bars and gets
    // back an id, then asynchronously updates a given bar's current/max and its three text rows
    // (above/progress/below) via that id, and can terminate the whole notification via the id.
    // `cancellable`, when true, adds
    // a Cancel button to the notification card; clicking it emits progressCancelRequested(id) --
    // see requestCancel() below. Wired to the custom copy/move pipeline (Architecture.md §14.47);
    // the mechanism stays generic for any future long-running operation to call into.
    ProgressId createProgressNotification(const QString& title, int barCount, bool cancellable = false);
    void updateProgressBar(ProgressId id, int barIndex, int current, int max, const QString& aboveText,
                            const QString& progressText, const QString& belowText);
    void terminateProgressNotification(ProgressId id);

    // Called by NotificationOverlayWidget when the user clicks a cancellable notification's Cancel
    // button; simply forwards as a signal so whoever owns that progress id (FileOperationsController)
    // can react. A no-op from this class's own point of view beyond emitting.
    void requestCancel(ProgressId id);

signals:
    void popupRequested(NotificationType type, const QString& title, const QString& body, int timeoutMs);
    void progressNotificationCreated(ProgressId id, const QString& title, int barCount, bool cancellable);
    void progressBarUpdated(ProgressId id, int barIndex, int current, int max, const QString& aboveText,
                             const QString& progressText, const QString& belowText);
    void progressNotificationTerminated(ProgressId id);
    void progressCancelRequested(ProgressId id);

private:
    const NotificationSettings& m_settings;
    ProgressId m_nextProgressId = 1;
};
