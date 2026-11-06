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

#include "TProjectConverter.h"
#include "Debugger.h"

#include <QTextStream>
#include <QFile>

#include "FileHelpers.h"
#include "TAudioFileMerger.h"
#include "TBufferedAudioStreamReader.h"
#include "TTypes.h"


TProjectConverter::TProjectConverter() 
{
	m_projectfileversion = -1;
	m_merger = 0;
	connect(this, &TProjectConverter::conversionFinished, this, &TProjectConverter::conversion_finished);
}

int TProjectConverter::start()
{

	switch(m_projectfileversion) {
		case 2: {
			start_conversion_from_version_2_to_3();
			break;
		}
		default: {
			emit message(tr("Project file with version %1 cannot be converted, only files with version 2 can!").arg(m_projectfileversion));
			return -1;
		}
	}
	
	return 1;
}

void TProjectConverter::set_project(const QString & rootdir, const QString & name)
{
	m_readsources.clear();
	m_filesToMerge = 0;
	m_filesMerged = 0;
	if (m_merger) {
		delete m_merger;
        m_merger = nullptr;
	}
	m_document.clear();
	
	
	m_projectfileversion = -1;
	
	m_rootdir = rootdir;
	m_projectname = name;

	QDomDocument doc("Project");
	
	QString filename(m_rootdir + "/project.tpf");
	QFile file(filename);
		
	if (!file.open(QIODevice::ReadOnly)) {
		TLog::write_printf(TLogLevel::Warning, TLOG_FILE_CATEGORY, "filename '%s' could not be opened!", qUtf8Printable(filename));
		return;
	}
		
	// Start setting and parsing the content of the xml file
	QString errorMsg;
    QDomDocument::ParseResult result = doc.setContent(file.readAll());
    if (!result) {
		QString error = tr("Project %1: Failed to parse project.tpf file! (Reason: %2)").arg(m_projectname).arg(result.errorMessage);
		TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "%s", qUtf8Printable(error));
		return;
	}
	
	QDomElement docElem = doc.documentElement();
	QDomNode propertiesNode = docElem.firstChildElement("Properties");
	QDomElement projectelement = propertiesNode.toElement();
	
	m_projectfileversion = projectelement.attribute("projectfileversion", "-1").toInt();
	
	m_document = doc.cloneNode(true).toDocument();
}

