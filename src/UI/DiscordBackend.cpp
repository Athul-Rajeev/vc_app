#include "UI/DiscordBackend.hpp"

#include <QCoreApplication>
#include <QMetaType>
#include <QRegularExpression>
#include <QFileInfo>

// ─────────────────────────────────────────────────────────────
const QStringList DiscordBackend::kAvatarColors = {
    "#5865F2","#57F287","#FEE75C","#EB459E","#ED4245",
    "#3498DB","#E67E22","#2ECC71","#9B59B6","#1ABC9C",
    "#E91E63","#FF5722","#009688","#673AB7","#FF9800"
};

// ─────────────────────────────────────────────────────────────
DiscordBackend::DiscordBackend(QObject* parent)
    : QObject(parent)
{
    // Wire cross-thread relay signals
    connect(this, &DiscordBackend::_setChannels,  this, &DiscordBackend::onSetChannels,  Qt::QueuedConnection);
    connect(this, &DiscordBackend::_appendMessage,this, &DiscordBackend::onAppendMessage,Qt::QueuedConnection);
    connect(this, &DiscordBackend::_setHistory,   this, &DiscordBackend::onSetHistory,   Qt::QueuedConnection);
    connect(this, &DiscordBackend::_setPeers,     this, &DiscordBackend::onSetPeers,     Qt::QueuedConnection);
    connect(this, &DiscordBackend::_markSpeaker,  this, &DiscordBackend::onMarkSpeaker,  Qt::QueuedConnection);

    m_speakerTimer = new QTimer(this);
    m_speakerTimer->setInterval(50);
    connect(m_speakerTimer, &QTimer::timeout, this, &DiscordBackend::onSpeakerDecay);
    m_speakerTimer->start();
}

DiscordBackend::~DiscordBackend() = default;

// ─────────────────────────────────────────────────────────────
// Property reads
// ─────────────────────────────────────────────────────────────
QString      DiscordBackend::username()              const { return m_username; }
bool         DiscordBackend::isLoggedIn()            const { return m_isLoggedIn.load(); }
bool         DiscordBackend::isMuted()               const { return m_isMuted.load(); }
bool         DiscordBackend::isDeafened()            const { return m_isDeafened.load(); }
int          DiscordBackend::selectedTextChannelId() const { return m_selectedTextChannelId.load(); }
int          DiscordBackend::activeVoiceChannelId()  const { return m_activeVoiceChannelId.load(); }
QVariantList DiscordBackend::textChannels()          const { return m_textChannels; }
QVariantList DiscordBackend::voiceChannels()         const { return m_voiceChannels; }
QVariantList DiscordBackend::chatMessages()          const { return m_chatMessages; }

