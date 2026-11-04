/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include <sal/config.h>

#include <QtDragAndDrop.hxx>
#include <QtDragAndDrop.moc>
#include <QtFrame.hxx>
#include <QtTools.hxx>
#include <QtTransferable.hxx>

#include <cppuhelper/supportsservice.hxx>

#include <com/sun/star/datatransfer/dnd/DNDConstants.hpp>

#include <QtGui/QDrag>
#ifdef EMSCRIPTEN
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#endif

using namespace com::sun::star;

namespace
{
/** QtMimeData subclass that ensures that at least one MIME type is
 *  reported (using a dummy one if necessary), to prevent drag and drop
 *  operations on Wayland from getting cancelled (see tdf#164380).
 */
class QtDragMimeData : public QtMimeData
{
public:
    explicit QtDragMimeData(const css::uno::Reference<css::datatransfer::XTransferable>& rContents)
        : QtMimeData(rContents)
    {
    }

    QStringList formats() const override
    {
        QStringList aFormats = QtMimeData::formats();
        if (!aFormats.empty())
            return aFormats;

        // report a dummy MIME type
        return { "application/x.libreoffice-internal-drag-and-drop" };
    }
};

Qt::DropAction lcl_getPreferredDropAction(sal_Int8 dragOperation)
{
    Qt::DropAction eAct = Qt::IgnoreAction;
    if (dragOperation & css::datatransfer::dnd::DNDConstants::ACTION_MOVE)
        eAct = Qt::MoveAction;
    else if (dragOperation & css::datatransfer::dnd::DNDConstants::ACTION_COPY)
        eAct = Qt::CopyAction;
    else if (dragOperation & css::datatransfer::dnd::DNDConstants::ACTION_LINK)
        eAct = Qt::LinkAction;
    return eAct;
}

css::uno::Reference<css::datatransfer::XTransferable>
lcl_getXTransferable(const QMimeData* pMimeData)
{
    css::uno::Reference<css::datatransfer::XTransferable> xTransferable;
    const QtMimeData* pQtMimeData = qobject_cast<const QtMimeData*>(pMimeData);
    if (!pQtMimeData)
        xTransferable = new QtDnDTransferable(pMimeData);
    else
        xTransferable = pQtMimeData->xTransferable();
    return xTransferable;
}

sal_Int8 lcl_getUserDropAction(const QDropEvent& rEvent, const sal_Int8 nSourceActions,
                               const QMimeData* pMimeData)
{
// we completely ignore all proposals by the Qt event, as they don't
// match at all with the preferred LO DnD actions.
// check the key modifiers to detect a user-overridden DnD action
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const Qt::KeyboardModifiers eKeyMod = rEvent.modifiers();
#else
    const Qt::KeyboardModifiers eKeyMod = rEvent.keyboardModifiers();
#endif
    sal_Int8 nUserDropAction = 0;
    if ((eKeyMod & Qt::ShiftModifier) && !(eKeyMod & Qt::ControlModifier))
        nUserDropAction = css::datatransfer::dnd::DNDConstants::ACTION_MOVE;
    else if ((eKeyMod & Qt::ControlModifier) && !(eKeyMod & Qt::ShiftModifier))
        nUserDropAction = css::datatransfer::dnd::DNDConstants::ACTION_COPY;
    else if ((eKeyMod & Qt::ShiftModifier) && (eKeyMod & Qt::ControlModifier))
        nUserDropAction = css::datatransfer::dnd::DNDConstants::ACTION_LINK;
    nUserDropAction &= nSourceActions;

    // select the default DnD action, if there isn't a user preference
    if (0 == nUserDropAction)
    {
        // default LO internal action is move, but default external action is copy
        nUserDropAction = qobject_cast<const QtMimeData*>(pMimeData)
                              ? css::datatransfer::dnd::DNDConstants::ACTION_MOVE
                              : css::datatransfer::dnd::DNDConstants::ACTION_COPY;
        nUserDropAction &= nSourceActions;

        // if the default doesn't match any allowed source action, fall back to the
        // preferred of all allowed source actions
        if (0 == nUserDropAction)
            nUserDropAction = toVclDropAction(lcl_getPreferredDropAction(nSourceActions));

        // this is "our" preference, but actually we would even prefer any default,
        // if there is any
        nUserDropAction |= css::datatransfer::dnd::DNDConstants::ACTION_DEFAULT;
    }
    return nUserDropAction;
}

#ifdef EMSCRIPTEN
/** Emulates a drag inside one frame for Qt WASM.
 *
 *  QDrag::exec needs a nested event loop, which the WASM build can't run, so
 *  the drag would end right away.  Instead, watch the mouse on the frame
 *  widget and turn the moves and the release into drag and drop events.
 */
class QtWasmDragFilter : public QObject
{
    QtFrame* m_pFrame;
    QtDragSource* m_pSource;
    QMimeData* m_pMimeData;
    Qt::DropActions m_eActions;
    bool m_bEntered = false;
    bool m_bDone = false;

