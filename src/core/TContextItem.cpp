#include "TContextItem.h"

#include "TContextPointer.h"
#include "qundogroup.h"
#include "qundostack.h"

#include "Debugger.h"

TContextItem::TContextItem(TContextItem *parent)
    : QObject(parent)
    , m_relatedContextItem(nullptr)
    , m_hasActiveContext(false)
    , m_ignoreContext(false)
{
    m_historyStack = nullptr;
}

TContextItem::~TContextItem()
{
    if (m_hasActiveContext) {
        cpointer().about_to_delete(this);
    }
}

QUndoStack *TContextItem::get_history_stack() const
{
    return m_historyStack;
}

void TContextItem::set_history_stack(QUndoStack *stack)
{
    m_historyStack = stack;
}

void TContextItem::set_has_active_context(bool context) {
    if (context == m_hasActiveContext) {
        return;
    }

    m_hasActiveContext = context;

    if (m_relatedContextItem) {
        m_relatedContextItem->set_has_active_context(context);
    }

    emit activeContextChanged();
}

void TContextItem::set_ignore_context(bool ignoreContext)
{
    m_ignoreContext = ignoreContext;
}

void TContextItem::create_history_stack()
{
    PENTER;
    m_historyStack = new QUndoStack(TContextItem::get_undogroup());
}

QUndoStack* TContextItem::resolve_undo_stack() const
{
    if (m_historyStack) {
        return m_historyStack;
    }

    if (m_relatedContextItem) {
        return m_relatedContextItem->get_history_stack();
    }

    return nullptr;
}

QUndoGroup* TContextItem::get_undogroup()
{
    static QUndoGroup group;
    return &group;
}


