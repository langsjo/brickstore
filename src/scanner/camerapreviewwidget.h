// Copyright (C) 2004-2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QWidget>

QT_FORWARD_DECLARE_CLASS(QQuickWidget)
QT_FORWARD_DECLARE_CLASS(QQmlEngine)

namespace Scanner {

class CameraPreviewWidget : public QWidget
{
    Q_OBJECT
public:
    CameraPreviewWidget(QQmlEngine *engine, QWidget *parent = nullptr);
    ~CameraPreviewWidget() override;

    bool isActive() const;
    void setActive(bool newActive);

    QObject *videoOutput() const;

    Q_SIGNAL void clicked();

private:
    QQuickWidget *m_widget = nullptr;
};

} // namespace Scanner
