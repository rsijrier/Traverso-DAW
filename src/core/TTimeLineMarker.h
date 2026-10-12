/*
Copyright (C) 2007 - 2024 Remon Sijrier

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

#ifndef TTIMELINEMARKER_H
#define TTIMELINEMARKER_H

#include "TContextItem.h"
#include "TTimeLineEntity.h"
#include "TTimeRef.h"
#include <QDomNode>

class TTimeLineRuler;
class TLocation;

class TTimeLineMarker final : public TContextItem, public TTimeLineEntity
{
    Q_OBJECT

public:
    enum MarkerType {
        CDTRACK,
        ENDMARKER,
        PLAYHEADMARKER
    };

    TTimeLineMarker(TTimeLineRuler* tl, const TTimeRef when, MarkerType type = CDTRACK);
    TTimeLineMarker(TTimeLineRuler* tl, const QDomNode& node);
    ~TTimeLineMarker() override = default;

    QDomNode get_state(QDomDocument doc);
    int set_state(const QDomNode& node);

    void set_description(const QString &);
    void set_performer(const QString &);
    void set_composer(const QString &);
    void set_songwriter(const QString &);
    void set_arranger(const QString &);
    void set_message(const QString &);
    void set_isrc(const QString &);
    void set_preemphasis(bool);
    void set_copyprotect(bool);
    void set_index(int);

    TTimeLineRuler * get_timeline() const {return m_timeline;}

    QString get_description() const {return m_description;}
    QString get_performer() const {return m_performer;}
    QString get_composer() const {return m_composer;}
    QString get_songwriter() const {return m_songwriter;}
    QString get_arranger() const {return m_arranger;}
    QString get_message() const {return m_message;}
    QString get_isrc() const {return m_isrc;}
    bool get_preemphasis();
    bool get_copyprotect();
    MarkerType get_type() {return m_type;}
    int get_index() {return m_index;}

    void move_to_location(const TTimeRef& newStartLocation) final;
    unsigned int get_timeline_entity_type() const final { return TTimeLineEntity::Type::TypeMarker; }

public slots:
    void set_when (const TTimeRef& when);

private:
    TTimeLineRuler* m_timeline;
    QString	m_description,
        m_performer,
        m_composer,
        m_songwriter,
        m_arranger,
        m_message,
        m_isrc;
    bool	m_preemph{};
    bool	m_copyprotect{};
    MarkerType	m_type;
    int     m_index{};

signals:
    void descriptionChanged();
    void indexChanged();
};

#endif
