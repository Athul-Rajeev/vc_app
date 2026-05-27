#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QStringList>
#include <QMutex>
#include <QQueue>
#include <QDateTime>
#include <QMap>
#include <QAtomicInteger>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>
#include <atomic>
#include <string>
#include <vector>
#include <utility>
#include <functional>

/**
 * DiscordBackend
 * ──────────────
 * Exposed to QML as the context property "backend".
 * All setters are thread-safe (emit queued signals to the main thread).
 * All Q_PROPERTYs are read from the main thread by QML.
 */

 struct LoginRequest
{
    std::string username;
    std::string password;
};

class DiscordBackend : public QObject
{
    std::function<void()> m_uiCallback;
    void notifyUiChanged();

    Q_OBJECT

    Q_PROPERTY(QString  username             READ username             NOTIFY usernameChanged)
    Q_PROPERTY(bool     isLoggedIn           READ isLoggedIn           NOTIFY isLoggedInChanged)
    Q_PROPERTY(bool     isMuted              READ isMuted              NOTIFY isMutedChanged)
    Q_PROPERTY(bool     isDeafened           READ isDeafened           NOTIFY isDeafenedChanged)
    Q_PROPERTY(int      selectedTextChannelId READ selectedTextChannelId NOTIFY selectedTextChannelIdChanged)
    Q_PROPERTY(int      activeVoiceChannelId  READ activeVoiceChannelId  NOTIFY activeVoiceChannelIdChanged)
    Q_PROPERTY(QVariantList textChannels     READ textChannels         NOTIFY textChannelsChanged)
    Q_PROPERTY(QVariantList voiceChannels    READ voiceChannels        NOTIFY voiceChannelsChanged)
    Q_PROPERTY(QVariantList chatMessages     READ chatMessages         NOTIFY chatMessagesChanged)

public:
    explicit DiscordBackend(QObject* parent = nullptr);
    ~DiscordBackend() override;

    // ── Property reads ──────────────────────────────────────────
    QString      username()              const;
    bool         isLoggedIn()            const;
    bool         isMuted()               const;
    bool         isDeafened()            const;
    int          selectedTextChannelId() const;
    int          activeVoiceChannelId()  const;
    QVariantList textChannels()          const;
    QVariantList voiceChannels()         const;
    QVariantList chatMessages()          const;

    // ── QML-invokable actions ───────────────────────────────────
    Q_INVOKABLE void login(const QString& user, const QString& pass);    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void toggleDeafen();
    Q_INVOKABLE void joinVoiceChannel(int channelId);
    Q_INVOKABLE void leaveVoiceChannel();
    Q_INVOKABLE void selectTextChannel(int channelId);
    Q_INVOKABLE void requestNewTextChannel(const QString& name);
    Q_INVOKABLE void requestNewVoiceChannel(const QString& name);
    Q_INVOKABLE QString avatarColor(const QString& name) const;
    Q_INVOKABLE QString channelNameForId(int id) const;

    void confirmLogin(const std::string& username);
    void confirmLogout();

    // ── Thread-safe setters (call from any thread) ─────────────
    void setChannels(const std::vector<std::pair<int,std::string>>& text,
                     const std::vector<std::pair<int,std::string>>& voice);
    void appendChatMessage(const std::string& message);
    void setChatHistory(const std::vector<std::string>& messages);
    void setVoicePeers(const std::vector<std::string>& peerDataList);
    void markSpeakerActive(const std::string& uuid);
    void setNotifyCallback(std::function<void()> cb);

    // ── Output queues (call from any thread) ───────────────────
    std::string dequeuePendingMessage();
    std::string dequeuePendingTextChannel();
    std::string dequeuePendingVoiceChannel();
    bool dequeuePendingLogin(LoginRequest& outRequest);
    bool dequeuePendingLogout();

signals:
    void usernameChanged();
    void isLoggedInChanged();
    void isMutedChanged();
    void isDeafenedChanged();
    void selectedTextChannelIdChanged();
    void activeVoiceChannelIdChanged();
    void textChannelsChanged();
    void voiceChannelsChanged();
    void chatMessagesChanged();
    void requestScrollToBottom();

    // Internal cross-thread relay signals
    void _setChannels(QVariantList text, QVariantList voice);
    void _appendMessage(QVariantMap msg);
    void _setHistory(QVariantList msgs);
    void _setPeers(QVariantList peers);
    void _markSpeaker(QString uuid);

private slots:
    void onSetChannels(QVariantList text, QVariantList voice);
    void onAppendMessage(QVariantMap msg);
    void onSetHistory(QVariantList msgs);
    void onSetPeers(QVariantList peers);
    void onMarkSpeaker(QString uuid);
    void onSpeakerDecay();

private:
    void rebuildVoiceChannelsWithPeers();
    static QVariantMap parsePeerEntry(const QString& entry);

    // ── State (main-thread read, atomic write) ──────────────────
    QString           m_username;
    std::atomic<bool> m_isLoggedIn           {false};
    std::atomic<bool> m_isMuted              {false};
    std::atomic<bool> m_isDeafened           {false};
    std::atomic<int>  m_selectedTextChannelId {1};
    std::atomic<int>  m_activeVoiceChannelId  {-1};

    // ── Model data (main-thread only) ───────────────────────────
    QVariantList m_textChannels;
    QVariantList m_voiceChannelDefs;   // raw {id, name} list
    QVariantList m_voiceChannels;      // with peers embedded
    QVariantList m_chatMessages;
    QVariantList m_voicePeers;         // flat peer list
    QMap<QString, qint64> m_speakerTimestamps;

    // ── Output queues (guarded by m_queueMutex) ─────────────────
    mutable QMutex m_queueMutex;
    QQueue<LoginRequest> m_pendingLogins;
    QQueue<QString> m_pendingMessages;
    QQueue<QString> m_pendingTextChannels;
    QQueue<QString> m_pendingVoiceChannels;

    bool m_pendingLogout{false};

    QTimer* m_speakerTimer = nullptr;

    static constexpr qint64 kSpeakerMs = 300;
    static constexpr char   kSettingsFile[] = "voicechat_user.cfg";

    static const QStringList kAvatarColors;
};