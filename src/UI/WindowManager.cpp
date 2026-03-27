#include "UI/WindowManager.hpp"
#include <iostream>
#include <cstring>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include "Core/Utils.hpp"
#include "UI/IconsFontAwesome6.h"

WindowManager::WindowManager()
    : m_window(nullptr),
      m_selectedTextChannelId(1),
      m_activeVoiceChannelId(-1),
      m_isMuted(false),
      m_isDeafened(false),
      m_showSettingsModal(false),
      m_isLoggedIn(false)
{
    std::memset(m_chatInputBuffer, 0, sizeof(m_chatInputBuffer));
    std::memset(m_usernameInputBuffer, 0, sizeof(m_usernameInputBuffer));

    m_frontBuffer = std::make_unique<UiDisplayState>();
    m_backBuffer = std::make_unique<UiDisplayState>();
    m_isBackBufferDirty = false;
    
    m_backBuffer->chatHistory.push_back("System: Welcome to the server!");
    *m_frontBuffer = *m_backBuffer;
    
    m_username = Utils::getSavedUsername();
    if (!m_username.empty())
    {
        m_isLoggedIn = true;
    }
}

WindowManager::~WindowManager()
{
    cleanup();
}

bool WindowManager::initialize()
{
    if (!glfwInit())
    {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

#if defined(__APPLE__)
    const char* glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    m_window = glfwCreateWindow(1024, 768, "VoiceChatApp", nullptr, nullptr);
    if (m_window == nullptr)
    {
        std::cerr << "Failed to create GLFW window" << std::endl;
        return false;
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& imguiIO = ImGui::GetIO();
    imguiIO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    imguiIO.Fonts->AddFontDefault();
    ImFontConfig iconsConfig;
    iconsConfig.MergeMode = true;
    iconsConfig.PixelSnapH = true;
    iconsConfig.OversampleH = 1;
    iconsConfig.OversampleV = 1;
    static const ImWchar iconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    imguiIO.Fonts->AddFontFromFileTTF("assets/fa-solid-900.ttf", 14.0f, &iconsConfig, iconsRanges);

    setupDarkTheme();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    return true;
}

void WindowManager::setupDarkTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 mainBgColor        = ImVec4(0.192f, 0.200f, 0.220f, 1.000f);
    const ImVec4 sidebarBgColor     = ImVec4(0.169f, 0.176f, 0.192f, 1.000f);
    const ImVec4 userPanelBgColor   = ImVec4(0.137f, 0.141f, 0.157f, 1.000f);
    const ImVec4 accentColor        = ImVec4(0.345f, 0.396f, 0.949f, 1.000f);

    colors[ImGuiCol_WindowBg]             = mainBgColor;
    colors[ImGuiCol_ChildBg]              = sidebarBgColor;
    colors[ImGuiCol_Header]               = ImVec4(0.243f, 0.255f, 0.282f, 1.000f);
    colors[ImGuiCol_HeaderHovered]        = accentColor;
    colors[ImGuiCol_HeaderActive]         = accentColor;
    colors[ImGuiCol_Button]               = ImVec4(0.306f, 0.322f, 0.353f, 1.000f);
    colors[ImGuiCol_ButtonHovered]        = accentColor;
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.280f, 0.320f, 0.810f, 1.000f);
    colors[ImGuiCol_FrameBg]              = ImVec4(0.118f, 0.125f, 0.137f, 1.000f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.141f, 0.149f, 0.165f, 1.000f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.169f, 0.176f, 0.192f, 1.000f);
    colors[ImGuiCol_Text]                 = ImVec4(0.859f, 0.867f, 0.886f, 1.000f);
    
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
}

