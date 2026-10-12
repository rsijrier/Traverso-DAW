/*
    Copyright (C) 2006 Remon Sijrier 
 
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
 
    $Id: TrackPanelViewPort.h,v 1.1 2008/01/21 16:17:30 r_sijrier Exp $
*/

#ifndef TTRACK_PANEL_VIEW_PORT_H
#define TTRACK_PANEL_VIEW_PORT_H

#include "TClipsViewPort.h"

class TSheetWidget;
		
class TTrackPanelViewPort : public TViewPort
{
public:
    TTrackPanelViewPort(QGraphicsScene* scene, TSheetWidget* sw);
    ~TTrackPanelViewPort() {};

private:
    TSheetWidget* m_sw;
};

#endif

//eof

 
 
 
 
