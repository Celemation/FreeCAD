/***************************************************************************
 *   Copyright (c) 2012-2013 Luke Parry <l.parry@warwick.ac.uk>            *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
# include <QGraphicsSceneHoverEvent>
# include <QGraphicsSceneMouseEvent>
# include <QPainter>
# include <QStyleOptionGraphicsItem>
# include <QTransform>
#endif

#include <App/Application.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>
#include <Gui/Application.h>
#include <Gui/Document.h>
#include <Gui/Selection.h>
#include <Gui/Tools.h>
#include <Gui/ViewProvider.h>
#include <Mod/TechDraw/App/DrawPage.h>
#include <Mod/TechDraw/App/DrawProjGroup.h>
#include <Mod/TechDraw/App/DrawProjGroupItem.h>
#include <Mod/TechDraw/App/DrawUtil.h>
#include <Mod/TechDraw/App/DrawView.h>

#include "QGIView.h"
#include "MDIViewPage.h"
#include "PreferencesGui.h"
#include "QGCustomBorder.h"
#include "QGCustomClip.h"
#include "QGCustomImage.h"
#include "QGCustomLabel.h"
#include "QGICaption.h"
#include "QGIVertex.h"
#include "QGIViewClip.h"
#include "QGSPage.h"
#include "QGVPage.h"
#include "Rez.h"
#include "ViewProviderDrawingView.h"
#include "ViewProviderPage.h"
#include "ZVALUE.h"


using namespace TechDrawGui;
using namespace TechDraw;

#define NODRAG 0
#define DRAGSTARTED 1
#define DRAGGING 2

const float labelCaptionFudge = 0.2f;   // temp fiddle for devel

QGIView::QGIView()
    :QGraphicsItemGroup(),
     viewObj(nullptr),
     m_locked(false),
     m_innerView(false),
     m_dragState(NODRAG)
{
    setCacheMode(QGraphicsItem::NoCache);
    setHandlesChildEvents(false);
    setAcceptHoverEvents(true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemSendsScenePositionChanges, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);

    m_colNormal = prefNormalColor();
    m_colCurrent = m_colNormal;
    m_pen.setColor(m_colCurrent);

    m_decorPen.setStyle(Qt::DashLine);
    m_decorPen.setWidth(0); // 0 => 1px "cosmetic pen"

    m_label = new QGCustomLabel();
    addToGroup(m_label);
    m_border = new QGCustomBorder();
    addToGroup(m_border);
    m_caption = new QGICaption();
    addToGroup(m_caption);
    m_lock = new QGCustomImage();
    m_lock->setParentItem(m_border);
    m_lock->load(QString::fromUtf8(":/icons/TechDraw_Lock.svg"));
    QSize sizeLock = m_lock->imageSize();
    m_lockWidth = (double) sizeLock.width();
    m_lockHeight = (double) sizeLock.height();
    m_lock->hide();
}

QGIView::~QGIView()
{
    signalSelectPoint.disconnect_all_slots();
}

void QGIView::onSourceChange(TechDraw::DrawView* newParent)
{
    Q_UNUSED(newParent);
}

void QGIView::isVisible(bool state)
{
    auto feat = getViewObject();
    if (feat) {
        auto vp = QGIView::getViewProvider(feat);
        if (vp) {
            Gui::ViewProviderDocumentObject* vpdo = dynamic_cast<Gui::ViewProviderDocumentObject*>(vp);
            if (vpdo) {
                vpdo->Visibility.setValue(state);
            }
        }
    }
}

bool QGIView::isVisible()
{
    bool result = false;
    auto feat = getViewObject();
    if (feat) {
        auto vp = QGIView::getViewProvider(feat);
        if (vp) {
            Gui::ViewProviderDocumentObject* vpdo = dynamic_cast<Gui::ViewProviderDocumentObject*>(vp);
            if (vpdo) {
                result = vpdo->Visibility.getValue();
            }
        }
    }
    return result;
}

//Set selection state for this and it's children
//required for items like dimensions & balloons
void QGIView::setGroupSelection(bool isSelected)
{
    setSelected(isSelected);
}

void QGIView::alignTo(QGraphicsItem*item, const QString &alignment)
{
    alignHash.clear();
    alignHash.insert(alignment, item);
}

QVariant QGIView::itemChange(GraphicsItemChange change, const QVariant &value)
{
    QPointF newPos(0.0, 0.0);
//    Base::Console().Message("QGIV::itemChange(%d)\n", change);
    if(change == ItemPositionChange && scene()) {
        newPos = value.toPointF();            //position within parent!
        if(m_locked){
            newPos.setX(pos().x());
            newPos.setY(pos().y());
            return newPos;
        }

        // TODO  find a better data structure for this
        // this is just a pair isn't it?
        if (getViewObject()->isDerivedFrom(TechDraw::DrawProjGroupItem::getClassTypeId())) {
            TechDraw::DrawProjGroupItem* dpgi = static_cast<TechDraw::DrawProjGroupItem*>(getViewObject());
            TechDraw::DrawProjGroup* dpg = dpgi->getPGroup();
            if (dpg) {
                if(alignHash.size() == 1) {   //if aligned.
                    QGraphicsItem* item = alignHash.begin().value();
                    QString alignMode   = alignHash.begin().key();
                    if(alignMode == QString::fromLatin1("Vertical")) {
                        newPos.setX(item->pos().x());
                    } else if(alignMode == QString::fromLatin1("Horizontal")) {
                        newPos.setY(item->pos().y());
                    }
                }
            }
        } else {
            //not a dpgi, not locked, but moved.
            //feat->setPosition(Rez::appX(newPos.x()), -Rez::appX(newPos.y());
        }
        return newPos;
    }

    if (change == ItemSelectedHasChanged && scene()) {
        if(isSelected()) {
            m_colCurrent = getSelectColor();
//            m_selectState = 2;
        } else {
            m_colCurrent = PreferencesGui::normalQColor();
//            m_selectState = 0;
        }
        drawBorder();
    }

    return QGraphicsItemGroup::itemChange(change, value);
}

void QGIView::mousePressEvent(QGraphicsSceneMouseEvent * event)
{
//    Base::Console().Message("QGIV::mousePressEvent() - %s\n", getViewName());
    signalSelectPoint(this, event->pos());
    if (m_dragState == NODRAG) {
        m_dragState = DRAGSTARTED;
    }

    QGraphicsItem::mousePressEvent(event);
}

void QGIView::mouseMoveEvent(QGraphicsSceneMouseEvent * event)
{
    if (m_dragState == DRAGSTARTED) {
        m_dragState = DRAGGING;
    }
    QGraphicsItem::mouseMoveEvent(event);
}

void QGIView::mouseReleaseEvent(QGraphicsSceneMouseEvent * event)
{
    //TODO: this should be done in itemChange - item position has changed
//    Base::Console().Message("QGIV::mouseReleaseEvent() - %s\n", getViewName());
//    if(scene() && this == scene()->mouseGrabberItem()) {
    if (m_dragState == DRAGGING) {
        if(!m_locked) {
            if (!isInnerView()) {
                double tempX = x(),
                       tempY = getY();
                getViewObject()->setPosition(Rez::appX(tempX), Rez::appX(tempY));
            } else {
                getViewObject()->setPosition(Rez::appX(x()), Rez::appX(getYInClip(y())));
            }
        }
    }
    m_dragState = NODRAG;

    QGraphicsItem::mouseReleaseEvent(event);
}

void QGIView::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
//    Base::Console().Message("QGIV::hoverEnterEvent()\n");
    Q_UNUSED(event);
    // TODO don't like this but only solution at the minute (MLP)
    if (isSelected()) {
        m_colCurrent = getSelectColor();
    } else {
        m_colCurrent = getPreColor();
    }
    drawBorder();
}

void QGIView::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event);
    if(isSelected()) {
        m_colCurrent = getSelectColor();
    } else {
        m_colCurrent = PreferencesGui::normalQColor();
    }
    drawBorder();
}

//sets position in /Gui(graphics), not /App
void QGIView::setPosition(qreal xPos, qreal yPos)
{
//    Base::Console().Message("QGIV::setPosition(%.3f, %.3f) (gui)\n", x, y);
    double newX = xPos;
    double newY;
    double oldX = pos().x();
    double oldY = pos().y();
    if (!isInnerView()) {
        newY = -yPos;
    } else {
        newY = getYInClip(yPos);
    }
    if ( (TechDraw::DrawUtil::fpCompare(newX, oldX)) &&
         (TechDraw::DrawUtil::fpCompare(newY, oldY)) ) {
        return;
    } else {
        setPos(newX, newY);
    }
}

//is this needed anymore???
double QGIView::getYInClip(double y)
{
    return -y;
}

QGIViewClip* QGIView::getClipGroup()
{
    if (!getViewObject()->isInClip()) {
        Base::Console().Log( "Logic Error - getClipGroup called for child "
                         "(%s) not in Clip\n", getViewName() );
        return nullptr;
    }

    QGIViewClip* result = nullptr;
    auto parentClip( dynamic_cast<QGCustomClip*>( parentItem() ) );
    if (parentClip) {
        auto parentView( dynamic_cast<QGIViewClip*>( parentClip->parentItem() ) );
        if (parentView) {
            result = parentView;
        }
    }
    return result;
}

void QGIView::updateView(bool forceUpdate)
{
//    Base::Console().Message("QGIV::updateView() - %s\n", getViewObject()->getNameInDocument());

    //allow/prevent dragging
    if (getViewObject()->isLocked()) {
        setFlag(QGraphicsItem::ItemIsMovable, false);
    } else {
        setFlag(QGraphicsItem::ItemIsMovable, true);
    }

    if (getViewObject() && forceUpdate) {
        setPosition(Rez::guiX(getViewObject()->X.getValue()),
                    Rez::guiX(getViewObject()->Y.getValue()));
    }

    double appRotation = getViewObject()->Rotation.getValue();
    double guiRotation = rotation();
    if (!TechDraw::DrawUtil::fpCompare(appRotation, guiRotation)) {
        rotateView();
    }

    QGIView::draw();
}

//QGIVP derived classes do not need a rotate view method as rotation is handled on App side.
void QGIView::rotateView()
{
//NOTE: QPainterPaths have to be rotated individually. This transform handles Rotation for everything else.
//Scale is handled in GeometryObject for DVP & descendents
//Objects not descended from DVP must setScale for themselves
//note that setTransform(, ,rotation, ,) is not the same as setRotation!!!
    double rot = getViewObject()->Rotation.getValue();
    QPointF centre = boundingRect().center();
    setTransform(QTransform().translate(centre.x(), centre.y()).rotate(-rot).translate(-centre.x(), -centre.y()));
}

double QGIView::getScale()
{
    double result = 1.0;
    TechDraw::DrawView* feat = getViewObject();
    if (feat) {
        result = feat->getScale();
    }
    return result;
}
const char * QGIView::getViewName() const
{
    return viewName.c_str();
}
const std::string QGIView::getViewNameAsString() const
{
    return viewName;
}


TechDraw::DrawView * QGIView::getViewObject() const
{
    return viewObj;
}

void QGIView::setViewFeature(TechDraw::DrawView *obj)
{
    if (!obj)
        return;

    viewObj = obj;
    viewName = obj->getNameInDocument();

    //mark the actual QGraphicsItem so we can check what's in the scene later
    setData(0, QString::fromUtf8("QGIV"));
    setData(1, QString::fromUtf8(obj->getNameInDocument()));
}

void QGIView::toggleCache(bool state)
{
    // temp for devl. chaching was hiding problems WF
    //setCacheMode((state)? NoCache : NoCache);
    Q_UNUSED(state);
    setCacheMode(NoCache);
}

void QGIView::draw()
{
//    Base::Console().Message("QGIV::draw()\n");
    double xFeat, yFeat;
    if (getViewObject()) {
        xFeat = Rez::guiX(getViewObject()->X.getValue());
        yFeat = Rez::guiX(getViewObject()->Y.getValue());
        if (!getViewObject()->LockPosition.getValue()) {
            setPosition(xFeat, yFeat);
        }
    }
    if (isVisible()) {
        drawBorder();
        show();
    } else {
        hide();
    }
}

void QGIView::drawCaption()
{
//    Base::Console().Message("QGIV::drawCaption()\n");
    prepareGeometryChange();
    QRectF displayArea = customChildrenBoundingRect();
    m_caption->setDefaultTextColor(m_colCurrent);
    m_font.setFamily(Preferences::labelFontQString());
    int fontSize = exactFontSize(Preferences::labelFont(),
                                 Preferences::labelFontSizeMM());
    m_font.setPixelSize(fontSize);
    m_caption->setFont(m_font);
    QString captionStr = QString::fromUtf8(getViewObject()->Caption.getValue());
    m_caption->setPlainText(captionStr);
    QRectF captionArea = m_caption->boundingRect();
    QPointF displayCenter = displayArea.center();
    m_caption->setX(displayCenter.x() - captionArea.width()/2.);
    double labelHeight = (1 - labelCaptionFudge) * m_label->boundingRect().height();
    auto vp = static_cast<ViewProviderDrawingView*>(getViewProvider(getViewObject()));
    if (getFrameState() || vp->KeepLabel.getValue()) {            //place below label if label visible
        m_caption->setY(displayArea.bottom() + labelHeight);
    } else {
        m_caption->setY(displayArea.bottom() + labelCaptionFudge * Preferences::labelFontSizeMM());
    }
    m_caption->show();
}

void QGIView::drawBorder()
{
//    Base::Console().Message("QGIV::drawBorder() - %s\n", getViewName());
    auto feat = getViewObject();
    if (!feat)
        return;

    drawCaption();   //always draw caption

    auto vp = static_cast<ViewProviderDrawingView*>(getViewProvider(getViewObject()));
    if (!getFrameState() && !vp->KeepLabel.getValue()) {
         m_label->hide();
         m_border->hide();
         m_lock->hide();
        return;
    }

    m_label->hide();
    m_border->hide();
    m_lock->hide();

    m_label->setDefaultTextColor(m_colCurrent);
    m_font.setFamily(Preferences::labelFontQString());
    int fontSize = exactFontSize(Preferences::labelFont(),
                                 Preferences::labelFontSizeMM());
    m_font.setPixelSize(fontSize);
    m_label->setFont(m_font);

    QString labelStr = QString::fromUtf8(getViewObject()->Label.getValue());
    m_label->setPlainText(labelStr);
    QRectF labelArea = m_label->boundingRect();                //m_label coords
    double labelWidth = m_label->boundingRect().width();
    double labelHeight = (1 - labelCaptionFudge) * m_label->boundingRect().height();

    QBrush b(Qt::NoBrush);
    m_border->setBrush(b);
    m_decorPen.setColor(m_colCurrent);
    m_border->setPen(m_decorPen);

    QRectF displayArea = customChildrenBoundingRect();
    double displayWidth = displayArea.width();
    double displayHeight = displayArea.height();
    QPointF displayCenter = displayArea.center();
    m_label->setX(displayCenter.x() - labelArea.width()/2.);
    m_label->setY(displayArea.bottom());

    double frameWidth = displayWidth;
    if (labelWidth > displayWidth) {
        frameWidth = labelWidth;
    }
    double frameHeight = labelHeight + displayHeight;

    QRectF frameArea = QRectF(displayCenter.x() - frameWidth/2.,
                              displayArea.top(),
                              frameWidth,
                              frameHeight);

    double lockX = frameArea.left();
    double lockY = frameArea.bottom() - m_lockHeight;
    if (feat->isLocked() &&
        feat->showLock()) {
        m_lock->setZValue(ZVALUE::LOCK);
        m_lock->setPos(lockX, lockY);
        m_lock->show();
    } else {
        m_lock->hide();
    }

    prepareGeometryChange();
    m_border->setRect(frameArea.adjusted(-2, -2, 2,2));
    m_border->setPos(0., 0.);

    m_label->show();
    if (getFrameState()) {
        m_border->show();
    }
}

void QGIView::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    QStyleOptionGraphicsItem myOption(*option);
    myOption.state &= ~QStyle::State_Selected;

//    painter->setPen(Qt::red);
//    painter->drawRect(boundingRect());          //good for debugging

    QGraphicsItemGroup::paint(painter, &myOption, widget);
}

QRectF QGIView::customChildrenBoundingRect() const
{
    QList<QGraphicsItem*> children = childItems();
    int dimItemType = QGraphicsItem::UserType + 106;  // TODO: Get magic number from include by name
    int borderItemType = QGraphicsItem::UserType + 136;  // TODO: Magic number warning
    int labelItemType = QGraphicsItem::UserType + 135;  // TODO: Magic number warning
    int captionItemType = QGraphicsItem::UserType + 180;  // TODO: Magic number warning
    int leaderItemType = QGraphicsItem::UserType + 232;  // TODO: Magic number warning
    int textLeaderItemType = QGraphicsItem::UserType + 233;  // TODO: Magic number warning
    int editablePathItemType = QGraphicsItem::UserType + 301;  // TODO: Magic number warning
    int movableTextItemType = QGraphicsItem::UserType + 300;
    int weldingSymbolItemType = QGraphicsItem::UserType + 340;
    QRectF result;
    for (QList<QGraphicsItem*>::iterator it = children.begin(); it != children.end(); ++it) {
        if (!(*it)->isVisible()) {
            continue;
        }
        if ( ((*it)->type() != dimItemType) &&
             ((*it)->type() != leaderItemType) &&
             ((*it)->type() != textLeaderItemType) &&
             ((*it)->type() != editablePathItemType) &&
             ((*it)->type() != movableTextItemType) &&
             ((*it)->type() != borderItemType) &&
             ((*it)->type() != labelItemType)  &&
             ((*it)->type() != weldingSymbolItemType)  &&
             ((*it)->type() != captionItemType) ) {
            QRectF childRect = mapFromItem(*it, (*it)->boundingRect()).boundingRect();
            result = result.united(childRect);
            //result = result.united((*it)->boundingRect());
        }
    }
    return result;
}

QRectF QGIView::boundingRect() const
{
    return m_border->rect().adjusted(-2., -2., 2., 2.);     //allow for border line width  //TODO: fiddle brect if border off?
}

QGIView* QGIView::getQGIVByName(std::string name)
{
    QList<QGraphicsItem*> qgItems = scene()->items();
    QList<QGraphicsItem*>::iterator it = qgItems.begin();
    for (; it != qgItems.end(); it++) {
        QGIView* qv = dynamic_cast<QGIView*>((*it));
        if (qv) {
            const char* qvName = qv->getViewName();
            if(name.compare(qvName) == 0) {
                return (qv);
            }
        }
    }
    return nullptr;
}

/* static */
Gui::ViewProvider* QGIView::getViewProvider(App::DocumentObject* obj)
{
    if (obj) {
        Gui::Document* guiDoc = Gui::Application::Instance->getDocument(obj->getDocument());
        return guiDoc->getViewProvider(obj);
    }
    return nullptr;
}

