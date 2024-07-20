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

#include "CDWritingDialog.h"

#include <QMessageBox>
#include <samplerate.h>

#include "TExportSpecification.h"
#include "TConfig.h"
#include "Project.h"
#include "ProjectManager.h"
#include "Information.h"
#include "Utils.h"


#if defined (Q_OS_WIN)
#define CDRDAO_BIN	"cdrdao.exe"
#else
#define CDRDAO_BIN	"cdrdao"
#endif

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

CDWritingDialog::CDWritingDialog( QWidget * parent )
	: QDialog(parent)
	, m_burnprocess(0)
	, m_exportSpec(0)
{
        setupUi(this);

	stopButton->hide();
	set_project(pm().get_project());
	
	connect(closeButton, SIGNAL(clicked()), this, SLOT(hide()));
	connect(&pm(), SIGNAL(projectLoaded(Project*)), this, SLOT(set_project(Project*)));

	m_burnprocess = new QProcess(this);
	m_burnprocess->setProcessChannelMode(QProcess::MergedChannels);
	QStringList env = QProcess::systemEnvironment();
	env << "LC_ALL=C";
	m_burnprocess->setEnvironment(env);
	m_writingState = NO_STATE;
	m_lastSheetExported = -1;
	
	refreshButton->setIcon(QIcon(find_pixmap(":/refresh-16")));
	refreshButton->setMaximumHeight(26);
	refreshButton->setMaximumWidth(30);
	
	connect(m_burnprocess, SIGNAL(readyReadStandardOutput()), this, SLOT(read_standard_output()));
	connect(m_burnprocess, SIGNAL(started()), this, SLOT(cdrdao_process_started()));
    connect(m_burnprocess, SIGNAL(finished(int,QProcess::ExitStatus)),
        this, SLOT(cdrdao_process_finished(int,QProcess::ExitStatus)));
	connect(startButton, SIGNAL(clicked()), this, SLOT(start_burn_process()));
	connect(stopButton, SIGNAL(clicked()), this, SLOT(stop_burn_process()));
	connect(refreshButton, SIGNAL(clicked()), this, SLOT(query_devices()));
	connect(cdDiskExportOnlyCheckBox, SIGNAL(stateChanged(int)), this, SLOT(export_only_changed(int)));
        connect(m_project, SIGNAL(exportMessage(QString)), this, SLOT(set_export_message(QString)));

	
	m_wodimAvailable = false;
	
	// A bit lame way to 'detect' if wodim is installed
	if (QProcess::execute("wodim") != QProcess::FailedToStart) {
		m_wodimAvailable = true;
	}
	
	query_devices();
}

CDWritingDialog::~ CDWritingDialog( )
{}


bool CDWritingDialog::is_safe_to_export()
{
	PENTER;
	if (m_project->is_recording()) {
		info().warning(tr("CD Writing during recording is not supported!"));
		return false;
	}
	
	return true;
}


void CDWritingDialog::on_stopButton_clicked( )
{
    m_exportSpec->cancel_export();
}

void CDWritingDialog::set_export_message(QString message)
{
        cdExportInformationLabel->setText(message);
}


void CDWritingDialog::set_project(Project * project)
{
	m_project = project;
	
	if (! m_project) {
		info().information(tr("No project loaded, to write a project to CD, load it first!"));
		setEnabled(false);
	} else {
		setEnabled(true);
        m_exportSpec = m_project->get_export_specification();
	}
}


/****************************************************************/
/*			CD EXPORT 				*/
/****************************************************************/


void CDWritingDialog::query_devices()
{
	PENTER;
	if ( ! (m_burnprocess->state() == QProcess::NotRunning) ) {
		printf("query_devices: burnprocess still running!\n");
		return;
	}
	
	m_writingState = QUERY_DEVICE;
	cdDeviceComboBox->clear();

#if defined (Q_OS_WIN)
	m_burnprocess->start(CDRDAO_BIN, QStringList() << "scanbus");
#elif defined (Q_OS_MAC)
	cdDeviceComboBox->clear();
	cdDeviceComboBox->addItem("IODVDServices");
	cdDeviceComboBox->addItem("IODVDServices/2");
	cdDeviceComboBox->addItem("IOCompactDiscServices");
	cdDeviceComboBox->addItem("IOCompactDiscServices/2");
#else
	// Detect the available devices with wodim if available,
	// since it seems to work better then cdrdao
	if (m_wodimAvailable) {
		m_burnprocess->start("wodim", QStringList() << "--devices");
	} else {
		m_burnprocess->start(CDRDAO_BIN, QStringList() << "drive-info");
	}
#endif
}

