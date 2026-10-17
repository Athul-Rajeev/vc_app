// DiscordBackend.cpp
#include "UI/DiscordBackend.hpp"

#include <QCoreApplication>
#include <QMetaType>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDate>
#include <QFile>
#include <QTextStream>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <utility>

const QStringList DiscordBackend::kAvatarColors = { "#5865F2", "#57F287", "#FEE75C", "#EB459E", "#ED4245", "#3498DB", "#E67E22", "#2ECC71", "#9B59B6", "#1ABC9C", "#E91E63", "#FF5722", "#009688", "#673AB7", "#FF9800" };

/// Client-side boundary check for user-submitted usernames. The wire payloads
/// are '|'-delimited (see CONTEXT.md §5), so protocol characters, spaces and
/// control characters must never reach the server.
/// Returns an empty string when acceptable, otherwise the error message.
static QString validateUsername(const QString& user)
{
    const QString forbiddenCharacters = QStringLiteral("|:=");

    if (user.isEmpty())
    {
        return "Please enter a username.";
    }
    if (user.length() > 32)
    {
        return "Username must be 32 characters or fewer.";
    }
    for (const QChar& character : user)
    {
        if (forbiddenCharacters.contains(character) || character.isSpace() || character.category() == QChar::Other_Control)
        {
            return "Username may not contain spaces or the characters " + forbiddenCharacters + ".";
        }
    }
    return "";
}

/// Client-side boundary check for user-submitted passwords.
/// Returns an empty string when acceptable, otherwise the error message.
static QString validatePassword(const QString& pass)
{
    if (pass.isEmpty())
    {
        return "Please enter a password.";
    }
    if (pass.length() > 128)
    {
        return "Password must be 128 characters or fewer.";
    }
    if (pass.contains('|'))
    {
        return "Password may not contain the '|' character.";
    }
    return "";
}

static QString validateCredentials(const QString& user, const QString& pass)
{
    QString usernameError = validateUsername(user);
    if (!usernameError.isEmpty())
    {
        return usernameError;
    }
    return validatePassword(pass);
}

/// Translates a raw server failure reason into a curated, information-free UI
/// message. The whitelist guarantees the UI can never echo sensitive details
/// that the (untrusted) message text might contain.
static QString curateAuthError(const QString& serverReason)
{
    if (serverReason == "Invalid credentials")
    {
        return "Incorrect username or password.";
    }
    if (serverReason == "Session expired")
    {
        return "Your session expired. Please sign in again.";
    }
    if (serverReason == "Username is already taken")
    {
        return "That username is taken. Try another one.";
    }
    if (serverReason == "Registration failed")
    {
        return "Could not create your account. Try again.";
    }
    if (serverReason == "Too many attempts")
    {
        return "Too many attempts. Wait a few minutes, then try again.";
    }
    if (serverReason == "Invalid or expired reset token")
    {
        return "That reset token is invalid or expired. Ask the server admin for a new one.";
    }
    return "Something went wrong. Try again.";
}

/// Same curation for the (authenticated) password-change flow: the notice is
/// acceptable to show since the user is signed in and only speaks for itself.
static QString curatePasswordNotice(const QString& serverNotice)
{
    if (serverNotice == "Success")
    {
        return "Password updated.";
    }
    if (serverNotice == "Incorrect password")
    {
        return "Current password is incorrect.";
    }
    if (serverNotice == "Too many attempts")
    {
        return "Too many attempts. Wait a few minutes, then try again.";
    }
    if (serverNotice == "Not signed in" || serverNotice == "Invalid message" || serverNotice == "Rejected")
    {
        return "Could not change your password. Try again.";
    }
    return "Could not change your password. Try again.";
}