QGVPage* QGIView::getQGVPage(TechDraw::DrawView* dView)
{
    ViewProviderPage* vpp = getViewProviderPage(dView);
    if (!vpp) {
        return vpp->getQGVPage();
    }
    return nullptr;
}

QGSPage* QGIView::getQGSPage(TechDraw::DrawView* dView)
{
    ViewProviderPage* vpp = getViewProviderPage(dView);
    if (vpp) {
        return vpp->getQGSPage();
    }
    return nullptr;
}

MDIViewPage* QGIView::getMDIViewPage() const
{
    if (!getViewObject()) {
        return nullptr;
    }
    ViewProviderPage* vpp = getViewProviderPage(getViewObject());
    if (vpp) {
        return vpp->getMDIViewPage();
    }
    return nullptr;
}

ViewProviderPage* QGIView::getViewProviderPage(TechDraw::DrawView* dView)
{
    if (!dView)  {
        return nullptr;
    }
    TechDraw::DrawPage* page = dView->findParentPage();
    if (!page) {
        return nullptr;
    }

    Gui::Document* activeGui = Gui::Application::Instance->getDocument(page->getDocument());
    if (!activeGui) {
        return nullptr;
    }

    return dynamic_cast<ViewProviderPage*>(activeGui->getViewProvider(page));
}

