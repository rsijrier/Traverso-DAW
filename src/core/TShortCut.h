/*
Copyright (C) 2011-2024 Remon Sijrier

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

#ifndef TSHORTCUT_H
#define TSHORTCUT_H

#include <QList>
#include <QMultiHash>

class TShortCutFunction;

class TShortCut
{
public:
    explicit TShortCut(int keyValue);
    ~ TShortCut();

    int get_key_value() const {return m_keyValue;}

    QList<TShortCutFunction*> get_shortcut_functions() const;
    QList<TShortCutFunction*> get_shortcut_functions_for_metaobject(const QMetaObject *metaObject) const;

    void set_autorepeat_interval(int interval) {m_autorepeatInterval = interval;}
    void set_autorepeat_start_delay(int delay) {m_autorepeatStartDelay = delay;}

    int get_autorepeat_interval() const {return m_autorepeatInterval;}
    int get_autorepeat_start_delay() const {return m_autorepeatStartDelay;}

    void add_shortcut_function(TShortCutFunction* shortCutFunction);

    // The function a key press resolves to for an object of class metaObject:
    // the first class in the superclass chain with a function whose modifier
    // keys equal activeModifierKeys (no modifiers if none are pressed).
    TShortCutFunction* find_function(const QMetaObject* metaObject, const QList<int>& activeModifierKeys) const;

    static bool modifier_keys_match(const QList<int>& first, const QList<int>& second);

private:
    QMultiHash<const QMetaObject*, TShortCutFunction*> m_dict;

    int		m_keyValue;
    int		m_autorepeatInterval{50};
    int		m_autorepeatStartDelay{50};

};

#endif // TSHORTCUT_H
