#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <queue>
#include <chrono>
#include <map>
#include <memory>

struct GLFWwindow;

struct VoicePeer
{
    std::string username;
    bool isMuted;
    bool isDeafened;
    std::string uuid;
    int channelId;
};

struct UiDisplayState
{
    std::vector<std::pair<int, std::string>> textChannelsList;
    std::vector<std::pair<int, std::string>> voiceChannelsList;
    std::vector<VoicePeer> voicePeers;
    std::vector<std::string> chatHistory;
    std::map<std::string, std::chrono::steady_clock::time_point> speakerActivity;
};

class WindowManager
{
public:
    WindowManager();
    ~WindowManager();

    bool initialize();
    void render();
    void cleanup();

    bool isMuted() const;
    bool isDeafened() const;
    bool shouldClose() const;
    
    bool isLoggedIn() const;
    std::string getUsername() const;
    int getSelectedTextChannelId() const;
    int getActiveVoiceChannelId() const;
    
    std::string getPendingNewTextChannel();
    std::string getPendingNewVoiceChannel();
    
    void setChannels(const std::vector<std::pair<int, std::string>>& textChannels, const std::vector<std::pair<int, std::string>>& voiceChannels);
    void appendChatMessage(const std::string& message);
    void setChatHistory(const std::vector<std::string>& messages);
    std::string getPendingOutgoingMessage();
    void setVoicePeers(const std::vector<std::string>& peerDataList);
    void markSpeakerActive(const std::string& uuid);

private:
    void setupDarkTheme();
    void renderLoginModal();

    GLFWwindow* m_window;

    std::unique_ptr<UiDisplayState> m_frontBuffer;
    std::unique_ptr<UiDisplayState> m_backBuffer;
    std::mutex m_bufferMutex;
    std::atomic<bool> m_isBackBufferDirty;

    std::atomic<int> m_selectedTextChannelId;
    std::atomic<int> m_activeVoiceChannelId;
    
    std::atomic<bool> m_isMuted;
    std::atomic<bool> m_isDeafened;
    char m_chatInputBuffer[2048];
    char m_usernameInputBuffer[256];
    bool m_showSettingsModal;
    bool m_isLoggedIn;
    std::string m_username;
    
    std::mutex m_inputQueueMutex;
    std::queue<std::string> m_pendingNewTextChannels;
    std::queue<std::string> m_pendingNewVoiceChannels;
    std::queue<std::string> m_outgoingMessages;
};