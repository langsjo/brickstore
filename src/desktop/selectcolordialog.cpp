// Copyright (C) 2004-2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <QPushButton>
#include <QToolButton>
#include <QAction>
#include <QVBoxLayout>
#include <QDialogButtonBox>

#include "bricklink/color.h"
#include "common/config.h"
#include "desktopuihelpers.h"
#include "selectcolor.h"
#include "selectcolordialog.h"


SelectColorDialog::SelectColorDialog(bool popupMode, QWidget *parent)
    : QDialog(parent)
    , m_popupMode(popupMode)
{
    if (popupMode)
        setWindowFlags(Qt::Tool | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
    setWindowTitle(tr("Select Color"));

    setSizeGripEnabled(true);
    setModal(true);
    w_sc = new SelectColor(this);
    w_buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, this);
    connect(w_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(w_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto vboxLayout = new QVBoxLayout(this);
    vboxLayout->addWidget(w_sc);
    vboxLayout->addWidget(w_buttons);

    auto ba = Config::inst()->value(u"MainWindow/ModifyColorDialog/SelectColor"_qs)
            .toByteArray();
    if (!w_sc->restoreState(ba))
        w_sc->restoreState(SelectColor::defaultState());

    connect(w_sc, &SelectColor::colorSelected,
            this, &SelectColorDialog::checkColor);

    w_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);

    m_resetGeometryAction = new QAction(this);
    m_resetGeometryAction->setIcon(QIcon::fromTheme(u"zoom-fit-best"_qs));
    m_resetGeometryAction->setToolTip(tr("Reset the position to automatic mode"));
    m_resetGeometryAction->setVisible(false);

    connect(m_resetGeometryAction, &QAction::triggered, this, [this]() {
        DesktopUIHelpers::setPopupPos(this, m_popupPos);
        m_resetGeometryAction->setVisible(false);
    }, Qt::QueuedConnection);

    if (popupMode) {
        auto reset = new QToolButton();
        reset->setProperty("iconScaling", true);
        reset->setToolButtonStyle(Qt::ToolButtonIconOnly);
        reset->setDefaultAction(m_resetGeometryAction);

        w_buttons->addButton(reset, QDialogButtonBox::ResetRole);
    }

    setFocusProxy(w_sc);

    m_geometryConfigKey = popupMode ? u"MainWindow/ModifyColorPopup/Geometry"_qs
                                    : u"MainWindow/ModifyColorDialog/Geometry"_qs;

    if (!popupMode)
        restoreGeometry(Config::inst()->value(m_geometryConfigKey).toByteArray());
}

SelectColorDialog::~SelectColorDialog()
{
    if (!m_popupMode)
        Config::inst()->setValue(m_geometryConfigKey, saveGeometry());
    Config::inst()->setValue(u"MainWindow/ModifyColorDialog/SelectColor"_qs, w_sc->saveState());
}


void SelectColorDialog::setColor(const BrickLink::Color *color)
{
    w_sc->setCurrentColor(color);
}

void SelectColorDialog::setColorAndItem(const BrickLink::Color *color, const BrickLink::Item *item)
{
    w_sc->setCurrentColorAndItem(color, item);
}

const BrickLink::Color *SelectColorDialog::color() const
{
    return w_sc->currentColor();
}

void SelectColorDialog::checkColor(const BrickLink::Color *col, bool ok)
{
    QPushButton *p = w_buttons->button(QDialogButtonBox::Ok);
    p->setEnabled((col));

    if (col && ok)
        p->animateClick();
}

void SelectColorDialog::setPopupGeometryChanged(bool b)
{
    m_resetGeometryAction->setVisible(b);
}

bool SelectColorDialog::isPopupGeometryChanged() const
{
    return m_resetGeometryAction->isVisible();
}

void SelectColorDialog::setPopupPosition(const QRect &pos)
{
    m_popupPos = pos; // we need to delay the positioning, because X11 doesn't know the frame size yet
}

void SelectColorDialog::moveEvent(QMoveEvent *e)
{
    QDialog::moveEvent(e);
    if (m_popupMode)
        setPopupGeometryChanged(true);
}

void SelectColorDialog::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    if (m_popupMode)
        setPopupGeometryChanged(true);
}

void SelectColorDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);

    if (m_popupMode) {
        activateWindow();
        w_sc->setFocus();

        if (m_popupPos.isValid()) {
            QMetaObject::invokeMethod(this, [this]() {
                auto ba = Config::inst()->value(m_geometryConfigKey).toByteArray();
                if (ba.isEmpty() || !restoreGeometry(ba)) {
                    DesktopUIHelpers::setPopupPos(this, m_popupPos);
                    setPopupGeometryChanged(false);
                } else {
                    setPopupGeometryChanged(true);
                }
            }, Qt::QueuedConnection);
        }
    }
}

void SelectColorDialog::hideEvent(QHideEvent *e)
{
    if (m_popupMode) {
        if (isPopupGeometryChanged())
            Config::inst()->setValue(m_geometryConfigKey, saveGeometry());
        else
            Config::inst()->remove(m_geometryConfigKey);
    }
    QDialog::hideEvent(e);
}

QSize SelectColorDialog::sizeHint() const
{
    QSize s = QDialog::sizeHint();
    return { s.rwidth() * 3 / 2, s.height() * 3 / 2 };
}

#include "moc_selectcolordialog.cpp"