void WindowManager::render()
{
    if (shouldClose())
    {
        return;
    }

    if (m_isBackBufferDirty)
    {
        std::lock_guard<std::mutex> lock(m_bufferMutex);
        *m_frontBuffer = *m_backBuffer;
        m_isBackBufferDirty = false;
    }

    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("MainLayout", nullptr, windowFlags);
    ImGui::PopStyleVar();

    const float sidebarWidth = 250.0f;
    const float userPanelHeight = 60.0f;
    const float chatAreaWidth = viewport->WorkSize.x - sidebarWidth;

    ImGui::BeginChild("LeftColumn", ImVec2(sidebarWidth, viewport->WorkSize.y), false);
    
    ImGui::BeginChild("ChannelSidebar", ImVec2(sidebarWidth, viewport->WorkSize.y - userPanelHeight), false);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::SetCursorPos(ImVec2(10, 10));
    ImGui::TextDisabled("SERVER NAME");
    ImGui::Separator();
    
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));

    bool isTextSectionOpen = ImGui::CollapsingHeader("TEXT CHANNELS", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);
    ImGui::SameLine(ImGui::GetWindowWidth() - 40);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::Button("+##text_add"))
    {
        ImGui::OpenPopup("Add Text Channel");
    }
    ImGui::PopStyleColor();

    if (ImGui::BeginPopupModal("Add Text Channel", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        static char channelNameBuffer[64] = "";
        ImGui::InputText("Name", channelNameBuffer, 64);
        if (ImGui::Button("Create", ImVec2(120, 0)))
        {
            if (strlen(channelNameBuffer) > 0)
            {
                std::lock_guard<std::mutex> lock(m_inputQueueMutex);
                m_pendingNewTextChannels.push(channelNameBuffer);
                std::memset(channelNameBuffer, 0, sizeof(channelNameBuffer));
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (isTextSectionOpen) 
    {
        for (const auto& channel : m_frontBuffer->textChannelsList)
        {
            std::string channelLabel = std::string(ICON_FA_HASHTAG) + " " + channel.second;
            if (ImGui::Selectable(channelLabel.c_str(), m_selectedTextChannelId == channel.first))
            {
                m_selectedTextChannelId = channel.first;
            }
        }
    }
    
    bool isVoiceSectionOpen = ImGui::CollapsingHeader("VOICE CHANNELS", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap);
    ImGui::SameLine(ImGui::GetWindowWidth() - 40);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::Button("+##voice_add"))
    {
        ImGui::OpenPopup("Add Voice Channel");
    }
    ImGui::PopStyleColor();

    if (ImGui::BeginPopupModal("Add Voice Channel", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    {
        static char voiceChannelNameBuffer[64] = "";
        ImGui::InputText("Name", voiceChannelNameBuffer, 64);
        if (ImGui::Button("Create", ImVec2(120, 0)))
        {
            if (strlen(voiceChannelNameBuffer) > 0)
            {
                std::lock_guard<std::mutex> lock(m_inputQueueMutex);
                m_pendingNewVoiceChannels.push(voiceChannelNameBuffer);
                std::memset(voiceChannelNameBuffer, 0, sizeof(voiceChannelNameBuffer));
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (isVoiceSectionOpen) 
    {
        for (const auto& channel : m_frontBuffer->voiceChannelsList)
        {
            std::string channelLabel = std::string(ICON_FA_VOLUME_HIGH) + " " + channel.second;
            if (m_activeVoiceChannelId == channel.first)
            {
                channelLabel += " (Connected)";
            }
            
            if (ImGui::Selectable(channelLabel.c_str(), m_activeVoiceChannelId == channel.first))
            {
                m_activeVoiceChannelId = channel.first;
            }
            
            ImGui::Indent();
            {
                auto currentTime = std::chrono::steady_clock::now();
                for (const auto& peer : m_frontBuffer->voicePeers) 
                {
                    if (peer.channelId != channel.first)
                    {
                        continue;
                    }

                    bool isSpeaking = m_frontBuffer->speakerActivity.count(peer.uuid) && std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - m_frontBuffer->speakerActivity[peer.uuid]).count() < 300;
                    
                    ImVec4 peerColor = isSpeaking ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                    
                    std::string statusBadges = "";
                    if (peer.isMuted)
                    {
                        statusBadges += std::string(" ") + ICON_FA_MICROPHONE_SLASH;
                    }
                    if (peer.isDeafened)
                    {
                        statusBadges += std::string(" ") + ICON_FA_HEADPHONES;
                    }
                    
                    ImGui::TextColored(peerColor, "  %s%s", peer.username.c_str(), statusBadges.c_str());
                }
            }
            if (m_activeVoiceChannelId == channel.first)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
                if (ImGui::Button("Leave VC", ImVec2(ImGui::GetContentRegionAvail().x, 24)))
                {
                    m_activeVoiceChannelId = -1;
                }
                ImGui::PopStyleColor(2);
            }
            
            ImGui::Unindent();
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.137f, 0.141f, 0.157f, 1.000f));
    ImGui::BeginChild("UserControlsPanel", ImVec2(sidebarWidth, userPanelHeight), false);
    
    ImGui::SetCursorPos(ImVec2(10, 12));
    ImGui::Text("%s", m_isLoggedIn ? m_username.c_str() : "Logging in...");
    ImGui::SetCursorPos(ImVec2(10, 30));
    if (m_activeVoiceChannelId.load() >= 0)
    {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Voice Connected");
    }
    else
    {
        ImGui::TextDisabled("Voice Disconnected");
    }

    ImGui::SetCursorPos(ImVec2(sidebarWidth - 110, 15));
    
    bool currentlyMuted = m_isMuted.load();
    if (ImGui::Button(currentlyMuted ? "Unmute" : "Mute", ImVec2(45, 30))) 
    {
        m_isMuted.store(!currentlyMuted);
    }
    
    ImGui::SameLine();
    bool currentlyDeafened = m_isDeafened.load();
    if (ImGui::Button(currentlyDeafened ? "Undeaf" : "Deaf", ImVec2(45, 30))) 
    {
        m_isDeafened.store(!currentlyDeafened);
        m_isMuted.store(!currentlyDeafened);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("RightColumn", ImVec2(chatAreaWidth, viewport->WorkSize.y), false);
    
    ImGui::SetCursorPos(ImVec2(15, 10));
    std::string channelHeaderName = "# unknown";
    for (const auto& channel : m_frontBuffer->textChannelsList)
    {
        if (channel.first == m_selectedTextChannelId.load())
        {
            channelHeaderName = std::string(ICON_FA_HASHTAG) + " " + channel.second;
            break;
        }
    }
    ImGui::TextDisabled("%s", channelHeaderName.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("| Text chat channel");
    
    ImGui::SetCursorPosY(35);
    ImGui::Separator();
    
    const float chatInputHeight = 50.0f;
    ImGui::BeginChild("ChatHistory", ImVec2(0, viewport->WorkSize.y - chatInputHeight - 50.0f), false);
    for (const auto& message : m_frontBuffer->chatHistory) 
    {
        ImGui::SetCursorPosX(15);
        ImGui::TextWrapped("%s", message.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    
    ImGui::SetCursorPos(ImVec2(15, viewport->WorkSize.y - chatInputHeight - 5.0f));
    ImGui::PushItemWidth(chatAreaWidth - 30);
    if (ImGui::InputText("##ChatInput", m_chatInputBuffer, IM_ARRAYSIZE(m_chatInputBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) 
    {
        if (m_chatInputBuffer[0] != '\0') 
        {
            std::lock_guard<std::mutex> lock(m_inputQueueMutex);
            m_outgoingMessages.push(std::string(m_chatInputBuffer));
            std::memset(m_chatInputBuffer, 0, sizeof(m_chatInputBuffer));
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::PopItemWidth();
    
    ImGui::EndChild();

    ImGui::End();

    if (m_showSettingsModal) 
    {
    }

    renderLoginModal();

    ImGui::Render();
    int displayWidth, displayHeight;
    glfwGetFramebufferSize(m_window, &displayWidth, &displayHeight);
    glViewport(0, 0, displayWidth, displayHeight);
    glClearColor(0.192f, 0.200f, 0.220f, 1.000f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(m_window);
}

void WindowManager::cleanup()
{
    if (m_window) 
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(m_window);
        glfwTerminate();
        m_window = nullptr;
    }
}

bool WindowManager::isMuted() const
{
    return m_isMuted.load();
}

bool WindowManager::isDeafened() const
{
    return m_isDeafened.load();
}

bool WindowManager::shouldClose() const
{
    return m_window != nullptr && glfwWindowShouldClose(m_window);
}

void WindowManager::renderLoginModal()
{
    if (m_isLoggedIn)
    {
        return;
    }
    
    ImGui::OpenPopup("Welcome to VoiceChat");
    
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    if (ImGui::BeginPopupModal("Welcome to VoiceChat", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::Text("Please enter a username to join the network:");
        ImGui::Separator();
        
        ImGui::InputText("Username", m_usernameInputBuffer, IM_ARRAYSIZE(m_usernameInputBuffer));
        
        if (ImGui::Button("Join", ImVec2(120, 0))) 
        {
            if (strlen(m_usernameInputBuffer) > 0) 
            {
                m_username = m_usernameInputBuffer;
                m_isLoggedIn = true;
                Utils::saveUsername(m_username);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SetItemDefaultFocus();
        ImGui::EndPopup();
    }
}

bool WindowManager::isLoggedIn() const 
{
    return m_isLoggedIn;
}

int WindowManager::getSelectedTextChannelId() const 
{
    return m_selectedTextChannelId.load();
}

int WindowManager::getActiveVoiceChannelId() const 
{
    return m_activeVoiceChannelId.load();
}

std::string WindowManager::getPendingNewTextChannel()
{
    std::lock_guard<std::mutex> lock(m_inputQueueMutex);
    if (!m_pendingNewTextChannels.empty())
    {
        std::string channelName = m_pendingNewTextChannels.front();
        m_pendingNewTextChannels.pop();
        return channelName;
    }
    return "";
}

std::string WindowManager::getPendingNewVoiceChannel()
{
    std::lock_guard<std::mutex> lock(m_inputQueueMutex);
    if (!m_pendingNewVoiceChannels.empty())
    {
        std::string channelName = m_pendingNewVoiceChannels.front();
        m_pendingNewVoiceChannels.pop();
        return channelName;
    }
    return "";
}

void WindowManager::setChannels(const std::vector<std::pair<int, std::string>>& textChannels, const std::vector<std::pair<int, std::string>>& voiceChannels)
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_backBuffer->textChannelsList = textChannels;
    m_backBuffer->voiceChannelsList = voiceChannels;
    m_isBackBufferDirty = true;
}

std::string WindowManager::getUsername() const 
{
    return m_username;
}

void WindowManager::appendChatMessage(const std::string& message) 
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_backBuffer->chatHistory.push_back(message);
    m_isBackBufferDirty = true;
}

void WindowManager::setChatHistory(const std::vector<std::string>& messages) 
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_backBuffer->chatHistory = messages;
    m_isBackBufferDirty = true;
}

std::string WindowManager::getPendingOutgoingMessage() 
{
    std::lock_guard<std::mutex> lock(m_inputQueueMutex);
    if (!m_outgoingMessages.empty())
    {
        std::string message = m_outgoingMessages.front();
        m_outgoingMessages.pop();
        return message;
    }
    return "";
}

void WindowManager::setVoicePeers(const std::vector<std::string>& peerDataList) 
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_backBuffer->voicePeers.clear();
    for (const auto& peerEntry : peerDataList)
    {
        size_t firstColon = peerEntry.find(':');
        if (firstColon == std::string::npos)
        {
            continue;
        }
        size_t secondColon = peerEntry.find(':', firstColon + 1);
        if (secondColon == std::string::npos)
        {
            continue;
        }
        size_t thirdColon = peerEntry.find(':', secondColon + 1);
        if (thirdColon == std::string::npos)
        {
            continue;
        }
        size_t fourthColon = peerEntry.find(':', thirdColon + 1);
        
        VoicePeer peer;
        peer.username = peerEntry.substr(0, firstColon);
        peer.isMuted = (peerEntry.substr(firstColon + 1, secondColon - firstColon - 1) == "1");
        peer.isDeafened = (peerEntry.substr(secondColon + 1, thirdColon - secondColon - 1) == "1");
        peer.uuid = peerEntry.substr(thirdColon + 1);
        peer.channelId = std::stoi(peerEntry.substr(fourthColon + 1));
        m_backBuffer->voicePeers.push_back(peer);
    }
    m_isBackBufferDirty = true;
}

void WindowManager::markSpeakerActive(const std::string& uuid)
{
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_backBuffer->speakerActivity[uuid] = std::chrono::steady_clock::now();
    m_isBackBufferDirty = true;
}