DiscordBackend::DiscordBackend(QObject* parent)
    : QObject(parent)
{
    connect(this, &DiscordBackend::_setChannels,  this, &DiscordBackend::onSetChannels,  Qt::QueuedConnection);
    connect(this, &DiscordBackend::_appendMessage,this, &DiscordBackend::onAppendMessage,Qt::QueuedConnection);
    connect(this, &DiscordBackend::_setHistory,   this, &DiscordBackend::onSetHistory,   Qt::QueuedConnection);
    connect(this, &DiscordBackend::_prependHistory, this, &DiscordBackend::onPrependHistory, Qt::QueuedConnection);
    connect(this, &DiscordBackend::_setPeers,     this, &DiscordBackend::onSetPeers,     Qt::QueuedConnection);
    connect(this, &DiscordBackend::_markSpeaker,  this, &DiscordBackend::onMarkSpeaker,  Qt::QueuedConnection);
    connect(this, &DiscordBackend::_setLoginError, this, &DiscordBackend::onSetLoginError, Qt::QueuedConnection);
    connect(this, &DiscordBackend::_setLoginState, this, &DiscordBackend::onSetLoginState, Qt::QueuedConnection);
    connect(this, &DiscordBackend::_setPasswordNotice, this, &DiscordBackend::onSetPasswordNotice, Qt::QueuedConnection);
    connect(this, &DiscordBackend::_confirmLogin,  this, &DiscordBackend::onConfirmLogin,  Qt::QueuedConnection);
    connect(this, &DiscordBackend::_confirmLogout, this, &DiscordBackend::onConfirmLogout, Qt::QueuedConnection);

    m_decayTimer = new QTimer(this);
    m_decayTimer->setSingleShot(true);
    connect(m_decayTimer, &QTimer::timeout, this, &DiscordBackend::onSpeakerDecay);

    m_rebuildTimer = new QTimer(this);
    m_rebuildTimer->setSingleShot(true);
    m_rebuildTimer->setInterval(kVoiceRebuildIntervalMs);
    connect(m_rebuildTimer, &QTimer::timeout, this, &DiscordBackend::onVoiceRebuildTick);

    loadSettingsFromFile();
}

DiscordBackend::~DiscordBackend() = default;

void DiscordBackend::scheduleDecayCheck()
{
    m_decayTimer->stop();
    if (m_speakerTimestamps.isEmpty())
    {
        return;
    }

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 earliest = std::numeric_limits<qint64>::max();
    for (auto it = m_speakerTimestamps.begin(); it != m_speakerTimestamps.end(); ++it)
    {
        qint64 expire = it.value() + kSpeakerMs;
        if (expire <= now)
        {
            earliest = now;
            break;
        }
        else if (expire < earliest)
        {
            earliest = expire;
        }
    }

    if (earliest <= now)
    {
        m_decayTimer->start(0);
    }
    else if (earliest < std::numeric_limits<qint64>::max())
    {
        m_decayTimer->start(static_cast<int>(earliest - now));
    }
}

QString      DiscordBackend::username()              const { return m_username; }
bool         DiscordBackend::isLoggedIn()            const { return m_isLoggedIn.load(); }
bool         DiscordBackend::isMuted()               const { return m_isMuted.load(); }
bool         DiscordBackend::isDeafened()            const { return m_isDeafened.load(); }
int          DiscordBackend::selectedTextChannelId() const { return m_selectedTextChannelId.load(); }
int          DiscordBackend::activeVoiceChannelId()  const { return m_activeVoiceChannelId.load(); }
QVariantList DiscordBackend::textChannels()          const { return m_textChannels; }
QVariantList DiscordBackend::voiceChannels()         const { return m_voiceChannels; }
QAbstractListModel* DiscordBackend::chatMessageModel()      { return &m_chatMessageModel; }
bool         DiscordBackend::hasMoreHistory()        const { return m_hasMoreHistory.load(); }
bool         DiscordBackend::historyLoading()        const { return m_historyLoadInFlight.load(); }
QString      DiscordBackend::loginError()            const { return m_loginError; }
QString      DiscordBackend::passwordNotice()        const { return m_passwordNotice; }
int          DiscordBackend::voiceActivationSensitivity() const { return m_voiceActivationSensitivity.load(); }

QString DiscordBackend::loginState() const
{
    switch (m_loginState.load(std::memory_order_relaxed))
    {
        case static_cast<int>(LoginState::Connecting):     return "connecting";
        case static_cast<int>(LoginState::Authenticating): return "authenticating";
        case static_cast<int>(LoginState::LoggedIn):       return "loggedIn";
        default:                                           return "disconnected";
    }
}

double DiscordBackend::voiceActivationThreshold() const
{
    // Logarithmic scale so slider steps feel even: 0 → 0.1 RMS (insensitive),
    // 100 → 0.001 RMS (very sensitive). Default 41 ≈ the historical 0.015 threshold.
    int sensitivity = m_voiceActivationSensitivity.load(std::memory_order_relaxed);
    return 0.1 * std::pow(10.0, -sensitivity / 50.0);
}

