/*
Copyright (C) 2011 Remon Sijrier

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

#ifndef TSHORTCUTEDITORDIALOG_H
#define TSHORTCUTEDITORDIALOG_H

#include <QDialog>

class TShortCutFunction;
class QAbstractButton;

namespace Ui {
	class TShortcutEditorDialog;
}

class TShortcutEditorDialog : public QDialog
{
	Q_OBJECT

public:
	explicit TShortcutEditorDialog(QWidget *parent = 0);
	~TShortcutEditorDialog();

protected:
	void changeEvent(QEvent *e);

private:
	Ui::TShortcutEditorDialog *ui;

	TShortCutFunction* getSelectedFunction();
	void  moveItemUpDown(int direction);

private slots:
	void objects_combo_box_activated(int index);
	void key1_combo_box_activated(int);
	void key_combo_box_activated(int);
	void shortcut_tree_widget_item_activated();
	void show_functions_checkbox_clicked();
	void function_keys_changed();
	void modifier_combo_box_toggled();
	void configure_inherited_shortcut_pushbutton_clicked();
	void base_function_checkbox_clicked();
	void on_restoreDefaultPushButton_clicked();
	void on_upPushButton_clicked();
	void on_downPushButton_clicked();
	void button_box_button_clicked(QAbstractButton*);
};

#endif // SHORTCUTEDITORDIALOG_H
