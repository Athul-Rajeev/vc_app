#include "UI/WindowManager.hpp"
#include "UI/DiscordBackend.hpp"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCoreApplication>
#include <QEventLoop>
#include <QQuickWindow>
#include <QQuickStyle>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

// ─────────────────────────────────────────────────────────────
WindowManager::WindowManager()
    : m_argc(1)
{
    std::strncpy(m_argv0, "app", sizeof(m_argv0));
    m_argv0[sizeof(m_argv0) - 1] = '\0';
    m_argv[0] = m_argv0;
    m_argv[1] = nullptr;
}

WindowManager::~WindowManager()
{
    cleanup();
}

// ─────────────────────────────────────────────────────────────
bool WindowManager::initialize()
{
    if (m_initialized) return true;

    if (!QCoreApplication::instance()) 
    {
        m_app = std::make_unique<QApplication>(m_argc, m_argv[0] ? m_argv : nullptr);
    }

    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QQuickStyle::setStyle("Basic");

    m_backend = new DiscordBackend();

    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->rootContext()->setContextProperty("backend", m_backend);

    // Load purely from the compiled binary resources
    const QUrl url(QStringLiteral("qrc:/Main.qml"));
    m_engine->load(url);

    if (m_engine->rootObjects().isEmpty()) 
    {
        // If it fails here, Main.qml has a syntax error or the QRC file is wrong.
        delete m_backend;
        m_backend = nullptr;
        return false;
    }

    m_backend->setParent(m_engine.get());

    applyDarkTitleBar(qobject_cast<QQuickWindow*>(m_engine->rootObjects().first()));

    m_initialized = true;
    return true;
}

void WindowManager::applyDarkTitleBar(QQuickWindow* window)
{
    if (!window)
    {
        return;
    }

#ifdef _WIN32
    HWND windowHandle = reinterpret_cast<HWND>(window->winId());
    if (!windowHandle)
    {
        return;
    }

    // Windows 10 1809+: the immersive dark-mode flag. Older Insider builds
    // exposed the same attribute under 19; try 20 first, fall back to 19.
    BOOL useDarkMode = TRUE;
    if (FAILED(DwmSetWindowAttribute(windowHandle, 20, &useDarkMode, sizeof(useDarkMode))))
    {
        DwmSetWindowAttribute(windowHandle, 19, &useDarkMode, sizeof(useDarkMode));
    }

    // Windows 11 build 22000+: paint the caption/border explicitly to match
    // the app theme. Windows 10 rejects these values harmlessly.
    // (0x22 = caption color, 0x22+border, text — the DWMA_* enum values for
    // them are not present in every SDK, so use the raw attribute numbers.)
    const COLORREF kCaptionColor = RGB(0x1E, 0x1F, 0x22);   // #1E1F22 caption
    const COLORREF kBorderStyle  = RGB(0x26, 0x27, 0x2B);   // #26272B border
    const COLORREF kTitleColor   = RGB(0xF2, 0xF3, 0xF5);   // #F2F3F5 title text
    DwmSetWindowAttribute(windowHandle, 35, &kCaptionColor, sizeof(kCaptionColor));  // DWMWA_CAPTION_COLOR
    DwmSetWindowAttribute(windowHandle, 34, &kBorderStyle,  sizeof(kBorderStyle));   // DWMWA_BORDER_COLOR
    DwmSetWindowAttribute(windowHandle, 36, &kTitleColor,   sizeof(kTitleColor));    // DWMWA_TEXT_COLOR
#else
    Q_UNUSED(window);   // Linux: the system theme styles the title bar.
#endif
}

void WindowManager::cleanup()
{
    if (!m_initialized) return;
    m_engine.reset();   // destroys all QML objects including backend
    m_app.reset();
    m_backend    = nullptr;
    m_initialized = false;
}

// ─────────────────────────────────────────────────────────────
// State queries — delegate directly; DiscordBackend uses atomics
// ─────────────────────────────────────────────────────────────
bool WindowManager::isMuted() const
{
    return m_backend && m_backend->isMuted();
}

bool WindowManager::isDeafened() const
{
    return m_backend && m_backend->isDeafened();
}

bool WindowManager::isLoggedIn() const
{
    return m_backend && m_backend->isLoggedIn();
}

int WindowManager::getSelectedTextChannelId() const
{
    return m_backend ? m_backend->selectedTextChannelId() : -1;
}

int WindowManager::getActiveVoiceChannelId() const
{
    return m_backend ? m_backend->activeVoiceChannelId() : -1;
}

std::string WindowManager::getUsername() const
{
    return m_backend ? m_backend->username().toStdString() : "";
}

// ─────────────────────────────────────────────────────────────
// Output queues
// ─────────────────────────────────────────────────────────────
std::string WindowManager::getPendingOutgoingMessage()
{
    return m_backend ? m_backend->dequeuePendingMessage() : "";
}

std::string WindowManager::getPendingNewTextChannel()
{
    return m_backend ? m_backend->dequeuePendingTextChannel() : "";
}

std::string WindowManager::getPendingNewVoiceChannel()
{
    return m_backend ? m_backend->dequeuePendingVoiceChannel() : "";
}

