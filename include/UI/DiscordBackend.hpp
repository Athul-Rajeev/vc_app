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
#include <QAbstractListModel>
#include "UI/ChatMessageModel.hpp"
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

/// Credentials submitted by the login or registration screen; shared shape.
/// (For a pending password change, username holds the *current* password.)
struct AuthRequest
{
    std::string username;
    std::string password;
};

/// One-time admin-issued reset token plus the new credentials it unlocks.
struct PasswordResetRequest
{
    std::string username;
    std::string resetToken;
    std::string newPassword;
};

/// Auth UI machine: drives the login overlay's spinner and form visibility.
enum class LoginState
{
    Disconnected  = 0,   // Idle: show the login form
    Connecting    = 1,   // TCP handshake in progress (retries invisible to the user)
    Authenticating = 2,  // Credentials sent, waiting for the server's verdict
    LoggedIn      = 3,
};

class DiscordBackend : public QObject
{
    std::function<void()> m_uiCallback;
    void notifyUiChanged();

    Q_OBJECT

    Q_PROPERTY(QString      username             READ username             NOTIFY usernameChanged)
    Q_PROPERTY(bool     isLoggedIn           READ isLoggedIn           NOTIFY isLoggedInChanged)
    Q_PROPERTY(bool     isMuted              READ isMuted              NOTIFY isMutedChanged)
    Q_PROPERTY(bool     isDeafened           READ isDeafened           NOTIFY isDeafenedChanged)
    Q_PROPERTY(int      selectedTextChannelId READ selectedTextChannelId NOTIFY selectedTextChannelIdChanged)
    Q_PROPERTY(int      activeVoiceChannelId  READ activeVoiceChannelId  NOTIFY activeVoiceChannelIdChanged)
    Q_PROPERTY(QVariantList textChannels     READ textChannels         NOTIFY textChannelsChanged)
    Q_PROPERTY(QVariantList voiceChannels    READ voiceChannels        NOTIFY voiceChannelsChanged)
    Q_PROPERTY(QAbstractListModel* chatMessageModel READ chatMessageModel CONSTANT)
    Q_PROPERTY(bool  hasMoreHistory READ hasMoreHistory NOTIFY hasMoreHistoryChanged)
    Q_PROPERTY(bool  historyLoading READ historyLoading NOTIFY historyLoadingChanged)
    Q_PROPERTY(QString      loginError       READ loginError           NOTIFY loginErrorChanged)
    Q_PROPERTY(QString      passwordNotice   READ passwordNotice       NOTIFY passwordNoticeChanged)
    Q_PROPERTY(QString      loginState       READ loginState           NOTIFY loginStateChanged)
    Q_PROPERTY(int      voiceActivationSensitivity READ voiceActivationSensitivity NOTIFY voiceActivationSensitivityChanged)

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
    QAbstractListModel* chatMessageModel();
    bool         hasMoreHistory()        const;
    bool         historyLoading()        const;
    QString      loginError()            const;
    QString      passwordNotice()        const;
    QString      loginState()            const;   ///< Mapped LoginState string for QML
    int          voiceActivationSensitivity() const;

    /// RMS threshold for the AudioEngine's VAD, derived from the 0–100 sensitivity scale.
    double voiceActivationThreshold() const;

    // ── QML-invokable actions ───────────────────────────────────
    Q_INVOKABLE void login(const QString& user, const QString& pass);
    Q_INVOKABLE void registerAccount(const QString& user, const QString& pass, const QString& confirmPass);
    Q_INVOKABLE void resetPassword(const QString& user, const QString& resetToken, const QString& newPass, const QString& confirmPass);
    Q_INVOKABLE void changePassword(const QString& currentPass, const QString& newPass, const QString& confirmPass);
    Q_INVOKABLE void clearLoginError();
    Q_INVOKABLE void clearPasswordNotice();
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void toggleDeafen();
    Q_INVOKABLE void joinVoiceChannel(int channelId);
    Q_INVOKABLE void leaveVoiceChannel();
    Q_INVOKABLE void selectTextChannel(int channelId);
    Q_INVOKABLE void requestNewTextChannel(const QString& name);
    Q_INVOKABLE void requestNewVoiceChannel(const QString& name);
    Q_INVOKABLE void requestOlderMessages();   ///< Fetch the next page above the oldest loaded row
    Q_INVOKABLE void setVoiceActivationSensitivity(int sensitivity);
    Q_INVOKABLE QString avatarColor(const QString& name) const;
    Q_INVOKABLE QString channelNameForId(int id) const;

