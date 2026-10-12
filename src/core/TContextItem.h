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

#ifndef TCONTEXTITEM_H
#define TCONTEXTITEM_H

#include <QObject>

class TCommand;
class QUndoStack;
class QUndoGroup;

/**
 * \class TContextItem
 * \brief Interface class for objects (both core and gui) that operate in a 'Soft Selection'
 *
    Each core object that has/can have a visual representation should inherit from this class.

    Only core objects that inherit this class need to set the historystack which <br />
    they need to create/get themselves.
 */

class TContextItem : public QObject
{
    Q_OBJECT
public:
    TContextItem(TContextItem* parent=nullptr);
    virtual ~TContextItem();

    TContextItem* get_related_context_item() const {return m_relatedContextItem;}

    qint64 get_id();

    void set_related_context_item(TContextItem* item) {m_relatedContextItem = item;}
    void set_has_active_context(bool context);
    void set_ignore_context(bool ignoreContext);
    void set_id(qint64 id);

    bool has_active_context() const {return m_hasActiveContext;}
    bool item_ignores_context() const {return m_ignoreContext;}

    void create_history_stack();

    /**
 * @brief Traverses the context hierarchy to resolve the authoritative history stack.
 * @details Walks up the relation chain and QObject tree to find the TSession (Sheet/Project)
 *          that explicitly owns and manages the QUndoStack.
 * @return A pointer to the authoritative QUndoStack, or nullptr if orphaned.
 */
    QUndoStack* resolve_undo_stack() const;
    QUndoStack* get_history_stack() const;
    void set_history_stack(QUndoStack* stack);

    static QUndoGroup* get_undogroup();


protected:

private:
    QUndoStack*             m_historyStack;
    qint64                  m_id;
    TContextItem*          m_relatedContextItem;
    bool                    m_hasActiveContext;
    bool                    m_ignoreContext;

signals:
    void activeContextChanged();
};

#endif

//eof