void DiscordBackend::login(const QString& user, const QString& pass)
{
    QString validationError = validateCredentials(user, pass);
    if (!validationError.isEmpty())
    {
        setLoginError(validationError);
        return;
    }

    setLoginError("");

    {
        QMutexLocker lock(&m_queueMutex);
        m_pendingLogins.enqueue({user.trimmed().toStdString(), pass.toStdString()});
    }
    notifyUiChanged();
}

void DiscordBackend::registerAccount(const QString& user, const QString& pass, const QString& confirmPass)
{
    QString validationError = validateCredentials(user, pass);
    if (!validationError.isEmpty())
    {
        setLoginError(validationError);
        return;
    }

    if (pass != confirmPass)
    {
        setLoginError("Passwords do not match.");
        return;
    }

    setLoginError("");

    {
        QMutexLocker lock(&m_queueMutex);
        m_pendingRegistrations.enqueue({user.trimmed().toStdString(), pass.toStdString()});
    }
    notifyUiChanged();
}

void DiscordBackend::logout()
{
    {
        QMutexLocker lock(&m_queueMutex);
        m_pendingLogout = true;
    }
    notifyUiChanged();
}

void DiscordBackend::clearLoginError()
{
    setLoginError("");
}

void DiscordBackend::resetPassword(const QString& user, const QString& resetToken, const QString& newPass, const QString& confirmPass)
{
    QString usernameError = validateUsername(user);
    if (!usernameError.isEmpty())
    {
        setLoginError(usernameError);
        return;
    }
    if (resetToken.trimmed().isEmpty())
    {
        setLoginError("Please enter your reset token.");
        return;
    }
    QString passwordError = validatePassword(newPass);
    if (!passwordError.isEmpty())
    {
        setLoginError(passwordError);
        return;
    }
    if (newPass != confirmPass)
    {
        setLoginError("Passwords do not match.");
        return;
    }

    setLoginError("");

    {
        QMutexLocker lock(&m_queueMutex);
        PasswordResetRequest request;
        request.username = user.trimmed().toStdString();
        request.resetToken = resetToken.trimmed().toStdString();
        request.newPassword = newPass.toStdString();
        m_pendingPasswordResets.enqueue(request);
    }
    notifyUiChanged();
}

void DiscordBackend::changePassword(const QString& currentPass, const QString& newPass, const QString& confirmPass)
{
    QString passwordError = validatePassword(currentPass);
    if (passwordError.isEmpty())
    {
        passwordError = validatePassword(newPass);
    }
    if (!passwordError.isEmpty())
    {
        displayPasswordNotice(passwordError);
        return;
    }
    if (newPass != confirmPass)
    {
        displayPasswordNotice("The new passwords do not match.");
        return;
    }
    if (newPass == currentPass)
    {
        displayPasswordNotice("Please choose a password different from your current one.");
        return;
    }

    // Neutral in-progress feedback; the server's verdict replaces it later.
    displayPasswordNotice("Changing password…");

    {
        QMutexLocker lock(&m_queueMutex);
        AuthRequest request;
        request.username = currentPass.toStdString();      // username == current password on the change-password wire
        request.password = newPass.toStdString();
        m_pendingPasswordChanges.enqueue(request);
    }
    notifyUiChanged();
}

void DiscordBackend::clearPasswordNotice()
{
    displayPasswordNotice("");
}

void DiscordBackend::sendMessage(const QString& text)
{
    if (text.trimmed().isEmpty())
    {
        return;
    }

    {
        QMutexLocker lock(&m_queueMutex);
        m_pendingMessages.enqueue(text.trimmed());
    }
    notifyUiChanged();
}

void DiscordBackend::toggleMute()
{
    bool now = !m_isMuted.load();
    m_isMuted.store(now);
    emit isMutedChanged();
    notifyUiChanged();
}

void DiscordBackend::toggleDeafen()
{
    bool now = !m_isDeafened.load();
    m_isDeafened.store(now);
    m_isMuted.store(now);
    emit isDeafenedChanged();
    emit isMutedChanged();
    notifyUiChanged();
}

void DiscordBackend::joinVoiceChannel(int channelId)
{
    m_activeVoiceChannelId.store(channelId);
    emit activeVoiceChannelIdChanged();
    rebuildVoiceChannelsWithPeers();
    notifyUiChanged();
}

void DiscordBackend::leaveVoiceChannel()
{
    m_activeVoiceChannelId.store(-1);
    emit activeVoiceChannelIdChanged();
    m_speakerTimestamps.clear();
    m_decayTimer->stop();
    rebuildVoiceChannelsWithPeers();
    notifyUiChanged();
}