    void enter(const QPoint& rPos, Qt::MouseButtons eButtons, Qt::KeyboardModifiers eMods)
    {
        if (m_bEntered)
            return;
        m_bEntered = true;
        QDragEnterEvent aEvent(rPos, m_eActions, m_pMimeData, eButtons, eMods);
        m_pFrame->handleDragEnter(&aEvent);
    }

    void finish(sal_Int8 nAction, bool bSuccess)
    {
        m_bDone = true;
        m_pFrame->GetQWidget().removeEventFilter(this);
        m_pSource->fire_dragEnd(nAction, bSuccess);
        deleteLater();
    }

public:
    QtWasmDragFilter(QtFrame* pFrame, QtDragSource* pSource, QMimeData* pMimeData,
                     Qt::DropActions eActions)
        : m_pFrame(pFrame)
        , m_pSource(pSource)
        , m_pMimeData(pMimeData)
        , m_eActions(eActions)
    {
        m_pFrame->GetQWidget().installEventFilter(this);
    }

    ~QtWasmDragFilter() override { delete m_pMimeData; }

    bool eventFilter(QObject*, QEvent* pEvent) override
    {
        if (m_bDone)
            return false;
        switch (pEvent->type())
        {
            case QEvent::MouseMove:
            {
                QMouseEvent* pMouse = static_cast<QMouseEvent*>(pEvent);
                enter(pMouse->pos(), pMouse->buttons(), pMouse->modifiers());
                QDragMoveEvent aEvent(pMouse->pos(), m_eActions, m_pMimeData, pMouse->buttons(),
                                      pMouse->modifiers());
                m_pFrame->handleDragMove(&aEvent);
                return true;
            }
            case QEvent::MouseButtonRelease:
            {
                QMouseEvent* pMouse = static_cast<QMouseEvent*>(pEvent);
                enter(pMouse->pos(), pMouse->buttons(), pMouse->modifiers());
                QDropEvent aEvent(QPointF(pMouse->pos()), m_eActions, m_pMimeData,
                                  pMouse->buttons(), pMouse->modifiers());
                m_pFrame->handleDrop(&aEvent);
                const bool bSuccess = aEvent.isAccepted();
                finish(bSuccess ? toVclDropAction(aEvent.dropAction())
                                : css::datatransfer::dnd::DNDConstants::ACTION_NONE,
                       bSuccess);
                return true;
            }
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
                return true;
            case QEvent::KeyPress:
                if (static_cast<QKeyEvent*>(pEvent)->key() == Qt::Key_Escape)
                {
                    if (m_bEntered)
                        m_pFrame->handleDragLeave();
                    finish(css::datatransfer::dnd::DNDConstants::ACTION_NONE, false);
                    return true;
                }
                break;
            default:
                break;
        }
        return false;
    }
};
#endif
}

QtDragSource::QtDragSource(QtFrame* pFrame)
    : WeakComponentImplHelper(m_aMutex)
    , m_pFrame(pFrame)
{
    assert(m_pFrame && "missing SalFrame");
    m_pFrame->registerDragSource(this);
}

QtDragSource::~QtDragSource() {}