//remove a child of this from scene while keeping scene indexes valid
void QGIView::removeChild(QGIView* child)
{
    if (child && (child->parentItem() == this) ) {
        prepareGeometryChange();
        scene()->removeItem(child);
    }
}

bool QGIView::getFrameState()
{
//    Base::Console().Message("QGIV::getFrameState() - %s\n", getViewName());
    bool result = true;
    TechDraw::DrawView* dv = getViewObject();
    if (dv) {
        TechDraw::DrawPage* page = dv->findParentPage();
        if (page) {
            Gui::Document* activeGui = Gui::Application::Instance->getDocument(page->getDocument());
            Gui::ViewProvider* vp = activeGui->getViewProvider(page);
            ViewProviderPage* vpp = dynamic_cast<ViewProviderPage*>(vp);
            if (vpp) {
                result = vpp->getFrameState();
            }
        }
    }
    return result;
}

void QGIView::hideFrame()
{
    m_border->hide();
    m_label->hide();
}

void QGIView::addArbitraryItem(QGraphicsItem* qgi)
{
    if (qgi) {
//        m_randomItems.push_back(qgi);
        addToGroup(qgi);
        qgi->show();
    }
}

void QGIView::setStack(int z)
{
    m_zOrder = z;
    setZValue(z);
    draw();
}

