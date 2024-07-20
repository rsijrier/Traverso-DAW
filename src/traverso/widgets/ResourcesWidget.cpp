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

#include "ResourcesWidget.h"

#include <ProjectManager.h>
#include <Project.h>
#include <Sheet.h>
#include <ResourcesManager.h>
#include <AudioSource.h>
#include <ReadSource.h>
#include <AudioClip.h>
#include <Utils.h>
#include <Themer.h>

#include <QHeaderView>
#include <QFileSystemModel>
#include <QListView>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QComboBox>

#define LENGTH_SECTION_WIDTH 60
#define COLUMN_INDENTION 18

FileWidget::FileWidget(QWidget *parent)
    : QWidget(parent)
{
    QPalette palette;
    palette.setColor(QPalette::AlternateBase, themer()->get_color("ResourcesBin:alternaterowcolor"));

    m_fileSystemModel = new QFileSystemModel;
    m_fileSystemModel->setFilter(QDir::Dirs | QDir::Files | QDir::NoDot);
    m_dirView = new QListView;
    m_dirView->setModel(m_fileSystemModel);
    m_dirView->setDragEnabled(true);
    m_dirView->setDropIndicatorShown(true);
    m_dirView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_dirView->setAlternatingRowColors(true);
    m_dirView->setPalette(palette);
    m_fileSystemModel->sort(QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    m_box = new QComboBox(this);
    m_box->addItem("", "");
#if defined (Q_OS_WIN)
    m_box->addItem(tr("My Computer"), "");
    m_box->addItem(tr("My Documents"), QDir::homePath() + "\\" + tr("My Documents"));
#else
    m_box->addItem(QDir::rootPath(), QDir::rootPath());
    m_box->addItem(QDir::homePath(), QDir::homePath());
#endif
    QPushButton* upButton = new QPushButton(this);
    QIcon upIcon = QApplication::style()->standardIcon(QStyle::SP_FileDialogToParent);
    upButton->setToolTip(tr("Parent Directory"));
    upButton->setIcon(upIcon);
    upButton->setMaximumHeight(25);
    upButton->setMaximumWidth(30);


    QHBoxLayout* hlay = new QHBoxLayout;
    hlay->addWidget(upButton);
    hlay->addStretch();

    QVBoxLayout* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    lay->addWidget(m_box);
    lay->addLayout(hlay);
    lay->addWidget(m_dirView);

    setLayout(lay);

    connect(m_dirView, SIGNAL(clicked(QModelIndex&)), this, SLOT(dirview_item_clicked(QModelIndex&)));
    connect(upButton, SIGNAL(clicked()), this, SLOT(dir_up_button_clicked()));
    connect(m_box, SIGNAL(activated(int)), this, SLOT(box_actived(int)));
}

void FileWidget::dirview_item_clicked(const QModelIndex & index)
{
    if (m_fileSystemModel->isDir(index)) {
		m_dirView->setRootIndex(index);
        pm().get_project()->set_import_dir(m_fileSystemModel->filePath(index));
        m_box->setItemText(0, m_fileSystemModel->filePath(index));
        m_box->setItemData(0, m_fileSystemModel->filePath(index));
		m_box->setCurrentIndex(0);
	}
}

void FileWidget::dir_up_button_clicked()
{
    QDir dir(m_fileSystemModel->filePath(m_dirView->rootIndex()));
	
#if defined (Q_OS_WIN)
	if (m_dirModel->filePath(m_dirView->rootIndex()) == "") {
		return;
	}
	QString oldDir = dir.canonicalPath();
#endif
	
	dir.cdUp();
	QString text = dir.canonicalPath();
	
#if defined (Q_OS_WIN)
	if (oldDir == dir.canonicalPath()) {
		dir.setPath("");
		text = tr("My Computer");
	}
#endif
	
    m_dirView->setRootIndex(m_fileSystemModel->index(dir.canonicalPath()));
	m_box->setItemText(0, text);
	m_box->setItemData(0, dir.canonicalPath());
	m_box->setCurrentIndex(0);
}

void FileWidget::box_actived(int i)
{
    m_dirView->setRootIndex(m_fileSystemModel->index(m_box->itemData(i).toString()));
}

void FileWidget::set_current_path(const QString& path) const
{
    m_fileSystemModel->setRootPath(path);
    m_box->setItemText(0, path);
	m_box->setItemData(0, path);
}


ResourcesWidget::ResourcesWidget(QWidget * parent)
    : QWidget(parent)
{
    m_project = nullptr;
    m_currentSheet = nullptr;

    setupUi(this);

    QPalette palette;
    palette.setColor(QPalette::AlternateBase, themer()->get_color("ResourcesBin:alternaterowcolor"));
    sourcesTreeWidget->setPalette(palette);
    sourcesTreeWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    sourcesTreeWidget->setAlternatingRowColors(true);
    sourcesTreeWidget->setDragEnabled(true);
    sourcesTreeWidget->setDropIndicatorShown(true);
    sourcesTreeWidget->setIndentation(COLUMN_INDENTION);
    sourcesTreeWidget->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    sourcesTreeWidget->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    sourcesTreeWidget->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    sourcesTreeWidget->header()->setSectionResizeMode(3, QHeaderView::Fixed);
    sourcesTreeWidget->header()->resizeSection(1, LENGTH_SECTION_WIDTH);
    sourcesTreeWidget->header()->resizeSection(2, LENGTH_SECTION_WIDTH);
    sourcesTreeWidget->header()->resizeSection(3, LENGTH_SECTION_WIDTH);
    sourcesTreeWidget->header()->setStretchLastSection(false);
    sourcesTreeWidget->setUniformRowHeights(true);


    m_filewidget = new FileWidget(this);
    tab_2->layout()->addWidget(m_filewidget);


    connect(sheetComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(sheet_combo_box_index_changed(int)));
    connect(sheetComboBox, SIGNAL(activated(int)), this, SLOT(sheet_combo_box_index_changed(int)));

    connect(&pm(), SIGNAL(projectLoaded(Project*)), this, SLOT(set_project(Project*)));
}

ResourcesWidget::~ ResourcesWidget()
{
}

void ResourcesWidget::showEvent( QShowEvent * event ) 
{
	Q_UNUSED(event);
}

void ResourcesWidget::set_project(Project * project)
{
    m_project = project;

    sourcesTreeWidget->clear();
    m_sourceindices.clear();
	m_clipindices.clear();
	sheetComboBox->clear();
	
	if (!m_project) {
        m_currentSheet = nullptr;
        m_filewidget->set_current_path(QDir::homePath());
        return;
	}

    connect(m_project, SIGNAL(projectLoadFinished()), this, SLOT(project_load_finished()));
}

void ResourcesWidget::project_load_finished()
{
	if (!m_project) {
		return;
	}
	
	ResourcesManager* rsmanager = m_project->get_audiosource_manager();
	
	connect(m_project, SIGNAL(sheetAdded(Sheet*)), this, SLOT(sheet_added(Sheet*)));
	connect(m_project, SIGNAL(sheetRemoved(Sheet*)), this, SLOT(sheet_removed(Sheet*)));
        connect(m_project, SIGNAL(currentSessionChanged(TSession*)), this, SLOT(set_current_session(TSession*)));
	
	connect(rsmanager, SIGNAL(clipAdded(AudioClip*)), this, SLOT(add_clip(AudioClip*)));
	connect(rsmanager, SIGNAL(clipRemoved(AudioClip*)), this, SLOT(remove_clip(AudioClip*)));
	connect(rsmanager, SIGNAL(sourceAdded(ReadSource*)), this, SLOT(add_source(ReadSource*)));
	connect(rsmanager, SIGNAL(sourceRemoved(ReadSource*)), this, SLOT(remove_source(ReadSource*)));
	
	foreach(ReadSource* rs, resources_manager()->get_all_audio_sources()) {
		add_source(rs);
	}
	
	foreach(AudioClip* clip, resources_manager()->get_all_clips()) {
		add_clip(clip);
	}
	
	foreach(Sheet* sheet, m_project->get_sheets()) {
		sheet_added(sheet);
	}
	
    set_current_session(m_project->get_current_session());

    m_filewidget->set_current_path(m_project->get_import_dir());

	sourcesTreeWidget->sortItems(0, Qt::AscendingOrder);
}

void ResourcesWidget::sheet_combo_box_index_changed(int index)
{
        if (!m_project) {
                return;
        }
	qint64 id = sheetComboBox->itemData(index).toLongLong();
	Sheet* sheet = m_project->get_sheet(id);
    set_current_session(sheet);
}

void ResourcesWidget::sheet_added(Sheet * sheet)
{
	sheetComboBox->addItem("Sheet " + QString::number(m_project->get_sheet_index(sheet->get_id())), sheet->get_id());
}

void ResourcesWidget::sheet_removed(Sheet * sheet)
{
	int index = sheetComboBox->findData(sheet->get_id());
	sheetComboBox->removeItem(index);
}

void ResourcesWidget::set_current_session(TSession * session)
{
    Sheet* sheet = qobject_cast<Sheet*>(session);
    if (! sheet)  {
        return;

    }
	if (m_currentSheet == sheet) {
		return;
	}
	
	if (sheet) {
		int index = sheetComboBox->findData(sheet->get_id());
		if (index != -1) {
			sheetComboBox->setCurrentIndex(index);
		}
	}
	
	m_currentSheet = sheet;
	
	filter_on_current_sheet();
}


void ResourcesWidget::filter_on_current_sheet()
{
	if (!m_currentSheet) {
		return;
	}
	
	foreach(ClipTreeItem* item, m_clipindices.values()) {
		item->apply_filter(m_currentSheet);
	}

	
	foreach(SourceTreeItem* item, m_sourceindices.values()) {
		item->apply_filter(m_currentSheet);
	}
}


void ResourcesWidget::add_clip(AudioClip * clip)
{
	ClipTreeItem* item = m_clipindices.value(clip->get_id());
	
	if (!item) {
		SourceTreeItem* sourceitem = m_sourceindices.value(clip->get_readsource_id());
	
		if (! sourceitem ) return;
	
		ClipTreeItem* clipitem = new ClipTreeItem(sourceitem, clip);
		m_clipindices.insert(clip->get_id(), clipitem);
		
		clipitem->setTextAlignment(1, Qt::AlignHCenter);
		clipitem->setTextAlignment(2, Qt::AlignHCenter);
		clipitem->setTextAlignment(3, Qt::AlignLeft);
		
		connect(clip, SIGNAL(positionChanged()), clipitem, SLOT(clip_state_changed()));
	}
	
	update_clip_state(clip);
}

void ResourcesWidget::remove_clip(AudioClip * clip)
{
	ClipTreeItem* item = m_clipindices.value(clip->get_id());
	
	if (!item) {
		 return;
	}
	
	update_clip_state(clip);
}

void ResourcesWidget::add_source(ReadSource * source)
{
	SourceTreeItem* item = m_sourceindices.value(source->get_id());
	
	if (! item) {
		item = new SourceTreeItem(sourcesTreeWidget, source);
		m_sourceindices.insert(source->get_id(), item);
		item->setTextAlignment(1, Qt::AlignHCenter);
		item->setTextAlignment(2, Qt::AlignHCenter);
		item->setTextAlignment(3, Qt::AlignLeft);
	}
	
	item->source_state_changed();
}

void ResourcesWidget::remove_source(ReadSource * source)
{
	SourceTreeItem* item = m_sourceindices.value(source->get_id());

	if (!item) {
		return;
	}

	m_sourceindices.remove(source->get_id());
	sourcesTreeWidget->takeTopLevelItem(sourcesTreeWidget->indexOfTopLevelItem(item));
	delete item;
}

void ResourcesWidget::update_clip_state(AudioClip* clip)
{
	ClipTreeItem* item = m_clipindices.value(clip->get_id());
	Q_ASSERT(item);
	
	item->clip_state_changed();
	
	update_source_state(clip->get_readsource_id());
}

void ResourcesWidget::update_source_state(qint64 id)
{
	SourceTreeItem* item = m_sourceindices.value(id);
	Q_ASSERT(item);
	item->source_state_changed();
}

ClipTreeItem::ClipTreeItem(SourceTreeItem * parent, AudioClip * clip)
	: QTreeWidgetItem(parent)
	, m_clip(clip)
{
	setData(0, Qt::UserRole, clip->get_id());
	connect(clip, SIGNAL(recordingFinished(AudioClip*)), this, SLOT(clip_state_changed()));
	connect(clip, SIGNAL(stateChanged()), this, SLOT(clip_state_changed()));
}

void ClipTreeItem::clip_state_changed()
{
	if (resources_manager()->is_clip_in_use(m_clip->get_id())) {
		for (int i=0; i<5; ++i) {
			setForeground(i, QColor(Qt::black));
		}
	} else {
		for (int i=0; i<5; ++i) {
			setForeground(i, QColor(Qt::lightGray));
		}
	}
	
	QString start = TTimeRef::timeref_to_ms(m_clip->get_source_start_location());
	QString end = TTimeRef::timeref_to_ms(m_clip->get_source_end_location());
		
	setText(0, m_clip->get_name());
	setText(1, TTimeRef::timeref_to_ms(m_clip->get_length()));
	setText(2, start);
	setText(3, end);
	setToolTip(0, m_clip->get_name() + "   " + start + " - " + end);
}

void ClipTreeItem::apply_filter(Sheet * sheet)
{
	if (m_clip->get_sheet_id() == sheet->get_id()) {
		if (isHidden()) {
			setHidden(false);
		}
	} else {
		if (!isHidden()) {
			setHidden(true);
		}
	} 
}



SourceTreeItem::SourceTreeItem(QTreeWidget* parent, ReadSource * source)
	: QTreeWidgetItem(parent)
	, m_source(source)
{
	connect(m_source, SIGNAL(stateChanged()), this, SLOT(source_state_changed()));
}

void SourceTreeItem::apply_filter(Sheet * sheet)
{
	if (m_source->get_orig_sheet_id() == sheet->get_id()) {
		setHidden(false);
		return;
	}
	
	bool show = false;
		
	for (int i=0; i < childCount(); ++i) {
		if (!child(i)->isHidden()) {
			show = true;
			break;
		}
	}
		
	if (show) {
		setHidden(false);
	} else {
		setHidden(true);
	}
}

void SourceTreeItem::source_state_changed()
{
	if (resources_manager()->is_source_in_use(m_source->get_id())) {
		for (int i=0; i<5; ++i) {
			setForeground(i, QColor(Qt::black));
		}
	} else {
		for (int i=0; i<5; ++i) {
			setForeground(i, QColor(Qt::lightGray));
		}
	}
	
    uint rate = m_source->get_sample_rate();
	if (rate == 0) rate = pm().get_project()->get_rate();
	QString duration = TTimeRef::timeref_to_ms(m_source->get_length());
	setText(0, m_source->get_short_name());
	setText(1, duration);
	setText(2, "");
	setText(3, "");
	setData(0, Qt::UserRole, m_source->get_id());
	setToolTip(0, m_source->get_short_name() + "   " + duration);

}

void ResourcesWidget::resizeEvent(QResizeEvent * /*e*/)
{
	if (sourcesTreeWidget) {
		int w = width() - COLUMN_INDENTION;
		int nameSectionWidth = w - (3 * LENGTH_SECTION_WIDTH);
		if (nameSectionWidth < 130) {
			nameSectionWidth = 130;
		}
		
		sourcesTreeWidget->header()->resizeSection(0, nameSectionWidth);
	}
}