    void confirmLogin(const std::string& username);
    void confirmLogout();

    // ── Thread-safe setters (call from any thread) ─────────────
    void setChannels(const std::vector<std::pair<int,std::string>>& text,
                     const std::vector<std::pair<int,std::string>>& voice);
    void appendChatMessage(int channelId, const std::string& message, qint64 timestampMs = 0);
    /// Replaces the model with one channel's initial page (path AUTH: first history fetch).
    void setChatHistory(int channelId, bool hasMoreHistory, const std::vector<std::pair<std::string, qint64>>& messages);
    /// Inserts an "older messages" page ABOVE the current model content.
    void prependChatHistory(int channelId, bool hasMoreHistory, const std::vector<std::pair<std::string, qint64>>& messages);
    void setVoicePeers(const std::vector<std::string>& peerDataList);
    void markSpeakerActive(const std::string& uuid);
    void setNotifyCallback(std::function<void()> cb);
    void setLoginError(const QString& error);
    void setAuthError(const std::string& serverReason);       ///< Curates a raw server failure reason before display
    void setPasswordNotice(const std::string& serverNotice);  ///< Curates password-change feedback before display
    void displayPasswordNotice(const QString& notice);        ///< Shows a pre-curated, client-local notice as-is
    void setLoginState(LoginState state);

    // ── Output queues (call from any thread) ───────────────────
    std::string dequeuePendingMessage();
    std::string dequeuePendingTextChannel();
    std::string dequeuePendingVoiceChannel();
    bool dequeuePendingLogin(AuthRequest& outRequest);
    bool dequeuePendingRegistration(AuthRequest& outRequest);
    bool dequeuePendingPasswordChange(AuthRequest& outRequest);
    bool dequeuePendingPasswordReset(PasswordResetRequest& outRequest);
    bool dequeuePendingLogout();
    bool dequeuePendingHistoryRequest(int& outChannelId, qint64& outBeforeTimestampMs);

signals:
    void usernameChanged();
    void isLoggedInChanged();
    void isMutedChanged();
    void isDeafenedChanged();
    void selectedTextChannelIdChanged();
    void activeVoiceChannelIdChanged();
    void textChannelsChanged();
    void voiceChannelsChanged();
    void hasMoreHistoryChanged();
    void historyLoadingChanged();
    void requestScrollToBottom();
    void loginErrorChanged();
    void passwordNoticeChanged();
    void loginStateChanged();
    void voiceActivationSensitivityChanged();

    // Internal cross-thread relay signals
    void _setChannels(QVariantList text, QVariantList voice);
    void _appendMessage(int channelId, QVariantMap msg);
    void _setHistory(int channelId, bool hasMoreHistory, QVariantList msgs);
    void _prependHistory(int channelId, bool hasMoreHistory, QVariantList msgs);
    void _setPeers(QVariantList peers);
    void _markSpeaker(QString uuid);