int TProjectConverter::start_conversion_from_version_2_to_3()
{
	emit message(tr("Starting to convert Project from version 2 to version 3"));
	
	m_merger = new TAudioFileMerger;
	m_filesToMerge = m_filesMerged = 0;
	connect(m_merger, &TAudioFileMerger::progress, this, &TProjectConverter::progress);
	connect(m_merger, &TAudioFileMerger::taskStarted, this, &TProjectConverter::file_merge_started);
	connect(m_merger, &TAudioFileMerger::taskFinished, this, &TProjectConverter::file_merge_finished);
	connect(m_merger, &TAudioFileMerger::processingStopped, this, &TProjectConverter::processing_stopped);
	
	QDomElement docElem = m_document.documentElement();
	QDomNode propertiesNode = docElem.firstChildElement("Properties");
	QDomElement projectelement = propertiesNode.toElement();
	
	QString version = projectelement.attribute("projectfileversion", "-1");
	int projectrate = projectelement.attribute("rate", "44100").toInt();
	version = QString::number(3);
	projectelement.setAttribute("projectfileversion", version);

		
	// Load all the AudioSources for this project
	QDomNode asmNode = docElem.firstChildElement("ResourcesManager");
	QDomNode sourcesNode = asmNode.firstChildElement("AudioSources").firstChild();
	
	while(!sourcesNode.isNull()) {
		QDomElement readsourceelement = sourcesNode.toElement();
		int channelcount = readsourceelement.attribute("channelcount", "0").toInt();
		int filecount = readsourceelement.attribute("filecount", "0").toInt();
		qint64 id = readsourceelement.attribute("id", "").toLongLong();
		QString dir = readsourceelement.attribute("dir", "" );
		QString name = readsourceelement.attribute("name", "No name supplied?" );
		bool wasrecording = readsourceelement.attribute("wasrecording", 0).toInt();

		if (filecount == 2 && channelcount == 2 && dir == (m_rootdir + "/audiosources/")) {
			TBufferedAudioStreamReader* readsource0 = new TBufferedAudioStreamReader(dir, name + "-ch0.wav");
			TBufferedAudioStreamReader* readsource1 = new TBufferedAudioStreamReader(dir, name + "-ch1.wav");
			readsource0->ref();
			readsource0->init();
			readsource1->ref();
			readsource1->init();
			
            m_readsources.insert(id, readsource0);
            m_readsources.insert(id, readsource1);
			
			m_filesToMerge++;
			m_merger->enqueue_task(readsource0, readsource1, dir, name);
			
			readsourceelement.setAttribute("name", name + ".wav");
			readsourceelement.setAttribute("length", readsource0->get_length().universal_frame());
			readsourceelement.setAttribute("rate", readsource0->get_sample_rate());
			
		} else {
			// The version 2 of the project file didn't have the full name
			// including the channel number, it was added by ReadSource so 
			// we have to do it now since newer version of ReadSource no longer add
			// the channel number + extension to the readsource name.
			if (!name.contains(".wav") && wasrecording && channelcount == 1) {
				readsourceelement.setAttribute("name", name + "-ch0.wav");
			}
		}
			
			
		sourcesNode = sourcesNode.nextSibling();
	}
		
		
	QDomNode clipsNode = asmNode.firstChildElement("AudioClips").firstChild();
	
	while(!clipsNode.isNull()) {
		QDomElement clipelement = clipsNode.toElement();
		qint64 readsourceid = clipelement.attribute("source", "").toLongLong();
		nframes_t length = clipelement.attribute( "length", "0" ).toUInt();
		nframes_t sourceStartFrame = clipelement.attribute( "sourcestart", "" ).toUInt();
		nframes_t trackStart = clipelement.attribute( "trackstart", "" ).toUInt();
		TBufferedAudioStreamReader* source = m_readsources.value(readsourceid);
		int rate = projectrate;
		if (source) {
			rate = source->get_sample_rate();
		}
			
		clipelement.setAttribute("trackstart", TTimeRef(trackStart, rate).universal_frame());
		clipelement.setAttribute("sourcestart", TTimeRef(sourceStartFrame, rate).universal_frame());
		clipelement.setAttribute("length", TTimeRef(length, rate).universal_frame());
		
		QDomElement fadeInNode = clipsNode.firstChildElement("FadeIn");
		if (!fadeInNode.isNull()) {
			QDomElement e = fadeInNode.toElement();
			QString rangestring = e.attribute("range", "1");
			double range;
			if (rangestring == "nan" || rangestring == "inf") {
				TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "FadeCurve::set_state: stored range was not a number!");
				range = 1;
			} else {
				range = rangestring.toDouble();
			}
			if (range > 1.0) {
				e.setAttribute("range", TTimeRef(nframes_t(range), rate).universal_frame());
			}
		}
		QDomElement fadeOutNode = clipsNode.firstChildElement("FadeOut");
		if (!fadeOutNode.isNull()) {
			QDomElement e = fadeOutNode.toElement();
			QString rangestring = e.attribute("range", "1");
			double range;
			if (rangestring == "nan" || rangestring == "inf") {
				TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "FadeCurve::set_state: stored range was not a number!");
				range = 1;
			} else {
				range = rangestring.toDouble();
			}
			if (range > 1.0)
				e.setAttribute("range", TTimeRef(nframes_t(range), rate).universal_frame());
		}
			
		QDomNode pluginChainNode = clipsNode.firstChildElement("PluginChain");
		if (!pluginChainNode.isNull()) {
			QDomNode pluginsNode = pluginChainNode.firstChild();
			QDomNode pluginNode = pluginsNode.firstChild();

			while(!pluginNode.isNull()) {
				if (pluginNode.toElement().attribute( "type", "") == "GainEnvelope") {
					QDomElement controlPortsNode = pluginNode.firstChildElement("ControlPorts");
					if (!controlPortsNode.isNull()) {
						QDomNode portNode = controlPortsNode.firstChild();
		
						if(!portNode.isNull()) {
							QDomElement curveNode = portNode.firstChildElement("PortAutomation");
							if (!curveNode.isNull()) {
								QDomElement e = curveNode.toElement();
								QStringList nodesList = e.attribute( "nodes", "" ).split(";");
								QStringList newNodesList;
								for (int i=0; i<nodesList.size(); ++i) {
									QStringList whenValueList = nodesList.at(i).split(",");
									double when = whenValueList.at(0).toDouble();
									double value = whenValueList.at(1).toDouble();
									newNodesList << QString::number(TTimeRef(nframes_t(when), rate).universal_frame(), 'g', 24).append(",").append(QString::number(value));
									e.setAttribute("nodes",  newNodesList.join(";"));
								}
							}
						}
					}
				}
				pluginNode = pluginNode.nextSibling();
			}
		}
		
		clipsNode = clipsNode.nextSibling();
	}
		
	QDomNode sheetsNode = docElem.firstChildElement("Sheets");
	QDomNode sheetNode = sheetsNode.firstChild();

		// Load all the Sheets
	while(!sheetNode.isNull())
	{
		QDomNode tracksNode = sheetNode.firstChildElement("Tracks");
		QDomNode trackNode = tracksNode.firstChild();

		while(!trackNode.isNull()) {
			QDomElement trackelement = trackNode.toElement();
                        trackelement.setAttribute("OutputBus", tr("Sheet Master"));
			trackNode = trackNode.nextSibling();
		}
		sheetNode = sheetNode.nextSibling();
	}
	
	emit message(tr("Converting project.tpf file..... Done!"));
	
	if (!m_filesToMerge) {
		finish_2_3_conversion();
	} else {
		emit message(tr("<b>Need to convert %1 files</b>").arg(m_filesToMerge));
	}
	
	return 1;
}

