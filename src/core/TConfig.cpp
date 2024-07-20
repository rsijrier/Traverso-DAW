/*
Copyright (C) 2006-2007 Remon Sijrier

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

#include "TConfig.h"
#include "../config.h"
#include "AudioDevice.h"
#include "Utils.h"
#include "TShortCutManager.h"
#include "../commands/plugins/TraversoCommands/TraversoCommands.h"

#include <QSettings>
#include <QString>
#include <QDir>

// Always put me below _all_ includes, this is needed
// in case we run with memory leak detection enabled!
#include "Debugger.h"

static const char* CONFIG_FILE_VERSION = "8";


TConfig& config()
{
        static TConfig conf;
	return conf;
}

TConfig::~ TConfig( )
{
}

void TConfig::load_configuration()
{
	QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Traverso", "Traverso");
	
	QStringList keys = settings.allKeys();
	
	foreach(const QString &key, keys) {
		m_configs.insert(key, settings.value(key));
	}
	
	set_audiodevice_driver_properties();

    TraversoCommands* commands = new TraversoCommands();
    tShortCutManager().register_command_plugin(commands, "TraversoCommands");
	tShortCutManager().loadFunctions();
	tShortCutManager().loadShortcuts();
}

void TConfig::reset_settings( )
{
	QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Traverso", "Traverso");
	
	settings.clear();

        settings.setValue("Project/directory", QDir::homePath());
        settings.setValue("ProgramVersion", VERSION);
	settings.setValue("ConfigFileVersion", CONFIG_FILE_VERSION);
	
	m_configs.clear();
	
	load_configuration();
}

void TConfig::check_and_load_configuration( )
{
        load_configuration();

	// Detect if the config file versions match, if not, there has been most likely 
	// a change, overwrite with the newest version...
	if (m_configs.value("ConfigFileVersion").toString() != CONFIG_FILE_VERSION) {
		reset_settings();
	}
}

void TConfig::save( )
{
	QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Traverso", "Traverso");
	printf("Saving config to %s\n", settings.fileName().toLatin1().data());
	
	QHash<QString, QVariant>::const_iterator i = m_configs.constBegin();
	
	while (i != m_configs.constEnd()) {
		settings.setValue(i.key(), i.value());
		++i;
	}
	
	set_audiodevice_driver_properties();
	
	emit configChanged();
}

QVariant TConfig::get_property( const QString & type, const QString & property, const QVariant& defaultValue )
{
	QVariant var = defaultValue;
	QString key = type + ("/") + property;
	
	if (m_configs.contains(key)) {
		var = m_configs.value(key);
	} else {
		m_configs.insert(key, defaultValue);
	}
	
	return var;
}

void TConfig::set_property( const QString & type, const QString & property, const QVariant& newValue )
{
	m_configs.insert(type + "/" + property, newValue);
    if (type == "Hardware" && property == "numberofperiods") {
        set_audiodevice_driver_properties();
    }
}


void TConfig::set_audiodevice_driver_properties()
{
	QHash<QString, QVariant> hardwareconfigs;
	hardwareconfigs.insert("jackslave", get_property("Hardware", "jackslave", false));
	hardwareconfigs.insert("numberofperiods", get_property("Hardware", "numberofperiods", 3));
	
	audiodevice().set_driver_properties(hardwareconfigs);
}

