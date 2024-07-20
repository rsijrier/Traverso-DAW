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

#ifndef EXPORT_DIALOG_H
#define EXPORT_DIALOG_H

#include "ui_ExportDialog.h"

#include <QDialog>

class ExportFormatOptionsWidget;
class Project;
class Sheet;

class ExportDialog : public QDialog, protected Ui::ExportDialog
{
	Q_OBJECT

public:
	ExportDialog(QWidget* parent = 0);
	~ExportDialog();

	void set_was_closed();

protected:
	void closeEvent(QCloseEvent* event);

private:
	Project* m_project{};
	ExportFormatOptionsWidget* m_formatOptionsWidget;

	bool is_safe_to_export();

	int m_lastSheetExported{};
	bool m_wasClosed{};
	int m_copyNumber{};

private slots:
	void set_project(Project* project);
	void render_finished();

	void on_fileSelectButton_clicked();
	void on_startButton_clicked();
	void on_closeButton_clicked();

	void reject();
};

#endif

//eof


