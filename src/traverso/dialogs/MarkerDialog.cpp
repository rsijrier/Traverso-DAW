/*
    Copyright (C) 2007 Remon Sijrier, Nicola Doebelin 
 
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

#include "MarkerDialog.h"
#include "Debugger.h"

#include <QMessageBox>
#include <QHeaderView>
#include <QPushButton>
#include <QCheckBox>
#include <QString>
#include <TProjectManager.h>
#include <TProject.h>
#include <TSheet.h>
#include <TTimeLineRuler.h>
#include <TTimeLineMarker.h>
#include <Utils.h>
#include <QDebug>
#include <QTextStream>
#include <QFileDialog>
#include <QDateTime>
#include <TAddRemoveCommand.h>
#include "TInformUser.h"
#include "TLocation.h"

MarkerDialog::MarkerDialog(QWidget * parent)
	: QDialog(parent)
{
	setupUi(this);
	
	m_project = pm().get_project();
        m_session = m_project->get_current_session();
	setWindowTitle("Marker Editor - Project " + m_project->get_title());

	QString mask = "99:99:99,99";
	lineEditPosition->setInputMask(mask);

	markersTreeWidget->header()->resizeSection(1, 100);

	pushButtonRemove->setAutoDefault(false);
	pushButtonExport->setAutoDefault(false);
	pushButtonOk->setAutoDefault(false);

	// connect other stuff related to the treeWidget
	connect(lineEditTitle, &QLineEdit::textEdited, this, &MarkerDialog::description_changed);
	connect(lineEditPosition, &QLineEdit::textEdited, this, &MarkerDialog::position_changed);
	connect(markersTreeWidget, &QTreeWidget::currentItemChanged, this, &MarkerDialog::item_changed);
	connect(pushButtonRemove, &QPushButton::clicked, this, &MarkerDialog::remove_marker);

	// connect the CD-Text widgets (LineEdits and ToolButtons)
	connect(lineEditPosition, &QLineEdit::returnPressed, this, &MarkerDialog::position_enter);
	connect(lineEditTitle, &QLineEdit::returnPressed, this, &MarkerDialog::title_enter);
	connect(lineEditComposer, &QLineEdit::returnPressed, this, &MarkerDialog::composer_enter);
	connect(lineEditPerformer, &QLineEdit::returnPressed, this, &MarkerDialog::performer_enter);
	connect(lineEditArranger, &QLineEdit::returnPressed, this, &MarkerDialog::arranger_enter);
	connect(lineEditMessage, &QLineEdit::returnPressed, this, &MarkerDialog::message_enter);
	connect(lineEditSongwriter, &QLineEdit::returnPressed, this, &MarkerDialog::sheetwriter_enter);
	connect(lineEditIsrc, &QLineEdit::returnPressed, this, &MarkerDialog::isrc_enter);

	connect(toolButtonTitleAll, &QPushButton::clicked, this, &MarkerDialog::title_all);
	connect(toolButtonComposerAll, &QPushButton::clicked, this, &MarkerDialog::composer_all);
	connect(toolButtonPerformerAll, &QPushButton::clicked, this, &MarkerDialog::performer_all);
	connect(toolButtonArrangerAll, &QPushButton::clicked, this, &MarkerDialog::arranger_all);
	connect(toolButtonMessageAll, &QPushButton::clicked, this, &MarkerDialog::message_all);
	connect(toolButtonSongWriterAll, &QPushButton::clicked, this, &MarkerDialog::songwriter_all);
	connect(toolButtonCopyAll, &QPushButton::clicked, this, &MarkerDialog::copy_all);
	connect(toolButtonPEmphAll, &QPushButton::clicked, this, &MarkerDialog::pemph_all);

	connect(pushButtonExport, &QPushButton::clicked, this, &MarkerDialog::export_toc);
	connect(pushButtonOk, &QPushButton::clicked, this, &MarkerDialog::apply);
	connect(pushButtonCancel, &QPushButton::clicked, this, &MarkerDialog::cancel);

	update_marker_treeview();
}

void MarkerDialog::update_marker_treeview()
{
	int currentIndex = markersTreeWidget->indexOfTopLevelItem(markersTreeWidget->currentItem());

	// since the treeWidget will be cleared, point m_marker to somewhere else
	m_marker = (TTimeLineMarker*)nullptr;
	markersTreeWidget->clear();

        TTimeLineRuler* tl = m_session->get_timeline_ruler();
		
	foreach(TTimeLineMarker* marker, tl->get_markers()) {
		QString name = marker->get_description();
        QString pos = TTimeRef::timeref_to_cd_including_hours(marker->get_location()->get_start());

		QTreeWidgetItem* item = new QTreeWidgetItem(markersTreeWidget);
		item->setText(0, pos.simplified());
		item->setText(1, name);
		item->setData(0, Qt::UserRole, marker->get_id());
	}

	if (currentIndex >= markersTreeWidget->topLevelItemCount()) {
		currentIndex = markersTreeWidget->topLevelItemCount() - 1;
	}

	markersTreeWidget->setCurrentItem(markersTreeWidget->topLevelItem(currentIndex));
}

void MarkerDialog::item_changed(QTreeWidgetItem * current, QTreeWidgetItem * previous)
{
	if (!current) {
		m_marker = (TTimeLineMarker*)nullptr;
		return;
	}

	m_marker = get_marker(current->data(0, Qt::UserRole).toLongLong());

	if (!m_marker) {
		return;
	}

	if (previous) {
		TTimeLineMarker *marker = get_marker(previous->data(0, Qt::UserRole).toLongLong());
		marker->set_when(TTimeRef::cd_to_timeref_including_hours(lineEditPosition->text()));
		marker->set_description(lineEditTitle->text());
		marker->set_performer(lineEditPerformer->text());
		marker->set_composer(lineEditComposer->text());
		marker->set_songwriter(lineEditSongwriter->text());
		marker->set_arranger(lineEditArranger->text());
		marker->set_message(lineEditMessage->text());
		marker->set_isrc(lineEditIsrc->text());
		marker->set_preemphasis(checkBoxPreEmph->isChecked());
		marker->set_copyprotect(checkBoxCopy->isChecked());
	}

    lineEditPosition->setText(TTimeRef::timeref_to_cd_including_hours(m_marker->get_location()->get_start()));
	lineEditTitle->setText(m_marker->get_description());
	lineEditPerformer->setText(m_marker->get_performer());
	lineEditComposer->setText(m_marker->get_composer());
	lineEditSongwriter->setText(m_marker->get_songwriter());
	lineEditArranger->setText(m_marker->get_arranger());
	lineEditMessage->setText(m_marker->get_message());
	lineEditIsrc->setText(m_marker->get_isrc());
	checkBoxPreEmph->setChecked(m_marker->get_preemphasis());
	checkBoxCopy->setChecked(m_marker->get_copyprotect());
}

// update the entry in the tree widget in real time
void MarkerDialog::description_changed(const QString &s)
{
	QTreeWidgetItem* item = markersTreeWidget->currentItem();

	if (!item || !m_marker) {
		return;
	}

	item->setText(1, s);
	m_marker->set_description(s);
}

// update the entry in the tree widget in real time
void MarkerDialog::position_changed(const QString &s)
{
	QTreeWidgetItem* item = markersTreeWidget->currentItem();

	if (!item || !m_marker) {
		return;
	}

	item->setText(0, s);
	
// 	AAAH, wouldn't it be sooo fun to have un/redo when 
// 	editing the Markers from here ?
// 	But the realtime thing plays not nice, what about only
// 	calling this function when the user hits enter ?

	TTimeRef location = TTimeRef::cd_to_timeref_including_hours(s);
	m_marker->set_when(location);
	markersTreeWidget->sortItems(0, Qt::AscendingOrder);
}

// find the marker based on it's id.
TTimeLineMarker * MarkerDialog::get_marker(qint64 id)
{
        TTimeLineRuler* tl = m_session->get_timeline_ruler();

	foreach(TTimeLineMarker* marker, tl->get_markers()) {
		if (marker->get_id() == id) {
			return marker;
		}
	}
	return nullptr;
}

void MarkerDialog::apply()
{
	accept();
}

void MarkerDialog::cancel()
{
	reject();
}

// One slot per widget, to avoid using QObject::sender() to determine the sender
void MarkerDialog::position_enter()
{
	next_item(lineEditPosition);
}

void MarkerDialog::title_enter()
{
	next_item(lineEditTitle);
}

void MarkerDialog::performer_enter()
{
	next_item(lineEditPerformer);
}

void MarkerDialog::composer_enter()
{
	next_item(lineEditComposer);
}

void MarkerDialog::arranger_enter()
{
	next_item(lineEditArranger);
}

void MarkerDialog::sheetwriter_enter()
{
	next_item(lineEditSongwriter);
}

void MarkerDialog::message_enter()
{
	next_item(lineEditMessage);
}

void MarkerDialog::isrc_enter()
{
	next_item(lineEditIsrc);
}

// this handles the case when "enter" is pressed on a lineEdit. It sets the next item current,
// and selects the text in the lineEdit
void MarkerDialog::next_item(QLineEdit *ledit)
{
	QTreeWidgetItem *pitem = markersTreeWidget->currentItem();
	int max = markersTreeWidget->topLevelItemCount();
	int idx = markersTreeWidget->indexOfTopLevelItem(pitem);
	int nidx = 0;

	if (idx < max-1) {
		nidx = idx + 1;
	}

	QTreeWidgetItem *citem = markersTreeWidget->topLevelItem(nidx);
	markersTreeWidget->setCurrentItem(citem);
	ledit->setSelection(0, ledit->text().length());
}

void MarkerDialog::title_all()
{
	QString str = lineEditTitle->text();
	if (QMessageBox::question(this, tr("Set all Titles"), 
					tr("Do you really want to set all titles to\n\"")
					+str+"\"?", QMessageBox::Yes | QMessageBox::No, 
					QMessageBox::Yes) == QMessageBox::No)
	{
		return;
	}

	for (int i = 0; i < markersTreeWidget->topLevelItemCount(); ++i) {
		QTreeWidgetItem *it = markersTreeWidget->topLevelItem(i);
		TTimeLineMarker *m = get_marker(it->data(0, Qt::UserRole).toLongLong());
		m->set_description(str);
		it->setText(1, str);
	}
}

void MarkerDialog::performer_all()
{
	QString str = lineEditPerformer->text();
	if (QMessageBox::question(this, tr("Set all Performers"), 
					tr("Do you really want to set all performers to\n\"")
					+str+"\"?", QMessageBox::Yes | QMessageBox::No, 
					QMessageBox::Yes) == QMessageBox::No)
	{
		return;
	}

	for (int i = 0; i < markersTreeWidget->topLevelItemCount(); ++i) {
		QTreeWidgetItem *it = markersTreeWidget->topLevelItem(i);
		TTimeLineMarker *m = get_marker(it->data(0, Qt::UserRole).toLongLong());
		m->set_performer(str);
	}
}

void MarkerDialog::composer_all()
{
	QString str = lineEditComposer->text();
	if (QMessageBox::question(this, tr("Set all Composers"), 
					tr("Do you really want to set all composers to\n\"")
					+str+"\"?", QMessageBox::Yes | QMessageBox::No, 
					QMessageBox::Yes) == QMessageBox::No)
	{
		return;
	}

	for (int i = 0; i < markersTreeWidget->topLevelItemCount(); ++i) {
		QTreeWidgetItem *it = markersTreeWidget->topLevelItem(i);
		TTimeLineMarker *m = get_marker(it->data(0, Qt::UserRole).toLongLong());
		m->set_composer(str);
	}
}

void MarkerDialog::arranger_all()
{
	QString str = lineEditArranger->text();
	if (QMessageBox::question(this, tr("Set all Arrangers"), 
					tr("Do you really want to set all arrangers to\n\"")
					+str+"\"?", QMessageBox::Yes | QMessageBox::No, 
					QMessageBox::Yes) == QMessageBox::No)
	{
		return;
	}

	for (int i = 0; i < markersTreeWidget->topLevelItemCount(); ++i) {
		QTreeWidgetItem *it = markersTreeWidget->topLevelItem(i);
		TTimeLineMarker *m = get_marker(it->data(0, Qt::UserRole).toLongLong());
		m->set_arranger(str);
	}
}

void MarkerDialog::songwriter_all()
{
	QString str = lineEditSongwriter->text();
	if (QMessageBox::question(this, tr("Set all Songwriters"), 
					tr("Do you really want to set all songwriters to\n\"")
					+str+"\"?", QMessageBox::Yes | QMessageBox::No, 
					QMessageBox::Yes) == QMessageBox::No)
	{
		return;
	}

	for (int i = 0; i < markersTreeWidget->topLevelItemCount(); ++i) {
		QTreeWidgetItem *it = markersTreeWidget->topLevelItem(i);
		TTimeLineMarker *m = get_marker(it->data(0, Qt::UserRole).toLongLong());
		m->set_songwriter(str);
	}
}

void MarkerDialog::message_all()
{
	QString str = lineEditMessage->text();
	if (QMessageBox::question(this, tr("Set all Messages"), 
					tr("Do you really want to set all messages to\n\"")
					+str+"\"?", QMessageBox::Yes | QMessageBox::No, 
					QMessageBox::Yes) == QMessageBox::No)
	{
		return;
	}

	for (int i = 0; i < markersTreeWidget->topLevelItemCount(); ++i) {
		QTreeWidgetItem *it = markersTreeWidget->topLevelItem(i);
		TTimeLineMarker *m = get_marker(it->data(0, Qt::UserRole).toLongLong());
		m->set_message(str);
	}
}

void MarkerDialog::copy_all()
{
	QString str = "off";

	if (checkBoxCopy->isChecked()) {
		str = "on";
	}

	if (QMessageBox::question(this, tr("Set all Copy Protection Flags"), 
					tr("Do you really want to set all copy protection flags to\n\"")
					+str+"\"?", QMessageBox::Yes | QMessageBox::No, 
					QMessageBox::Yes) == QMessageBox::No)
	{
		return;
	}

	for (int i = 0; i < markersTreeWidget->topLevelItemCount(); ++i) {
		QTreeWidgetItem *it = markersTreeWidget->topLevelItem(i);
		TTimeLineMarker *m = get_marker(it->data(0, Qt::UserRole).toLongLong());
		m->set_copyprotect(checkBoxCopy->isChecked());
	}
}

void MarkerDialog::pemph_all()
{
	QString str = "off";

	if (checkBoxPreEmph->isChecked()) {
		str = "on";
	}

	if (QMessageBox::question(this, tr("Set all Pre-Emphasis Flags"), 
					tr("Do you really want to set all pre-emphasis flags to\n\"")
					+str+"\"?", QMessageBox::Yes | QMessageBox::No, 
					QMessageBox::Yes) == QMessageBox::No)
	{
		return;
	}

	for (int i = 0; i < markersTreeWidget->topLevelItemCount(); ++i) {
		QTreeWidgetItem *it = markersTreeWidget->topLevelItem(i);
		TTimeLineMarker *m = get_marker(it->data(0, Qt::UserRole).toLongLong());
		m->set_preemphasis(checkBoxPreEmph->isChecked());
	}
}
/**
 * @brief GUI context slot triggering the removal of the currently selected timeline marker.
 *        Casts polymorphically via the clean TAddRemoveCommandBase interface to apply the command.
 */
