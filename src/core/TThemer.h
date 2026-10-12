/*
    Copyright (C) 2005-2024 Remon Sijrier
 
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
 
    $Id: Themer.h,v 1.5 2009/04/16 18:05:34 n_doebelin Exp $
*/

#ifndef T_THEMER_H
#define T_THEMER_H

#include <QObject>
#include <QColor>
#include <QLinearGradient>
#include <QFont>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QPalette>

class QFileSystemWatcher;

class TThemer : public QObject
{
    Q_OBJECT
public:
    void save();
    void load();

    void set_path_and_theme(const QString& path, const QString& theme);
    void use_builtin_theme(const QString& theme);
    void set_color_adjust_value(int value);
    void validate_loaded_theme();

    QColor get_color(const QString& name) const;
    QColor get_system_palette_color(QPalette::ColorRole colorRole) const;
    void set_new_theme_color(const QString& name, const QColor& color);
    QList<QString> get_colors();
    QFont get_font(const QString& fontname) const;
    QVariant get_property(const QString& propertyname, const QVariant& defaultValue=0) const;
    QStringList get_builtin_themes();
    QBrush get_brush(const QString& name, QPoint start = QPoint(0,0), QPoint stop = QPoint(0,0)) const;
    QLinearGradient get_gradient(const QString& name) const;

    static TThemer* instance();

private:
    TThemer();

    void load_defaults();

    QHash<QString, QColor>	 m_colors;
    QHash<QString, QColor>	 m_defaultColors;
    QHash<QString, QLinearGradient> m_gradients;
    QHash<QString, QVariant> m_properties;
    QHash<QString, QFont>	m_fonts;
    QString			m_themefile;
    int			m_coloradjust;
    QString			m_currentTheme;

    QColor get_default_color(const QString& name);
    QColor set_color_alpha(QColor color, int alpha);

    static TThemer* m_instance;


private slots:
    void reload_on_themefile_change(const QString&);

signals:
    void themeLoaded();
};

// use this function to get the Colormanager object
TThemer* themer();

#endif

