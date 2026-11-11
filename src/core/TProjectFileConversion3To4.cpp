// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TProjectFileConversion3To4.h"

#include "TObjectIdTags.h"
#include "TPortNames.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>

// The old ids of the file, each to the new id it became
struct Tables
{
    QString projectId;
    QHash<QString, QString> tracks;
    QHash<QString, QString> sources;
    QHash<QString, QString> clips;
    QHash<QString, QString> songs;
    QHash<QString, QString> workspaces;
    // A hardware bus by old id, to its name
    QHash<QString, QString> ports;
    // The software buses: a track's own jack buses, rebuilt at load, and the
    // jack master, named by its name
    QSet<QString> softwareBuses;
    QString jackMaster;
    // The old ids of every track and master, known before the numbering, so
    // a send that is dropped gets no number
    QSet<QString> trackIds;
    object_id_value_t next{1};
    object_id_value_t song{0};
    QList<QDomElement> removed;
    QStringList warnings;
};

static QString id_text(uint8_t kind, uint8_t subtype, object_id_value_t number)
{
    const object_id_value_t value = (object_id_value_t(TObjectIds::make_tag(kind, subtype)) << TObjectIds::SequenceBits) | number;
    return QString::fromStdString(TObjectIds::to_hex(value));
}

static QString parent_name(const QDomElement& element, int up = 1)
{
    QDomNode node = element;
    for (int i = 0; i < up; ++i) {
        node = node.parentNode();
    }
    return node.toElement().tagName();
}

// A new number from the sequence for the element, in document order
static QString give_id(QDomElement& element, Tables& tables, uint8_t kind, uint8_t subtype)
{
    const QString id = id_text(kind, subtype, tables.next++);
    element.setAttribute("objectid", id);
    return id;
}

static void remember(QHash<QString, QString>& table, const QDomElement& element, const QString& id, Tables& tables)
{
    const QString old = element.attribute("id");
    if (old.isEmpty()) {
        return;
    }
    if (table.contains(old)) {
        tables.warnings.append(QString("the old id %1 is used twice: the second (%2) cannot be referred to").arg(old, element.tagName()));
        return;
    }
    table.insert(old, id);
}

static void collect_tracks(const QDomElement& element, Tables& tables)
{
    const QString name = element.tagName();
    const QString parent = parent_name(element);
    if ((name == "BusTrack" && (parent == "BusTracks" || parent == "MasterOut"))
        || (name == "Track" && parent == "Tracks" && parent_name(element, 2) == "Sheet")) {
        const QString old = element.attribute("id");
        if (!old.isEmpty()) {
            tables.trackIds.insert(old);
        }
    }
    for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        collect_tracks(child, tables);
    }
}

// A send to a track, a hardware bus or the jack master stays; one to a
// track's own jack bus is made again when the track connects to jack at load
static bool send_stays(const QDomElement& send, Tables& tables)
{
    const QString bus = send.attribute("bus");
    const QString track = send.parentNode().parentNode().toElement().attribute("name");
    if (bus.isEmpty()) {
        tables.warnings.append(QString("a send of the track %1 names no bus (%2), it is dropped")
                               .arg(track, send.attribute("busname")));
        return false;
    }
    if (tables.trackIds.contains(bus) || tables.ports.contains(bus) || bus == tables.jackMaster
        || (tables.jackMaster.isEmpty() && bus == "1")) {
        return true;
    }
    if (!tables.softwareBuses.contains(bus)) {
        tables.warnings.append(QString("a send of the track %1 to the bus %2 (%3), which is not in the project, is dropped")
                               .arg(track, bus, send.attribute("busname")));
    }
    return false;
}

// A track of a song without a post send after the conversion: the version 3
// load gave it one to its master (a song's master one to the project's),
// version 4 does not, so the file gets it once
static bool has_post_send(const QDomElement& track, const Tables& tables)
{
    const QDomElement sends = track.firstChildElement("Sends");
    for (QDomElement send = sends.firstChildElement("Send"); !send.isNull(); send = send.nextSiblingElement("Send")) {
        if (send.attribute("type") != "pre" && !tables.removed.contains(send)) {
            return true;
        }
    }
    return false;
}