void CDWritingDialog::unlock_device()
{
	if ( ! (m_burnprocess->state() == QProcess::NotRunning) ) {
		return;
	}
	
	m_writingState = UNLOCK_DEVICE;
	int index = cdDeviceComboBox->currentIndex();
	if (index == -1) {
		return;
	}
		
	QString device = get_device(index);

	QStringList args;
	args  << "unlock" << "--device" << device;
#if defined (Q_OS_MAC)
	m_burnprocess->start(qApp->applicationDirPath() + "/cdrdao", args);
#else
	m_burnprocess->start(CDRDAO_BIN, args);
#endif
}


void CDWritingDialog::stop_burn_process()
{
	PENTER;
	
	if (m_writingState == RENDER) {
		update_cdburn_status(tr("Aborting Render process ..."), NORMAL_MESSAGE);
        m_exportSpec->cancel_export();
	}
	if (m_writingState == BURNING) {
		update_cdburn_status(tr("Aborting CD Burn process ..."), NORMAL_MESSAGE);
		m_burnprocess->terminate();
		m_writingState = ABORT_BURN;
	}
	
	stopButton->setEnabled(false);
}


void CDWritingDialog::start_burn_process()
{
	PENTER;
	
	if(!is_safe_to_export()) {
		return;
	}
	
	m_copyNumber = 0;
	cd_render();
	
	int index = cdDeviceComboBox->currentIndex();
	if (index != -1 && cdDeviceComboBox->itemData(index) != QVariant::Invalid) {
		config().set_property("Cdrdao", "drive", cdDeviceComboBox->itemData(index));
	}
}


void CDWritingDialog::cdrdao_process_started()
{
	PENTER;
	
	if (m_writingState == BURNING) {
		update_cdburn_status(tr("Waiting for CD-Writer..."), NORMAL_MESSAGE);
		progressBar->setMaximum(0);
	}

}

