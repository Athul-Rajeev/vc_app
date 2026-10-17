// ChatMessageModel.cpp
#include "UI/ChatMessageModel.hpp"

ChatMessageModel::ChatMessageModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ChatMessageModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant ChatMessageModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
    {
        return {};
    }

    const ChatMessageEntry& entry = m_entries.at(index.row());
    switch (role)
    {
        case AuthorRole:        return entry.author;        case ContentRole:       return entry.content;
        case TimestampRole:     return entry.timestamp;
        case TimestampMsRole:   return entry.timestampMs;
        case AvatarColorRole:   return entry.avatarColor;
        case AvatarLetterRole:  return entry.avatarLetter;
        case ShowDividerRole:   return entry.showDivider;
        case IsGroupedRole:     return entry.isGrouped;
        default:                return {};
    }
}

QHash<int, QByteArray> ChatMessageModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[AuthorRole]        = "author";
    roles[ContentRole]       = "content";
    roles[TimestampRole]     = "timestamp";
    roles[TimestampMsRole]   = "timestampMs";
    roles[AvatarColorRole]   = "avatarColor";
    roles[AvatarLetterRole]  = "avatarLetter";
    roles[ShowDividerRole]   = "showDivider";
    roles[IsGroupedRole]     = "isGrouped";
    return roles;
}

int ChatMessageModel::count() const
{
    return m_entries.size();
}

bool ChatMessageModel::isEmpty() const
{
    return m_entries.isEmpty();
}

qint64 ChatMessageModel::oldestTimestampMs() const
{
    return m_entries.isEmpty() ? 0 : m_entries.first().timestampMs;
}

void ChatMessageModel::setHistory(const QList<ChatMessageEntry>& entries)
{
    beginResetModel();
    m_entries = entries;
    refreshGrouping();
    endResetModel();
}

void ChatMessageModel::appendEntry(const ChatMessageEntry& entry)
{
    int newRow = m_entries.size();
    beginInsertRows(QModelIndex(), newRow, newRow);
    m_entries.append(entry);
    refreshGrouping();
    endInsertRows();
}

void ChatMessageModel::prependEntries(const QList<ChatMessageEntry>& entries)
{
    if (entries.isEmpty())
    {
        return;
    }

    beginInsertRows(QModelIndex(), 0, entries.size() - 1);
    m_entries = entries + m_entries;
    refreshGrouping();
    endInsertRows();
}

void ChatMessageModel::clear()
{
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

void ChatMessageModel::trimToLimit(int limit)
{
    int excess = m_entries.size() - limit;
    if (excess <= 0)
    {
        return;
    }

    // History grows at the tail; the cap sacrifices the OLDEST rows (the front).
    beginRemoveRows(QModelIndex(), 0, excess - 1);
    for (int removed = 0; removed < excess; ++removed)
    {
        m_entries.removeFirst();
    }
    refreshGrouping();
    endRemoveRows();
}

void ChatMessageModel::refreshGrouping()
{
    constexpr qint64 kTimeGap = kTimeGapThresholdMs;

    for (int row = 0; row < m_entries.size(); ++row)
    {
        ChatMessageEntry& entry = m_entries[row];
        if (row == 0)
        {
            entry.showDivider = false;   // Top of the list: the welcome header plays that role
            entry.isGrouped   = false;
            continue;
        }

        const ChatMessageEntry& previous = m_entries.at(row - 1);
        qint64 timeDifference = entry.timestampMs - previous.timestampMs;

        entry.showDivider = timeDifference > kTimeGap;
        entry.isGrouped   = !entry.showDivider && previous.author == entry.author;
    }
}
