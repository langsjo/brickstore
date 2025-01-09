// Copyright (C) 2004-2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QtQml/QQmlParserStatus>
#include <QtQuick/QQuickPaintedItem>

#include "bricklink/core.h"
#include "bricklink/lot.h"

class Document;
class DocumentModel;
class UpdateDatabase;
class QmlColor;
class QmlCategory;
class QmlItemType;
class QmlItem;
class QmlLot;
class QmlLots;
class QmlPriceGuide;
class QmlPicture;


class QmlImageItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(QImageItem)
    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged)

public:
    QmlImageItem(QQuickItem *parent = nullptr);

    QImage image() const;
    Q_INVOKABLE void setImage(const QImage &image);
    Q_INVOKABLE void clear();

    void paint(QPainter *painter) override;

signals:
    void imageChanged();

private:
    QImage m_image;
};

class QmlImage // make QImage known to qmllint
{
    Q_GADGET
    QML_FOREIGN(QImage)
    QML_NAMED_ELEMENT(QImage)
    QML_UNCREATABLE("")
};

Q_DECLARE_METATYPE(QImage)
