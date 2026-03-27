#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <queue>
#include <chrono>
#include <map>

struct GLFWwindow;

struct VoicePeer
{
    std::string username;
    bool isMuted;
    bool isDeafened;
    std::string uuid;
    int channelId;
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
    void addIncomingMessage(const std::string& message);
    void setChatHistory(const std::vector<std::string>& messages);
    std::string getPendingOutgoingMessage();
    void setVoicePeers(const std::vector<std::string>& peerDataList);
    void markSpeakerActive(const std::string& uuid);

private:
    void setupDarkTheme();
    void renderLoginModal();

    GLFWwindow* m_window;

    // UI state
    std::atomic<int> m_selectedTextChannelId;
    std::atomic<int> m_activeVoiceChannelId;
    
    std::mutex m_channelsMutex;
    std::vector<std::pair<int, std::string>> m_textChannelsList;
    std::vector<std::pair<int, std::string>> m_voiceChannelsList;
    
    std::queue<std::string> m_pendingNewTextChannels;
    std::queue<std::string> m_pendingNewVoiceChannels;
    
    std::atomic<bool> m_isMuted;
    std::atomic<bool> m_isDeafened;
    char m_chatInputBuffer[2048];
    char m_usernameInputBuffer[256];
    std::vector<std::string> m_chatHistory;
    bool m_showSettingsModal;
    bool m_isLoggedIn;
    std::string m_username;
    
    // Thread-safe networking state
    std::mutex m_chatMutex;
    std::queue<std::string> m_outgoingMessages;
    
    std::mutex m_peersMutex;
    std::vector<VoicePeer> m_voicePeers;
    std::map<std::string, std::chrono::steady_clock::time_point> m_speakerActivity;
};