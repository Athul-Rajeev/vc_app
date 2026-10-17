#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtGlobal>

/**
 * One resolved chat message row. The heavy lifting (author/content splitting,
 * timestamp formatting, avatar color hashing) happens where the message enters
 * the system; the model only stores and publishes the finished row.
 */
struct ChatMessageEntry
{
    QString author;
    QString content;
    QString timestamp;      // Human-readable ("Today 3:24 PM")
    qint64  timestampMs;    // Epoch milliseconds — the paging cursor
    QString avatarColor;
    QString avatarLetter;
    bool showDivider;      ///< A day/time divider belongs above this row
    bool isGrouped;        ///< Same-author continuation row (compact rendering)
};

/**
 * ChatMessageModel
 * ────────────────
 * QAbstractListModel backing the chat view.
 *
 * Replaces the old QVariantList property which re-emitted (and re-created)
 * the ENTIRE message list on every appended message — that full-model reset is
 * what made the ListView jump to the top and back on each new message.
 * This model appends/prepends rows incrementally, so QML ListView inserts
 * delegates without a reset and keeps its scroll position.
 *
 * Lives on the main thread; all mutations must happen there (DiscordBackend
 * bridges incoming thread-safe calls onto the main thread via queued signals).
 */
class ChatMessageModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum ChatMessageRoles
    {
        AuthorRole = Qt::UserRole + 1,
        ContentRole,
        TimestampRole,
        TimestampMsRole,
        AvatarColorRole,
        AvatarLetterRole,
        ShowDividerRole,   ///< A "Today/yesterday" divider belongs above this row
        IsGroupedRole,     ///< Consecutive message by the same author: compact row
    };

    explicit ChatMessageModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // ── Queries ─────────────────────────────────────────────
    int     count() const;
    bool    isEmpty() const;
    /// Timestamp of the oldest loaded row; the cursor for "load older" pages.
    /// Returns 0 when empty.
    qint64  oldestTimestampMs() const;

    // ── Mutations (main thread only) ────────────────────────
    void setHistory(const QList<ChatMessageEntry>& entries);
    void appendEntry(const ChatMessageEntry& entry);
    void prependEntries(const QList<ChatMessageEntry>& entries);
    void clear();
    void trimToLimit(int limit);

private:
    /// Recomputes the derived ShowDivider/IsGrouped flags after any mutation.
    void refreshGrouping();

    QList<ChatMessageEntry> m_entries;

    static constexpr int kTimeGapThresholdMs = 5 * 60 * 1000;  // Divider over 5-minute gaps
};