void CDWritingDialog::cdrdao_process_finished(int exitcode, QProcess::ExitStatus exitstatus)
{
	PENTER;
	
	Q_UNUSED(exitcode);
	
	if (exitstatus == QProcess::CrashExit) {
		update_cdburn_status(tr("CD Burn process failed!"), ERROR_MESSAGE);
	}
	
	if (m_writingState == ABORT_BURN) {
		update_cdburn_status(tr("CD Burn process stopped on user request."), NORMAL_MESSAGE);
	}
	
	if (m_writingState == BURNING) {
		update_cdburn_status(tr("CD Writing process finished!"), NORMAL_MESSAGE);
	}
	
	if (exitstatus == QProcess::CrashExit || m_writingState == ABORT_BURN) {
		unlock_device();
	}
	
	progressBar->setMaximum(100);
	progressBar->setValue(0);

	if (m_writingState == BURNING) {
		// check if we have to write another CD
		bool writeAnotherCd = false;
		if (m_copyNumber < spinBoxNumCopies->value()) {
			if (QMessageBox::information(this, tr("Writing CD %1 of %2").arg(m_copyNumber+1).arg(spinBoxNumCopies->value()), tr("Please insert an empty CD and press OK to continue."), QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Ok) {
				writeAnotherCd = true;
			}
		}
	
		if (writeAnotherCd) {
			write_to_cd();
			return;
		}
	}
	
	enable_ui_interaction();
}

void CDWritingDialog::cd_render()
{
	PENTER;
	
	if ( ! (m_burnprocess->state() == QProcess::NotRunning) ) {
		info().critical(tr("Burn process is still running, cannot start it twice!!"));
		return;
	}
	
	// FIXME: We should instead check export wav file timestamps/revision numbers as a dirty test
	if (! cdAllSheetsButton->isChecked() && m_lastSheetExported != m_project->get_current_sheet_id()) {
        // m_exportSpec->renderfinished = false;
	}
	
    if (m_wasClosed) {
		
		if (QMessageBox::question(this, tr("Rerender CD content"), 
		    		tr("There is already a CD render available.\nShould I re-render the CD content?"), 
				QMessageBox::Yes | QMessageBox::No, 
				QMessageBox::No) == QMessageBox::Yes)
		{
            // m_exportSpec->renderfinished = false;
		}
	}

	m_wasClosed = false;
	
    if (true) {

        m_exportSpec->set_data_format(SF_FORMAT_PCM_16);
        m_exportSpec->set_channel_count(2);
		m_exportSpec->writeToc = true;
        m_exportSpec->set_dither_type(GDitherTri);
        // TODO
        // What about offering user option to select samplerate conversion quality?
        m_exportSpec->set_is_cd_export(true);
		
        if (m_exportSpec->create_cdrdao_toc(m_exportSpec) < 0) {
			info().warning(tr("Creating CDROM table of contents failed, unable to write CD"));
			return;
		}
	
		m_writingState = RENDER;

		connect(m_project, SIGNAL(overallExportProgressChanged(int)), this, SLOT(cd_export_progress(int)));
		connect(m_project, SIGNAL(exportFinished()), this, SLOT(cd_export_finished()));
	
		update_cdburn_status(tr("Rendering Sheet(s)"), NORMAL_MESSAGE);
		
		disable_ui_interaction();
        m_project->export_project();
		m_lastSheetExported = m_project->get_current_sheet_id();
	} else {
		if (cdDiskExportOnlyCheckBox->isChecked()) {
			return;
		}
		disable_ui_interaction();
		write_to_cd();
	}
}

void CDWritingDialog::write_to_cd()
{
	PENTER;

	m_copyNumber++;

	if ( ! (m_burnprocess->state() == QProcess::NotRunning) ) {
		info().critical(tr("Burn process is still running, cannot start it twice!!"));
		return;
	}
	
	m_writingState = BURNING;
	progressBar->setValue(0);
	
	int index = cdDeviceComboBox->currentIndex();
	if (index == -1) {
		QMessageBox::information( 0, tr("No Burn Device"), 
					  tr("No burn Device selected or available!"),
					     QMessageBox::Ok);
		enable_ui_interaction();
		return;
	}
		
	QString device = get_device(index);
	QStringList arguments;
	QString burnprogram;
	
	
	// wodim vs cdrecord vs cdrdao?? a lot of fuzz about these, 
	// but so far cdrdao works for me just fine, so let's continue
	// using it for the actual burning for now.
	
/*	if (m_wodimAvailable) {
		burnprogram = "wodim";
		arguments << "-vv";
		if (simulateCheckBox->isChecked()) {
			arguments << "-dummy";
		}
		arguments << QString("dev=").append(device);
		arguments << "driveropts=burnfree";
		arguments << "-dao";
		arguments << "-eject";
		if (speedComboBox->currentIndex() != 0) {
			arguments << "speed=" << speedComboBox->currentText().remove("x");
		}
	} else {*/
		burnprogram = CDRDAO_BIN;
		arguments << "write" << "--device" << device << "-n" << "--eject" << "--driver" << "generic-mmc";
		if (speedComboBox->currentIndex() != 0) {
			arguments << "--speed" << speedComboBox->currentText().remove("x");
		}
		if (simulateCheckBox->isChecked()) {
			arguments << "--simulate";
		}
// 	}
	
	arguments << m_exportSpec->tocFileName;
	
	printf("%s arguments: %s\n", QS_C(burnprogram), QS_C(arguments.join(" ")));

#if defined (Q_OS_MAC)
	m_burnprocess->start(qApp->applicationDirPath() + "/cdrdao", arguments);
#else
	m_burnprocess->start(burnprogram, arguments);
#endif
}

void CDWritingDialog::cd_export_finished()
{
	PENTER;
	disconnect(m_project, SIGNAL(overallExportProgressChanged(int)), this, SLOT(cd_export_progress(int)));
	disconnect(m_project, SIGNAL(exportFinished()), this, SLOT(cd_export_finished()));
	
    if (m_exportSpec->cancel_export_requested()) {
		update_cdburn_status(tr("Render process stopped on user request."), NORMAL_MESSAGE);
		enable_ui_interaction();
		return;
	}
	
	if (cdDiskExportOnlyCheckBox->isChecked()) {
		update_cdburn_status(tr("Export to disk finished!"), NORMAL_MESSAGE);
		enable_ui_interaction();
		return;
	}
	
	write_to_cd();
}

void CDWritingDialog::cd_export_progress(int progress)
{
	progressBar->setValue(progress);
}

void CDWritingDialog::update_cdburn_status(const QString& message, int type)
{
	if (type == NORMAL_MESSAGE) {
		QPalette palette;
		palette.setColor(QPalette::WindowText, QColor(Qt::black));
		cdExportInformationLabel->setPalette(palette);
		cdExportInformationLabel->setText(message);
	}
	
	if (type == ERROR_MESSAGE) {
		QPalette palette;
		palette.setColor(QPalette::WindowText, QColor(Qt::red));
		cdExportInformationLabel->setPalette(palette);
		cdExportInformationLabel->setText(message);
	}
}

void CDWritingDialog::read_standard_output()
{
	PENTER;
	
	Q_ASSERT(m_burnprocess);
	
	if (m_writingState == QUERY_DEVICE) {
		
		QByteArray output = m_burnprocess->readAllStandardOutput();
		QList<QByteArray> lines = output.split('\n');
		
		foreach(QByteArray data, lines) {
			
			if (data.isEmpty()) {
				continue;
			}
			
			printf("%s\n", data.data());
			
			if (data.contains("trying to open")) {
				update_cdburn_status(tr("Trying to access CD Writer ..."), NORMAL_MESSAGE);
				return;
			}
			
			if (data.contains("Cannot open") || data.contains("Cannot setup")) {
				update_cdburn_status(tr("Cannot access CD Writer, is it in use ?"), ERROR_MESSAGE);
				return;
			}
#if defined (Q_OS_WIN)
			if (QString(data).contains(QRegExp("[0-9],[0-9],[0-9]"))) {
#else
			if (data.contains("/dev/") || data.contains("dev=")) {
#endif
                QStringList strlist = QString(data).split(QRegularExpression("\\s+"));
				QString deviceName = "No Device Available";
				QString device = "/no/device/detected";
				
				if (m_wodimAvailable) {
					if (strlist.size() > 5) {
						deviceName = strlist.at(5) + " ";
						deviceName = deviceName.remove("'");
					}
					if (strlist.size() > 7) {
						deviceName += strlist.at(7) + "  ";
						deviceName = deviceName.remove("'");
					}
					if (strlist.size() > 2) {
						device = strlist.at(2);
						device = device.remove("dev=").remove("'");
						deviceName += "(" + device + ")";
					}
				} else {
					if (strlist.size() > 1) {
						deviceName = strlist.at(1) + " ";
					}
					if (strlist.size() > 3) {
						deviceName += strlist.at(3) + "  ";
					}
					if (strlist.size() > 0) {
						device = strlist.at(0);
						device = device.remove(":");
						deviceName += "(" + device + ")";
					}
				}
				cdDeviceComboBox->addItem(deviceName, device);
			}
		}
		
		QString cdrdaoDrive = config().get_property("Cdrdao", "drive", "").toString();
		int index = cdDeviceComboBox->findData(cdrdaoDrive);
		if (index >= 0) {
			cdDeviceComboBox->setCurrentIndex(index);
		}
		
		update_cdburn_status(tr("Information"), NORMAL_MESSAGE);
		
		return;
	}
	
	
	QString sout = m_burnprocess->readAllStandardOutput();
	
	if (sout.simplified().isEmpty()) {
		return;
	}
	
	if (sout.contains("Disk seems to be written")) {
		int index = cdDeviceComboBox->currentIndex();
		if (index != -1) {
#if defined (Q_OS_WIN)
			// No idea if this works.....
			QProcess::execute("rsm.exe", QStringList() << "eject" << "/n0");
#else
			QProcess::execute("eject", QStringList() << cdDeviceComboBox->itemData(index).toString());
#endif
		}
		QMessageBox::information( 0, tr("Disc not empty"), 
					  tr("Please, insert an empty disc and hit enter"),
					     QMessageBox::Ok);
		m_burnprocess->write("enter");
		return;
	}
	
	if (sout.contains("Inserted disk is not empty and not appendable.")) {
		QMessageBox::information( 0, tr("Disc not empty"),
					  tr("Inserted disk is not empty, and cannot append data to it!"),
					     QMessageBox::Ok);
		return;
	}
		
	
	if (sout.contains("Unit not ready")) {
		update_cdburn_status(tr("Waiting for CD Writer... (no disk inserted?)"), NORMAL_MESSAGE);
		progressBar->setMaximum(0);
		return;
	}
		
		
	if (sout.contains("Turning BURN-Proof on")) {
		update_cdburn_status(tr("Turning BURN-Proof on"), NORMAL_MESSAGE);
		return;
	}
	
	if (sout.contains("Writing track")) {
        QStringList strlist = sout.split(QRegularExpression("\\s+"));
		if (strlist.size() > 3) {
			QString text = strlist.at(0) + " " + strlist.at(1) + " " + strlist.at(2);
			update_cdburn_status(text, NORMAL_MESSAGE);
		}
		return;
	}	
	
	if (sout.contains("%") && sout.contains("(") && sout.contains(")")) {
        QStringList strlist = sout.split(QRegularExpression("\\s+"));
		if (strlist.size() > 7) {
			int written = strlist.at(1).toInt();
			int total = strlist.at(3).toInt();
			if (total == 0) {
				progressBar->setValue(0);
			} else {
				if (progressBar->maximum() == 0) {
					progressBar->setMaximum(100);
				}
				int progress = (100 * written) / total;
				progressBar->setValue(progress);
			}
		}
		return;
	}
	
	// Write out only the unhandled cdrdao lines
	printf("CD Writing: %s\n", QS_C(sout.trimmed()));
}

void CDWritingDialog::closeEvent(QCloseEvent * event)
{
	if (m_writingState != NO_STATE) {
		event->setAccepted(false);
		return;
	}
	QDialog::closeEvent(event);
}

void CDWritingDialog::reject()
{
	if (m_writingState == NO_STATE) {
		hide();
	}
}

void CDWritingDialog::export_only_changed(int state)
{
	if (state == Qt::Checked) {
		burnGroupBox->setEnabled(false);
	} else {
		burnGroupBox->setEnabled(true);
	}
}

void CDWritingDialog::disable_ui_interaction()
{
	closeButton->setEnabled(false);
	burnGroupBox->setEnabled(false);
	startButton->hide();
	stopButton->show();
}

void CDWritingDialog::enable_ui_interaction()
{
	m_writingState = NO_STATE;
	burnGroupBox->setDisabled(cdDiskExportOnlyCheckBox->isChecked());
	closeButton->setEnabled(true);
	startButton->show();
	stopButton->hide();
	stopButton->setEnabled(true);
	progressBar->setValue(0);
}

void CDWritingDialog::set_was_closed()
{
	m_wasClosed = true;
}

QString CDWritingDialog::get_device(int index)
{
#if defined (Q_OS_MAC)
	return cdDeviceComboBox->currentText();
#else
	return cdDeviceComboBox->itemData(index).toString();
#endif
}

//// TODO: uh, what does sheet mode have to do with cd writing ?
/// modes are no longer there so find out what this was doing I guess ?
//void CDWritingDialog::sheet_mode_changed(bool b)
//{
//        TTimeRef t = TTimeRef();
//        m_exportSpec->allSheets = !b;
//        t = m_project->get_cd_totaltime(m_exportSpec);
//        cdTotalTimeLabel->setText(TTimeRef::timeref_to_cd(t));
//}

