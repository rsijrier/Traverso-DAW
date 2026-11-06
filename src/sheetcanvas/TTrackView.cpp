/*
Copyright (C) 2005-2010 Remon Sijrier

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

#include <QLineEdit>
#include <QInputDialog>
#include <QGraphicsScene>

#include "TTrackView.h"
#include "TTrackLaneView.h"
#include "TAudioPluginChainView.h"
#include "TThemer.h"
#include "TTrackPanelViewPort.h"
#include "TSheetView.h"
#include "TTrackPanelView.h"
#include "TMainWindow.h"

#include <TSheet.h>
#include <TTrack.h>
#include <Utils.h>
#include "TAudioPluginChain.h"
#include "TCurveView.h"

#include <PluginSelectorDialog.h>
#include "dialogs/TTrackManagerDialog.h"

#include <Debugger.h>

TTrackView::TTrackView(TSheetView* sv, TTrack * track)
	: TViewItem(nullptr, track)
{
        PENTERCONS;
	m_sv = sv;
	m_track = track;
    m_animation = new QPropertyAnimation(this, "yPosition", this);
	setZValue(sv->zValue() + 1);

	setFlags(QGraphicsItem::ItemUsesExtendedStyleOption);

	m_sv->scene()->addItem(this);

    TTrackView::load_theme_data();

	m_isMoving = false;

	connect(m_track, &TTrack::activeContextChanged, this, &TTrackView::active_context_changed);
	connect(m_track, &TTrack::automationVisibilityChanged, this, &TTrackView::automation_visibility_changed);

	m_primaryLaneView = new TTrackLaneView(this);
	m_laneViews.append(m_primaryLaneView);

	m_volumeAutomationLaneView = new TTrackLaneView(this);
	m_laneViews.append(m_volumeAutomationLaneView);

	m_curveView = new TCurveView(m_sv, m_volumeAutomationLaneView, m_track->audio()->get_fader()->get_curve());
	m_volumeAutomationLaneView->set_child_view(m_curveView);

	m_visibleLanes = 1;
}

TTrackView:: ~ TTrackView( )
= default;

void TTrackView::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
        Q_UNUSED(widget);

    painter->save();
// 	printf("TrackView:: PAINT :: exposed rect is: x=%f, y=%f, w=%f, h=%f\n", option->exposedRect.x(), option->exposedRect.y(), option->exposedRect.width(), option->exposedRect.height());

	int xstart = (int)option->exposedRect.x();
	int pixelcount = (int)option->exposedRect.width();

    // if (m_topborderwidth > 0) {
    // 	QColor color = themer()->get_color("Track:cliptopoffset");
    // 	painter->fillRect(xstart, 0, pixelcount+1, m_topborderwidth, color);
    // }

 //    if (m_bottomborderwidth > 0) {
 //        QColor color = themer()->get_color("Track:clipbottomoffset");
 //        painter->fillRect(xstart, get_total_height() - m_bottomborderwidth, pixelcount+1, m_bottomborderwidth, color);
 //    }

    if (m_track->has_active_context()) {
		QPen pen;
		int penwidth = 1;
		pen.setWidth(penwidth);
        pen.setColor(themer()->get_color("Track:mousehover"));
		painter->setPen(pen);
        painter->drawLine(xstart, m_topborderwidth, xstart+pixelcount, m_topborderwidth);
        painter->drawLine(xstart, get_total_height() - m_bottomborderwidth - 1, xstart+pixelcount, get_total_height() - m_bottomborderwidth - 1);

        painter->fillRect(option->exposedRect, themer()->get_color("Track:mousehover").lighter(260));

	}

    if (m_isMoving) {
        painter->fillRect(option->exposedRect, themer()->get_color("Track:mousehover"));
    }

    if (m_visibleLanes > 1) {
        QPen pen;
        pen.setColor(themer()->get_color("Track:laneseperator"));
        painter->setPen(pen);
        for (int i = 1; i<m_laneViews.size(); ++i) {
            TTrackLaneView* laneView = m_laneViews.at(i);
            if (!laneView->isVisible()) {
                continue;
            }
            int y =  laneView->pos().y() - 1;
            painter->drawLine(xstart, y, xstart+pixelcount, y);
        }
    }
    painter->restore();
}

int TTrackView::get_height( ) const
{
	return m_sv->get_track_height(m_track);
}

TCommand* TTrackView::edit_properties( )
{
        TTrackManagerDialog* manager = new TTrackManagerDialog(m_track, TMainWindow::instance());
        manager->open();
        return nullptr;
}

TCommand* TTrackView::add_new_plugin( )
{
        PluginSelectorDialog::instance()->set_description(tr("Track %1:  %2")
                        .arg(m_track->get_sort_index()+1).arg(m_track->get_name()));

        if (PluginSelectorDialog::instance()->exec() == QDialog::Accepted) {
                TAudioPlugin* plugin = PluginSelectorDialog::instance()->get_selected_plugin();
                if (plugin) {
                    return m_track->audio()->add_plugin(plugin);
                }
        }

        return nullptr;
}

void TTrackView::add_lane_view(TTrackLaneView *laneView)
{
	m_laneViews.append(laneView);
}

void TTrackView::set_height( int height )
{
        m_height = height;
	m_primaryLaneView->set_height(height);
	layout_lanes();
}

void TTrackView::set_moving(bool move)
{
        m_isMoving = move;
        update();
        m_panel->update();
}

void TTrackView::move_to( int x, int y )
{
	Q_UNUSED(x);
//	setPos(0, y);
    if (m_animation->state() == QPropertyAnimation::Running) {
        m_animation->stop();
    }
    m_animation->setStartValue(getYPosition());
    m_animation->setEndValue(y);
    m_animation->setDuration(100);
    m_animation->start();
}

bool TTrackView::animatedMoveRunning() const
{
    return m_animation->state() == QPropertyAnimation::Running;
}

void TTrackView::setYPosition(qreal position)
{
    setPos(0, position);
    m_panel->setPos(-m_sv->get_trackpanel_view_port()->width(), position);
}

void TTrackView::layout_lanes()
{
	int verticalposition = m_cliptopmargin;
	m_visibleLanes = 0;

	foreach(TTrackLaneView* lane, m_laneViews) {
		if (lane->isVisible()) {
			lane->move_to(0, verticalposition);
			verticalposition += lane->get_height() + m_laneSpacing;
			m_visibleLanes += 1;
		}
	}
}

int TTrackView::get_total_height()
{
	int totalHeight = 0;
	foreach(TTrackLaneView* lane, m_laneViews) {
		if (lane->isVisible()) {
			totalHeight += lane->get_height() + m_laneSpacing;
		}

	}

	totalHeight += m_laneSpacing;

	return totalHeight;
}

void TTrackView::calculate_bounding_rect()
{
        prepareGeometryChange();
	set_height(m_sv->get_track_height(m_track));
	m_boundingRect = QRectF(0, 0, MAX_CANVAS_WIDTH, get_total_height());
        m_panel->calculate_bounding_rect();
        TViewItem::calculate_bounding_rect();
}

void TTrackView::load_theme_data()
{
        m_paintBackground = themer()->get_property("Track:paintbackground").toInt();
        m_topborderwidth = themer()->get_property("Track:topborderwidth").toInt();
        m_bottomborderwidth = themer()->get_property("Track:bottomborderwidth").toInt();
	m_cliptopmargin = themer()->get_property("Track:cliptopmargin").toInt();
	m_clipbottommargin = themer()->get_property("Track:clipbottommargin").toInt();

	m_laneSpacing = 2;
}

void TTrackView::automation_visibility_changed()
{
	if (m_track->show_track_volume_automation()) {
		m_volumeAutomationLaneView->setVisible(true);
	} else {
		m_volumeAutomationLaneView->setVisible(false);
	}

	layout_lanes();
	calculate_bounding_rect();

	emit totalTrackHeightChanged();
}
