/*
Copyright (C) 2007-2024 Remon Sijrier

This file is part of Traverso

Traverso is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.

*/

#include "TShortCutFunction.h"

#include "TShortCutManager.h"

#include <QTranslator>
#include "Debugger.h"
#include "Utils.h"
#include "qmetaobject.h"

TShortCutFunction::TShortCutFunction(const QMetaObject *metaObject, const QString &description, const char *commandName)
    : m_metaObject(metaObject)
    , m_commandName(commandName)
    , m_description(description)
{
    Q_ASSERT(metaObject);
    Q_ASSERT(std::strlen(commandName) > 0);
}

TShortCutFunction::TShortCutFunction(const QMetaObject *metaObject, const TAction &action)
    : m_metaObject(metaObject)
    , m_action(&action)
    , m_commandName(action.name)
    , m_description(action.translated_description())
{
    Q_ASSERT(metaObject);
}

TShortCutFunction::~TShortCutFunction()
{
}

QString TShortCutFunction::get_modifier_sequence(bool fromInheritedBase) const
{
    QString modifiersString;

    foreach(int modifier, get_modifier_keys(fromInheritedBase)) {
        switch(modifier) {
        case Qt::Key_Alt:
            modifiersString += "Alt+";
            break;
        case Qt::Key_Control:
            modifiersString += "Ctrl+";
            break;
        case Qt::Key_Shift:
            modifiersString += "Shift+";
            break;
        case Qt::Key_Meta:
            modifiersString += "Meta+";
            break;
        default:
            PERROR(QString("Unkown modifier key %1, programming error").arg(modifier));
        }
    }
    return modifiersString;
}

QString TShortCutFunction::get_key_sequence(bool formatHtml) const
{
    QString sequence;
    QStringList sequenceList;
    QString modifiersString = get_modifier_sequence();

    if (get_modifier_keys().size())
    {
        modifiersString += " ";
    }

    foreach(QString keyString, get_keys())
    {

        sequenceList << (modifiersString + keyString);
    }

    sequence = sequenceList.join(" , ");

    TShortCutFunction::make_shortcut_key_human_readable(sequence, formatHtml);

    return sequence;
}

QList<int> TShortCutFunction::get_modifier_keys(bool fromInheritedBase) const
{
    if (m_baseShortCutFunction && m_usesBaseFunction && fromInheritedBase)
    {
        return m_baseShortCutFunction->get_modifier_keys();
    }

    return m_modifierkeys;
}

QString TShortCutFunction::get_description() const
{
    // Translated when shown, the translator is installed after the
    // functions are registered
    QString description = m_action ? m_action->translated_description() : m_description;

    // A function without a description of its own shows the one of its base
    if (description.isEmpty() && m_baseShortCutFunction)
    {
        return m_baseShortCutFunction->get_description();
    }

    return description;
}

QString TShortCutFunction::get_long_description() const
{
    QString description = get_description();
    if (!m_submenu.isEmpty())
    {
        description = m_submenu + " : " + description;
    }
    return description;
}

QStringList TShortCutFunction::get_keys(bool fromInheritedBase) const
{
    if (m_baseShortCutFunction && m_usesBaseFunction && fromInheritedBase)
    {
        return m_baseShortCutFunction->get_keys();
    }

    return m_keys;
}

int TShortCutFunction::get_autorepeat_interval() const
{
    if (m_baseShortCutFunction && m_usesBaseFunction)
    {
        return m_baseShortCutFunction->get_autorepeat_interval();
    }

    return m_autorepeatInterval;
}

int TShortCutFunction::get_autorepeat_start_delay() const
{
    if (m_baseShortCutFunction && m_usesBaseFunction)
    {
        return m_baseShortCutFunction->get_autorepeat_start_delay();
    }

    return m_autorepeatStartDelay;
}

void TShortCutFunction::make_shortcut_key_human_readable(QString& keyfact, bool formatHtml)
{
    keyfact.replace(QString("MOUSESCROLLVERTICALUP"), QObject::tr("Scroll Up"));
    keyfact.replace(QString("MOUSESCROLLVERTICALDOWN"), QObject::tr("Scroll Down"));
    keyfact.replace(QString("MOUSEBUTTONRIGHT"), QObject::tr("Right Button"));
    keyfact.replace(QString("MOUSEBUTTONLEFT"), QObject::tr("Left Button"));
    keyfact.replace(QString("MOUSEBUTTONMIDDLE"), QObject::tr("Center Button"));
    if (formatHtml) {
        keyfact.replace(QString("UPARROW"), QString("&uarr;"));
        keyfact.replace(QString("DOWNARROW"), QString("&darr;"));
        keyfact.replace(QString("LEFTARROW"), QString("&larr;"));
        keyfact.replace(QString("RIGHTARROW"), QString("&rarr;"));
        keyfact.replace(QString("PAGEDOWN"), "Page Up");
        keyfact.replace(QString("PAGEUP"), "Page Down");
        keyfact.replace(QString("MINUS"), QString("&#45;"));
        keyfact.replace(QString("PLUS"), QString("&#43;"));
    } else {
        keyfact.replace(QString("UPARROW"), QObject::tr("Up"));
        keyfact.replace(QString("DOWNARROW"), QObject::tr("Down"));
        keyfact.replace(QString("LEFTARROW"), QObject::tr("Left"));
        keyfact.replace(QString("RIGHTARROW"), QObject::tr("Right"));
        keyfact.replace(QString("PAGEDOWN"), "PgUp");
        keyfact.replace(QString("PAGEUP"), "PgDown");
        keyfact.replace(QString("MINUS"), "-");
        keyfact.replace(QString("PLUS"), "+");
    }
    keyfact.replace(QString("DELETE"), "Delete");
    keyfact.replace(QString("BKSPACE"), "Backspace");
    keyfact.replace(QString("ESC"), "Esc");
    keyfact.replace(QString("ENTER"), "Enter");
    keyfact.replace(QString("RETURN"), "Return");
    keyfact.replace(QString("SPACE"), QObject::tr("Space Bar"));
    keyfact.replace(QString("HOME"), "Home");
    keyfact.replace(QString("END"), "End");
    keyfact.replace(QString("NUMERICAL"), "0, 1, ... 9");
}
