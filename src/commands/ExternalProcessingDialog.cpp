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

#include "ExternalProcessingDialog.h"

#include "AudioClipExternalProcessing.h"

#include <AudioClip.h>
#include <AudioClipView.h>
#include <AudioTrack.h>
#include <ReadSource.h>
#include <ProjectManager.h>
#include <Project.h>
#include <ResourcesManager.h>
#include <Utils.h>
#include "TMainWindow.h"

#include <QFile>
#include <QCompleter>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"



ExternalProcessingDialog::ExternalProcessingDialog(QWidget * parent, AudioClipExternalProcessing* acep)
	: QDialog(parent)
{
	setupUi(this);
	m_acep = acep;
	m_queryOptions = false;
	
	m_processor = new QProcess(this);
	m_processor->setProcessChannelMode(QProcess::MergedChannels);
	
	m_completer = 0;
	
	command_lineedit_text_changed("sox");
	
	connect(m_processor, SIGNAL(readyReadStandardOutput()), this, SLOT(read_standard_output()));
	connect(m_processor, SIGNAL(started()), this, SLOT(process_started()));
    connect(m_processor, SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(process_finished(int,QProcess::ExitStatus)));
    connect(m_processor, SIGNAL(errorOccurred(QProcess::ProcessError)), this, SLOT(process_error(QProcess::ProcessError)));
    connect(argsComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(arg_combo_index_changed(int)));
    connect(programLineEdit, SIGNAL(textChanged(QString)), this, SLOT(command_lineedit_text_changed(QString)));
	connect(startButton, SIGNAL(clicked()), this, SLOT(prepare_for_external_processing()));
	connect(cancelButton, SIGNAL(clicked()), this, SLOT(reject()));
}

ExternalProcessingDialog::~ ExternalProcessingDialog()
{
	delete m_processor;
}


void ExternalProcessingDialog::prepare_for_external_processing()
{
	m_commandargs = argumentsLineEdit->text();
	
	if (m_commandargs.isEmpty()) {
		statusText->setText(tr("You have to supply an argument before starting the external process!"));
		return;
	}
	
	ReadSource* rs = resources_manager()->get_readsource(m_acep->m_clip->get_readsource_id());
	
	//This should NOT be possible, but just in case....
	if (! rs) {
		qDebug("ExternalProcessing:: resources manager did NOT return a resource for the to be processed audioclip (%lld) !!!!\n", m_acep->m_clip->get_id());
		return;
	}
	
	m_filename = rs->get_name();
	m_newClipName= rs->get_short_name().remove(".wav") + "-" + m_commandargs.simplified();
	
	m_infilename = rs->get_filename();
	// remove the extension and any dots that might confuse the external program, append the 
	// new name and again the extension.
	m_outfilename = pm().get_project()->get_audiosources_dir() + 
			m_filename.remove(".wav").remove(".").append("-").append(m_commandargs.simplified()).append(".wav");
	
	
	start_external_processing();
}

void ExternalProcessingDialog::start_external_processing()
{
	m_arguments.clear();
	
	// On mac os x (and perhaps windows) the full path is given, so we check if the path contains sox!
	if (m_program.contains("sox")) {
		m_arguments.append("-S");
	}
	
	m_arguments.append(m_infilename);
	m_arguments.append(m_outfilename);
    static QRegularExpression expression("\\s+");
    m_arguments += m_commandargs.split(expression);
	
	m_processor->start(m_program, m_arguments);
}

