#include "ContextItem.h"

#include "Utils.h"
#include "qundogroup.h"
#include "qundostack.h"

#include "Debugger.h"

ContextItem::ContextItem(ContextItem *parent)
    : QObject(parent)
    , m_parentContextItem(parent)
    , m_relatedContextItem(nullptr)
    , m_hasActiveContext(false)
    , m_ignoreContext(false)
{
    m_id = 0;
    m_historyStack = nullptr;
}

ContextItem::~ContextItem()
{
    if (m_hasActiveContext) {
        cpointer().about_to_delete(this);
    }
}

QUndoStack *ContextItem::get_history_stack() const
{
    return m_historyStack;
}

void ContextItem::set_history_stack(QUndoStack *stack)
{
    m_historyStack = stack;
}

void ContextItem::set_has_active_context(bool context) {
    if (context == m_hasActiveContext) {
        return;
    }

    m_hasActiveContext = context;

    if (m_relatedContextItem) {
        m_relatedContextItem->set_has_active_context(context);
    }

    emit activeContextChanged();
}

void ContextItem::set_ignore_context(bool ignoreContext)
{
    m_ignoreContext = ignoreContext;
}

void ContextItem::set_id(qint64 id)
{
    if (id == 0) {
        PERROR(QString("Setting Context Item id to 0 for object %1").arg(metaObject()->className()));
        get_id();
        return;
    }
    m_id = id;
}

void ContextItem::create_history_stack()
{
    PENTER;
    m_historyStack = new QUndoStack(ContextItem::get_undogroup());
}

QUndoGroup* ContextItem::get_undogroup()
{
    static QUndoGroup group;
    return &group;
}

qint64 ContextItem::get_id()
{
    if (m_id == 0) {
        m_id = create_id();
    }
    return m_id;
}

