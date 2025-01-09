// Copyright (C) 2004-2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QObject>
#include <QDateTime>
#include <QtQml/qqmlregistration.h>

#include "bricklink/global.h"
#include "bricklink/color.h"
#include "bricklink/category.h"
#include "bricklink/item.h"
#include "bricklink/itemtype.h"
#include "bricklink/changelogentry.h"
#include "bricklink/partcolorcode.h"
#include "bricklink/relationship.h"
#include "utility/memoryresource.h"


class Transfer;
class TransferJob;

namespace BrickLink {

class Database : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
    Q_PROPERTY(bool valid READ isValid NOTIFY validChanged FINAL)
    Q_PROPERTY(BrickLink::UpdateStatus updateStatus READ updateStatus NOTIFY updateStatusChanged FINAL)
    Q_PROPERTY(QDateTime lastUpdated READ lastUpdated NOTIFY lastUpdatedChanged FINAL)

public:
    ~Database() override;

    enum class Version {
        Invalid,
        V1,  // deprecated
        V2,  // deprecated
        V3,  // deprecated
        V4,  // 2021.5.1
        V5,  // 2022.1.1
        V6,  // 2022.2.1
        V7,  // 2022.6.1 (not released)
        V8,  // 2022.9.1
        V9,  // 2023.3.1
        V10, // 2023.11.1
        V11, // 2024.1.2
        V12, // 2024.3.1

        OldestStillSupported = V6,

        Latest = V12
    };

    void setUpdateInterval(int interval);

    Q_INVOKABLE bool isUpdateNeeded() const;

    bool isValid() const          { return m_valid; }
    QDateTime lastUpdated() const { return m_lastUpdated; }
    BrickLink::UpdateStatus updateStatus() const  { return m_updateStatus; }

    static QString defaultDatabaseName(Version version = Version::Latest);

    bool startUpdate();
    bool startUpdate(bool force);
    void cancelUpdate();

    void read(const QString &fileName = { });
    void write(const QString &fileName, Version version) const;

    static void remove();

signals:
    void updateStarted();
    void updateProgress(int received, int total);
    void updateFinished(bool success, const QString &message);
    void updateStatusChanged(BrickLink::UpdateStatus updateStatus);
    void lastUpdatedChanged(const QDateTime &lastUpdated);
    void validChanged(bool valid);
    void databaseAboutToBeReset();
    void databaseReset();

private:
    Database(const QString &updateUrl, QObject *parent = nullptr);
    void setUpdateStatus(UpdateStatus updateStatus);
    QString dumpDatabaseInformation(const QString &title, bool itemTypeInfo, bool apiQuirksInfo) const;

    void clear();

    QString m_updateUrl;
    bool m_valid = false;
    BrickLink::UpdateStatus m_updateStatus = BrickLink::UpdateStatus::UpdateFailed;
    int m_updateInterval = 0;
    QDateTime m_lastUpdated;
    QString m_etag;
    Transfer *m_transfer;
    TransferJob *m_job = nullptr;

    std::unique_ptr<MemoryResource>  m_pool;
    std::vector<Color>               m_colors;
    std::vector<Color>               m_ldrawExtraColors;
    std::vector<Category>            m_categories;
    std::vector<ItemType>            m_itemTypes;
    std::vector<Item>                m_items;
    std::vector<ItemChangeLogEntry>  m_itemChangelog;
    std::vector<ColorChangeLogEntry> m_colorChangelog;
    std::vector<Relationship>        m_relationships;
    std::vector<RelationshipMatch>   m_relationshipMatches;
    QHash<QByteArray, QString>       m_apiKeys;
    QSet<ApiQuirk>                   m_apiQuirks;

    uint m_latestChangelogId = 0;

    friend class Core;
    friend class TextImport;

    // IO

    static void readColorFromDatabase(Color &col, QDataStream &dataStream, MemoryResource *pool);
    void writeColorToDatabase(const Color &color, QDataStream &dataStream, Version v) const;
    static void readCategoryFromDatabase(Category &cat, QDataStream &dataStream, MemoryResource *pool);
    void writeCategoryToDatabase(const Category &category, QDataStream &dataStream, Version v) const;
    static void readItemTypeFromDatabase(ItemType &itt, QDataStream &dataStream, MemoryResource *pool);
    void writeItemTypeToDatabase(const ItemType &itemType, QDataStream &dataStream, Version v) const;
    static void readItemFromDatabase(Item &item, QDataStream &dataStream, MemoryResource *pool);
    void writeItemToDatabase(const Item &item, QDataStream &dataStream, Version v) const;
    void writePCCToDatabase(const PartColorCode &pcc, QDataStream &dataStream, Version v) const;
    static void readItemChangeLogFromDatabase(ItemChangeLogEntry &e, QDataStream &dataStream, MemoryResource *pool);
    void writeItemChangeLogToDatabase(const ItemChangeLogEntry &e, QDataStream &dataStream, Version v) const;
    static void readColorChangeLogFromDatabase(ColorChangeLogEntry &e, QDataStream &dataStream, MemoryResource *pool);
    void writeColorChangeLogToDatabase(const ColorChangeLogEntry &e, QDataStream &dataStream, Version v) const;
    static void readRelationshipFromDatabase(Relationship &e, QDataStream &dataStream, MemoryResource *pool);
    void writeRelationshipToDatabase(const Relationship &e, QDataStream &dataStream, Version v) const;
    static void readRelationshipMatchFromDatabase(RelationshipMatch &e, QDataStream &dataStream, MemoryResource *pool);
    void writeRelationshipMatchToDatabase(const RelationshipMatch &e, QDataStream &dataStream, Version v) const;
    static void readApiKeyFromDatabase(QByteArray &id, QString &key, QDataStream &dataStream, MemoryResource *pool);
    void writeApiKeyToDatabase(const QByteArray &id, const QString &key, QDataStream &dataStream, Version v) const;

};

} // namespace BrickLink
