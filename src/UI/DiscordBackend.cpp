// DiscordBackend.cpp
#include "UI/DiscordBackend.hpp"

#include <QCoreApplication>
#include <QMetaType>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDate>
#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>

const QStringList DiscordBackend::kAvatarColors = { "#5865F2", "#57F287", "#FEE75C", "#EB459E", "#ED4245", "#3498DB", "#E67E22", "#2ECC71", "#9B59B6", "#1ABC9C", "#E91E63", "#FF5722", "#009688", "#673AB7", "#FF9800" };

/// Client-side boundary check for credentials. The wire payloads are '|'-delimited
/// (see CONTEXT.md §5), so these characters must never reach the protocol.
/// Returns an empty string when the credentials are acceptable, otherwise the error message.
static QString validateCredentials(const QString& user, const QString& pass)
{
    const QString forbiddenCharacters = QStringLiteral("|:=,");

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

DiscordBackend::DiscordBackend(QObject* parent)
    : QObject(parent)
{
    connect(this, &DiscordBackend::_setChannels,  this, &DiscordBackend::onSetChannels,  Qt::QueuedConnection);
    connect(this, &DiscordBackend::_appendMessage,this, &DiscordBackend::onAppendMessage,Qt::QueuedConnection);
    connect(this, &DiscordBackend::_setHistory,   this, &DiscordBackend::onSetHistory,   Qt::QueuedConnection);
    connect(this, &DiscordBackend::_setPeers,     this, &DiscordBackend::onSetPeers,     Qt::QueuedConnection);
    connect(this, &DiscordBackend::_markSpeaker,  this, &DiscordBackend::onMarkSpeaker,  Qt::QueuedConnection);
    connect(this, &DiscordBackend::_setLoginError, this, &DiscordBackend::onSetLoginError, Qt::QueuedConnection);
    connect(this, &DiscordBackend::_confirmLogin,  this, &DiscordBackend::onConfirmLogin,  Qt::QueuedConnection);
    connect(this, &DiscordBackend::_confirmLogout, this, &DiscordBackend::onConfirmLogout, Qt::QueuedConnection);

    m_decayTimer = new QTimer(this);
    m_decayTimer->setSingleShot(true);
    connect(m_decayTimer, &QTimer::timeout, this, &DiscordBackend::onSpeakerDecay);

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
QVariantList DiscordBackend::chatMessages()          const { return m_chatMessages; }
QString      DiscordBackend::loginError()            const { return m_loginError; }
int          DiscordBackend::voiceActivationSensitivity() const { return m_voiceActivationSensitivity.load(); }

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
    m_selectedTextChannelId.store(channelId);
    emit selectedTextChannelIdChanged();
    qDebug() << "[BACKEND] selectedTextChannelId set to" << channelId;
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
    if (name.isEmpty())
    {
        return kAvatarColors[0];
    }

    int idx = 0;
    for (QChar c : name)
    {
        idx = (idx * 31 + c.unicode()) % kAvatarColors.size();
    }

    return kAvatarColors[idx < 0 ? 0 : idx];
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

void DiscordBackend::appendChatMessage(const std::string& message, qint64 timestampMs)
{
    QString s = QString::fromStdString(message);
    QString author, content;
    int sep = s.indexOf(": ");
    
    if (sep > 0)
    {
        author = s.left(sep);
        content = s.mid(sep + 2);
    }
    else
    {
        author = "System";
        content = s;
    }

    QString letter = author.isEmpty() ? "?" : QString(author.at(0).toUpper());
    qint64 tsMs = (timestampMs > 0) ? timestampMs : QDateTime::currentMSecsSinceEpoch();

    QVariantMap msg;
    msg["author"]        = author;
    msg["content"]       = content;
    msg["timestamp"]     = formatRelativeTimestamp(tsMs);
    msg["timestampMs"]   = tsMs;
    msg["avatarColor"]   = avatarColor(author);
    msg["avatarLetter"]  = letter;
    
    emit _appendMessage(msg);
}

void DiscordBackend::setChatHistory(const std::vector<std::pair<std::string, qint64>>& messages)
{
    QVariantList list;
    for (auto& [m, ts] : messages)
    {
        QString s = QString::fromStdString(m);
        QString author, content;
        int sep = s.indexOf(": ");
        
        if (sep > 0)
        {
            author = s.left(sep);
            content = s.mid(sep + 2);
        }
        else
        {
            author = "System";
            content = s;
        }

        qint64 tsMs = (ts > 0) ? ts : QDateTime::currentMSecsSinceEpoch();
        QString letter = author.isEmpty() ? "?" : QString(author.at(0).toUpper());
        
        QVariantMap entry;
        entry["author"]       = author;
        entry["content"]      = content;
        entry["timestamp"]    = formatRelativeTimestamp(tsMs);
        entry["timestampMs"]  = tsMs;
        entry["avatarColor"]  = avatarColor(author);
        entry["avatarLetter"] = letter;
        
        list.append(entry);
    }
    emit _setHistory(list);
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
    QString qUuid = QString::fromStdString(uuid);
    m_speakerTimestamps[qUuid] = QDateTime::currentMSecsSinceEpoch();
    scheduleDecayCheck(); 
    rebuildVoiceChannelsWithPeers();
}

void DiscordBackend::setLoginError(const QString& error)
{
    emit _setLoginError(error);
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

void DiscordBackend::onSetChannels(QVariantList text, QVariantList voice)
{
    m_textChannels      = text;
    m_voiceChannelDefs  = voice;
    emit textChannelsChanged();
    rebuildVoiceChannelsWithPeers();
    emit selectedTextChannelIdChanged();
}

void DiscordBackend::onAppendMessage(QVariantMap msg)
{
    m_chatMessages.append(msg);
    emit chatMessagesChanged();
    emit requestScrollToBottom();
}

void DiscordBackend::onSetHistory(QVariantList msgs)
{
    m_chatMessages = msgs;
    emit chatMessagesChanged();
    emit requestScrollToBottom();
}

void DiscordBackend::onSetPeers(QVariantList peers)
{
    m_voicePeers = peers;
    rebuildVoiceChannelsWithPeers();
}

void DiscordBackend::onMarkSpeaker(QString uuid)
{
    m_speakerTimestamps[uuid] = QDateTime::currentMSecsSinceEpoch();
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

void DiscordBackend::onConfirmLogin(QString username)
{
    m_username = username;
    m_isLoggedIn.store(true);
    emit usernameChanged();
    emit isLoggedInChanged();
    notifyUiChanged();
}

void DiscordBackend::onConfirmLogout()
{
    m_username.clear();
    m_isLoggedIn.store(false);
    emit usernameChanged();
    emit isLoggedInChanged();
    notifyUiChanged();
}