void QGIView::setStackFromVP()
{
    TechDraw::DrawView* feature = getViewObject();
    ViewProviderDrawingView* vpdv = static_cast<ViewProviderDrawingView*>
                                    (getViewProvider(feature));
    int z = vpdv->getZ();
    setStack(z);
}

QColor QGIView::prefNormalColor()
{
    return PreferencesGui::normalQColor();
}

QColor QGIView::getPreColor()
{
    return PreferencesGui::preselectQColor();
}

QColor QGIView::getSelectColor()
{
    return PreferencesGui::selectQColor();
}

Base::Reference<ParameterGrp> QGIView::getParmGroupCol()
{
    Base::Reference<ParameterGrp> hGrp = App::GetApplication().GetUserParameter()
                                        .GetGroup("BaseApp")->GetGroup("Preferences")->GetGroup("Mod/TechDraw/Colors");
    return hGrp;
}

//convert input font size in mm to scene units
//note that when used to set font size this will result in
//text that is smaller than sizeInMillimetres.  If exactly
//correct sized text is required, use exactFontSize.
int QGIView::calculateFontPixelSize(double sizeInMillimetres)
{
    // Calculate font size in pixels by using resolution conversion
    // and round to nearest integer
    return (int) (Rez::guiX(sizeInMillimetres) + 0.5);
}

