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

#include <TProjectManager.h>
#include <TProject.h>
#include <TSheet.h>
#include <TResourcesManager.h>
#include <TBufferedAudioStream.h>
#include <TBufferedAudioStreamReader.h>
#include <TAudioClip.h>
#include <TAudioTrack.h>
#include <TThemer.h>
#include "TLocation.h"

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
    // QPalette palette;
    // palette.setColor(QPalette::AlternateBase, themer()->get_color("ResourcesBin:alternaterowcolor"));

    m_fileSystemModel = new QFileSystemModel;
    m_fileSystemModel->setFilter(QDir::Dirs | QDir::Files | QDir::NoDot);
    m_dirView = new QListView;
    m_dirView->setModel(m_fileSystemModel);
    m_dirView->setDragEnabled(true);
    m_dirView->setDropIndicatorShown(true);
    m_dirView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_dirView->setAlternatingRowColors(true);
    // m_dirView->setPalette(palette);
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

	connect(m_dirView, &QListView::clicked, this, &FileWidget::dirview_item_clicked);
	connect(upButton, &QPushButton::clicked, this, &FileWidget::dir_up_button_clicked);
	connect(m_box, &QComboBox::activated, this, &FileWidget::box_actived);
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
	if (m_fileSystemModel->filePath(m_dirView->rootIndex()) == "") {
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

    // QPalette palette;
    // palette.setColor(QPalette::AlternateBase, themer()->get_color("ResourcesBin:alternaterowcolor"));
    // sourcesTreeWidget->setPalette(palette);
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


    connect(sheetComboBox, &QComboBox::currentIndexChanged, this, &ResourcesWidget::sheet_combo_box_index_changed);
    connect(sheetComboBox, &QComboBox::activated, this, &ResourcesWidget::sheet_combo_box_index_changed);

    connect(&pm(), &TProjectManager::projectLoaded, this, &ResourcesWidget::set_project);
}

ResourcesWidget::~ ResourcesWidget()
{
}

void ResourcesWidget::showEvent( QShowEvent * event ) 
{
	Q_UNUSED(event);
}

void ResourcesWidget::set_project(TProject * project)
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

    connect(m_project, &TProject::projectLoadFinished, this, &ResourcesWidget::project_load_finished);
}