void DiscordBackend::selectTextChannel(int channelId)
{
    int previousChannelId = m_selectedTextChannelId.load();
    if (channelId == previousChannelId)
    {
        return;
    }

    m_selectedTextChannelId.store(channelId);
    emit selectedTextChannelIdChanged();
    qDebug() << "[BACKEND] selectedTextChannelId set to" << channelId;

    // The model still holds the previous channel's page; clear it immediately so
    // the stale rows never flash under the new channel's header. The fresh page
    // arrives via REQ_CHAT_LOG (sent by the control loop on the channel change).
    if (channelId != m_loadedChannelId.load())
    {
        m_chatMessageModel.clear();
        m_hasMoreHistory.store(false);
        emit hasMoreHistoryChanged();
        m_historyLoadInFlight.store(false);
        emit historyLoadingChanged();
    }

    notifyUiChanged();
}

void DiscordBackend::requestNewTextChannel(const QString& name)
{
    if (name.trimmed().isEmpty())
    {
        return;
    }

    {
        QMutexLocker lock(&m_queueMutex);
        m_pendingTextChannels.enqueue(name.trimmed().toLower().replace(' ', '-'));
    }
    notifyUiChanged();
}

void DiscordBackend::requestNewVoiceChannel(const QString& name)
{
    if (name.trimmed().isEmpty()) 
    {
        return;
    }
    
    {
        QMutexLocker lock(&m_queueMutex);
        m_pendingVoiceChannels.enqueue(name.trimmed());
    }
    notifyUiChanged();
}

void DiscordBackend::setVoiceActivationSensitivity(int sensitivity)
{
    int clampedSensitivity = std::clamp(sensitivity, 0, kMaxVoiceActivationSensitivity);
    if (clampedSensitivity == m_voiceActivationSensitivity.load())
    {
        return;
    }

    m_voiceActivationSensitivity.store(clampedSensitivity);
    emit voiceActivationSensitivityChanged();
    saveSettingsToFile();
}

QString DiscordBackend::avatarColor(const QString& name) const
{
    return avatarColorFor(name);
}

QString DiscordBackend::avatarColorFor(const QString& name)
{
    if (name.isEmpty())
    {
        return kAvatarColors[0];
    }

    int colorIndex = 0;
    for (QChar character : name)
    {
        colorIndex = (colorIndex * 31 + character.unicode()) % kAvatarColors.size();
    }

    return kAvatarColors[colorIndex < 0 ? 0 : colorIndex];
}

QString DiscordBackend::channelNameForId(int id) const
{
    for (const auto& v : m_textChannels)
    {
        QVariantMap m = v.toMap();
        if (m["id"].toInt() == id)
        {
            return m["name"].toString();
        }
    }
    return "general";
}

static QString formatRelativeTimestamp(qint64 epochMs)
{
    QDateTime msgDt = QDateTime::fromMSecsSinceEpoch(epochMs);
    QDate today = QDateTime::currentDateTime().date();
    QDate msgDate = msgDt.date();

    if (msgDate == today)
    {
        return QStringLiteral("Today %1").arg(msgDt.toString("h:mm AP"));
    }

    if (msgDate == today.addDays(-1))
    {
        return QStringLiteral("Yesterday %1").arg(msgDt.toString("h:mm AP"));
    }

    return msgDt.toString("MMM d yyyy h:mm AP");
}

void DiscordBackend::setChannels(const std::vector<std::pair<int,std::string>>& text, const std::vector<std::pair<int,std::string>>& voice)
{
    QVariantList t, v;
    for (auto& p : text)
    {
        QVariantMap m;
        m["id"]   = p.first;
        m["name"] = QString::fromStdString(p.second);
        t.append(m);
    }
    for (auto& p : voice)
    {
        QVariantMap m;
        m["id"]   = p.first;
        m["name"] = QString::fromStdString(p.second);
        v.append(m);
    }
    emit _setChannels(t, v);
}

ChatMessageEntry DiscordBackend::buildChatEntry(const QString& rawMessage, qint64 timestampMs)
{
    QString rawText = rawMessage;
    QString author, content;
    int separator = rawText.indexOf(": ");

    if (separator > 0)
    {
        author = rawText.left(separator);
        content = rawText.mid(separator + 2);
    }
    else
    {
        author = "System";
        content = rawText;
    }

    ChatMessageEntry entry;
    entry.author       = author;
    entry.content      = content;
    entry.timestamp    = formatRelativeTimestamp(timestampMs);
    entry.timestampMs  = timestampMs;
    entry.avatarColor  = avatarColorFor(author);
    entry.avatarLetter = author.isEmpty() ? "?" : QString(author.at(0).toUpper());
    return entry;
}

