/*
Copyright (C) 2007 Remon Sijrier 

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

#include <QWidget>
#include <QTreeWidgetItem>
#include "TSession.h"
#include "TObjectIdTags.h"
#include "ui_ResourcesWidget.h"

class TProject;
class TSheet;
class TAudioClip;
class TBufferedAudioStreamReader;
class SourceTreeItem;
class QShowEvent;
class QListView;
class QFileSystemModel;
class QComboBox;

class FileWidget : public QWidget
{
	Q_OBJECT
public:

    FileWidget(QWidget* parent=nullptr);

    void set_current_path(const QString& path) const;

private slots:
	void dirview_item_clicked(const QModelIndex & index);
	void dir_up_button_clicked();
	void box_actived(int i);

private:
	QListView* m_dirView;
    QFileSystemModel* m_fileSystemModel;
	QComboBox* m_box;
};


class ClipTreeItem : public QObject, public QTreeWidgetItem
{
	Q_OBJECT
	
public:
	ClipTreeItem(SourceTreeItem* parent, TAudioClip* clip);
	
	void apply_filter(TSheet* sheet);


public slots:
	void clip_state_changed();	

private:
	TAudioClip* m_clip;
};

class SourceTreeItem : public QObject, public QTreeWidgetItem
{
	Q_OBJECT
	
public:
	SourceTreeItem(QTreeWidget* parent, TBufferedAudioStreamReader* source);

	void apply_filter(TSheet* sheet);

private:
	TBufferedAudioStreamReader* m_source;
	
public slots:
	void source_state_changed();
};

class ResourcesWidget : public QWidget, protected Ui::ResourcesWidget
{
	Q_OBJECT

public:
    ResourcesWidget(QWidget* parent=nullptr);
	~ResourcesWidget();

protected:
	void showEvent( QShowEvent * event );
	void resizeEvent( QResizeEvent * e );

private:
	TProject* m_project;
	TSheet* m_currentSheet;
	FileWidget* m_filewidget;
	QHash<ClipId, ClipTreeItem*> m_clipindices;
	QHash<qint64, SourceTreeItem*> m_sourceindices;
	
	void update_clip_state(TAudioClip* clip);
	void update_source_state(qint64 id);
	
	void filter_on_current_sheet();
	
private slots:
	void set_project(TProject* project);
	void project_load_finished();
	
	void sheet_combo_box_index_changed(int index);
	
	void sheet_added(TSheet* sheet);
	void sheet_removed(TSheet* sheet);
    void set_current_session(TSession *sheet);
	
	void add_clip(TAudioClip* clip);
	void remove_clip(TAudioClip* clip);
	void add_source(TBufferedAudioStreamReader* source);
	void remove_source(TBufferedAudioStreamReader* source);
};