// ─────────────────────────────────────────────────────────────
// QML-invokable actions
// ─────────────────────────────────────────────────────────────
void DiscordBackend::login(const QString& user, const QString& pass)
{
    if (user.trimmed().isEmpty() || pass.trimmed().isEmpty()) 
    {
        return;
    }
    
    {
        QMutexLocker lock(&m_queueMutex);
        m_pendingLogins.enqueue({user.trimmed().toStdString(), pass.trimmed().toStdString()});
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

void DiscordBackend::sendMessage(const QString& text)
{
    if (text.trimmed().isEmpty()) return;
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
    rebuildVoiceChannelsWithPeers();
    notifyUiChanged();
}

void DiscordBackend::selectTextChannel(int channelId)
{
    m_selectedTextChannelId.store(channelId);
    emit selectedTextChannelIdChanged();
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

QString DiscordBackend::avatarColor(const QString& name) const
{
    if (name.isEmpty()) return kAvatarColors[0];
    int idx = 0;
    for (QChar c : name) idx = (idx * 31 + c.unicode()) % kAvatarColors.size();
    return kAvatarColors[idx < 0 ? 0 : idx];
}

QString DiscordBackend::channelNameForId(int id) const
{
    for (const auto& v : m_textChannels) {
        QVariantMap m = v.toMap();
        if (m["id"].toInt() == id) return m["name"].toString();
    }
    return "general";
}

// ─────────────────────────────────────────────────────────────
// Thread-safe setters
// ─────────────────────────────────────────────────────────────
void DiscordBackend::setChannels(const std::vector<std::pair<int,std::string>>& text,
                                  const std::vector<std::pair<int,std::string>>& voice)
{
    QVariantList t, v;
    for (auto& p : text) {
        QVariantMap m;
        m["id"]   = p.first;
        m["name"] = QString::fromStdString(p.second);
        t.append(m);
    }
    for (auto& p : voice) {
        QVariantMap m;
        m["id"]   = p.first;
        m["name"] = QString::fromStdString(p.second);
        v.append(m);
    }
    emit _setChannels(t, v);
}

void DiscordBackend::appendChatMessage(const std::string& message)
{
    QString s = QString::fromStdString(message);
    QString author, content;
    int sep = s.indexOf(": ");
    if (sep > 0) { author = s.left(sep); content = s.mid(sep + 2); }
    else          { author = "System";   content = s; }

    QString letter = author.isEmpty() ? QString("?") : QString(author.at(0).toUpper());
    QVariantMap msg;
    msg["author"]      = author;
    msg["content"]     = content;
    msg["timestamp"]   = QDateTime::currentDateTime().toString("h:mm AP");
    msg["avatarColor"] = avatarColor(author);
    msg["avatarLetter"]= letter;
    emit _appendMessage(msg);
}

void DiscordBackend::setChatHistory(const std::vector<std::string>& messages)
{
    QVariantList list;
    for (auto& m : messages) {
        QString s = QString::fromStdString(m);
        QString author, content;
        int sep = s.indexOf(": ");
        if (sep > 0) { author = s.left(sep); content = s.mid(sep + 2); }
        else          { author = "System";   content = s; }

        QString letter = author.isEmpty() ? QString("?") : QString(author.at(0).toUpper());
        QVariantMap entry;
        entry["author"]      = author;
        entry["content"]     = content;
        entry["timestamp"]   = QDateTime::currentDateTime().toString("h:mm AP");
        entry["avatarColor"] = avatarColor(author);
        entry["avatarLetter"]= letter;
        list.append(entry);
    }
    emit _setHistory(list);
}

void DiscordBackend::setVoicePeers(const std::vector<std::string>& peerDataList)
{
    QVariantList peers;
    for (auto& e : peerDataList) {
        QVariantMap p = parsePeerEntry(QString::fromStdString(e));
        if (!p.isEmpty()) peers.append(p);
    }
    emit _setPeers(peers);
}

void DiscordBackend::markSpeakerActive(const std::string& uuid)
{
    emit _markSpeaker(QString::fromStdString(uuid));
}

// ─────────────────────────────────────────────────────────────
// Output queue dequeue
// ─────────────────────────────────────────────────────────────
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

bool DiscordBackend::dequeuePendingLogin(LoginRequest& outRequest)
{
    QMutexLocker lock(&m_queueMutex);
    if (!m_pendingLogins.isEmpty())
    {
        outRequest = m_pendingLogins.dequeue();
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

// ─────────────────────────────────────────────────────────────
// Private slots
// ─────────────────────────────────────────────────────────────
void DiscordBackend::onSetChannels(QVariantList text, QVariantList voice)
{
    m_textChannels      = text;
    m_voiceChannelDefs  = voice;
    emit textChannelsChanged();
    rebuildVoiceChannelsWithPeers();
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
    for (auto it = m_speakerTimestamps.begin(); it != m_speakerTimestamps.end(); ) {
        if (now - it.value() > kSpeakerMs) {
            it = m_speakerTimestamps.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) rebuildVoiceChannelsWithPeers();
}

// ─────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────
void DiscordBackend::rebuildVoiceChannelsWithPeers()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_voiceChannels.clear();
    int activeId = m_activeVoiceChannelId.load();

    for (const auto& def : m_voiceChannelDefs) {
        QVariantMap ch = def.toMap();
        int id = ch["id"].toInt();

        QVariantList peers;
        for (const auto& pv : m_voicePeers) {
            QVariantMap p = pv.toMap();
            if (p["channelId"].toInt() != id) continue;
            QString uuid = p["uuid"].toString();
            bool speaking = m_speakerTimestamps.contains(uuid)
                            && (now - m_speakerTimestamps[uuid]) < kSpeakerMs;
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
    if (p.size() < 5) return {};
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

void DiscordBackend::notifyUiChanged()
{
    if (m_uiCallback)
    {
        m_uiCallback();
    }
}

void DiscordBackend::confirmLogin(const std::string& username)
{
    m_username = QString::fromStdString(username);
    m_isLoggedIn.store(true);
    emit usernameChanged();
    emit isLoggedInChanged();
    notifyUiChanged();
}

void DiscordBackend::confirmLogout()
{
    m_username = "";
    m_isLoggedIn.store(false);
    emit usernameChanged();
    emit isLoggedInChanged();
    notifyUiChanged();
}