void ExternalProcessingDialog::read_standard_output()
{
	if (m_queryOptions) {
        QString result = m_processor->readAllStandardOutput();
		// This list is used to collect the availabe arguments for the 
		// arugment lineedit completer.
		QStringList completionlist;
		
		// On mac os x (and perhaps windows) the full path is given, so we check if the path contains sox!
		if (m_program.contains("sox")) {
			QStringList list = result.split("\n");
			foreach(QString string, list) {
                if (string.contains("Supported effects:") || string.contains("effect:") || string.contains("EFFECTS:")) {
                    result = string.remove("Supported effects:").remove("effect:").remove("EFFECTS:");
                    static QRegularExpression expression("\\s+");
                    QStringList options = string.split(expression);
					foreach(QString string, options) {
						if (!string.isEmpty()) {
							argsComboBox->addItem(string);
							completionlist << string;
						}
					}
				}
			}
		}
			// If there was allready a completer, delete it.
		if (m_completer) {
			delete m_completer;
		}
			
			// Set the completer for the arguments line edit.
		m_completer = new QCompleter(completionlist, this);
		argumentsLineEdit->setCompleter(m_completer);
		
		return;
	}
	
	QString result = m_processor->readAllStandardOutput();
	
	if (result.contains("%")) {
        static QRegularExpression expression("\\s+");
        QStringList tokens = result.split(expression);
		foreach(QString token, tokens) {
			if (token.contains("%")) {
                token = token.remove("%").remove("(").remove(")").remove("In:");
				bool ok;
				int number = (int)token.toDouble(&ok);
				if (ok && number > progressBar->value()) {
					progressBar->setValue(number);
				}
				return;
			}
		}
	}
	
	statusText->append(result);
}

void ExternalProcessingDialog::process_started()
{
	statusText->clear();
}

void ExternalProcessingDialog::process_finished(int exitcode, QProcess::ExitStatus exitstatus)
{
	Q_UNUSED(exitcode);
	Q_UNUSED(exitstatus);
	
	if (m_queryOptions) {
		m_queryOptions = false;
		return;
	}
	
	if (exitstatus == QProcess::CrashExit) {
		statusText->setHtml(tr("Program <b>%1</b> crashed!").arg(m_program));
		return;
	}
	
	QString dir = pm().get_project()->get_audiosources_dir();
	
	// In case we used the merger, remove the file...
	QFile::remove(dir + "/merged.wav");
	
	
	QString result = m_processor->readAllStandardOutput();
	// print anything on command line we didn't catch
	printf("output: \n %s", QS_C(result));
		
	ReadSource* source = resources_manager()->import_source(dir, m_filename);
	if (!source) {
		printf("ResourcesManager didn't return a ReadSource, most likely sox didn't understand your command\n");
		return rejected();
	}
		
	m_acep->m_resultingclip = resources_manager()->new_audio_clip(m_newClipName);
	resources_manager()->set_source_for_clip(m_acep->m_resultingclip, source);
	// Clips live at project level, we have to set its Sheet, Track and ReadSource explicitely!!
	m_acep->m_resultingclip->set_sheet(m_acep->m_clip->get_sheet());
	m_acep->m_resultingclip->set_track(m_acep->m_clip->get_track());
	m_acep->m_resultingclip->set_location_start(m_acep->m_clip->get_location()->get_start());
	
	close();
}

void ExternalProcessingDialog::query_options()
{
	m_queryOptions = true;
	argsComboBox->clear();
	m_processor->start(m_program, QStringList() << "-h");
}

void ExternalProcessingDialog::arg_combo_index_changed(int /*index*/)
{    
    argumentsLineEdit->setText(argsComboBox->currentText());
}

void ExternalProcessingDialog::command_lineedit_text_changed(const QString & text)
{
	m_program = text.simplified();
	if (m_program == "sox") {
		#if defined (Q_OS_MAC)
			m_program = qApp->applicationDirPath() + "/sox";
		#endif

		query_options();
		argsComboBox->show();
		argsComboBox->setToolTip(tr("Available arguments for the sox program"));
		return;
	}
	
	argsComboBox->hide();
}

void ExternalProcessingDialog::process_error(QProcess::ProcessError error)
{
	if (error == QProcess::FailedToStart) {
		statusText->setHtml(tr("Program <b>%1</b> not installed, or insufficient permissions to run!").arg(m_program));
	}
}

 