void ResourcesWidget::project_load_finished()
{
	if (!m_project) {
		return;
	}
	
	TResourcesManager* rsmanager = m_project->get_audiosource_manager();
	
    connect(m_project, &TProject::sheetAdded, this, &ResourcesWidget::sheet_added);
    connect(m_project, &TProject::sheetRemoved, this, &ResourcesWidget::sheet_removed);
    connect(m_project, &TProject::currentSessionChanged, this, &ResourcesWidget::set_current_session);
	
    connect(rsmanager, &TResourcesManager::clipAdded, this, &ResourcesWidget::add_clip);
    connect(rsmanager, &TResourcesManager::clipRemoved, this, &ResourcesWidget::remove_clip);
    connect(rsmanager, &TResourcesManager::sourceAdded, this, &ResourcesWidget::add_source);
    connect(rsmanager, &TResourcesManager::sourceRemoved, this, &ResourcesWidget::remove_source);
	
	foreach(TBufferedAudioStreamReader* rs, resources_manager()->get_all_audio_sources()) {
		add_source(rs);
	}
	
	foreach(TAudioClip* clip, resources_manager()->get_all_clips()) {
		add_clip(clip);
	}
	
	foreach(TSheet* sheet, m_project->get_sheets()) {
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
	TSheet* sheet = m_project->get_sheet(sheetComboBox->itemData(index).value<SongId>());
    set_current_session(sheet);
}

void ResourcesWidget::sheet_added(TSheet * sheet)
{
	sheetComboBox->addItem("Sheet " + QString::number(m_project->get_sheet_index(sheet->object_id())), QVariant::fromValue(sheet->object_id()));
}

void ResourcesWidget::sheet_removed(TSheet * sheet)
{
	int index = sheetComboBox->findData(QVariant::fromValue(sheet->object_id()));
	sheetComboBox->removeItem(index);
}

void ResourcesWidget::set_current_session(TSession * session)
{
    TSheet* sheet = qobject_cast<TSheet*>(session);
    if (! sheet)  {
        return;

    }
	if (m_currentSheet == sheet) {
		return;
	}
	
	if (sheet) {
		int index = sheetComboBox->findData(QVariant::fromValue(sheet->object_id()));
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


void ResourcesWidget::add_clip(TAudioClip * clip)
{
	ClipTreeItem* item = m_clipindices.value(clip->object_id());
	
	if (!item) {
		SourceTreeItem* sourceitem = m_sourceindices.value(clip->get_readsource_id());
	
		if (! sourceitem ) return;
	
		ClipTreeItem* clipitem = new ClipTreeItem(sourceitem, clip);
		m_clipindices.insert(clip->object_id(), clipitem);
		
		clipitem->setTextAlignment(1, Qt::AlignHCenter);
		clipitem->setTextAlignment(2, Qt::AlignHCenter);
		clipitem->setTextAlignment(3, Qt::AlignLeft);
		
		connect(clip->get_location(), &TLocation::locationChanged, clipitem, &ClipTreeItem::clip_state_changed);
	}
	
	update_clip_state(clip);
}

void ResourcesWidget::remove_clip(TAudioClip * clip)
{
	ClipTreeItem* item = m_clipindices.value(clip->object_id());
	
	if (!item) {
		 return;
	}
	
	update_clip_state(clip);
}

void ResourcesWidget::add_source(TBufferedAudioStreamReader * source)
{
	SourceTreeItem* item = m_sourceindices.value(source->object_id());
	
	if (! item) {
		item = new SourceTreeItem(sourcesTreeWidget, source);
		m_sourceindices.insert(source->object_id(), item);
		item->setTextAlignment(1, Qt::AlignHCenter);
		item->setTextAlignment(2, Qt::AlignHCenter);
		item->setTextAlignment(3, Qt::AlignLeft);
	}
	
	item->source_state_changed();
}

void ResourcesWidget::remove_source(TBufferedAudioStreamReader * source)
{
	SourceTreeItem* item = m_sourceindices.value(source->object_id());

	if (!item) {
		return;
	}

	m_sourceindices.remove(source->object_id());
	// Its clips' items are its children: they go with it
	for (auto it = m_clipindices.begin(); it != m_clipindices.end();) {
		if (it.value()->QTreeWidgetItem::parent() == item) {
			it = m_clipindices.erase(it);
		} else {
			++it;
		}
	}
	sourcesTreeWidget->takeTopLevelItem(sourcesTreeWidget->indexOfTopLevelItem(item));
	delete item;
}

void ResourcesWidget::update_clip_state(TAudioClip* clip)
{
	ClipTreeItem* item = m_clipindices.value(clip->object_id());
	Q_ASSERT(item);
	
	item->clip_state_changed();
	
	update_source_state(clip->get_readsource_id());
}

void ResourcesWidget::update_source_state(AudioSourceId id)
{
	SourceTreeItem* item = m_sourceindices.value(id);
	Q_ASSERT(item);
	item->source_state_changed();
}

ClipTreeItem::ClipTreeItem(SourceTreeItem * parent, TAudioClip * clip)
	: QTreeWidgetItem(parent)
	, m_clip(clip)
{
	// A drop tells a clip from a source by the type of the id
	setData(0, Qt::UserRole, QVariant::fromValue(clip->object_id()));
    connect(clip, &TAudioClip::recordingFinished, this, &ClipTreeItem::clip_state_changed);
	connect(clip, &TAudioClip::stateChanged, this, &ClipTreeItem::clip_state_changed);
}

void ClipTreeItem::clip_state_changed()
{
	if (resources_manager()->is_clip_in_use(m_clip->object_id())) {
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

void ClipTreeItem::apply_filter(TSheet * sheet)
{
	// The song of its track; a clip on no track has the song of its take, an
	// import belongs to the project and shows under every song
	bool belongs;
	if (m_clip->has_track()) {
		belongs = m_clip->get_track()->get_sheet() == sheet;
	} else {
		TBufferedAudioStreamReader* source = m_clip->get_readsource();
		const SongId origin = source ? source->get_orig_sheet_id() : SongId();
		belongs = !origin.is_valid() || origin == sheet->object_id();
	}
	if (belongs) {
		if (isHidden()) {
			setHidden(false);
		}
	} else {
		if (!isHidden()) {
			setHidden(true);
		}
	} 
}



SourceTreeItem::SourceTreeItem(QTreeWidget* parent, TBufferedAudioStreamReader * source)
	: QTreeWidgetItem(parent)
	, m_source(source)
{
	connect(m_source, &TBufferedAudioStreamReader::stateChanged, this, &SourceTreeItem::source_state_changed);
}

void SourceTreeItem::apply_filter(TSheet * sheet)
{
	// A take shows under its song, an import under every song
	const SongId origin = m_source->get_orig_sheet_id();
	if (!origin.is_valid() || origin == sheet->object_id()) {
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
	if (resources_manager()->is_source_in_use(m_source->object_id())) {
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
	setData(0, Qt::UserRole, QVariant::fromValue(m_source->object_id()));
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

