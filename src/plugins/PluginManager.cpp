/*Copyright (C) 2006-2007 Remon Sijrier

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


#include "ProjectManager.h"
#include "Project.h"
#include "Sheet.h"
#include "PluginManager.h"
#include "Plugin.h"
#include "CorrelationMeter.h"
#include "SpectralMeter.h"
#include "Utils.h"
#include "Information.h"

#if defined (LV2_SUPPORT)
#include <LV2Plugin.h>
#endif

#include "Debugger.h"

PluginManager* PluginManager::m_instance = 0;


PluginManager::PluginManager()
{
	init();
}


PluginManager::~PluginManager()
{
#if defined (LV2_SUPPORT)
	lilv_world_free(m_lilvWorld);
#endif
}


PluginManager* PluginManager::instance()
{
	if (m_instance == nullptr) {
		m_instance = new PluginManager;
	}

	return m_instance;
}


void PluginManager::init()
{
#if defined (LV2_SUPPORT)
// LV2 part:
	m_lilvWorld = lilv_world_new();
	lilv_world_load_all(m_lilvWorld);
	m_lilvPlugins = lilv_world_get_all_plugins(m_lilvWorld);
#endif
}


Plugin* PluginManager::get_plugin(const  QDomNode& node )
{
	QDomElement e = node.toElement();
	QString type = e.attribute( "type", "");

    Plugin* plugin = 0;
        TSession* session = pm().get_project()->get_current_session();

    if (type == "LV2Plugin") {
#if defined (LV2_SUPPORT)
                plugin = new LV2Plugin(session);
#endif
    }
    // Well, this looks a little ehm, ugly hehe
    // I'll investigate sometime in the future to make
    // a Plugin a _real_ plugin, by using the Qt Plugin
    // framework. (loading it as a shared library object...)
     else if (type == "CorrelationMeterPlugin") {
		plugin = new CorrelationMeter();
    } else if (type == "SpectralMeterPlugin") {
		plugin = new SpectralMeter();
	}
	
	if (plugin) {
		if (plugin->set_state(node) > 0) {
			return plugin;
		} else {
			delete plugin;
            plugin = nullptr;
		}
	} else {
//		PERROR("PluginManager couldn't create Plugin ???? (%s)", QS_C(type));
	}

	return plugin;
}

#if defined (LV2_SUPPORT)

const LilvPlugins* PluginManager::get_lilv_plugins()
{
	return m_lilvPlugins;
}

Plugin* PluginManager::create_lv2_plugin(const QString& uri)
{
        TSession* session = pm().get_project()->get_current_session();
        LV2Plugin* plugin = new LV2Plugin(session, QS_C(uri));
	
	if (plugin->init() < 0) {
		info().warning(QObject::tr("Plugin %1 initialization failed!").arg(uri));
		delete plugin;
        plugin = nullptr;
	}
	
	return plugin;
}
#endif

//eof
