// Copyright (C) 2004-2024 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QFrame>

#include <QCoro/QCoroTask>

#include <common/filter.h>

QT_FORWARD_DECLARE_CLASS(QToolButton)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QStackedLayout)
QT_FORWARD_DECLARE_CLASS(QStackedWidget)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QButtonGroup)
QT_FORWARD_DECLARE_CLASS(QSplitter)
QT_FORWARD_DECLARE_CLASS(QStringListModel)

class Document;
class DocumentModel;
class FilterWidget;
class HistoryLineEdit;
class View;
class OpenDocumentsMenu;


class ViewPane : public QWidget
{
    Q_OBJECT

public:
    ViewPane(const std::function<ViewPane *(Document *, QWidget *)> &viewPaneCreate,
             Document *activeDocument);
    ~ViewPane() override;

    void newWindow();
    void split(Qt::Orientation o);
    bool canUnsplit() const;
    void unsplit();

    void documentCurrencyChanged(const QString &ccode);
    QCoro::Task<> changeDocumentCurrency();
    void updateStatistics();
    void updateBlockState(bool blocked);
    void focusFilter();

    void setFilterFavoritesModel(QStringListModel *model);

    void setView(View *view);

    void activateDocument(Document *document);
    Document *activeDocument() const;
    View *activeView() const;
    View *viewForDocument(Document *document);

    bool isActive() const;
    void setActive(bool b);

signals:
    void viewActivated(View *view);
    void beingDestroyed();

protected:
    void languageChange();
    void fontChange();
    void paletteChange();
    void changeEvent(QEvent *e) override;

private:
    void createToolBar();
    void setupViewStack();
    View *newView(Document *doc);

    View *m_view = nullptr;
    DocumentModel *m_model = nullptr;

    FilterWidget *m_filter;
    QWidget *m_toolBar;
    QToolButton *m_filterOnOff;
    QComboBox *m_viewList = nullptr;
    QWidget *m_viewListBackground = nullptr;
    QToolButton *m_closeView;
    QWidget *m_orderSeparator;
    QToolButton *m_order;
    QWidget *m_differencesSeparator;
    QToolButton *m_differences;
    QWidget *m_errorsSeparator;
    QToolButton *m_errors;
    QLabel *m_weight;
    QLabel *m_count;
    QLabel *m_value;
    QLabel *m_profit;
    QToolButton *m_currency;
    QToolButton *m_split;
    QAction *m_splitH;
    QAction *m_splitV;
    QAction *m_splitClose;
    QAction *m_splitWindow;

    QStackedWidget *m_viewStack;
    QMap<Document *, View *> m_viewStackMapping;

    bool m_active = false;
    QObject *m_viewConnectionContext = nullptr;
    std::function<ViewPane *(Document *, QWidget *)> m_viewPaneCreate;

    OpenDocumentsMenu *m_openDocumentsMenu;
};
