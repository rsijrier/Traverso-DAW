/*
Copyright (C) 2005-2006 Remon Sijrier 

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

$Id: TThemer.cpp,v 1.14 2009/04/16 19:38:18 n_doebelin Exp $
*/

#include "TThemer.h"

#include <Utils.h>
#include "TConfig.h"
#include <QDir>
#include <QTextStream>
#include <QDomDocument>
#include <QApplication>
#include <QStyle>
#include <QFileSystemWatcher>
#include <QDebug>
#include <qstylehints.h>
#include <QPixmapCache>

#include "Debugger.h"

TThemer* TThemer::m_instance = nullptr;

TThemer* themer()
{
    return TThemer::instance();
}

TThemer* TThemer::instance()
{
    if (!m_instance) {
        m_instance = new TThemer();
    }

    return m_instance;
}

TThemer::TThemer()
{
    QStringList iconSearchPaths = QIcon::themeSearchPaths();
    if (!iconSearchPaths.contains(QStringLiteral(":/ui"))) {
        iconSearchPaths.append(QStringLiteral(":/ui"));
        QIcon::setThemeSearchPaths(iconSearchPaths);
    }

    if (QIcon::themeName().isEmpty()) {
        QIcon::setThemeName(QStringLiteral("icons"));
    }

    QString themepath = config().get_property("Themer", "themepath", "").toString();
    if (themepath.isEmpty() || themepath == ":/themes") {
        themepath = ":/ui/palettes";
        config().set_property("Themer", "themepath", themepath);
    }

    load_defaults();

    connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &TThemer::load);
}

void TThemer::load()
{
    // If the OS theme has changed this function is also called
    // pre-rendered pixmaps need to be re-rendered so clear the
    // pixmap cache
    QPixmapCache::clear();

    // QDomDocument doc("TraversoTheming");

    // // Force absolute resource evaluation to fix double extension errors
    // if (!m_themefile.startsWith(":/ui/palettes/")) {
    //     m_themefile = ":/ui/palettes/" + m_currentTheme + ".xml";
    // }

    // QFile file(m_themefile);

    // if (!file.exists()) {
    //     printf("Themer:: Warning: Theme resource %s not found. Resetting to traversolight.xml\n", QS_C(m_themefile));
    //     m_currentTheme = "traversolight";
    //     m_themefile = ":/ui/palettes/traversolight.xml";
    //     config().set_property("Themer", "currenttheme", m_currentTheme);
    //     file.setFileName(m_themefile);
    // }

    // printf("Themer:: Successfully targeted unified palette resource: %s\n", QS_C(m_themefile));

    // if (!file.open(QIODevice::ReadOnly)) {
    //     printf("Themer:: Critical Error: Cannot open resource file %s\n", QS_C(m_themefile));
    //     return;
    // }

    // QDomDocument::ParseResult result = doc.setContent(file.readAll());
    // if (!result) {
    //     file.close();
    //     printf("Themer:: Critical Error: Cannot set XML content (%s)\n", QS_C(result.errorMessage));
    //     return;
    // }
    // file.close();

    // QDomElement docElem = doc.documentElement();
    // QDomNode colorsNode = docElem.firstChildElement("colors");
    // QDomNode colorNode = colorsNode.firstChild();

    // int coloradjust = config().get_property("Themer", "coloradjust", 100).toInt();
    // if (m_coloradjust != -1) {
    //     coloradjust = m_coloradjust;
    // }

    // while (!colorNode.isNull()) {
    //     QDomElement e = colorNode.toElement();
    //     QColor color(
    //         e.attribute("red").toInt(),
    //         e.attribute("green").toInt(),
    //         e.attribute("blue").toInt(),
    //         e.attribute("alpha").toInt()
    //         );
    //     QString name = e.attribute("name", "");

    //     if (coloradjust != 100) {
    //         int adjust = coloradjust - 100;
    //         if (adjust < 0) {
    //             color = color.darker(-1 * adjust + 100);
    //         } else {
    //             color = color.lighter(adjust + 100);
    //         }
    //     }
    //     m_colors.insert(name, color);
    //     colorNode = colorNode.nextSibling();
    // }

    // QDomNode gradientsNode = docElem.firstChildElement("gradients");
    // QDomNode gradientNode = gradientsNode.firstChild();

    // while (!gradientNode.isNull()) {
    //     QLinearGradient gradient;
    //     QDomElement e = gradientNode.toElement();
    //     QString name = e.attribute("name", "");
    //     QDomNode gradientStopNode = gradientNode.firstChild();

    //     while (!gradientStopNode.isNull()) {
    //         QDomElement ee = gradientStopNode.toElement();
    //         QColor color(
    //             ee.attribute("red").toInt(),
    //             ee.attribute("green").toInt(),
    //             ee.attribute("blue").toInt(),
    //             ee.attribute("alpha").toInt()
    //             );

    //         if (coloradjust != 100) {
    //             int adjust = coloradjust - 100;
    //             if (adjust < 0) {
    //                 color = color.darker(-1 * adjust + 100);
    //             } else {
    //                 color = color.lighter(adjust + 100);
    //             }
    //         }

    //         qreal value = ee.attribute("value", "0.0").toDouble();
    //         gradient.setColorAt(value, color);
    //         gradientStopNode = gradientStopNode.nextSibling();
    //     }
    //     m_gradients.insert(name, gradient);
    //     gradientNode = gradientNode.nextSibling();
    // }

    // QDomNode fontsNode = docElem.firstChildElement("fonts");
    // QDomNode fontNode = fontsNode.firstChild();
    // QFont basefont = QApplication::font();

    // while (!fontNode.isNull()) {
    //     QDomElement e = fontNode.toElement();
    //     QString name = e.attribute("name", "");
    //     QFont font(basefont);
    //     font.setPointSizeF(e.attribute("value", "1.0").toDouble() * basefont.pointSizeF());
    //     m_fonts.insert(name, font);
    //     fontNode = fontNode.nextSibling();
    // }

    // QDomNode propertiesNode = docElem.firstChildElement("properties");
    // QDomNode propertyNode = propertiesNode.firstChild();

    // while (!propertyNode.isNull()) {
    //     QDomElement e = propertyNode.toElement();
    //     QString name = e.attribute("name", "");
    //     QVariant value = e.attribute("value", "");
    //     m_properties.insert(name, value);
    //     propertyNode = propertyNode.nextSibling();
    // }

    // validate_loaded_theme();

    load_defaults();

    emit themeLoaded();
}