sal_Bool QtDragSource::isDragImageSupported() { return true; }

sal_Int32 QtDragSource::getDefaultCursor(sal_Int8) { return 0; }

void QtDragSource::startDrag(
    const datatransfer::dnd::DragGestureEvent& /*rEvent*/, sal_Int8 sourceActions,
    sal_Int32 /*cursor*/, sal_Int32 /*image*/,
    const css::uno::Reference<css::datatransfer::XTransferable>& rTrans,
    const css::uno::Reference<css::datatransfer::dnd::XDragSourceListener>& rListener)
{
    m_xListener = rListener;

#ifdef EMSCRIPTEN
    if (m_pFrame)
    {
        // fire_dragEnd is called by the filter when the mouse is released
        new QtWasmDragFilter(m_pFrame, this, new QtDragMimeData(rTrans),
                             toQtDropActions(sourceActions));
        return;
    }
#endif
    if (m_pFrame)
    {
        QDrag* drag = new QDrag(&m_pFrame->GetQWidget());
        drag->setMimeData(new QtDragMimeData(rTrans));
        // just a reminder that exec starts a nested event loop, so everything after
        // this call is just executed, after D'n'D has finished!
        drag->exec(toQtDropActions(sourceActions), lcl_getPreferredDropAction(sourceActions));
    }

    // the drop will eventually call fire_dragEnd, which will clear the listener.
    // if D'n'D ends without success, we just get a leave event without any indicator,
    // but the event loop will be terminated, so we have to try to inform the source of
    // a failure in any way.
    fire_dragEnd(datatransfer::dnd::DNDConstants::ACTION_NONE, false);
}

void QtDragSource::fire_dragEnd(sal_Int8 nAction, bool bDropSuccessful)
{
    if (!m_xListener.is())
        return;

    datatransfer::dnd::DragSourceDropEvent aEv;
    aEv.DropAction = nAction;
    aEv.DropSuccess = bDropSuccessful;

    auto xListener = m_xListener;
    m_xListener.clear();
    xListener->dragDropEnd(aEv);
}

OUString SAL_CALL QtDragSource::getImplementationName()
{
    return u"com.sun.star.datatransfer.dnd.VclQtDragSource"_ustr;
}

sal_Bool SAL_CALL QtDragSource::supportsService(OUString const& ServiceName)
{
    return cppu::supportsService(this, ServiceName);
}

css::uno::Sequence<OUString> SAL_CALL QtDragSource::getSupportedServiceNames()
{
    return { u"com.sun.star.datatransfer.dnd.QtDragSource"_ustr };
}

QtDropTarget::QtDropTarget()
    : m_nDropAction(datatransfer::dnd::DNDConstants::ACTION_NONE)
    , m_bDropSuccessful(false)
{
}

OUString SAL_CALL QtDropTarget::getImplementationName()
{
    return u"com.sun.star.datatransfer.dnd.VclQtDropTarget"_ustr;
}

sal_Bool SAL_CALL QtDropTarget::supportsService(OUString const& ServiceName)
{
    return cppu::supportsService(this, ServiceName);
}

css::uno::Sequence<OUString> SAL_CALL QtDropTarget::getSupportedServiceNames()
{
    return { u"com.sun.star.datatransfer.dnd.QtDropTarget"_ustr };
}

QtDropTarget::~QtDropTarget() {}

void QtDropTarget::handleDragEnterEvent(QDragEnterEvent& rEvent, qreal fScaleFactor)
{
    css::datatransfer::dnd::DropTargetDragEnterEvent aEvent
        = createDropTargetDragEnterEvent(rEvent, true, fScaleFactor);
    dragEnter(aEvent);

    if (qobject_cast<const QtMimeData*>(rEvent.mimeData()))
        rEvent.accept();
    else
        rEvent.acceptProposedAction();
}

