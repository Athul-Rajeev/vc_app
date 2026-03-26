#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <queue>

struct GLFWwindow;

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
    void addIncomingMessage(const std::string& msg);
    void setChatHistory(const std::vector<std::string>& msgs);
    std::string getPendingOutgoingMessage();
    void setVoicePeers(const std::vector<std::string>& peers);

private:
    void setupDarkTheme();
    void renderLoginModal();

    GLFWwindow* m_window;

    // UI state
    int m_selectedTextChannelId;
    int m_activeVoiceChannelId;
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
    std::vector<std::string> m_voicePeers;
};