int TProjectConverter::save_converted_document()
{
	QString filename(m_rootdir + "/project.tpf");
	
	QFile backup(filename);
	backup.copy(m_rootdir + "/projectv" + QString::number(m_projectfileversion) + "backup.tpf");
	
	QString errorstring;
	if (!TFileHelper::save_document(m_document, filename, &errorstring)) {
		TLog::write_printf(TLogLevel::Warning, TLOG_FILE_CATEGORY, "%s", qUtf8Printable(tr("Couldn't write the Project properties file! (File %1. Reason: %2)").arg(filename).arg(errorstring)));
		return -1;
	}
	TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "%s", qUtf8Printable(tr("Project %1 converted").arg(m_projectname)));
	emit message(tr("Saving converted project.tpf file.... Done!"));
	
	return 1;
}

void TProjectConverter::conversion_finished()
{
	emit message(tr("Conversion finished succesfully"));
}

QString TProjectConverter::get_conversion_description()
{
	switch(m_projectfileversion) {
		case 2 : {
			QFile file(":/project_conversion_description_2_3");
			if (!file.open(QIODevice::ReadOnly)) {
				qWarning("TProjectConverter::Could not open project_conversion_description_2_3");
				return QByteArray();
			}
			return file.readAll();
		}
	}
	return tr("No conversion description available!");
}

void TProjectConverter::file_merge_started(QString file)
{
	emit fileMergeStarted(file + "   (" + QString::number(m_filesMerged + 1) + "/" + QString::number(m_filesToMerge) + ")");
}

void TProjectConverter::file_merge_finished(QString file)
{
	emit fileMergeFinished(file);
	
	m_filesMerged++;
	
	if ((m_filesToMerge - m_filesMerged) == 0) {
		finish_2_3_conversion();
	}
}

void TProjectConverter::finish_2_3_conversion()
{
	foreach(TBufferedAudioStreamReader* source, m_readsources) {
		delete source;
	}
	
	// Exit the FileMerger event loop
	m_merger->exit(0);	
	
	if ( ! m_merger->wait(1000) ) {
		qWarning("FileMerger:: Still running after 1 second wait, terminating!");
		m_merger->terminate();
	}
	
	delete m_merger;
	
	save_converted_document();
	
	emit conversionFinished();
}

void TProjectConverter::stop_conversion()
{
	if (m_merger && m_merger->isRunning()) {
		m_merger->stop_merging();
	}
}

void TProjectConverter::processing_stopped()
{
	emit message(tr("Conversion stopped on user request, you can continue to use this Project with Traverso <= 0.41.0, or reopen it with this version of Traverso and start the conversion again"));
}