void MarkerDialog::remove_marker()
{
    if (!m_marker) {
        return;
    }

    if (m_marker->get_type() == TTimeLineMarker::ENDMARKER) {
        tInformUser().information(tr("It's not possible to remove the endmarker!!"));
        return;
    }

    TTimeLineRuler* tl = m_session->get_timeline_ruler();

    auto* ar = tl->remove_marker(m_marker);

    if (ar) {
        TCommand::process_command(ar);
    }

    update_marker_treeview();
}


void MarkerDialog::export_toc()
{
	QString fn = QFileDialog::getSaveFileName (0, tr("Export Table of Contents"), m_project->get_root_dir(), tr("HTML File (*.html)"));

	// if aborted exit here
	if (fn.isEmpty()) {
		return;
	}

	QFile file(fn);

	// check if the selected file can be opened for writing
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		TLog::write_printf(TLogLevel::Warning, TLOG_FILE_CATEGORY, "Could not open file for writing.");
		return;
	}

	QTextStream out(&file);

	out << "<html>\n  <head>\n    <meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\">\n  </head>\n\n  <body>\n";

	out << "    <h1>" << m_project->get_title() << "</h1>\n";
	out << "    <h2>" << m_project->get_description() << "</h2>\n";
	
	out << "    <hr>\n";
	out << "    <table>\n      <tr><th>Position (mm:ss:frames)</th><th>Title</th>\n";

        TTimeLineRuler* tl = m_session->get_timeline_ruler();
	foreach(TTimeLineMarker* marker, tl->get_markers()) {
		QString name = marker->get_description();
        QString pos = TTimeRef::timeref_to_cd(marker->get_location()->get_start());

		out << "      <tr><td>" << pos << "</td>\n        <td>" << name << "</td></tr>\n";
	}	

	QDateTime dt = QDateTime::currentDateTime();

	out << "    </table>\n  <hr>\n  " << dt.toString("MMM dd, yyyy, hh:mm") << "\n</body>\n</html>\n";
}


//eof
