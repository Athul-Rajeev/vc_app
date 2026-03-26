#pragma once

#include <string>
#include <vector>
#include <atomic>

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

private:
    void setupDarkTheme();

    GLFWwindow* m_window;

    // UI state
    int m_selectedTextChannelId;
    int m_activeVoiceChannelId;
    std::atomic<bool> m_isMuted;
    std::atomic<bool> m_isDeafened;
    char m_chatInputBuffer[2048];
    std::vector<std::string> m_chatHistory;
    bool m_showSettingsModal;
};