/*
Copyright (C) 2005-2007 Remon Sijrier 

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

#ifndef CONTEXTITEM_H
#define CONTEXTITEM_H

#include <QObject>
#include <QPointer>
#include "ContextPointer.h"

class TCommand;
class QUndoStack;
class QUndoGroup;

/**
 * \class ContextItem
 * \brief Interface class for objects (both core and gui) that operate in a 'Soft Selection'
 *
    Each core object that has/can have a visual representation should inherit from this class.

    Only core objects that inherit this class need to set the historystack which <br />
    they need to create/get themselves.
 */

class ContextItem : public QObject
{
    Q_OBJECT
public:
    ContextItem(ContextItem* parent=nullptr);
    virtual ~ContextItem();

    ContextItem* get_related_context_item() const {return m_relatedContextItem;}

    QUndoStack* get_history_stack() const;
    void set_history_stack(QUndoStack* stack);
    static QUndoGroup* get_undogroup();

    qint64 get_id();

    void set_core_context_item(ContextItem* item) {m_relatedContextItem = item;}
    void set_has_active_context(bool context);
    void set_ignore_context(bool ignoreContext);
    void set_id(qint64 id);

    bool has_active_context() const {return m_hasActiveContext;}
    bool item_ignores_context() const {return m_ignoreContext;}

    void create_history_stack();


protected:

private:
    QUndoStack*             m_historyStack;
    qint64                  m_id;
    QPointer<ContextItem>   m_parentContextItem;
    QPointer<ContextItem>   m_relatedContextItem;
    bool                    m_hasActiveContext;
    bool                    m_ignoreContext;

signals:
    void activeContextChanged();
};

#endif

//eof
