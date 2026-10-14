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
 
*/

#pragma once

#include <QGraphicsScene>
#include <QList>
#include <QStringList>
#include "TViewPort.h"

class TSheetWidget;
class TAudioTrack;

class TClipsViewPort : public TViewPort
{
    Q_OBJECT

public:
    TClipsViewPort(QGraphicsScene* scene, TSheetWidget* sw);
    ~TClipsViewPort() override = default;

protected:
    void resizeEvent(QResizeEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;

private:
    TSheetWidget*                   m_sw;
    QStringList                     m_importFiles;
    QList<qint64>                   m_resourcesImport;
    TAudioTrack*                    m_importTrack{nullptr};
};