    // Internal queued signals for thread‑safe state updates
    void _setLoginError(QString error);
    void _setLoginState(int state);
    void _setPasswordNotice(QString notice);
    void _confirmLogin(QString username);
    void _confirmLogout();

private slots:
    void onSetChannels(QVariantList text, QVariantList voice);
    void onAppendMessage(int channelId, QVariantMap msg);
    void onSetHistory(int channelId, bool hasMoreHistory, QVariantList msgs);
    void onPrependHistory(int channelId, bool hasMoreHistory, QVariantList msgs);
    void onSetPeers(QVariantList peers);
    void onMarkSpeaker(QString uuid);
    void onVoiceRebuildTick();
    void onSpeakerDecay();
    void onSetLoginError(QString error);
    void onSetLoginState(int state);
    void onSetPasswordNotice(QString notice);
    void onConfirmLogin(QString username);
    void onConfirmLogout();

private:
    void rebuildVoiceChannelsWithPeers();
    void scheduleDecayCheck();
    void scheduleVoiceRebuild();   ///< Debounced rebuild: voice packets update indicators at most ~10 Hz
    void loadSettingsFromFile();
    void saveSettingsToFile();
    static QVariantMap parsePeerEntry(const QString& entry);
    static QString avatarColorFor(const QString& name);   ///< Deterministic per-author color (shared by rows/avatars)
    static ChatMessageEntry buildChatEntry(const QString& rawMessage, qint64 timestampMs);
    static QVariantMap chatEntryToMap(const ChatMessageEntry& entry);

    // ── State (main-thread read, atomic write) ──────────────────
    QString           m_username;
    QString           m_loginError;
    QString           m_passwordNotice;
    std::atomic<int>  m_loginState {static_cast<int>(LoginState::Disconnected)};
    std::atomic<bool> m_isLoggedIn           {false};
    std::atomic<bool> m_isMuted              {false};
    std::atomic<bool> m_isDeafened           {false};
    std::atomic<int>  m_selectedTextChannelId {1};
    std::atomic<int>  m_activeVoiceChannelId  {-1};
    std::atomic<int>  m_voiceActivationSensitivity {kDefaultVoiceActivationSensitivity};

    // ── Model data (main-thread only) ───────────────────────────
    QVariantList m_textChannels;
    QVariantList m_voiceChannelDefs;   // raw {id, name} list
    QVariantList m_voiceChannels;      // with peers embedded
    ChatMessageModel m_chatMessageModel;
    QVariantList m_voicePeers;         // flat peer list
    QMap<QString, qint64> m_speakerTimestamps;

    // ── History paging state (main thread) ─────────────────────
    std::atomic<bool> m_hasMoreHistory    {false};
    std::atomic<bool> m_historyLoadInFlight {false};
    std::atomic<int>  m_loadedChannelId   {-1};   ///< Channel whose messages the model currently holds

    // ── Output queues (guarded by m_queueMutex) ─────────────────
    mutable QMutex m_queueMutex;
    QQueue<AuthRequest> m_pendingLogins;
    QQueue<AuthRequest> m_pendingRegistrations;
    QQueue<AuthRequest> m_pendingPasswordChanges;
    QQueue<PasswordResetRequest> m_pendingPasswordResets;
    QQueue<QString> m_pendingMessages;
    QQueue<QString> m_pendingTextChannels;
    QQueue<QString> m_pendingVoiceChannels;
    QQueue<std::pair<int, qint64>> m_pendingHistoryRequests;   // {channelId, beforeTimestampMs}

    bool m_pendingLogout{false};

    QTimer* m_decayTimer;
    QTimer* m_rebuildTimer;   ///< Paces voice-channel model rebuilds (speaker churn)

    static constexpr qint64 kSpeakerMs = 300;
    static constexpr int    kVoiceRebuildIntervalMs = 100;   // >=10 Hz worst-case indicator updates
    static constexpr int    kChatHistoryMemoryLimit = 200;   // In-memory rows per channel (paged beyond)
    static constexpr char   kSettingsFile[] = "voicechat_user.cfg";
    static constexpr int    kDefaultVoiceActivationSensitivity = 41;  ///< Maps to ~0.015 RMS
    static constexpr int    kMaxVoiceActivationSensitivity = 100;

    static const QStringList kAvatarColors;
};