/*
Copyright (C) 2007-2024 Remon Sijrier

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

#pragma once

#include "TContextItem.h"
#include "TObjectIdTags.h"
#include <QDomNode>
#include <QList>
#include "TTimeRef.h"

class TSession;
class TTimeLineMarker;
class TLocation;
class TCommand;
class TExportSpecification;

class TTimeLineRuler : public TContextItem
{
    Q_OBJECT
public:
    TTimeLineRuler(TSession* sheet);
    ~TTimeLineRuler() {}

    QDomNode get_state(QDomDocument doc);
    int set_state(const QDomNode& node);

    QList<TTimeLineMarker*> get_markers() const {return m_markers;}
    QList<TTimeLineMarker*> get_cd_layout(bool & endmarker);
    TSession *get_sheet() const {return m_sheet;}

    // Derived from its song's id (TObjectFactory::make_owned())
    RulerId object_id() const { return m_objectId; }

    TTimeLineMarker* get_end_marker();
    bool get_end_location(TTimeRef& location);
    bool get_start_location(TTimeRef& location);
    bool has_end_marker();

    TCommand* add_marker_at(const TTimeRef when);
    TCommand* add_marker(TTimeLineMarker* marker, bool historable=true);
    TCommand* remove_marker(TTimeLineMarker* marker, bool historable=true);

    QString format_cdtrack_name(TTimeLineMarker *, int);
    QList<TTimeLineMarker *> get_cdtrack_list(TExportSpecification*);
    QString get_cdrdao_tracklist(TExportSpecification* spec, QString file_path, bool pregap = false);

private:
    friend class TObjectFactory;
    RulerId m_objectId;
    TSession* m_sheet;
    QList<TTimeLineMarker*> m_markers;
    void index_markers();

private slots:
    void private_add_marker(TTimeLineMarker* marker);
    void private_remove_marker(TTimeLineMarker* marker);
    void marker_position_changed();

signals:
    void markerAdded(TTimeLineMarker*);
    void markerRemoved(TTimeLineMarker*);
    void markerPositionChanged();
};


//eof
