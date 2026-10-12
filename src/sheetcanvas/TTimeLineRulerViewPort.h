/*
    Copyright (C) 2006-2024 Remon Sijrier
 
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
 
    $Id: TimeLineViewPort.h,v 1.2 2009/01/19 21:07:19 r_sijrier Exp $
*/

#pragma once

#include <QGraphicsScene>
#include "TViewPort.h"

class TSheetWidget;
class TSheetView;
class TTimeLineRulerView;

#define TIMELINE_HEIGHT 46

class TTimeLineRulerViewPort : public TViewPort
{
public:
    TTimeLineRulerViewPort(QGraphicsScene* scene, TSheetWidget* sw);
    ~TTimeLineRulerViewPort() override;

    void set_sheetview(TSheetView* view) override;
    void scale_factor_changed();
    TTimeLineRulerView* get_timeline_view() const { return m_timeLineView; }

private:
    TTimeLineRulerView* m_timeLineView{nullptr};
};
