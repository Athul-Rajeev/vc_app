#pragma once

#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <functional>

class QApplication;
class QQmlApplicationEngine;
class QQuickWindow;
class DiscordBackend;

class WindowManager
{
public:
    WindowManager();
    ~WindowManager();

    // ── Lifecycle ──────────────────────────────────────────────
    bool initialize();    ///< Creates QApplication, QML engine, and window.
    void cleanup();       ///< Destroys engine + app. Called by dtor if needed
    int exec();

    // ── State queries (thread-safe) ────────────────────────────
    bool        isMuted()                  const;
    bool        isDeafened()               const;
    bool        isLoggedIn()               const;
    int         getSelectedTextChannelId() const;
    int         getActiveVoiceChannelId()  const;
    std::string getUsername()              const;

    // ── Pending input queues (thread-safe) ─────────────────────
    std::string getPendingOutgoingMessage();
    std::string getPendingNewTextChannel();
    std::string getPendingNewVoiceChannel();
    bool getPendingLogin(std::string& outUser, std::string& outPass);
    bool getPendingRegistration(std::string& outUser, std::string& outPass);
    bool getPendingLogout();
    bool getPendingPasswordChange(std::string& outCurrentPassword, std::string& outNewPassword);
    bool getPendingPasswordReset(std::string& outUser, std::string& outResetToken, std::string& outNewPassword);

    /// Current RMS threshold for voice activation (idempotently applied by the control loop).
    double getVoiceActivationThreshold() const;

    // ── Thread-safe UI setters ─────────────────────────────────
    /// int is a DiscordBackend::LoginState value; opaque here to avoid a circular include.
    void setLoginState(int loginState);

    void confirmLogin(const std::string& username);
    void confirmLogout();
    void setLoginError(const std::string& error);
    void setPasswordNotice(const std::string& notice);

    // ── Thread-safe UI setters ─────────────────────────────────
    void setChannels(const std::vector<std::pair<int,std::string>>& textChannels,
                     const std::vector<std::pair<int,std::string>>& voiceChannels);

    void appendChatMessage(int channelId, const std::string& message, long long timestampMs = 0);
    void setChatHistory(int channelId, bool hasMoreHistory, const std::vector<std::pair<std::string, long long>>& messages);
    void prependChatHistory(int channelId, bool hasMoreHistory, const std::vector<std::pair<std::string, long long>>& messages);
    bool getPendingHistoryRequest(int& outChannelId, long long& outBeforeTimestampMs);

    /// Each entry: "username:isMuted(0|1):isDeafened(0|1):uuid:channelId"
    void setVoicePeers(const std::vector<std::string>& peerDataList);

    /// Mark UUID as actively speaking; indicator decays after ~300 ms.
    void markSpeakerActive(const std::string& uuid);

    void setUiUpdateCallback(std::function<void()> callback);

private:
    /// Platforms the window frame ourselves where the OS allows it: tells
    /// Windows to draw a dark title bar/caption matching the app theme.
    /// No-op on other platforms (their system theme decides).
    void applyDarkTitleBar(QQuickWindow* window);

    int    m_argc;
    char*  m_argv[2];
    char   m_argv0[8];

    std::unique_ptr<QApplication>          m_app;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    DiscordBackend*                        m_backend = nullptr; // owned by engine
    bool                                   m_initialized = false;

    std::function<void()> m_uiCallback;
};
