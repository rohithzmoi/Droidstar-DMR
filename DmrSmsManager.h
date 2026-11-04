/*
    Copyright (C) 2026 Rohith Namboothiri
    GPL-3.0-or-later
*/

#ifndef DMRSMSMANAGER_H
#define DMRSMSMANAGER_H

#include <QObject>
#include <QMutex>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class DroidStar;
class VUIDUpdater;

class DmrSmsManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList threads READ threads NOTIFY threadsChanged)
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)
    Q_PROPERTY(int defaultFormat READ defaultFormat WRITE setDefaultFormat NOTIFY settingsChanged)
    Q_PROPERTY(bool sendPreambleCsbk READ sendPreambleCsbk WRITE setSendPreambleCsbk NOTIFY settingsChanged)
    Q_PROPERTY(double keyboardHeight READ keyboardHeight NOTIFY keyboardHeightChanged)

public:
    enum MessageStatus {
        Sending = 0,
        Sent = 1,
        Delivered = 2,
        Failed = 3,
        Received = 4
    };
    Q_ENUM(MessageStatus)

    explicit DmrSmsManager(DroidStar *droidStar,
                           VUIDUpdater *vuidUpdater,
                           QObject *parent = nullptr);
    ~DmrSmsManager() override;

    static DmrSmsManager *instance();

    QVariantList threads() const;
    int unreadCount() const;
    int defaultFormat() const { return m_defaultFormat; }
    void setDefaultFormat(int format);
    bool sendPreambleCsbk() const { return m_sendPreambleCsbk; }
    void setSendPreambleCsbk(bool enabled);
    double keyboardHeight() const { return m_keyboardHeight; }
    void updateNativeKeyboardHeight(double heightPts);

    Q_INVOKABLE void setChatKeyboardMode(bool active);
    Q_INVOKABLE int sendMessage(const QString &targetIdOrCall,
                                const QString &text,
                                bool isGroup,
                                int formatOverride = -1);
    Q_INVOKABLE int retryMessage(int msgId);
    Q_INVOKABLE void deleteMessage(int msgId);
    Q_INVOKABLE void copyToClipboard(const QString &text) const;
    Q_INVOKABLE QVariantList messagesForThread(const QString &threadKey) const;
    Q_INVOKABLE void markThreadRead(const QString &threadKey);
    Q_INVOKABLE void deleteThread(const QString &threadKey);
    Q_INVOKABLE void clearAllHistory();
    Q_INVOKABLE QVariantMap resolveTarget(const QString &input, bool isGroup = false) const;
    Q_INVOKABLE QVariantMap ensureThreadMeta(uint32_t id, bool isGroup);
    Q_INVOKABLE QVariantList searchRecipients(const QString &query,
                                              bool isGroup,
                                              const QString &hostName = QString()) const;
    Q_INVOKABLE QString threadKeyFor(uint32_t id, bool isGroup) const;
    Q_INVOKABLE void openThread(const QString &threadKey);
    Q_INVOKABLE void setActiveThread(const QString &threadKey);
    Q_INVOKABLE uint32_t aprsServiceIdForHost(const QString &hostName) const;
    Q_INVOKABLE bool canSend() const;

signals:
    void threadsChanged();
    void unreadCountChanged();
    void settingsChanged();
    void keyboardHeightChanged();
    void messageReceived(uint32_t srcId, const QString &senderLabel,
                         const QString &preview, bool isGroup,
                         const QString &threadKey);
    void threadRequested(const QString &threadKey);
    void operationError(const QString &message);

private slots:
    void onSmsReceived(uint32_t srcId, uint32_t dstId, const QString &text,
                       bool isGroup, const QString &format);
    void onSmsSendStatus(int msgId, int status, const QString &detail);
    void onSmsAckReceived(uint32_t srcId, bool success);
    void onUserLookupReady(unsigned int dmrId, const QString &callsign,
                           const QString &name, const QString &country);
    void flushSaveAsync();

private:
    QString storagePath() const;
    void load();
    void saveSync() const;
    void scheduleSave();
    void trimHistoryLocked();
    void rebuildCachesLocked();
    int nextMessageId();
    static bool isAprsGatewayId(uint32_t id);
    static bool isServiceId(uint32_t id);
    QString labelForId(uint32_t id, bool isGroup = false) const;
    QString subtitleForId(uint32_t id, bool isGroup = false) const;
    static QString formatDisplayTime(const QString &isoTimestamp);
    static QString previewFor(const QString &text);
    static QString timestampNow();
    void notifyModels(int oldUnread);

    DroidStar *m_droidStar { nullptr };
    VUIDUpdater *m_vuidUpdater { nullptr };
    mutable QMutex m_mutex;
    QVariantList m_messages;
    QVariantList m_threadsCache;
    int m_unreadCountCache { 0 };
    int m_nextId { 1 };
    int m_defaultFormat { 0 };
    bool m_sendPreambleCsbk { true };
    double m_keyboardHeight { 0.0 };
    QString m_openThread;
    QTimer *m_saveTimer { nullptr };
};

#endif // DMRSMSMANAGER_H