QVariantMap DiscordBackend::chatEntryToMap(const ChatMessageEntry& entry)
{
    QVariantMap map;
    map["author"]        = entry.author;
    map["content"]       = entry.content;
    map["timestamp"]     = entry.timestamp;
    map["timestampMs"]   = entry.timestampMs;
    map["avatarColor"]   = entry.avatarColor;
    map["avatarLetter"]  = entry.avatarLetter;
    return map;
}

void DiscordBackend::appendChatMessage(int channelId, const std::string& message, qint64 timestampMs)
{
    qint64 effectiveTimestampMs = (timestampMs > 0) ? timestampMs : QDateTime::currentMSecsSinceEpoch();
    emit _appendMessage(channelId, chatEntryToMap(buildChatEntry(QString::fromStdString(message), effectiveTimestampMs)));
}

void DiscordBackend::setChatHistory(int channelId, bool hasMoreHistory, const std::vector<std::pair<std::string, qint64>>& messages)
{
    QVariantList historyList;
    for (const auto& [message, timestamp] : messages)
    {
        qint64 effectiveTimestampMs = (timestamp > 0) ? timestamp : QDateTime::currentMSecsSinceEpoch();
        historyList.append(chatEntryToMap(buildChatEntry(QString::fromStdString(message), effectiveTimestampMs)));
    }
    emit _setHistory(channelId, hasMoreHistory, historyList);
}

void DiscordBackend::prependChatHistory(int channelId, bool hasMoreHistory, const std::vector<std::pair<std::string, qint64>>& messages)
{
    QVariantList historyList;
    for (const auto& [message, timestamp] : messages)
    {
        qint64 effectiveTimestampMs = (timestamp > 0) ? timestamp : QDateTime::currentMSecsSinceEpoch();
        historyList.append(chatEntryToMap(buildChatEntry(QString::fromStdString(message), effectiveTimestampMs)));
    }
    emit _prependHistory(channelId, hasMoreHistory, historyList);
}

void DiscordBackend::requestOlderMessages()
{
    if (m_historyLoadInFlight.load() || !m_hasMoreHistory.load())
    {
        return;
    }

    int selectedChannelId = m_selectedTextChannelId.load();
    if (selectedChannelId != m_loadedChannelId.load())
    {
        // The page for this channel has not arrived yet — paging is anchored on
        // the freshly loaded model, not on leftovers of the previous channel.
        return;
    }

    qint64 oldestLoadedTimestampMs = m_chatMessageModel.oldestTimestampMs();
    if (oldestLoadedTimestampMs <= 0)
    {
        return;
    }

    m_historyLoadInFlight.store(true);
    emit historyLoadingChanged();

    {
        QMutexLocker lock(&m_queueMutex);
        m_pendingHistoryRequests.enqueue({selectedChannelId, oldestLoadedTimestampMs});
    }
    notifyUiChanged();
}

void DiscordBackend::setVoicePeers(const std::vector<std::string>& peerDataList)
{
    QVariantList peers;
    for (auto& e : peerDataList)
    {
        QVariantMap p = parsePeerEntry(QString::fromStdString(e));
        if (!p.isEmpty())
        {
            peers.append(p);
        }
    }
    emit _setPeers(peers);
}

void DiscordBackend::markSpeakerActive(const std::string& uuid)
{
    // Voice packets arrive on a network thread at up to 50 Hz; routing through
    // the queued relay keeps all model/indicator mutations on the main thread.
    emit _markSpeaker(QString::fromStdString(uuid));
}

void DiscordBackend::setLoginError(const QString& error)
{
    emit _setLoginError(error);
}

void DiscordBackend::setAuthError(const std::string& serverReason)
{
    emit _setLoginError(curateAuthError(QString::fromStdString(serverReason)));
}

void DiscordBackend::setPasswordNotice(const std::string& serverNotice)
{
    displayPasswordNotice(curatePasswordNotice(QString::fromStdString(serverNotice)));
}

void DiscordBackend::displayPasswordNotice(const QString& notice)
{
    emit _setPasswordNotice(notice);
}

void DiscordBackend::setLoginState(LoginState state)
{
    int newState = static_cast<int>(state);
    if (m_loginState.load(std::memory_order_relaxed) == newState)
    {
        return;   // Setting an unchanged state must not churn the UI
    }
    emit _setLoginState(newState);
}

