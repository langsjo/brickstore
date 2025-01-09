// Copyright (C) 2004-2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMenu>
#include <QLayout>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QLabel>
#include <QDialog>
#include <QHeaderView>
#include <QDebug>
#include <QProxyStyle>
#include <QApplication>
#include <QPixmap>
#include <QPainter>
#include <QToolTip>
#include <QPixmapCache>
#include <QStyleFactory>

#include "headerview.h"


class SectionItem : public QListWidgetItem
{
public:
    SectionItem() = default;

    int logicalIndex() const { return m_lidx; }
    void setLogicalIndex(int idx) { m_lidx = idx; }

private:
    int m_lidx = -1;
};


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////


class SectionConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SectionConfigDialog(HeaderView *header)
        : QDialog(header), m_header(header)
    {
        m_label = new QLabel(this);
        m_label->setWordWrap(true);
        m_list = new QListWidget(this);
        m_list->setAlternatingRowColors(true);
        m_list->setDragDropMode(QAbstractItemView::InternalMove);
        m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);

        auto *lay = new QVBoxLayout(this);
        lay->addWidget(m_label);
        lay->addWidget(m_list);
        lay->addWidget(m_buttons);

        QVector<SectionItem *> v(m_header->count());
        for (int i = 0; i < m_header->count(); ++i) {
            SectionItem *it = nullptr;

            it = new SectionItem();
            it->setText(header->model()->headerData(i, m_header->orientation(), Qt::DisplayRole).toString());
            it->setCheckState(header->isSectionHidden(i) ? Qt::Unchecked : Qt::Checked);
            it->setLogicalIndex(i);

            v[m_header->visualIndex(i)] = it;
        }
        for (SectionItem *si : std::as_const(v)) {
            if (si)
                m_list->addItem(si);
        }
        connect(m_buttons, &QDialogButtonBox::accepted, this, &SectionConfigDialog::accept);
        connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        retranslateUi();
    }

    void retranslateUi()
    {
        setWindowTitle(tr("Configure Columns"));
        m_label->setText(tr("Drag the columns into the order you prefer and show/hide them using the check mark."));
    }

    void accept() override
    {
        for (int vi = 0; vi < m_list->count(); ++vi) {
            auto *si = static_cast<SectionItem *>(m_list->item(vi));
            int li = si->logicalIndex();

            int oldvi = m_header->visualIndex(li);

            if (oldvi != vi)
                m_header->moveSection(oldvi, vi);

            bool oldvis = !m_header->isSectionHidden(li);
            bool vis = (si->checkState() == Qt::Checked);

            if (vis != oldvis)
                vis ? m_header->showSection(li) : m_header->hideSection(li);
        }

        QDialog::accept();
    }

    void changeEvent(QEvent *e) override
    {
        if (e->type() == QEvent::LanguageChange)
            retranslateUi();
        QDialog::changeEvent(e);
    }

private:
    HeaderView *      m_header;
    QLabel *          m_label;
    QListWidget *     m_list;
    QDialogButtonBox *m_buttons;
};


///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////


