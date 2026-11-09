/*
Copyright (C) 2005-2007 Remon Sijrier 

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

#include "ProjectManagerDialog.h"

#include "TInformUser.h"
#include "TProjectManager.h"

#include <QStringList>
#include <QInputDialog>
#include <QHeaderView>
#include <QTextStream>
#include <QFile>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QUndoStack>
#include <dialogs/project/NewSheetDialog.h>
#include "TProject.h"
#include "TSheet.h"
#include "TCommand.h"
#include "TMainWindow.h"



#include "Debugger.h"

ProjectManagerDialog::ProjectManagerDialog( QWidget * parent )
	: QDialog(parent)
{
	setupUi(this);

        QIcon icon = QApplication::style()->standardIcon(QStyle::SP_DirClosedIcon);
        sheetAudioSourcesPushButton->setIcon(icon);

	treeSheetWidget->setColumnCount(3);
	treeSheetWidget->header()->resizeSection(0, 160);
	treeSheetWidget->header()->resizeSection(1, 55);
	treeSheetWidget->header()->resizeSection(2, 70);
	QStringList stringList;
	stringList << "Sheet Name" << "Tracks" << "Length";
	treeSheetWidget->setHeaderLabels(stringList);
	
	set_project(pm().get_project());
	
    undoButton->setIcon(QIcon(TMainWindow::find_pixmap(":/undo")));
    redoButton->setIcon(QIcon(TMainWindow::find_pixmap(":/redo")));
	
	buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);

	connect(treeSheetWidget, &QTreeWidget::itemClicked, this, &ProjectManagerDialog::sheetitem_clicked);
    connect(sheetsAreTrackFolderCheckBox, &QCheckBox::checkStateChanged, this, &ProjectManagerDialog::sheets_are_track_folder_check_box_state_changed);
    connect(&pm(), &TProjectManager::projectLoaded, this, &ProjectManagerDialog::set_project);
}

ProjectManagerDialog::~ProjectManagerDialog()
{}

void ProjectManagerDialog::set_project(TProject* project)
{
	m_project = project;
	
	if (m_project) {
        connect(m_project, &TProject::sheetAdded, this, &ProjectManagerDialog::update_sheet_list);
        connect(m_project, &TProject::sheetRemoved, this, &ProjectManagerDialog::update_sheet_list);
        connect(m_project->get_history_stack(), &QUndoStack::redoTextChanged, this, &ProjectManagerDialog::redo_text_changed);
        connect(m_project->get_history_stack(), &QUndoStack::undoTextChanged, this, &ProjectManagerDialog::undo_text_changed);
        setWindowTitle("Manage Project - " + m_project->get_title());
		descriptionTextEdit->setText(m_project->get_description());
        createdDateLabel->setText("Created on: " + m_project->get_created_on().toLocalTime().toString());
		lineEditTitle->setText(m_project->get_title());
		lineEditId->setText(m_project->get_discid());
		lineEditUPC->setText(m_project->get_upc_ean());
		lineEditPerformer->setText(m_project->get_performer());
		lineEditArranger->setText(m_project->get_arranger());
		lineEditSongwriter->setText(m_project->get_songwriter());
		lineEditMessage->setText(m_project->get_message());
		comboBoxGenre->setCurrentIndex(m_project->get_genre());
		redoButton->setText(m_project->get_history_stack()->redoText());
		undoButton->setText(m_project->get_history_stack()->undoText());

                sheetsAreTrackFolderCheckBox->setChecked(m_project->sheets_are_track_folder());
                if (m_project->sheets_are_track_folder()) {
                        sheetAudioSourcesFrame->show();
                } else {
                        sheetAudioSourcesFrame->hide();
                }
	} else {
		setWindowTitle("Manage Project - No Project loaded!");
		treeSheetWidget->clear();
		descriptionTextEdit->clear();
		lineEditTitle->clear();
		lineEditId->clear();
		lineEditUPC->clear();
		lineEditPerformer->clear();
		lineEditArranger->clear();
		lineEditSongwriter->clear();
		lineEditMessage->clear();
		comboBoxGenre->setCurrentIndex(0);
                sheetAudioSourcesFrame->hide();
        }
	
	update_sheet_list();
}

void ProjectManagerDialog::sheets_are_track_folder_check_box_state_changed(int state)
{
        if (!m_project) {
                return;
        }

        if (state == Qt::Checked) {
                m_project->set_sheets_are_tracks_folder(true);
                sheetAudioSourcesFrame->show();

        } else {
                m_project->set_sheets_are_tracks_folder(false);
                sheetAudioSourcesFrame->hide();
        }
}


void ProjectManagerDialog::update_sheet_list( )
{
	if ( ! m_project) {
		TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "ProjectManagerDialog:: no project ?");
		return;
	}
	
	treeSheetWidget->clear();
	foreach(TSheet* sheet, m_project->get_sheets()) {

		QString sheetNr = QString::number(m_project->get_sheet_index(sheet->object_id()));
                QString sheetName = "Sheet " + sheetNr + " - " + sheet->get_name();
		QString numberOfTracks = QString::number(sheet->get_audio_track_count());
		QString sheetLength = TTimeRef::timeref_to_ms_2(sheet->get_last_location());
		QString sheetStatus = sheet->is_changed()?"UnSaved":"Saved";
		QString sheetSpaceAllocated = "Unknown";

		QTreeWidgetItem* item = new QTreeWidgetItem(treeSheetWidget);
		item->setTextAlignment(1, Qt::AlignHCenter);
		item->setTextAlignment(2, Qt::AlignHCenter);
		item->setText(0, sheetName);
		item->setText(1, numberOfTracks);
		item->setText(2, sheetLength);
		
		item->setData(0, Qt::UserRole, QVariant::fromValue(sheet->object_id()));
	}
}

void ProjectManagerDialog::sheetitem_clicked( QTreeWidgetItem* item, int)
{
	if (!item) {
		return;
	}

	TSheet* sheet;

	sheet = m_project->get_sheet(item->data(0, Qt::UserRole).value<SongId>());

	Q_ASSERT(sheet);
		
        selectedSheetName->setText(sheet->get_name());
	
        m_project->set_current_session(sheet);
}

void ProjectManagerDialog::on_renameSheetButton_clicked( )
{
	if( ! m_project) {
		return;
	}
	
	QTreeWidgetItem* item = treeSheetWidget->currentItem();
	
	if ( ! item) {
		return;
	}
	
	TSheet* sheet = m_project->get_sheet(item->data(0, Qt::UserRole).value<SongId>());
	
	Q_ASSERT(sheet);
	
	QString newtitle = selectedSheetName->text();
	
	if (newtitle.isEmpty()) {
		tInformUser().information(tr("No new Sheet name was supplied!"));
		return;
	}
	
        sheet->set_name(newtitle);

	update_sheet_list();
}

void ProjectManagerDialog::on_deleteSheetButton_clicked( )
{
	QTreeWidgetItem* item = treeSheetWidget->currentItem();
	
	if ( ! item ) {
		return;
	}
	
	TSheet* sheet = m_project->get_sheet(item->data(0, Qt::UserRole).value<SongId>());
	Q_ASSERT(sheet);
	
	TCommand::process_command(m_project->remove_sheet(sheet));
}

void ProjectManagerDialog::on_createSheetButton_clicked( )
{
	TMainWindow::instance()->show_newsheet_dialog();
}

void ProjectManagerDialog::redo_text_changed(QString text)
{
	redoButton->setText(text);
}

void ProjectManagerDialog::undo_text_changed(QString text)
{
	undoButton->setText(text);
}

void ProjectManagerDialog::on_undoButton_clicked()
{
	if (! m_project ) {
		return;
	}
	
	m_project->get_history_stack()->undo();
}

void ProjectManagerDialog::on_redoButton_clicked()
{
	if (! m_project ) {
		return;
	}
	
	m_project->get_history_stack()->redo();
}

void ProjectManagerDialog::on_sheetsExportButton_clicked()
{
	TMainWindow::instance()->show_export_widget();
}

void ProjectManagerDialog::on_exportTemplateButton_clicked()
{
	bool ok;
	QString text = QInputDialog::getText(this, tr("Save Template"),
					     tr("Enter Template name"),
					     QLineEdit::Normal, "", &ok);
	if (! ok || text.isEmpty()) {
		return;
	}
	
	QString fileName = QDir::homePath() + "/.traverso/ProjectTemplates/";
	
	QDir dir;
	if (! dir.exists(fileName)) {
		if (! dir.mkdir(fileName)) {
			tInformUser().critical( tr("Unable to create directory %1!").arg(fileName));
			return;
		}
	}
	
	fileName.append(text + ".tpt");
	
	QDomDocument doc(text);
	
	if (QFile::exists(fileName)) {
		QMessageBox::StandardButton button = QMessageBox::question(this,
				tr("Traverso - Information"),
				tr("Template with name %1 already exists!\n Do you want to overwrite it?").arg(fileName),
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::No);
				
		if (button == QMessageBox::No) {
			return;
		}
	}
	
	QFile file(fileName);

	if (file.open( QIODevice::WriteOnly ) ) {
		m_project->get_state(doc, true);
		QTextStream stream(&file);
		doc.save(stream, 4);
		file.close();
		tInformUser().information(tr("Saved Project Template: %1").arg(text));
	} else {
		tInformUser().critical( tr("Couldn't open file %1 for writing!").arg(fileName));
	}
	
}


void ProjectManagerDialog::on_sheetAudioSourcesPushButton_clicked()
{
        if (!m_project) {
                return;
        }

        QString path = m_project->get_audiosources_dir();
        QString newPath = QFileDialog::getExistingDirectory(this,
                        tr("Choose or create a new Audio Sources Directory"), path);

        if (newPath.isEmpty() || newPath.isNull()) {
                return;
        }

        QTreeWidgetItem* item = treeSheetWidget->currentItem();

        if ( ! item ) {
                return;
        }

        TSheet* sheet = m_project->get_sheet(item->data(0, Qt::UserRole).value<SongId>());

        if (!sheet) {
                return;
        }

        sheet->set_audio_sources_dir(newPath);
}

void ProjectManagerDialog::accept()
{
	if ( ! m_project ) {
		hide();
		return;
	}
	
	QString newtitle = lineEditTitle->text();
	
	if (newtitle != m_project->get_title()) {
		if (pm().project_exists(newtitle)) {
			QMessageBox::warning( 0, 
				tr("Traverso - Information"), 
				tr("Project with title '%1' allready exists, please supply a different title!").arg(newtitle),
				QMessageBox::Ok);
			return rejected();
		}
	}

	
	m_project->set_title(newtitle);
	m_project->set_description(descriptionTextEdit->toPlainText());
	m_project->set_discid(lineEditId->text());
	m_project->set_upc_ean(lineEditUPC->text());
	m_project->set_performer(lineEditPerformer->text());
	m_project->set_arranger(lineEditArranger->text());
	m_project->set_songwriter(lineEditSongwriter->text());
	m_project->set_message(lineEditMessage->text());
	m_project->set_genre(comboBoxGenre->currentIndex());
	
	hide();
}

void ProjectManagerDialog::reject()
{
	if ( ! m_project ) {
		hide();
		return;
	}
	
	descriptionTextEdit->setText(m_project->get_description());
	lineEditTitle->setText(m_project->get_title());
	lineEditId->setText(m_project->get_discid());
	lineEditUPC->setText(m_project->get_upc_ean());
	lineEditPerformer->setText(m_project->get_performer());
	lineEditArranger->setText(m_project->get_arranger());
	lineEditSongwriter->setText(m_project->get_songwriter());
	lineEditMessage->setText(m_project->get_message());
	comboBoxGenre->setCurrentIndex(m_project->get_genre());

	hide();
}


/* ---------------------------------------------------------------------------------------------- */
/* Here is some stuff about CD-Text. It is difficult to find in the web, so let's archive it here */
/* ---------------------------------------------------------------------------------------------- */