std::string DiscordBackend::dequeuePendingMessage()
{
    QMutexLocker lock(&m_queueMutex);
    return m_pendingMessages.isEmpty() ? "" : m_pendingMessages.dequeue().toStdString();
}

std::string DiscordBackend::dequeuePendingTextChannel()
{
    QMutexLocker lock(&m_queueMutex);
    return m_pendingTextChannels.isEmpty() ? "" : m_pendingTextChannels.dequeue().toStdString();
}

std::string DiscordBackend::dequeuePendingVoiceChannel()
{
    QMutexLocker lock(&m_queueMutex);
    return m_pendingVoiceChannels.isEmpty() ? "" : m_pendingVoiceChannels.dequeue().toStdString();
}

bool DiscordBackend::dequeuePendingLogin(AuthRequest& outRequest)
{
    QMutexLocker lock(&m_queueMutex);
    if (!m_pendingLogins.isEmpty())
    {
        outRequest = m_pendingLogins.dequeue();
        return true;
    }
    return false;
}

bool DiscordBackend::dequeuePendingRegistration(AuthRequest& outRequest)
{
    QMutexLocker lock(&m_queueMutex);
    if (!m_pendingRegistrations.isEmpty())
    {
        outRequest = m_pendingRegistrations.dequeue();
        return true;
    }
    return false;
}

bool DiscordBackend::dequeuePendingPasswordChange(AuthRequest& outRequest)
{
    QMutexLocker lock(&m_queueMutex);
    if (!m_pendingPasswordChanges.isEmpty())
    {
        outRequest = m_pendingPasswordChanges.dequeue();
        return true;
    }
    return false;
}

bool DiscordBackend::dequeuePendingPasswordReset(PasswordResetRequest& outRequest)
{
    QMutexLocker lock(&m_queueMutex);
    if (!m_pendingPasswordResets.isEmpty())
    {
        outRequest = m_pendingPasswordResets.dequeue();
        return true;
    }
    return false;
}

bool DiscordBackend::dequeuePendingLogout()
{
    QMutexLocker lock(&m_queueMutex);
    if (m_pendingLogout)
    {
        m_pendingLogout = false;
        return true;
    }
    return false;
}

bool DiscordBackend::dequeuePendingHistoryRequest(int& outChannelId, qint64& outBeforeTimestampMs)
{
    QMutexLocker lock(&m_queueMutex);
    if (m_pendingHistoryRequests.isEmpty())
    {
        return false;
    }

    auto [requestChannelId, requestBeforeTimestampMs] = m_pendingHistoryRequests.dequeue();
    outChannelId = requestChannelId;
    outBeforeTimestampMs = requestBeforeTimestampMs;
    return true;
}

void DiscordBackend::onSetChannels(QVariantList text, QVariantList voice)
{
    m_textChannels      = text;
    m_voiceChannelDefs  = voice;
    emit textChannelsChanged();
    rebuildVoiceChannelsWithPeers();
    emit selectedTextChannelIdChanged();
}

void DiscordBackend::onAppendMessage(int channelId, QVariantMap msg)
{
    // Only apply messages for the channel whose page the model currently
    // displays. Broadcasts for other channels would silently corrupt this view.
    if (channelId != m_loadedChannelId.load())
    {
        return;
    }

    ChatMessageEntry entry;
    entry.author       = msg["author"].toString();
    entry.content      = msg["content"].toString();
    entry.timestamp    = msg["timestamp"].toString();
    entry.timestampMs  = msg["timestampMs"].toLongLong();
    entry.avatarColor  = msg["avatarColor"].toString();
    entry.avatarLetter = msg["avatarLetter"].toString();

    m_chatMessageModel.appendEntry(entry);
    m_chatMessageModel.trimToLimit(kChatHistoryMemoryLimit);
    emit requestScrollToBottom();
}

