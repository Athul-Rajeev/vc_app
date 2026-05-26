#pragma once

#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <functional>

class QApplication;
class QQmlApplicationEngine;
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

    // ── Thread-safe UI setters ─────────────────────────────────
    void setChannels(const std::vector<std::pair<int,std::string>>& textChannels,
                     const std::vector<std::pair<int,std::string>>& voiceChannels);

    void appendChatMessage(const std::string& message);
    void setChatHistory(const std::vector<std::string>& messages);

    /// Each entry: "username:isMuted(0|1):isDeafened(0|1):uuid:channelId"
    void setVoicePeers(const std::vector<std::string>& peerDataList);

    /// Mark UUID as actively speaking; indicator decays after ~300 ms.
    void markSpeakerActive(const std::string& uuid);

    void setUiUpdateCallback(std::function<void()> callback);

private:
    int    m_argc;
    char*  m_argv[2];
    char   m_argv0[8];

    std::unique_ptr<QApplication>          m_app;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    DiscordBackend*                        m_backend = nullptr; // owned by engine
    bool                                   m_initialized = false;

    std::function<void()> m_uiCallback;
};
