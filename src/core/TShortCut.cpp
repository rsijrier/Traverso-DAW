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

#include "TShortCut.h"
#include "TShortCutFunction.h"

TShortCut::TShortCut(int keyValue)
    : m_keyValue(keyValue)
{
}

TShortCut::~TShortCut()
{
}

QList<TShortCutFunction*> TShortCut::get_shortcut_functions() const
{
    return m_dict.values();
}

QList<TShortCutFunction *> TShortCut::get_shortcut_functions_for_metaobject(const QMetaObject *metaObject) const
{
    return m_dict.values(metaObject);
}

TShortCutFunction* TShortCut::find_function(const QMetaObject* metaObject, const QList<int>& activeModifierKeys) const
{
    // traverse upwards till no more superclasses are found
    // this supports inheritance on QObjects.
    while (metaObject)
    {
        const QList<TShortCutFunction*> functions = get_shortcut_functions_for_metaobject(metaObject);

        for(TShortCutFunction* function : functions) {
            if (activeModifierKeys.size()) {
                if (modifier_keys_match(activeModifierKeys, function->get_modifier_keys())) {
                    return function;
                }
            } else if (function->get_modifier_keys().isEmpty()) {
                return function;
            }
        }

        metaObject = metaObject->superClass();
    }

    return nullptr;
}

bool TShortCut::modifier_keys_match(const QList<int>& first, const QList<int>& second)
{
    if (first.size() != second.size())
    {
        return false;
    }

    for(int key : first)
    {
        if (!second.contains(key))
        {
            return false;
        }
    }

    return true;
}

void TShortCut::add_shortcut_function(TShortCutFunction *shortCutFunction)
{
    Q_ASSERT(shortCutFunction);

    m_dict.insert(shortCutFunction->get_metaobject(), shortCutFunction);
}