static void add_master_send(QDomElement& track, Tables& tables, bool toProjectMaster)
{
    QDomElement sends = track.firstChildElement("Sends");
    if (sends.isNull()) {
        sends = track.ownerDocument().createElement("Sends");
        track.appendChild(sends);
    }
    QDomElement send = track.ownerDocument().createElement("Send");
    give_id(send, tables, SendKind, SendSubtype);
    send.setAttribute("gain", "1");
    send.setAttribute("pan", "0");
    send.setAttribute("targettrack", toProjectMaster ? id_text(TrackKind, ProjectMasterSubtype, 0)
                                                     : id_text(TrackKind, SongMasterSubtype, tables.song));
    send.setAttribute("busname", toProjectMaster ? "Master" : "Sheet Master");
    send.setAttribute("type", "post");
    sends.appendChild(send);
}

static void remove_development_ids(QDomElement element)
{
    element.removeAttribute("objectid");
    element.removeAttribute("owner");
    for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        remove_development_ids(child);
    }
}

static void give_ids(QDomElement element, Tables& tables)
{
    const QString name = element.tagName();
    const QString parent = parent_name(element);
    const QString grandParent = parent_name(element, 2);
    const object_id_value_t outerSong = tables.song;

    if (name == "Properties" && parent == "Project") {
        tables.projectId = element.attribute("id");
    } else if (name == "AudioChannels" && parent == "Project") {
        tables.removed.append(element);
    } else if (name == "Bus" && parent == "AudioBuses") {
        const QString old = element.attribute("id");
        if (element.attribute("bustype", "software") == "software") {
            if (!old.isEmpty()) {
                tables.softwareBuses.insert(old);
                if (element.attribute("name") == QLatin1String(JackMasterPortName)) {
                    tables.jackMaster = old;
                }
            }
            tables.removed.append(element);
        } else {
            if (!old.isEmpty()) {
                tables.ports.insert(old, element.attribute("name"));
            }
            element.removeAttribute("id");
            element.removeAttribute("channelids");
        }
    } else if (name == "BusTrack" && parent == "BusTracks") {
        remember(tables.tracks, element, give_id(element, tables, TrackKind, BusTrackSubtype), tables);
    } else if (name == "BusTrack" && parent == "MasterOut") {
        if (grandParent == "Project") {
            remember(tables.tracks, element, id_text(TrackKind, ProjectMasterSubtype, 0), tables);
        } else {
            remember(tables.tracks, element, id_text(TrackKind, SongMasterSubtype, tables.song), tables);
        }
    } else if (name == "Source" && parent == "AudioSources") {
        remember(tables.sources, element, give_id(element, tables, AudioSourceKind, AudioSourceSubtype), tables);
    } else if (name == "Clip" && parent == "AudioClips") {
        remember(tables.clips, element, give_id(element, tables, ClipKind, AudioClipSubtype), tables);
    } else if (name == "Sheet" && parent == "Sheets") {
        tables.song = tables.next;
        remember(tables.songs, element, give_id(element, tables, SongKind, SongSubtype), tables);
    } else if (name == "Marker" && parent == "Markers") {
        give_id(element, tables, MarkerKind, MarkerSubtype);
    } else if (name == "Track" && parent == "Tracks" && grandParent == "Sheet") {
        remember(tables.tracks, element, give_id(element, tables, TrackKind, AudioTrackSubtype), tables);
    } else if (name == "Send" && parent == "Sends") {
        if (send_stays(element, tables)) {
            give_id(element, tables, SendKind, SendSubtype);
        } else {
            tables.removed.append(element);
        }
    } else if (name == "WorkSheet" && parent == "WorkSheets") {
        remember(tables.workspaces, element, give_id(element, tables, WorkspaceKind, WorkspaceSubtype), tables);
    }

    for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        give_ids(child, tables);
    }

    // A song's audio track, bus track or master: what the version 3 load added
    const bool songTrack = grandParent == "Sheet"
                           && ((name == "Track" && parent == "Tracks") || (name == "BusTrack" && parent == "BusTracks"));
    const bool songMaster = grandParent == "Sheet" && name == "BusTrack" && parent == "MasterOut";
    if ((songTrack || songMaster) && !has_post_send(element, tables)) {
        add_master_send(element, tables, songMaster);
    }
    tables.song = outerSong;
}