HeaderView::HeaderView(Qt::Orientation o, QWidget *parent)
    : QHeaderView(o, parent)
{
    setProperty("multipleSortColumns", true);

    connect(this, &QHeaderView::sectionClicked,
            this, [this](int section) {
        if (!isConfigurable())
            return;

        auto firstSC = m_sortColumns.isEmpty() ? qMakePair(-1, Qt::AscendingOrder)
                                               : m_sortColumns.constFirst();
        if (!m_sortColumns.isEmpty() && !m_isSorted && (section == firstSC.first)) {
            m_isSorted = true;
            update();
            emit isSortedChanged(m_isSorted);
        } else {
            if (qApp->keyboardModifiers() == Qt::ShiftModifier) {
                bool found = false;
                for (auto &[s, order] : m_sortColumns) {
                    if (s == section) {
                        order = Qt::SortOrder(1 - order);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    m_sortColumns.append(qMakePair(section, firstSC.second));
            } else {
                if (firstSC.first == section)
                    m_sortColumns = { qMakePair(section, Qt::SortOrder(1 - firstSC.second)) };
                else
                    m_sortColumns = { qMakePair(section, Qt::AscendingOrder) };
            }
            if (!m_isSorted) {
                m_isSorted = true;
                emit isSortedChanged(m_isSorted);
            }
            emit sortColumnsChanged(m_sortColumns);
        }
    });

    connect(this, &QHeaderView::sectionMoved,
            this, [this](int li, int oldVi, int newVi) {
        if (!isSectionHidden(li) && (oldVi != newVi))
            emit visualColumnOrderChanged(visualColumnOrder());
    });
}

void HeaderView::setModel(QAbstractItemModel *m)
{
    QAbstractItemModel *oldm = model();

    if (m == oldm)
        return;

    bool horiz = (orientation() == Qt::Horizontal);
    if (oldm) {
        disconnect(oldm, horiz ? &QAbstractItemModel::columnsRemoved : &QAbstractItemModel::rowsRemoved,
                   this, &HeaderView::sectionsRemoved);
    }


    if (m) {
        connect(m, horiz ? &QAbstractItemModel::columnsRemoved : &QAbstractItemModel::rowsRemoved,
                this, &HeaderView::sectionsRemoved);
    }

    QHeaderView::setModel(m);
}

QVector<QPair<int, Qt::SortOrder> > HeaderView::sortColumns() const
{
    return m_sortColumns;
}

void HeaderView::setSortColumns(const QVector<QPair<int, Qt::SortOrder> > &sortColumns)
{
    if (sortColumns != m_sortColumns) {
        m_sortColumns = sortColumns;
        update();
        emit sortColumnsChanged(sortColumns);
    }
}

bool HeaderView::isSorted() const
{
    return m_isSorted;
}

void HeaderView::setSorted(bool b)
{
    if (m_isSorted != b) {
        m_isSorted = b;
        update();
        emit isSortedChanged(b);
    }
}

QVector<int> HeaderView::visualColumnOrder() const
{
    QVector<int> order;

    for (int vi = 0; vi < count(); ++vi) {
        int li = logicalIndex(vi);
        if (!isSectionHidden(li))
            order << li;  // clazy:exclude=reserve-candidates
    }
    return order;
}

bool HeaderView::isConfigurable() const
{
    return m_isConfigurable;
}

void HeaderView::setConfigurable(bool configurable)
{
    m_isConfigurable = configurable;
    setSectionsClickable(configurable);
    setSectionsMovable(configurable);
}

void HeaderView::setSectionHidden(int logicalIndex, bool hide)
{
    bool hidden = isSectionHidden(logicalIndex);

    if (hide && !hidden)
        m_hiddenSizes.insert(logicalIndex, sectionSize(logicalIndex));

    QHeaderView::setSectionHidden(logicalIndex, hide);

    if (!hide && hidden) {
        auto it = m_hiddenSizes.constFind(logicalIndex);
        if (it != m_hiddenSizes.constEnd()) {
            QHeaderView::resizeSection(logicalIndex, it.value());
            m_hiddenSizes.erase(it);
        }
    }
    if (hide != hidden)
        emit visualColumnOrderChanged(visualColumnOrder());
}

void HeaderView::resizeSection(int logicalIndex, int size)
{
    if (isSectionHidden(logicalIndex))
        m_hiddenSizes[logicalIndex] = size;
    else
        QHeaderView::resizeSection(logicalIndex, size);
}

bool HeaderView::viewportEvent(QEvent *e)
{
    switch (e->type()) {
    case QEvent::ContextMenu:
        if (isConfigurable())
            showMenu(static_cast<QContextMenuEvent *>(e)->globalPos());
        e->accept();
        return true;
    case QEvent::ToolTip: {
        auto he = static_cast<QHelpEvent *>(e);
        int li = logicalIndexAt(he->pos());
        if (li >= 0) {
            QString t = model()->headerData(li, orientation()).toString();
            if (isConfigurable()) {
                t = t + u"\n\n"
                        + tr("Click to set as primary sort column.") + u"\n"
                        + tr("Shift-click to set as additional sort column.") + u"\n"
                        + tr("Right-click for context menu.") + u"\n"
                        + tr("Drag to reposition and resize.");
            }
            QToolTip::showText(he->globalPos(), t, this);
        }
        e->accept();
        return true;
    }
    default:
        break;
    }
    return QHeaderView::viewportEvent(e);
}

void HeaderView::showMenu(const QPoint &pos)
{
    if (!isEnabled())
        return;

    QVector<int> order;
    order.reserve(count());
    for (int vi = 0; vi < count(); ++vi) {
        int li = logicalIndex(vi);
        order << li;
    }

    auto *m = new QMenu(this);
    m->setAttribute(Qt::WA_DeleteOnClose);

    m->addAction(tr("Configure columns..."))->setData(-1);
    m->addSeparator();

    for (int vi = 0; vi < order.count(); ++vi) {
        int li = order.at(vi);

        QAction *a = m->addAction(model()->headerData(li, Qt::Horizontal).toString());
        a->setCheckable(true);
        a->setChecked(!isSectionHidden(li));
        a->setData(li);
    }

    connect(m, &QMenu::triggered, this, [this](QAction *a) {
        int li = a->data().toInt();
        bool on = a->isChecked();

        if (li == -1) {
            auto *dlg = new SectionConfigDialog(this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->setWindowModality(Qt::ApplicationModal);
            dlg->show();
        } else if (li >= 0 && li < count()) {
            setSectionHidden(li, !on);
        }
    });

    m->popup(pos);
}

void HeaderView::sectionsRemoved(const QModelIndex &parent, int logicalFirst, int logicalLast)
{
    if (parent.isValid())
        return;

    for (int i = logicalFirst; i <= logicalLast; ++i) {
        m_hiddenSizes.remove(i);
    }
}

#include "headerview.moc"
#include "moc_headerview.cpp"