void TThemer::save( )
{
    QDomDocument doc("TraversoTheming");
    QString fileName = QDir::homePath();
    fileName +=  "/.traverso/themes/editedtheme.xml";
    QFile data( fileName );

    if (!data.open( QIODevice::WriteOnly ) ) {
        PWARN(QString("Could not open Themer properties file for writing! (%1)").arg(fileName).toLatin1().data());
        return;
    }

    QDomElement themerNode = doc.createElement("Themer");
    doc.appendChild(themerNode);

    QDomElement properties = doc.createElement("properties");
    QHash<QString, QVariant>::Iterator propertiesIt = m_properties.begin();
    while (propertiesIt != m_properties.end()) {
        QDomElement e = doc.createElement("property");
        e.setAttribute("name", propertiesIt.key());
        e.setAttribute("value", propertiesIt.value().toString());
        properties.appendChild(e);
        ++propertiesIt;
    }

    themerNode.appendChild(properties);

    QFont basefont = QApplication::font();

    QDomElement fonts = doc.createElement("fonts");
    QHash<QString, QFont>::Iterator fontsIt = m_fonts.begin();
    while (fontsIt != m_fonts.end()) {
        QDomElement e = doc.createElement("font");
        e.setAttribute("name", fontsIt.key());
        QFont font = fontsIt.value();
        e.setAttribute("value", font.pointSizeF() / basefont.pointSizeF());
        fonts.appendChild(e);
        ++fontsIt;
    }

    themerNode.appendChild(fonts);


    QDomElement colors = doc.createElement("colors");
    themerNode.appendChild(colors);

    QHash<QString, QColor>::ConstIterator it = m_colors.begin();
    while (it != m_colors.end()) {
        QColor color = it.value();
        QDomElement colorProperty = doc.createElement("color");
        colorProperty.setAttribute("red", color.red());
        colorProperty.setAttribute("green", color.green());
        colorProperty.setAttribute("blue", color.blue());
        colorProperty.setAttribute("alpha", color.alpha() );
        colorProperty.setAttribute("name", it.key() );
        ++it;
        colors.appendChild(colorProperty);
    }


    QDomElement gradients = doc.createElement("gradients");
    QHash<QString, QLinearGradient>::ConstIterator gradientsIt = m_gradients.begin();
    while(gradientsIt != m_gradients.end()) {
        QDomElement e = doc.createElement("gradient");
        e.setAttribute("name", gradientsIt.key());
        QLinearGradient gradient = gradientsIt.value();
        foreach(QGradientStop gradientstop, gradient.stops()) {
            QDomElement stopNode = doc.createElement("stop");
            stopNode.setAttribute("value", gradientstop.first);
            QColor color = gradientstop.second;
            stopNode.setAttribute("red", color.red());
            stopNode.setAttribute("green", color.green());
            stopNode.setAttribute("blue", color.blue());
            stopNode.setAttribute("alpha", color.alpha() );
            e.appendChild(stopNode);
        }
        gradients.appendChild(e);
        ++gradientsIt;
    }

    themerNode.appendChild(gradients);

    QTextStream stream(&data);
    doc.save(stream, 4);
    data.close();
}