void DiscordBackend::onSetHistory(int channelId, bool hasMoreHistory, QVariantList msgs)
{
    // Stale response guard: the user may have switched channels (or the request
    // may predate a reconnect). Never render a page for a channel the user has
    // already left — it would splice foreign messages into the current view.
    if (channelId != m_selectedTextChannelId.load())
    {
        spdlog::debug("Discarded chat history page for channel {} (now on {})",
                      channelId, m_selectedTextChannelId.load());
        return;
    }

    QList<ChatMessageEntry> entries;
    for (const QVariant& item : msgs)
    {
        QVariantMap msg = item.toMap();
        ChatMessageEntry entry;
        entry.author       = msg["author"].toString();
        entry.content      = msg["content"].toString();
        entry.timestamp    = msg["timestamp"].toString();
        entry.timestampMs  = msg["timestampMs"].toLongLong();
        entry.avatarColor  = msg["avatarColor"].toString();
        entry.avatarLetter = msg["avatarLetter"].toString();
        entries.append(entry);
    }

    m_chatMessageModel.setHistory(entries);
    m_loadedChannelId.store(channelId);
    m_hasMoreHistory.store(hasMoreHistory);
    emit hasMoreHistoryChanged();
    emit requestScrollToBottom();
}

void DiscordBackend::onPrependHistory(int channelId, bool hasMoreHistory, QVariantList msgs)
{
    // Whatever happened, the in-flight page request is now finished.
    bool requestWasInFlight = m_historyLoadInFlight.exchange(false);
    if (requestWasInFlight)
    {
        emit historyLoadingChanged();
    }

    if (channelId != m_loadedChannelId.load())
    {
        // The user switched channels while the page was in flight; dropping the
        // response is correct — the fresh channel's page replaces the model.
        spdlog::debug("Discarded stale older-history page for channel {}", channelId);
        return;
    }

    QList<ChatMessageEntry> entries;
    for (const QVariant& item : msgs)
    {
        QVariantMap msg = item.toMap();
        ChatMessageEntry entry;
        entry.author       = msg["author"].toString();
        entry.content      = msg["content"].toString();
        entry.timestamp    = msg["timestamp"].toString();
        entry.timestampMs  = msg["timestampMs"].toLongLong();
        entry.avatarColor  = msg["avatarColor"].toString();
        entry.avatarLetter = msg["avatarLetter"].toString();
        entries.append(entry);
    }

    m_chatMessageModel.prependEntries(entries);
    m_hasMoreHistory.store(hasMoreHistory);
    emit hasMoreHistoryChanged();
}

void DiscordBackend::onSetPeers(QVariantList peers)
{
    m_voicePeers = peers;
    rebuildVoiceChannelsWithPeers();
}

void DiscordBackend::onMarkSpeaker(QString uuid)
{
    m_speakerTimestamps[uuid] = QDateTime::currentMSecsSinceEpoch();
    scheduleDecayCheck();
    scheduleVoiceRebuild();
}

void DiscordBackend::scheduleVoiceRebuild()
{
    // Rebuilding the voice-channel model was triggered on EVERY voice packet
    // (up to 50 Hz per speaker). Gating through a 100 ms single-shot timer
    // bounds indicator updates to ~10 Hz without noticeable latency.
    if (m_rebuildTimer->isActive())
    {
        return;
    }
    m_rebuildTimer->start();
}

void DiscordBackend::onVoiceRebuildTick()
{
    rebuildVoiceChannelsWithPeers();
}

void DiscordBackend::onSpeakerDecay()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    auto it = m_speakerTimestamps.begin();
    
    while (it != m_speakerTimestamps.end())
    {
        if (now - it.value() >= kSpeakerMs)
        {
            it = m_speakerTimestamps.erase(it);
            changed = true;
        }
        else
        {
            ++it;
        }
    }
    
    if (changed)
    {
        rebuildVoiceChannelsWithPeers();
    }
    
    scheduleDecayCheck();
}

void DiscordBackend::rebuildVoiceChannelsWithPeers()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_voiceChannels.clear();
    int activeId = m_activeVoiceChannelId.load();

    for (const auto& def : m_voiceChannelDefs)
    {
        QVariantMap ch = def.toMap();
        int id = ch["id"].toInt();

        QVariantList peers;
        for (const auto& pv : m_voicePeers)
        {
            QVariantMap p = pv.toMap();
            if (p["channelId"].toInt() != id)
            {
                continue;
            }
            
            QString uuid = p["uuid"].toString();
            bool speaking = m_speakerTimestamps.contains(uuid) && (now - m_speakerTimestamps[uuid]) < kSpeakerMs;
            QString uname = p["username"].toString();
            QString letter = uname.isEmpty() ? QString("?") : QString(uname.at(0).toUpper());
            
            QVariantMap pm = p;
            pm["isSpeaking"]   = speaking;
            pm["avatarColor"]  = avatarColor(uname);
            pm["avatarLetter"] = letter;
            
            peers.append(pm);
        }

        ch["peers"]    = peers;
        ch["isActive"] = (id == activeId);
        m_voiceChannels.append(ch);
    }
    emit voiceChannelsChanged();
}