// The old short name of a take: its name without "-<old id>" and what the
// version 2 converter added behind it ("-ch0.wav", ".wav")
static QString take_short_name(const QString& name)
{
    static const QRegularExpression takeName("^(.*)-\\d{19}(-ch0)?(\\.wav)?$");
    const QRegularExpressionMatch match = takeName.match(name);
    return match.hasMatch() ? match.captured(1) : name;
}

static void convert_properties(QDomElement& element, Tables& tables, const QUuid& uuid)
{
    if (!tables.projectId.isEmpty()) {
        const qint64 oldId = tables.projectId.toLongLong();
        // The old project id held its creation time: seconds since the epoch
        // times 10^9, plus a random number
        element.setAttribute("createdon", QDateTime::fromSecsSinceEpoch(oldId / 1000000000, QTimeZone::UTC).toString(Qt::ISODate));
        element.setAttribute("uuid", uuid.toString(QUuid::WithoutBraces));
    }

    const QString currentSheet = element.attribute("currentsheetid");
    const QString activeSession = element.attribute("activesessionid");
    QString currentSong = tables.songs.value(currentSheet);
    // -1 and 0: version 3 had no current song
    if (currentSong.isEmpty() && !currentSheet.isEmpty() && currentSheet != "-1" && currentSheet != "0") {
        tables.warnings.append(QString("the current song %1 is not in the project: the first song will be current").arg(currentSheet));
    }
    if (tables.workspaces.contains(activeSession)) {
        element.setAttribute("activeworkspace", tables.workspaces.value(activeSession));
    } else if (tables.songs.contains(activeSession)) {
        // The version 3 load made this song current after the saved current one
        currentSong = tables.songs.value(activeSession);
    }
    if (!currentSong.isEmpty()) {
        element.setAttribute("currentsong", currentSong);
    }

    element.removeAttribute("id");
    element.removeAttribute("currentsheetid");
    element.removeAttribute("activesessionid");
    element.setAttribute("nextobjectid", QString::number(tables.next));
    element.setAttribute("projectfileversion", 4);
}

static void convert_send(QDomElement& element, Tables& tables)
{
    // The send to a master that give_ids() added is in version 4 already
    if (!element.hasAttribute("bus") && element.hasAttribute("targettrack")) {
        return;
    }
    const QString bus = element.attribute("bus");
    if (tables.tracks.contains(bus)) {
        element.setAttribute("targettrack", tables.tracks.value(bus));
    } else if (tables.ports.contains(bus)) {
        element.setAttribute("targetport", tables.ports.value(bus));
    } else {
        // send_stays() kept only these three
        element.setAttribute("targetport", JackMasterPortName);
    }
    element.removeAttribute("bus");
    element.removeAttribute("id");
}

static void convert_reference(QDomElement& element, const QHash<QString, QString>& table, const char* attribute,
                       const char* what, Tables& tables)
{
    const QString old = element.attribute("id");
    if (table.contains(old)) {
        element.setAttribute(attribute, table.value(old));
    } else {
        tables.warnings.append(QString("a %1 %2 (%3) that is not in the project is dropped")
                               .arg(what, old, element.parentNode().parentNode().toElement().attribute("name")));
        tables.removed.append(element);
    }
    element.removeAttribute("id");
}