QColor TThemer::get_color(const QString& name) const
{
    if (m_colors.contains(name)) {
        return m_colors.value(name);
    } else {
        return themer()->get_default_color(name);
    }
}

QColor TThemer::get_system_palette_color(QPalette::ColorRole colorRole) const
{
    QPalette p = QApplication::style()->standardPalette();

    return p.color(colorRole);
}

// Returns the brush with the name "name". QPoints "start" and "end" are the
// start and finalStop positions of linear gradients. If a solid colour is 
// returned, both points are ignored.
// If a gradient and a colour with the same name exists, colour brush will be
// returned.
QBrush TThemer::get_brush(const QString& name, QPoint start, QPoint stop) const
{

    // check if there is a gradient with that name
    if (m_gradients.contains(name))
    {
        QLinearGradient gradient = m_gradients.value(name);

        // use a solid colour if the gradient only contains one stop point.
        // saves a huge amount of resources in the painting routines.
        if (gradient.stops().size() == 1) {
            return QBrush(gradient.stops().at(0).second);
        }

        gradient.setStart(start);
        gradient.setFinalStop(stop);
        gradient.setSpread(QGradient::ReflectSpread);

        return QBrush(gradient);
    }

    // if there is no gradient, check if there is a solid colour with that name
    if (m_colors.contains(name))
    {
        return QBrush(m_colors.value(name));
    }

    // not a colour either? return a fallback colour.
    printf("Brush %s was requested, but no such element was found in the theme file\n", QS_C(name));
    return QBrush(themer()->get_default_color(name));
}

// sometimes we need access to the gradient (e.g. if the start and finalStops have to be
// modified
QLinearGradient TThemer::get_gradient(const QString& name) const
{
    if (m_gradients.contains(name))
    {
        return m_gradients.value(name);
    } else {
        printf("Gradient %s was requested, but no such element was found in the theme file\n", QS_C(name));
        return QLinearGradient();
    }
}

QFont TThemer::get_font(const QString& fontname) const
{
    return m_fonts.value(fontname);
}

QVariant TThemer::get_property(const QString& propertyname, const QVariant& defaultValue) const
{
    return m_properties.value(propertyname, defaultValue);
}

void TThemer::reload_on_themefile_change(const QString&)
{
    m_colors.clear();
    m_fonts.clear();
    m_properties.clear();
    load();
}

void TThemer::set_path_and_theme(const QString& path, const QString& theme)
{
    if (m_currentTheme == theme) {
        return;
    }
    m_currentTheme = theme;
    m_themefile = path + "/" + theme;
    reload_on_themefile_change("");
}

void TThemer::set_color_adjust_value(int value)
{
    m_coloradjust = value;
    reload_on_themefile_change("");
}

QStringList TThemer::get_builtin_themes()
{
    QStringList list;
    QDir themesdir(":/themes");
    foreach (const QString &fileName, themesdir.entryList(QDir::Files)) {
        list << fileName;
    }
    return list;
}

void TThemer::use_builtin_theme(const QString & theme)
{
    if (m_currentTheme == theme) {
        return;
    }

    m_currentTheme = theme;
    m_themefile = QString(":/themes/") + theme;
    reload_on_themefile_change("");
}

QColor TThemer::get_default_color(const QString & name)
{
    if (!m_defaultColors.contains(name)) {
        printf("Default color %s was requested, but no such default color does exist, please add it to the themer!\n", QS_C(name));
        return QColor(Qt::blue);
    }

    return m_defaultColors.value(name);
}

QColor TThemer::set_color_alpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

