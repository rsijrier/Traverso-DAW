// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TWorkspace.h"

#include "TCommand.h"
#include "TObjectFactory.h"
#include "TSheet.h"
#include "TTrack.h"

#include "Debugger.h"

#include <QPoint>

TWorkspace::TWorkspace(TSession* parentSession)
    : TSession(parentSession)
{
}

int TWorkspace::set_state(const QDomNode& node)
{
    PENTER;

    QDomElement e = node.toElement();

    m_name = e.attribute("name", "");
    m_scrollBarXValue = e.attribute("sbx", "0").toInt();
    m_scrollBarYValue = e.attribute("sby", "0").toInt();

    QDomNode tracksNode = node.firstChildElement("Tracks");
    QDomNode trackNode = tracksNode.firstChild();

    while (!trackNode.isNull()) {
        QDomElement trackElement = trackNode.toElement();

        const std::optional<TrackId> id = TObjectFactory::read_reference<TrackTag>(trackElement, "track");
        TTrack* track = id ? m_parentSession->get_track(*id) : nullptr;
        if (track) {
            TCommand* cmd = add_track(track);
            // add_track(track) should not return a TCommand object
            // but directly add track and return nullptr, this is by design
            Q_ASSERT(!cmd);
            set_track_height(track->object_id(), trackElement.attribute("height", "90").toInt());
        } else {
            TLOG_W("ids", "workspace {}: the track {} is not in its parent, left out", m_name,
                   trackElement.attribute("track"));
        }

        trackNode = trackNode.nextSibling();
    }

    return 1;
}

QDomNode TWorkspace::get_state(QDomDocument doc)
{
    QDomElement sheetNode = doc.createElement("WorkSheet");

    TObjectFactory::write(sheetNode, object_id());
    if (TSheet* sheet = qobject_cast<TSheet*>(m_parentSession)) {
        TObjectFactory::write_owner(sheetNode, sheet->object_id());
    }
    sheetNode.setAttribute("name", m_name);
    sheetNode.setAttribute("sbx", get_scrollbar_xy().x());
    sheetNode.setAttribute("sby", get_scrollbar_xy().y());

    QDomNode tracksNode = doc.createElement("Tracks");

    foreach (TTrack* track, get_tracks()) {
        QDomElement trackNode = doc.createElement("Track");

        TObjectFactory::write_reference(trackNode, "track", track->object_id());
        trackNode.setAttribute("height", get_track_height(track->object_id()));
        tracksNode.appendChild(trackNode);
    }

    sheetNode.appendChild(tracksNode);

    return sheetNode;
}