bool WindowManager::getPendingLogin(std::string& outUser, std::string& outPass)
{
    if (!m_backend) return false;
    AuthRequest req;
    if (m_backend->dequeuePendingLogin(req))
    {
        outUser = req.username;
        outPass = req.password;
        return true;
    }
    return false;
}

bool WindowManager::getPendingRegistration(std::string& outUser, std::string& outPass)
{
    if (!m_backend) return false;
    AuthRequest req;
    if (m_backend->dequeuePendingRegistration(req))
    {
        outUser = req.username;
        outPass = req.password;
        return true;
    }
    return false;
}

bool WindowManager::getPendingLogout()
{
    return m_backend && m_backend->dequeuePendingLogout();
}

bool WindowManager::getPendingPasswordChange(std::string& outCurrentPassword, std::string& outNewPassword)
{
    if (!m_backend) return false;
    AuthRequest request;
    if (m_backend->dequeuePendingPasswordChange(request))
    {
        outCurrentPassword = request.username;   // reuses the credentials struct: username=current password
        outNewPassword = request.password;
        return true;
    }
    return false;
}

bool WindowManager::getPendingPasswordReset(std::string& outUser, std::string& outResetToken, std::string& outNewPassword)
{
    if (!m_backend) return false;
    PasswordResetRequest request;
    if (m_backend->dequeuePendingPasswordReset(request))
    {
        outUser = request.username;
        outResetToken = request.resetToken;
        outNewPassword = request.newPassword;
        return true;
    }
    return false;
}

double WindowManager::getVoiceActivationThreshold() const
{
    return m_backend ? m_backend->voiceActivationThreshold() : 0.015;
}

// ─────────────────────────────────────────────────────────────
// Thread-safe setters
// ─────────────────────────────────────────────────────────────
void WindowManager::setChannels(
    const std::vector<std::pair<int,std::string>>& textChannels,
    const std::vector<std::pair<int,std::string>>& voiceChannels)
{
    if (m_backend) m_backend->setChannels(textChannels, voiceChannels);
}

void WindowManager::appendChatMessage(int channelId, const std::string& message, long long timestampMs)
{
    if (m_backend) m_backend->appendChatMessage(channelId, message, static_cast<qint64>(timestampMs));
}

void WindowManager::setChatHistory(int channelId, bool hasMoreHistory, const std::vector<std::pair<std::string, long long>>& messages)
{
    if (!m_backend)
    {
        return;
    }
    std::vector<std::pair<std::string, qint64>> converted;
    for (auto& p : messages)
        converted.emplace_back(p.first, static_cast<qint64>(p.second));
    m_backend->setChatHistory(channelId, hasMoreHistory, converted);
}

void WindowManager::prependChatHistory(int channelId, bool hasMoreHistory, const std::vector<std::pair<std::string, long long>>& messages)
{
    if (!m_backend)
    {
        return;
    }
    std::vector<std::pair<std::string, qint64>> converted;
    for (auto& p : messages)
        converted.emplace_back(p.first, static_cast<qint64>(p.second));
    m_backend->prependChatHistory(channelId, hasMoreHistory, converted);
}

bool WindowManager::getPendingHistoryRequest(int& outChannelId, long long& outBeforeTimestampMs)
{
    if (!m_backend) return false;
    int channelId = 0;
    qint64 beforeTimestampMs = 0;
    if (m_backend->dequeuePendingHistoryRequest(channelId, beforeTimestampMs))
    {
        outChannelId = channelId;
        outBeforeTimestampMs = static_cast<long long>(beforeTimestampMs);
        return true;
    }
    return false;
}

void WindowManager::setVoicePeers(const std::vector<std::string>& peerDataList)
{
    if (m_backend) m_backend->setVoicePeers(peerDataList);
}

void WindowManager::markSpeakerActive(const std::string& uuid)
{
    if (m_backend) m_backend->markSpeakerActive(uuid);
}

void WindowManager::setUiUpdateCallback(std::function<void()> callback)
{
    m_uiCallback = std::move(callback);
    
    if (m_backend)
    {
        m_backend->setNotifyCallback(m_uiCallback);
    }
}

void WindowManager::confirmLogin(const std::string& username)
{
    if (m_backend) m_backend->confirmLogin(username);
}

void WindowManager::confirmLogout()
{
    if (m_backend) m_backend->confirmLogout();
}

void WindowManager::setLoginError(const std::string& error)
{
    if (m_backend) 
    {
        m_backend->setAuthError(error);
    }
}

void WindowManager::setPasswordNotice(const std::string& notice)
{
    if (m_backend)
    {
        m_backend->setPasswordNotice(notice);
    }
}

void WindowManager::setLoginState(int loginState)
{
    if (m_backend)
    {
        m_backend->setLoginState(static_cast<LoginState>(loginState));
    }
}

int WindowManager::exec()
{
    if (!m_initialized || !m_app) return -1;
    
    // This hands control to Qt. It will block here at 0% CPU 
    // until the user closes the main window.
    return m_app->exec(); 
}