void TThemer::load_defaults()
{
    QPalette p = QApplication::style()->standardPalette();
    QColor c = Qt::black;
    QString name;

    m_defaultColors.insert("Toolbar:icon-primary",   QColor(197, 144, 0));   // #C59000 (Gold)
    m_defaultColors.insert("Toolbar:icon-secondary", QColor(255, 51, 0));    // #FF3300 (Traverso red)
    m_defaultColors.insert("Toolbar:button-active",  QColor(27, 117, 188));  // #1B75BC (Light blue)

    m_defaultColors.insert("Text:light", p.color(QPalette::BrightText));
    m_defaultColors.insert("Text:dark", p.color(QPalette::Text));

    m_defaultColors.insert("Text:light", p.color(QPalette::BrightText));
    m_defaultColors.insert("Text:dark", p.color(QPalette::Text));
    m_defaultColors.insert("AudioClip:wavemacroview:outline", p.color(QPalette::WindowText));
    m_defaultColors.insert("AudioClip:wavemacroview:outline:curvemode", p.color(QPalette::WindowText));
    m_defaultColors.insert("AudioClip:wavemacroview:outline:muted", p.color(QPalette::WindowText));
    m_defaultColors.insert("AudioClip:wavemacroview:brush", p.color(QPalette::Highlight));
    m_defaultColors.insert("AudioClip:wavemacroview:brush:hover", p.color(QPalette::Highlight));
    m_defaultColors.insert("AudioClip:wavemacroview:brush:muted", p.color(QPalette::Base));
    m_defaultColors.insert("AudioClip:wavemacroview:brush:curvemode", p.color(QPalette::Highlight));
    m_defaultColors.insert("AudioClip:wavemacroview:brush:curvemode:hover", p.color(QPalette::Highlight));
    m_defaultColors.insert("AudioClip:wavemicroview", p.color(QPalette::Highlight));
    m_defaultColors.insert("AudioClip:wavemicroview:curvemode", p.color(QPalette::Highlight));
    m_defaultColors.insert("AudioClip:background:muted", p.color(QPalette::Base));
    m_defaultColors.insert("AudioClip:background:recording", p.color(QPalette::Base));
    m_defaultColors.insert("AudioClip:background:muted:mousehover", p.color(QPalette::Base));
    m_defaultColors.insert("AudioClip:background:selected", set_color_alpha(p.color(QPalette::Highlight), 100));
    m_defaultColors.insert("AudioClip:background:selected:mousehover", set_color_alpha(p.color(QPalette::Highlight), 150));
    m_defaultColors.insert("AudioClip:background", set_color_alpha(p.color(QPalette::Base), 100));
    m_defaultColors.insert("AudioClip:background:mousehover", p.color(QPalette::Button));
    m_defaultColors.insert("AudioClip:channelseperator", p.color(QPalette::WindowText));
    m_defaultColors.insert("AudioClip:channelseperator:selected", p.color(QPalette::WindowText));
    m_defaultColors.insert("AudioClip:contour", p.color(QPalette::WindowText));
    m_defaultColors.insert("AudioClip:clipinfobackground", p.color(QPalette::AlternateBase));
    m_defaultColors.insert("AudioClip:clipinfobackground:inactive", p.color(QPalette::AlternateBase));
    m_defaultColors.insert("AudioClip:sampleoverload", QColor(Qt::red));
    m_defaultColors.insert("AudioClip:invalidreadsource", QColor(Qt::red));
    m_defaultColors.insert("AudioClip:text", p.color(QPalette::WindowText));
    m_defaultColors.insert("Curve:active", p.color(QPalette::BrightText));
    m_defaultColors.insert("CurveNode:default", p.color(QPalette::BrightText));
    m_defaultColors.insert("CurveNode:blink", p.color(QPalette::BrightText));
    c = p.color(QPalette::Highlight);
    c.setAlpha(150);
    m_defaultColors.insert("Fade:default", c);
    c = p.color(QPalette::Highlight);
    c.setAlpha(50);
    m_defaultColors.insert("Fade:bypassed", c);
    m_defaultColors.insert("CorrelationMeter:margin", p.color(QPalette::Window));
    m_defaultColors.insert("CorrelationMeter:background", p.color(QPalette::Base));
    m_defaultColors.insert("CorrelationMeter:grid", p.color(QPalette::Dark));
    m_defaultColors.insert("CorrelationMeter:foreground:center", p.color(QPalette::Link));
    m_defaultColors.insert("CorrelationMeter:foreground:side", p.color(QPalette::LinkVisited));
    m_defaultColors.insert("CorrelationMeter:centerline", p.color(QPalette::Highlight));
    m_defaultColors.insert("CorrelationMeter:text", p.color(QPalette::WindowText));
    m_defaultColors.insert("CorrelationMeter:foreground", p.color(QPalette::Text));
    m_defaultColors.insert("GainSlider:6db", QColor(Qt::red));
    m_defaultColors.insert("GainSlider:0db", QColor(Qt::yellow));
    m_defaultColors.insert("GainSlider:-6db", QColor(Qt::green));
    m_defaultColors.insert("GainSlider:-60db", QColor(Qt::blue));
    m_defaultColors.insert("FFTMeter:margin", p.color(QPalette::Window));
    m_defaultColors.insert("FFTMeter:background", p.color(QPalette::Base));
    m_defaultColors.insert("FFTMeter:grid", p.color(QPalette::Dark));
    m_defaultColors.insert("FFTMeter:foreground", p.color(QPalette::Link));
    m_defaultColors.insert("FFTMeter:curve:average", p.color(QPalette::LinkVisited));
    m_defaultColors.insert("FFTMeter:tickmarks:main", p.color(QPalette::Dark));
    m_defaultColors.insert("FFTMeter:tickmarks:sub", p.color(QPalette::Mid));
    m_defaultColors.insert("FFTMeter:text", p.color(QPalette::WindowText));
    m_defaultColors.insert("VUMeter:background:widget", p.color(QPalette::Window));
    m_defaultColors.insert("VUMeter:background:bar", p.color(QPalette::Base));
    m_defaultColors.insert("VUMeter:foreground:6db", QColor(Qt::red));
    m_defaultColors.insert("VUMeter:foreground:0db", QColor(Qt::yellow));
    m_defaultColors.insert("VUMeter:foreground:-6db", QColor(Qt::green));
    m_defaultColors.insert("VUMeter:foreground:-60db", QColor(Qt::blue));
    m_defaultColors.insert("VUMeter:font:active", p.color(QPalette::WindowText));
    m_defaultColors.insert("VUMeter:font:inactive", p.color(QPalette::WindowText));
    m_defaultColors.insert("VUMeter:overled:active", QColor(Qt::red));
    m_defaultColors.insert("VUMeter:overled:inactive", p.color(QPalette::Base));
    m_defaultColors.insert("VUMeter:levelseparator", p.color(QPalette::Mid));
    m_defaultColors.insert("InfoWidget:background", p.color(QPalette::Window));
    m_defaultColors.insert("PanSlider:-1", QColor(Qt::red));
    m_defaultColors.insert("PanSlider:0", p.color(QPalette::Base));
    m_defaultColors.insert("PanSlider:1", QColor(Qt::red));
    m_defaultColors.insert("Playhead:active", QColor(255, 0, 0, 180));
    m_defaultColors.insert("Playhead:inactive", QColor(255, 0, 0, 120));
    m_defaultColors.insert("Plugin:background", p.color(QPalette::Button));
    m_defaultColors.insert("Plugin:background:bypassed", p.color(QPalette::Light));
    m_defaultColors.insert("Plugin:text", p.color(QPalette::WindowText));
    m_defaultColors.insert("PluginSlider:background", p.color(QPalette::Mid));
    m_defaultColors.insert("PluginSlider:value", p.color(QPalette::Highlight));
    m_defaultColors.insert("PluginSlider:text", p.color(QPalette::WindowText));
    m_defaultColors.insert("ResourcesBin:alternaterowcolor", p.color(QPalette::AlternateBase));
    m_defaultColors.insert("Sheet:background", p.color(QPalette::Base));
    m_defaultColors.insert("SheetPanel:background", p.color(QPalette::Window));
    m_defaultColors.insert("Timeline:background", p.color(QPalette::Window));
    m_defaultColors.insert("Timeline:text", p.color(QPalette::WindowText));
    m_defaultColors.insert("Timeline:majorticks", p.color(QPalette::WindowText));
    m_defaultColors.insert("Timeline:minorticks", p.color(QPalette::WindowText));
    m_defaultColors.insert("Track:cliptopoffset", p.color(QPalette::Dark));
    m_defaultColors.insert("Track:clipbottomoffset", p.color(QPalette::Dark));
    m_defaultColors.insert("Track:background", p.color(QPalette::Base));
    m_defaultColors.insert("Track:mousehover", set_color_alpha(p.color((QPalette::Base)), 50));
    m_defaultColors.insert("Track:laneseperator", p.color((QPalette::Dark)));
    m_defaultColors.insert("TrackPanel:header:background", p.color(QPalette::Base));
    m_defaultColors.insert("TrackPanel:background", p.color(QPalette::Window));
    m_defaultColors.insert("TrackPanel:text", p.color(QPalette::WindowText));
    m_defaultColors.insert("TrackPanel:sliderborder", p.color(QPalette::WindowText));
    m_defaultColors.insert("TrackPanel:slider:background", p.color(QPalette::Button));
    m_defaultColors.insert("TrackPanel:slider:border", p.color(QPalette::Window));
    m_defaultColors.insert("TrackPanel:head:active", p.color(QPalette::Highlight));
    m_defaultColors.insert("TrackPanel:head:inactive", p.color(QPalette::Highlight));
    m_defaultColors.insert("TrackPanel:muteled", QColor(Qt::yellow));
    m_defaultColors.insert("TrackPanel:sololed", QColor(Qt::green));
    m_defaultColors.insert("TrackPanel:recled", QColor(Qt::red));
    m_defaultColors.insert("TrackPanel:led:inactive", p.color(QPalette::Button));
    m_defaultColors.insert("TrackPanel:trackseparation", p.color(QPalette::WindowText));
    m_defaultColors.insert("TrackPanel:led:margin:active", p.color(QPalette::Dark));
    m_defaultColors.insert("TrackPanel:led:margin:inactive", p.color(QPalette::Dark));
    m_defaultColors.insert("TrackPanel:led:font:active", p.color(QPalette::WindowText));
    m_defaultColors.insert("TrackPanel:led:font:inactive", p.color(QPalette::WindowText));
    m_defaultColors.insert("TrackPanel:bus:font", p.color(QPalette::WindowText));
    m_defaultColors.insert("TrackPanel:bus:background", p.color(QPalette::Button));
    m_defaultColors.insert("TrackPanel:bus:margin", p.color(QPalette::Dark));
    m_defaultColors.insert("BusTrack:background", p.color(QPalette::Base));
    m_defaultColors.insert("BusTrackPanel:background", p.color(QPalette::Base));
    m_defaultColors.insert("Workcursor:default", QColor(100, 50, 100, 180));
    m_defaultColors.insert("Marker:default", QColor(Qt::red));
    m_defaultColors.insert("Marker:blink", p.color(QPalette::Highlight));
    m_defaultColors.insert("Marker:end", QColor(Qt::blue));
    m_defaultColors.insert("Marker:outline", p.color(QPalette::HighlightedText));
    m_defaultColors.insert("Marker:line:active", p.color(QPalette::Accent));
    c = p.color(QPalette::Accent);
    c.setAlpha(70);
    m_defaultColors.insert("Marker:line:inactive", c);
    m_defaultColors.insert("Marker:blinkend", p.color(QPalette::Highlight));
}