/* @(#)cdtext.h 1.1 02/02/23 Copyright 1999-2002 J. Schilling */
/*
 *      Generic CD-Text support definitions
 *
 *      Copyright (c) 1999-2002 J. Schilling
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
...

#define tc_title        textcodes[0x00]
#define tc_performer    textcodes[0x01]
#define tc_sheetwriter   textcodes[0x02]
#define tc_composer     textcodes[0x03]
#define tc_arranger     textcodes[0x04]
#define tc_message      textcodes[0x05]
#define tc_diskid       textcodes[0x06]
#define tc_genre        textcodes[0x07]
#define tc_toc          textcodes[0x08]
#define tc_toc2         textcodes[0x09]

#define tc_closed_info  textcodes[0x0d]
#define tc_isrc         textcodes[0x0e]
*/

/*
 *      binaere Felder sind
 *      Disc ID                 (Wirklich ???)
 *      Genre ID
 *      TOC
 *      Second TOC
 *      Size information
 */

/*
 * Genre codes from Enhanced CD Specification page 21
 */

// #define GENRE_UNUSED            0       /* not used                          */
// #define GENRE_UNDEFINED         1       /* not defined                       */
// #define GENRE_ADULT_CONTEMP     2       /* Adult Contemporary                */
// #define GENRE_ALT_ROCK          3       /* Alternative Rock                  */
// #define GENRE_CHILDRENS         4       /* Childrens Music                   */
// #define GENRE_CLASSIC           5       /* Classical                         */
// #define GENRE_CHRIST_CONTEMP    6       /* Contemporary Christian            */
// #define GENRE_COUNTRY           7       /* Country                           */
// #define GENRE_DANCE             8       /* Dance                             */
// #define GENRE_EASY_LISTENING    9       /* Easy Listening                    */
// #define GENRE_EROTIC            10      /* Erotic                            */
// #define GENRE_FOLK              11      /* Folk                              */
// #define GENRE_GOSPEL            12      /* Gospel                            */
// #define GENRE_HIPHOP            13      /* Hip Hop                           */
// #define GENRE_JAZZ              14      /* Jazz                              */
// #define GENRE_LATIN             15      /* Latin                             */
// #define GENRE_MUSICAL           16      /* Musical                           */
// #define GENRE_NEWAGE            17      /* New Age                           */
// #define GENRE_OPERA             18      /* Opera                             */
// #define GENRE_OPERETTA          19      /* Operetta                          */
// #define GENRE_POP               20      /* Pop Music                         */
// #define GENRE_RAP               21      /* RAP                               */
// #define GENRE_REGGAE            22      /* Reggae                            */
// #define GENRE_ROCK              23      /* Rock Music                        */
// #define GENRE_RYTHMANDBLUES     24      /* Rhythm & Blues                    */
// #define GENRE_SOUNDEFFECTS      25      /* Sound Effects                     */
// #define GENRE_SPOKEN_WORD       26      /* Spoken Word                       */
// #define GENRE_WORLD_MUSIC       28      /* World Music                       */
// #define GENRE_RESERVED          29      /* Reserved is 29..32767             */
// #define GENRE_RIAA              32768   /* Registration by RIAA 32768..65535 */

/*
 * Character codings used in CD-Text data.
 * Korean and Mandarin Chinese to be defined in sept 1996
 */
// #define CC_8859_1       0x00            /* ISO 8859-1                   */
// #define CC_ASCII        0x01            /* ISO 646, ASCII (7 bit)       */
// #define CC_RESERVED_02  0x02            /* Reserved codes 0x02..0x7f    */
// #define CC_KANJI        0x80            /* Music Shift-JIS Kanji        */
// #define CC_KOREAN       0x81            /* Korean                       */
// #define CC_CHINESE      0x82            /* Mandarin Chinese             */
// #define CC_RESERVED_83  0x83            /* Reserved codes 0x83..0xFF    */


/*
 * Language codes (currently guessed)
 */
// #define LANG_ENGLISH    9


//eof