QVariantMap DiscordBackend::parsePeerEntry(const QString& entry)
{
    QStringList p = entry.split(':');
    if (p.size() < 5)
    {
        return {};
    }
    
    QVariantMap m;
    m["username"]   = p[0];
    m["isMuted"]    = (p[1] == "1");
    m["isDeafened"] = (p[2] == "1");
    m["uuid"]       = p[3];
    m["channelId"]  = p[4].toInt();
    m["isSpeaking"] = false;
    
    return m;
}

void DiscordBackend::setNotifyCallback(std::function<void()> cb)
{
    m_uiCallback = std::move(cb);
}

void DiscordBackend::loadSettingsFromFile()
{
    QFile settingsFile(kSettingsFile);
    if (!settingsFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return;   // No saved settings yet — defaults apply.
    }

    // Simple "key=value" lines; the file is written by saveSettingsToFile().
    QTextStream streamIn(&settingsFile);
    while (!streamIn.atEnd())
    {
        QString line = streamIn.readLine();
        int separatorIndex = line.indexOf('=');
        if (separatorIndex <= 0)
        {
            continue;
        }

        QString key = line.left(separatorIndex);
        QString value = line.mid(separatorIndex + 1);

        if (key == "voiceActivationSensitivity")
        {
            bool parsedOk = false;
            int sensitivity = value.toInt(&parsedOk);
            if (parsedOk)
            {
                m_voiceActivationSensitivity.store(std::clamp(sensitivity, 0, kMaxVoiceActivationSensitivity));
            }
        }
    }
}

void DiscordBackend::saveSettingsToFile()
{
    QFile settingsFile(kSettingsFile);
    if (!settingsFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        qWarning() << "[BACKEND] Could not write settings file:" << kSettingsFile;
        return;
    }

    QTextStream streamOut(&settingsFile);
    streamOut << "voiceActivationSensitivity=" << m_voiceActivationSensitivity.load() << "\n";
}

void DiscordBackend::notifyUiChanged()
{
    if (m_uiCallback)
    {
        m_uiCallback();
    }
}

void DiscordBackend::confirmLogin(const std::string& username)
{
    emit _confirmLogin(QString::fromStdString(username));
}

void DiscordBackend::confirmLogout()
{
    emit _confirmLogout();
}

void DiscordBackend::onSetLoginError(QString error)
{
    m_loginError = error;
    emit loginErrorChanged();
    notifyUiChanged();
}

void DiscordBackend::onSetLoginState(int state)
{
    if (m_loginState.load(std::memory_order_relaxed) == state)
    {
        return;
    }
    m_loginState.store(state, std::memory_order_relaxed);
    emit loginStateChanged();
}

void DiscordBackend::onSetPasswordNotice(QString notice)
{
    m_passwordNotice = notice;
    emit passwordNoticeChanged();
    notifyUiChanged();
}

void DiscordBackend::onConfirmLogin(QString username)
{
    m_username = username;
    m_isLoggedIn.store(true);
    m_loginError.clear();
    emit loginErrorChanged();
    m_loginState.store(static_cast<int>(LoginState::LoggedIn), std::memory_order_relaxed);
    emit loginStateChanged();
    emit usernameChanged();
    emit isLoggedInChanged();
    notifyUiChanged();
}

void DiscordBackend::onConfirmLogout()
{
    m_username.clear();
    m_isLoggedIn.store(false);
    // NOTE: m_loginError is deliberately preserved here — an AUTH_FAILED push
    // sets the error first and confirms the logout second, so clearing it in
    // this slot would blank the message the user should see.
    m_loginState.store(static_cast<int>(LoginState::Disconnected), std::memory_order_relaxed);

    // The chat belongs to the ended session; drop it along with the paging
    // state so a re-login starts from a clean, empty model.
    m_chatMessageModel.clear();
    m_hasMoreHistory.store(false);
    emit hasMoreHistoryChanged();
    m_historyLoadInFlight.store(false);
    emit historyLoadingChanged();
    m_loadedChannelId.store(-1);
    {
        QMutexLocker lock(&m_queueMutex);
        m_pendingHistoryRequests.clear();
    }

    emit loginStateChanged();
    emit usernameChanged();
    emit isLoggedInChanged();
    notifyUiChanged();
}