void TThemer::validate_loaded_theme()
{
    QStringList list;
    foreach(const QString& key, m_defaultColors.keys()) {
        if (!m_colors.contains(key)) {
            QColor color = m_defaultColors.value(key);
            // add the missing color from default colors
            // so the theme editor will be able to save those too.
            m_colors.insert(key, color);
            list << tr("<color name=\"%1\"  red=\"%2\" green=\"%3\" blue=\"%4\"  alpha=\"%5\" />").
                    arg(key).arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
        }
    }

    if (list.size()) {
        printf("\n");
        printf("Themer: the following entries are missing, please edit theme: %s\n", QS_C(m_themefile));

        foreach(QString string, list) {
            printf("%s\n", QS_C(string));
        }
        printf("\nAnd adjust the color(s) to fit your theme, using the edit theme button in the Appearance config page!\n"
               "The edited theme will be saved to ~/.traverso/themes/editedtheme.xml\n\n");
    }
}

QList<QString> TThemer::get_colors()
{
    QList<QString> colors = m_colors.keys();
    // if the current theme misses some colors, add them here.
    foreach(QString color, m_defaultColors.keys()) {
        if (!colors.contains(color)) {
            colors.append(color);
        }
    }

    return colors;
}

void TThemer::set_new_theme_color(const QString &name, const QColor &color)
{
    m_colors.insert(name, color);
    emit themeLoaded();
}