void QtDropTarget::handleDragMoveEvent(QDragMoveEvent& rEvent, qreal fScaleFactor)
{
    css::datatransfer::dnd::DropTargetDragEnterEvent aEvent
        = createDropTargetDragEnterEvent(rEvent, false, fScaleFactor);
    dragOver(aEvent);

    // the drop target accepted our drop action => inform Qt
    if (proposedDropAction() != 0)
    {
        rEvent.setDropAction(lcl_getPreferredDropAction(proposedDropAction()));
        rEvent.accept();
    }
    else // or maybe someone else likes it?
        rEvent.ignore();
}

void QtDropTarget::handleDropEvent(QDropEvent& rEvent, qreal fScaleFactor)
{
    m_bDropSuccessful = true;

    // ask the drop target to accept our drop action
    css::datatransfer::dnd::DropTargetDropEvent aEvent
        = createDropTargetDropEvent(rEvent, fScaleFactor);
    drop(aEvent);

    const bool bDropSuccessful = dropSuccessful();
    const sal_Int8 nDropAction = proposedDropAction();

    // the drop target accepted our drop action => inform Qt
    if (bDropSuccessful)
    {
        rEvent.setDropAction(lcl_getPreferredDropAction(nDropAction));
        rEvent.accept();
    }
    else // or maybe someone else likes it?
        rEvent.ignore();
}

void QtDropTarget::acceptDrag(sal_Int8 dragOperation) { m_nDropAction = dragOperation; }

void QtDropTarget::rejectDrag() { m_nDropAction = 0; }

void QtDropTarget::acceptDrop(sal_Int8 dropOperation) { m_nDropAction = dropOperation; }

void QtDropTarget::rejectDrop() { m_nDropAction = 0; }

void QtDropTarget::dropComplete(sal_Bool success)
{
    m_bDropSuccessful = (m_bDropSuccessful && success);
}

css::datatransfer::dnd::DropTargetDragEnterEvent
QtDropTarget::createDropTargetDragEnterEvent(const QDragMoveEvent& rEvent, bool bSetDataFlavors,
                                             qreal fScaleFactor)
{
    // prepare our suggested drop action for the drop target
    const sal_Int8 nSourceActions = toVclDropActions(rEvent.possibleActions());
    const QMimeData* pMimeData = rEvent.mimeData();
    const sal_Int8 nUserDropAction = lcl_getUserDropAction(rEvent, nSourceActions, pMimeData);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint aPos = rEvent.position().toPoint();
#else
    const QPoint aPos = rEvent.pos();
#endif

    css::datatransfer::dnd::DropTargetDragEnterEvent aEvent;
    aEvent.Source = static_cast<css::datatransfer::dnd::XDropTarget*>(this);
    aEvent.Context = this;
    aEvent.LocationX = aPos.x() * fScaleFactor;
    aEvent.LocationY = aPos.y() * fScaleFactor;
    aEvent.DropAction = nUserDropAction;
    aEvent.SourceActions = nSourceActions;

    if (bSetDataFlavors)
        aEvent.SupportedDataFlavors = lcl_getXTransferable(pMimeData)->getTransferDataFlavors();

    return aEvent;
}

css::datatransfer::dnd::DropTargetDropEvent
QtDropTarget::createDropTargetDropEvent(const QDropEvent& rEvent, qreal fScaleFactor)
{
    // prepare our suggested drop action for the drop target
    const sal_Int8 nSourceActions = toVclDropActions(rEvent.possibleActions());
    const sal_Int8 nUserDropAction
        = lcl_getUserDropAction(rEvent, nSourceActions, rEvent.mimeData());

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint aPos = rEvent.position().toPoint();
#else
    const QPoint aPos = rEvent.pos();
#endif

    css::datatransfer::dnd::DropTargetDropEvent aEvent;
    aEvent.Source = static_cast<css::datatransfer::dnd::XDropTarget*>(this);
    aEvent.Context = this;
    aEvent.LocationX = aPos.x() * fScaleFactor;
    aEvent.LocationY = aPos.y() * fScaleFactor;
    aEvent.SourceActions = nSourceActions;
    aEvent.DropAction = nUserDropAction;
    aEvent.Transferable = lcl_getXTransferable(rEvent.mimeData());

    return aEvent;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