static void convert_source(QDomElement& element, Tables& tables)
{
    const bool wasRecording = element.attribute("wasrecording") == "1";
    const QString song = element.attribute("origsheetid");
    if (wasRecording) {
        if (tables.songs.contains(song)) {
            element.setAttribute("originsong", tables.songs.value(song));
        } else {
            tables.warnings.append(QString("the song of the take %1 is not in the project").arg(element.attribute("name")));
        }
    }
    const QString name = element.attribute("name");
    element.setAttribute("shortname", wasRecording ? take_short_name(name) : name);
    element.removeAttribute("origsheetid");
    element.removeAttribute("id");
}

static void convert_clip(QDomElement& element, Tables& tables)
{
    const QString source = element.attribute("source");
    if (tables.sources.contains(source)) {
        element.setAttribute("audiosource", tables.sources.value(source));
    } else {
        tables.warnings.append(QString("the source %1 of the clip %2 is not in the project").arg(source, element.attribute("clipname")));
    }
    element.removeAttribute("source");
    element.removeAttribute("sheet");
    element.removeAttribute("id");
}

static void convert_selection(QDomElement& element, Tables& tables)
{
    QStringList selected;
    for (const QString& old : element.attribute("clips").split(';', Qt::SkipEmptyParts)) {
        if (tables.clips.contains(old)) {
            selected.append(tables.clips.value(old));
        } else {
            tables.warnings.append(QString("a selected clip %1 that is not in the project is dropped").arg(old));
        }
    }
    element.setAttribute("selectedclips", selected.join(';'));
    element.removeAttribute("clips");
}

static void convert_references(QDomElement element, Tables& tables, const QUuid& uuid)
{
    const QString name = element.tagName();
    const QString parent = parent_name(element);
    const QString grandParent = parent_name(element, 2);

    if (name == "Properties" && parent == "Project") {
        convert_properties(element, tables, uuid);
    } else if (name == "Send" && parent == "Sends") {
        convert_send(element, tables);
    } else if (name == "Clip" && parent == "Clips" && grandParent == "Track") {
        convert_reference(element, tables.clips, "clip", "clip of a track", tables);
    } else if (name == "Track" && parent == "Tracks" && grandParent == "WorkSheet") {
        convert_reference(element, tables.tracks, "track", "track of a workspace", tables);
    } else if (name == "GlobalSelection") {
        convert_selection(element, tables);
    } else if (name == "Source" && parent == "AudioSources") {
        convert_source(element, tables);
    } else if (name == "Clip" && parent == "AudioClips") {
        convert_clip(element, tables);
    } else if ((name == "BusTrack" && (parent == "BusTracks" || parent == "MasterOut"))
               || (name == "Track" && parent == "Tracks" && grandParent == "Sheet")
               || (name == "Sheet" && parent == "Sheets") || (name == "Marker" && parent == "Markers")
               || (name == "WorkSheet" && parent == "WorkSheets") || name == "PortAutomation") {
        element.removeAttribute("id");
    }

    for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        convert_references(child, tables, uuid);
    }
}

static int count_ids(const QDomElement& element)
{
    int count = element.hasAttribute("id") ? 1 : 0;
    for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        count += count_ids(child);
    }
    return count;
}

static void remove_all(Tables& tables)
{
    for (QDomElement& element : tables.removed) {
        element.parentNode().removeChild(element);
    }
    tables.removed.clear();
}

TProjectFileConversion3To4::Result TProjectFileConversion3To4::convert(QDomDocument& doc, const QUuid& uuid)
{
    QDomElement root = doc.documentElement();
    Q_ASSERT(root.firstChildElement("Properties").attribute("projectfileversion") == "3");

    Tables tables;

    // Pass 0: ids of development builds; every object gets a new one
    remove_development_ids(root);
    // Pass 1: the new ids, and the old ones they replace
    collect_tracks(root, tables);
    give_ids(root, tables);
    remove_all(tables);
    // Pass 2: the references by new id
    convert_references(root, tables, uuid);
    remove_all(tables);

    // Pass 3: nothing of the old ids is left
    Result result;
    result.warnings = tables.warnings;
    result.leftOverIds = count_ids(root);
    return result;
}
