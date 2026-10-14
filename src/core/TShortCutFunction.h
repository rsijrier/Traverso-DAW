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

#ifndef TSHORTCUTFUNCTION_H
#define TSHORTCUTFUNCTION_H

#include "Debugger.h"
#include "TCommand.h"
#include <QStringList>
#include <QVariantList>

#include <functional>

class TShortCutFunction {

public:
    // Calls the function on an object of the class it is registered for
    using Invoker = std::function<TCommand*(QObject*)>;

    explicit TShortCutFunction(const QMetaObject *metaObject, const QString &description, const char *commandName);
    ~TShortCutFunction();

    QString get_key_sequence(bool formatHtml=false) const;
    QString get_modifier_sequence(bool fromInheritedBase=true) const;
    QString get_description() const;
    QString get_long_description() const;
    QString get_plugin_name() const {return m_pluginname;}
    QString get_submenu_name() const {return m_submenu;}

    const char* get_command_name() const {return m_commandName;}

    QList<int> get_modifier_keys(bool fromInheritedBase=true) const;
    QStringList get_keys(bool fromInheritedBase=true) const;

    int get_sort_order() const {return m_sortorder;}
    int get_autorepeat_interval() const;
    int get_autorepeat_start_delay() const;

    QVariantList get_arguments() const {return m_arguments;}

    const QMetaObject* get_metaobject() const {return m_metaObject;}
    const QMetaObject* get_base_metaobject() const {return m_baseMetaObject;}

    TShortCutFunction* get_base_shortcut_function() const {return m_baseShortCutFunction;}

    bool uses_autorepeat() const {return m_usesAutoRepeat;}
    bool auto_repeats_on_itself() const {return m_autoRepeatsOnItself;}
    bool uses_inherited_base() const {return m_usesBaseFunction;}
    bool uses_x() const {return m_useX;}
    bool uses_y() const {return m_useY;}
    bool always_safe_to_dispatch() const {return m_alwaysSafeToDispatch;}
    bool plugin_based() const {return !m_pluginname.isEmpty();}

    void set_base_metaobject(const QMetaObject* base)
    {
        Q_ASSERT(base);
        m_baseMetaObject = base;
    }
    void set_base_shortcut_function(TShortCutFunction* baseShortCutFunction)
    {
        Q_ASSERT(baseShortCutFunction);
        m_baseShortCutFunction = baseShortCutFunction;
    }

    void add_keys(const QStringList & keys) {m_keys << keys;}
    void add_modifier_key(int modifier) { m_modifierkeys.append(modifier);}

    void clear_keys() {m_keys.clear();}
    void clear_modifier_keys() {m_modifierkeys.clear();}

    void set_uses_base_function(bool usesBaseFunction) {m_usesBaseFunction = usesBaseFunction;}
    void set_autorepeat_interval(int interval) {m_autorepeatInterval = interval; m_usesAutoRepeat = true;}
    void set_autorepeat_start_delay(int delay) {m_autorepeatStartDelay = delay;}
    void set_plugin_name(const QString &pluginName) {m_pluginname = pluginName;}
    void set_submenu_name(const QString &name) {m_submenu = name;}
    void set_sort_order(int order) {m_sortorder = order;}
    void set_use_x(bool useX) {m_useX = useX;}
    void set_use_y(bool useY) {m_useY = useY;}
    void set_arguments(const QVariantList &args) {m_arguments = args;}
    void set_auto_repeats_on_itself(bool dispatchesOnItself) {m_autoRepeatsOnItself = dispatchesOnItself;}
    void set_invoker(Invoker invoker, bool alwaysSafeToDispatch)
    {
        m_invoker = std::move(invoker);
        m_alwaysSafeToDispatch = alwaysSafeToDispatch;
    }

    static void make_shortcut_key_human_readable(QString& key, bool formatHtml=false);

    // Plugin based functions create a command instead and have no invoker
    TCommand* dispatch_with_command_return(QObject* object) {
        PENTER;
        Q_ASSERT(m_invoker);
        return m_invoker ? m_invoker(object) : nullptr;
    }
    bool dispatch(QObject* object) {
        PENTER;
        Q_ASSERT(m_invoker);
        if (!m_invoker) {
            return false;
        }
        m_invoker(object);
        return true;
    }


private:
    const QMetaObject*  m_metaObject{nullptr};
    const char*         m_commandName;
    const QMetaObject*	m_baseMetaObject{nullptr};
    TShortCutFunction*  m_baseShortCutFunction{nullptr};
    Invoker             m_invoker;

    QVariantList        m_arguments;
    QStringList         m_keys;
    QList<int >         m_modifierkeys;

    QString             m_description;
    QString             m_pluginname;
    QString             m_submenu;

    bool                m_useX{false};
    bool                m_useY{false};
    bool                m_usesAutoRepeat{false};
    bool                m_autoRepeatsOnItself{false};
    bool                m_usesBaseFunction{false};
    bool                m_alwaysSafeToDispatch{false};

    int                 m_sortorder{0};
    int                 m_autorepeatInterval{-1};
    int                 m_autorepeatStartDelay{-1};
};

#endif // TSHORTCUTFUNCTION_H
