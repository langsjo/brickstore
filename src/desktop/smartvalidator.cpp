// Copyright (C) 2004-2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QRegularExpressionValidator>
#include <QDebug>

#include "smartvalidator.h"


void DotCommaFilter::install()
{
    static bool once = false;
    if (!once) {
        once = true;
        qApp->installEventFilter(new DotCommaFilter(qApp));
    }
}

DotCommaFilter::DotCommaFilter(QObject *parent)
    : QObject(parent)
{ }

bool DotCommaFilter::eventFilter(QObject *o, QEvent *e)
{
    if ((e->type() == QEvent::KeyPress) || (e->type() == QEvent::KeyRelease)) {
        if (qobject_cast<QDoubleSpinBox *>(o)
                || (qobject_cast<QLineEdit *>(o)
                    && qobject_cast<const QDoubleValidator *>(
                        static_cast<QLineEdit *>(o)->validator()))) {

            auto *ke = static_cast<QKeyEvent *>(e);

            QString text = ke->text();
            bool fixed = false;

            for (int i = 0; i < text.length(); ++i) {
                QChar &ir = text[i];
                if (ir == u'.' || ir == u',') {
                    ir = QLocale::system().decimalPoint().at(0);
                    fixed = (text != ke->text());
                }
            }

            if (fixed) {
                // send new event, eat this one.
                auto nke = new QKeyEvent(ke->type(), ke->key(), ke->modifiers(), text,
                                         ke->isAutoRepeat(), ushort(ke->count()));
                QCoreApplication::postEvent(o, nke);
                return true;
            }
        }
    }
    return false;
}


SmartDoubleValidator::SmartDoubleValidator(QObject *parent)
    : SmartDoubleValidator(std::numeric_limits<double>::min(), std::numeric_limits<double>::max(),
                           1000, 0, parent)
{ }

SmartDoubleValidator::SmartDoubleValidator(double bottom, double top, int decimals, double empty,
                                           QObject *parent)
    : QDoubleValidator(bottom, top, decimals, parent)
    , m_empty(empty)
    , m_regexp(new QRegularExpressionValidator(QRegularExpression(uR"(=[*\/\+-]-?[.,\d]+)"_qs), this))
{
    DotCommaFilter::install();
}

QValidator::State SmartDoubleValidator::validate(QString &input, int &pos) const
{
    if (input.startsWith(u'=')) {
        auto v = m_regexp->validate(input, pos);
        if (v == QValidator::Acceptable) {
            bool ok = false;
            locale().toDouble(input.mid(2), &ok);
            if (!ok)
                v = QValidator::Intermediate;
        }
        return v;
    } else {
        return QDoubleValidator::validate(input, pos);
    }
}

void SmartDoubleValidator::fixup(QString &str) const
{
    if (str.isEmpty())
        str = locale().toString(m_empty);
}

bool SmartDoubleValidator::event(QEvent *e)
{
    if (e && (e->type() == QEvent::LanguageChange)) {
        setLocale(QLocale());
        emit changed();
    }
    return QDoubleValidator::event(e);
}


SmartIntValidator::SmartIntValidator(QObject *parent)
    : SmartIntValidator(std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 0, parent)
{ }

SmartIntValidator::SmartIntValidator(int bottom, int top, int empty, QObject *parent)
    : QIntValidator(bottom, top, parent)
    , m_empty(empty)
    , m_regexp(new QRegularExpressionValidator(QRegularExpression(uR"(=[*\/\+-]-?\d+)"_qs), this))
{ }

QValidator::State SmartIntValidator::validate(QString &input, int &pos) const
{
    if (input.startsWith(u'='))
        return m_regexp->validate(input, pos);
    else
        return QIntValidator::validate(input, pos);
}

void SmartIntValidator::fixup(QString &str) const
{
    if (str.isEmpty())
        str = locale().toString(m_empty);
}

bool SmartIntValidator::event(QEvent *e)
{
    if (e && (e->type() == QEvent::LanguageChange)) {
        setLocale(QLocale());
        emit changed();
    }
    return QIntValidator::event(e);
}

#include "moc_smartvalidator.cpp"