int QGIView::calculateFontPixelWidth(const QFont &font)
{
    // Return the width of digit 0, most likely the most wide digit
    return Gui::QtTools::horizontalAdvance(QFontMetrics(font), QChar::fromLatin1('0'));
}

const double QGIView::DefaultFontSizeInMM = 5.0;

void QGIView::dumpRect(const char* text, QRectF rect) {
    Base::Console().Message("DUMP - %s - rect: (%.3f, %.3f) x (%.3f, %.3f)\n", text,
                            rect.left(), rect.top(), rect.right(), rect.bottom());
}

//determine the required font size to generate text with upper case
//letter height = nominalSize
int QGIView::exactFontSize(std::string fontFamily, double nominalSize)
{
    double sceneSize = Rez::guiX(nominalSize);      //desired height in scene units
    QFont font;
    font.setFamily(QString::fromUtf8(fontFamily.c_str()));
    font.setPixelSize(sceneSize);

    QFontMetricsF fm(font);
    double capHeight = fm.capHeight();
    double ratio = sceneSize / capHeight;
    return (int) sceneSize * ratio;
}

void QGIView::makeMark(double xPos, double yPos, QColor color)
{
    QGIVertex* vItem = new QGIVertex(-1);
    vItem->setParentItem(this);
    vItem->setPos(xPos, yPos);
    vItem->setWidth(2.0);
    vItem->setRadius(20.0);
    vItem->setNormalColor(color);
    vItem->setFillColor(color);
    vItem->setPrettyNormal();
    vItem->setZValue(ZVALUE::VERTEX);
}

void QGIView::makeMark(Base::Vector3d pos, QColor color)
{
    makeMark(pos.x, pos.y, color);
}

void QGIView::makeMark(QPointF pos, QColor color)
{
    makeMark(pos.x(), pos.y(), color);
}

#include <Mod/TechDraw/Gui/moc_QGIView.cpp>
