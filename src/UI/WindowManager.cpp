#include "UI/WindowManager.hpp"
#include <iostream>
#include <cstring>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include "Core/Utils.hpp"

WindowManager::WindowManager()
    : m_window(nullptr),
      m_selectedTextChannelId(0),
      m_activeVoiceChannelId(-1),
      m_isMuted(false),
      m_isDeafened(false),
      m_showSettingsModal(false),
      m_isLoggedIn(false)
{
    std::memset(m_chatInputBuffer, 0, sizeof(m_chatInputBuffer));
    std::memset(m_usernameInputBuffer, 0, sizeof(m_usernameInputBuffer));
    m_chatHistory.push_back("System: Welcome to the server!");
    
    m_username = Utils::getSavedUsername();
    if (!m_username.empty()) {
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

    // Decide GL+GLSL versions
#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    const char* glsl_version = "#version 130";
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
    glfwSwapInterval(1); // Enable vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    setupDarkTheme();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    return true;
}

void WindowManager::setupDarkTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    // Discord-like Color Palette
    const ImVec4 mainBgColor        = ImVec4(0.192f, 0.200f, 0.220f, 1.000f); // #313338
    const ImVec4 sidebarBgColor     = ImVec4(0.169f, 0.176f, 0.192f, 1.000f); // #2B2D31
    const ImVec4 userPanelBgColor   = ImVec4(0.137f, 0.141f, 0.157f, 1.000f); // #232428
    const ImVec4 accentColor        = ImVec4(0.345f, 0.396f, 0.949f, 1.000f); // #5865F2

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
        return;

    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("MainLayout", nullptr, window_flags);
    ImGui::PopStyleVar();

    const float sidebarWidth = 250.0f;
    const float userPanelHeight = 60.0f;
    const float chatAreaWidth = viewport->WorkSize.x - sidebarWidth;

    // --- LEFT COLUMN (Panel A + Panel B) ---
    ImGui::BeginChild("LeftColumn", ImVec2(sidebarWidth, viewport->WorkSize.y), false);
    
    // Panel A: Channel Sidebar
    ImGui::BeginChild("PanelA", ImVec2(sidebarWidth, viewport->WorkSize.y - userPanelHeight), false);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
    ImGui::SetCursorPos(ImVec2(10, 10));
    ImGui::TextDisabled("SERVER NAME");
    ImGui::Separator();
    
    if (ImGui::CollapsingHeader("TEXT CHANNELS", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Selectable("# general", m_selectedTextChannelId == 0)) m_selectedTextChannelId = 0;
        if (ImGui::Selectable("# development", m_selectedTextChannelId == 1)) m_selectedTextChannelId = 1;
    }
    
    if (ImGui::CollapsingHeader("VOICE CHANNELS", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Selectable(m_activeVoiceChannelId == 0 ? "(Connected) Voice General" : "Voice General", m_activeVoiceChannelId == 0)) {
            m_activeVoiceChannelId = 0;
        }
        if (m_activeVoiceChannelId == 0) {
            ImGui::Indent();
            {
                std::lock_guard<std::mutex> lock(m_peersMutex);
                for (const auto& peer : m_voicePeers) {
                    ImGui::TextColored(ImVec4(0.345f, 0.396f, 0.949f, 1.0f), "- %s", peer.c_str());
                }
                
                if (m_voicePeers.empty() && m_isLoggedIn) {
                    ImGui::TextColored(ImVec4(0.345f, 0.396f, 0.949f, 1.0f), "- %s (Connecting...)", m_username.c_str());
                }
            }
            ImGui::Unindent();
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    
    // Panel B: User Controls
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.137f, 0.141f, 0.157f, 1.000f)); // #232428
    ImGui::BeginChild("PanelB", ImVec2(sidebarWidth, userPanelHeight), false);
    
    ImGui::SetCursorPos(ImVec2(10, 12));
    ImGui::Text("%s", m_isLoggedIn ? m_username.c_str() : "Logging in...");
    ImGui::SetCursorPos(ImVec2(10, 30));
    ImGui::TextDisabled("Voice Connected");

    ImGui::SetCursorPos(ImVec2(sidebarWidth - 110, 15));
    
    // Toggle Buttons
    bool isMutedVar = m_isMuted.load();
    if (ImGui::Button(isMutedVar ? "Unmute" : "Mute", ImVec2(45, 30))) {
        m_isMuted.store(!isMutedVar);
    }
    
    ImGui::SameLine();
    bool isDeafenedVar = m_isDeafened.load();
    if (ImGui::Button(isDeafenedVar ? "Undeaf" : "Deaf", ImVec2(45, 30))) {
        m_isDeafened.store(!isDeafenedVar);
        if (!isDeafenedVar) {
            m_isMuted.store(true);
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    ImGui::EndChild(); // End LeftColumn

    // --- RIGHT COLUMN (Panel C) ---
    ImGui::SameLine();
    ImGui::BeginChild("RightColumn", ImVec2(chatAreaWidth, viewport->WorkSize.y), false);
    
    // Panel C Top Header
    ImGui::SetCursorPos(ImVec2(15, 10));
    ImGui::TextDisabled((m_selectedTextChannelId == 0) ? "# general" : "# development");
    ImGui::SameLine();
    ImGui::TextDisabled("| Text chat channel");
    
    ImGui::SetCursorPosY(35);
    ImGui::Separator();
    
    // Message History Area
    const float chatInputHeight = 50.0f;
    ImGui::BeginChild("ChatHistory", ImVec2(0, viewport->WorkSize.y - chatInputHeight - 50.0f), false);
    for (const auto& msg : m_chatHistory) {
        ImGui::SetCursorPosX(15);
        ImGui::TextWrapped("%s", msg.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    
    // Chat Input box
    ImGui::SetCursorPos(ImVec2(15, viewport->WorkSize.y - chatInputHeight - 5.0f));
    ImGui::PushItemWidth(chatAreaWidth - 30);
    if (ImGui::InputText("##ChatInput", m_chatInputBuffer, IM_ARRAYSIZE(m_chatInputBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (m_chatInputBuffer[0] != '\0') {
            std::lock_guard<std::mutex> lock(m_chatMutex);
            m_outgoingMessages.push(std::string(m_chatInputBuffer));
            std::memset(m_chatInputBuffer, 0, sizeof(m_chatInputBuffer));
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::PopItemWidth();
    
    ImGui::EndChild(); // End RightColumn

    ImGui::End(); // End MainLayout

    if (m_showSettingsModal) {
        // Placeholder Settings Modal for phase 1.
    }

    renderLoginModal();

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(m_window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.192f, 0.200f, 0.220f, 1.000f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(m_window);
}

void WindowManager::cleanup()
{
    if (m_window) {
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
    if (m_isLoggedIn) return;
    
    ImGui::OpenPopup("Welcome to VoiceChat");
    
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    
    if (ImGui::BeginPopupModal("Welcome to VoiceChat", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::Text("Please enter a username to join the network:");
        ImGui::Separator();
        
        ImGui::InputText("Username", m_usernameInputBuffer, IM_ARRAYSIZE(m_usernameInputBuffer));
        
        if (ImGui::Button("Join", ImVec2(120, 0))) {
            if (strlen(m_usernameInputBuffer) > 0) {
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

bool WindowManager::isLoggedIn() const {
    return m_isLoggedIn;
}

int WindowManager::getSelectedTextChannelId() const {
    return m_selectedTextChannelId.load();
}

std::string WindowManager::getUsername() const {
    return m_username;
}

void WindowManager::addIncomingMessage(const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_chatMutex);
    m_chatHistory.push_back(msg);
}

void WindowManager::setChatHistory(const std::vector<std::string>& msgs) {
    std::lock_guard<std::mutex> lock(m_chatMutex);
    m_chatHistory = msgs;
}

std::string WindowManager::getPendingOutgoingMessage() {
    std::lock_guard<std::mutex> lock(m_chatMutex);
    if (!m_outgoingMessages.empty()) {
        std::string msg = m_outgoingMessages.front();
        m_outgoingMessages.pop();
        return msg;
    }
    return "";
}

void WindowManager::setVoicePeers(const std::vector<std::string>& peers) {
    std::lock_guard<std::mutex> lock(m_peersMutex);
    m_voicePeers = peers;
}