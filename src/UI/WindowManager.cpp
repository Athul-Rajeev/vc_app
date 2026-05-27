#include "UI/WindowManager.hpp"
#include "UI/DiscordBackend.hpp"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCoreApplication>
#include <QEventLoop>
#include <QQuickWindow>
#include <cstring>

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

    if (!QCoreApplication::instance()) {
        m_app = std::make_unique<QApplication>(m_argc, m_argv[0] ? m_argv : nullptr);
    }

    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    m_backend = new DiscordBackend();

    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->rootContext()->setContextProperty("backend", m_backend);

    // Load purely from the compiled binary resources
    const QUrl url(QStringLiteral("qrc:/Main.qml"));
    m_engine->load(url);

    if (m_engine->rootObjects().isEmpty()) {
        // If it fails here, Main.qml has a syntax error or the QRC file is wrong.
        delete m_backend;
        m_backend = nullptr;
        return false;
    }

    m_backend->setParent(m_engine.get());
    m_initialized = true;
    return true;
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
    LoginRequest req;
    if (m_backend->dequeuePendingLogin(req))
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

// ─────────────────────────────────────────────────────────────
// Thread-safe setters
// ─────────────────────────────────────────────────────────────
void WindowManager::setChannels(
    const std::vector<std::pair<int,std::string>>& textChannels,
    const std::vector<std::pair<int,std::string>>& voiceChannels)
{
    if (m_backend) m_backend->setChannels(textChannels, voiceChannels);
}

void WindowManager::appendChatMessage(const std::string& message)
{
    if (m_backend) m_backend->appendChatMessage(message);
}

void WindowManager::setChatHistory(const std::vector<std::string>& messages)
{
    if (m_backend) m_backend->setChatHistory(messages);
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

int WindowManager::exec()
{
    if (!m_initialized || !m_app) return -1;
    
    // This hands control to Qt. It will block here at 0% CPU 
    // until the user closes the main window.
    return m_app->exec(); 
}
