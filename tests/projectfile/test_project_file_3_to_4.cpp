// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

// Headless test of TProjectFileConversion3To4: a version 3 project with every
// kind of id and reference, dangling ones included, becomes version 4 with no
// old id left, new ids in document order, every reference by new id, masters
// derived, the jack buses and the channels out of the file, the creation date
// from the old project id; a template converts without uuid and date; the
// same input converts the same way twice; an element the converter does not
// know that holds an id is reported.

#include "TObjectIdTags.h"
#include "TProjectFileConversion3To4.h"

#include <QDomDocument>
#include <QHash>
#include <QSet>

#include <cstdio>
#include <functional>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("%s: %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) {
        ++failures;
    }
}

// The old project id: 1700000000 seconds after the epoch, times 10^9, plus a
// random part
static const char* const ProjectFile = R"(<!DOCTYPE Project>
<Project>
 <Properties title="test" id="1700000000123456789" currentsheetid="101" activesessionid="%1" nextobjectid="40" objectid="0101000000000009" projectfileversion="3"/>
 <AudioDriverConfigurations><AudioDriverConfiguration driver="ALSA"/></AudioDriverConfigurations>
 <AudioChannels><Channel id="900" name="trackjack_out_1" type="output"/></AudioChannels>
 <AudioBuses>
  <Bus id="201" name="Playback 1-2" channels="a;b" channelids="" type="output" bustype="hardware"/>
  <Bus id="202" name="trackjack" channels="trackjack_out_1" channelids="900" type="output" bustype="software"/>
  %2
 </AudioBuses>
 <BusTracks>
  <BusTrack id="301" name="project bus"><Sends/></BusTrack>
 </BusTracks>
 <MasterOut>
  <BusTrack id="302" objectid="0304000000000000" name="Master">
   <Sends>
    <Send id="401" bus="201" busname="Playback 1-2" type="post"/>
    <Send id="402" bus="1" busname="jackmaster" type="post"/>
   </Sends>
  </BusTrack>
 </MasterOut>
 <ResourcesManager>
  <AudioSources>
   <Source id="501" name="take-1700000000123456789" wasrecording="1" origsheetid="101"/>
   <Source id="502" name="import.wav" wasrecording="0" origsheetid="701"/>
   <Source id="503" name="Silence" wasrecording="0" origsheetid="-1" channelcount="0"/>
   <Source id="504" name="old-1700000000123456789-ch0.wav" wasrecording="1" origsheetid="102"/>
  </AudioSources>
  <AudioClips>
   <Clip id="601" source="501" sheet="101" clipname="one"/>
   <Clip id="602" source="502" sheet="101" clipname="two"/>
   <Clip id="603" source="502" sheet="101" clipname="in the bin"/>
  </AudioClips>
 </ResourcesManager>
 <Sheets>
  <Sheet id="101" objectid="0201000000000005">
   <Properties title="song one"/>
   <ClipManager><GlobalSelection clips="601;602;699"/></ClipManager>
   <TimeLine objectid="0a01000000000000" owner="0201000000000005"><Markers><Marker id="801" position="0"/></Markers></TimeLine>
   <MasterOut>
    <BusTrack id="102001" owner="0201000000000005">
     <Sends><Send id="403" bus="302" type="post"/></Sends>
    </BusTrack>
   </MasterOut>
   <Tracks>
    <Track id="1001" name="audio one">
     <PluginChain><Plugins><Plugin><ControlPorts><ControlPort><PortAutomation id="1101" nodes="1,1"/></ControlPort></ControlPorts></Plugin></Plugins></PluginChain>
     <Clips><Clip id="601"/><Clip id="699"/><Clip id="602"/></Clips>
     <Sends>
      <Send id="404" bus="102001" type="post"/>
      <Send id="405" bus="1002" type="post"/>
      <Send id="406" bus="302" type="post"/>
      <Send id="407" bus="201" type="post"/>
      <Send id="408" bus="999" type="post"/>
      <Send id="409" bus="202" type="post"/>
     </Sends>
    </Track>
    <Track id="1003" name="audio two"><Clips/><Sends/></Track>
   </Tracks>
   <BusTracks>
    <BusTrack id="1002" name="song bus"><Sends><Send id="410" bus="102001" type="post"/></Sends></BusTrack>
   </BusTracks>
   <WorkSheets>
    <WorkSheet id="701" name="view"><Tracks><Track id="1001" height="90"/><Track id="1999"/></Tracks></WorkSheet>
   </WorkSheets>
  </Sheet>
  <Sheet id="102">
   <Properties title="song two"/>
   <ClipManager><GlobalSelection clips=""/></ClipManager>
   <TimeLine><Markers/></TimeLine>
   <MasterOut><BusTrack id="102002"><Sends/></BusTrack></MasterOut>
   <Tracks/><BusTracks/><WorkSheets/>
  </Sheet>
 </Sheets>
 <WorkSheets>
  <WorkSheet id="702" name="project view"><Tracks><Track id="301"/></Tracks></WorkSheet>
 </WorkSheets>
</Project>
)";

static const char* const JackMasterBus = R"(<Bus id="1" name="jackmaster" channels="jackmaster_0;jackmaster_1" channelids="" type="output" bustype="software"/>)";

static const char* const TemplateFile = R"(<!DOCTYPE Project>
<Project>
 <Properties title="Template Project File!!" currentsheetid="-1" activesessionid="-1" projectfileversion="3"/>
 <AudioBuses><Bus id="201" name="Playback 1-2" type="output" bustype="hardware"/></AudioBuses>
 <BusTracks/>
 <MasterOut><BusTrack id="302"><Sends><Send id="401" bus="201"/></Sends></BusTrack></MasterOut>
 <Sheets>
  <Sheet id="101">
   <MasterOut><BusTrack id="102001"><Sends><Send id="403" bus="302"/></Sends></BusTrack></MasterOut>
   <Tracks><Track id="1001"><Sends><Send id="404" bus="102001"/></Sends></Track></Tracks>
  </Sheet>
 </Sheets>
</Project>
)";

static QDomDocument parse(const QString& text)
{
    QDomDocument doc;
    const QDomDocument::ParseResult result = doc.setContent(text);
    if (!result) {
        std::printf("fixture does not parse: %s\n", qUtf8Printable(result.errorMessage));
    }
    return doc;
}

static void for_each(const QDomElement& element, const std::function<void(const QDomElement&)>& visit)
{
    visit(element);
    for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        for_each(child, visit);
    }
}

static uint64_t value_of(const QString& hex)
{
    bool ok = false;
    const uint64_t value = hex.toULongLong(&ok, 16);
    return ok ? value : 0;
}

static uint8_t kind_of(const QString& hex)
{
    return TObjectIds::kind_of(TObjectIds::tag_of(value_of(hex)));
}

static QString id_text(uint8_t kind, uint8_t subtype, uint64_t number)
{
    return QString::fromStdString(TObjectIds::to_hex((uint64_t(TObjectIds::make_tag(kind, subtype)) << TObjectIds::SequenceBits) | number));
}

static QString parent_name(const QDomElement& element, int up = 1)
{
    QDomNode node = element;
    for (int i = 0; i < up; ++i) {
        node = node.parentNode();
    }
    return node.toElement().tagName();
}

// The kind an element's objectid must have, 0 for an element that has none
static uint8_t kind_for(const QDomElement& e)
{
    const QString name = e.tagName();
    const QString parent = parent_name(e);
    if (name == "BusTrack" && parent == "BusTracks") return TrackKind;
    if (name == "Track" && parent == "Tracks" && parent_name(e, 2) == "Sheet") return TrackKind;
    if (name == "Source") return AudioSourceKind;
    if (name == "Clip" && parent == "AudioClips") return ClipKind;
    if (name == "Sheet") return SongKind;
    if (name == "Marker") return MarkerKind;
    if (name == "Send") return SendKind;
    if (name == "WorkSheet") return WorkspaceKind;
    return 0;
}

static QDomElement first(const QDomElement& root, const std::function<bool(const QDomElement&)>& match)
{
    QDomElement found;
    for_each(root, [&](const QDomElement& e) {
        if (found.isNull() && match(e)) {
            found = e;
        }
    });
    return found;
}

static QDomElement send_with_name(const QDomElement& root, const QString& trackName, int index)
{
    const QDomElement track = first(root, [&](const QDomElement& e) { return e.attribute("name") == trackName && e.hasAttribute("objectid"); });
    QDomElement send = track.firstChildElement("Sends").firstChildElement("Send");
    for (int i = 0; i < index; ++i) {
        send = send.nextSiblingElement("Send");
    }
    return send;
}

static void test_project(const char* activeSession, bool jackMasterBus)
{
    const QUuid uuid("{12345678-9abc-def0-1234-56789abcdef0}");
    QDomDocument doc = parse(QString(ProjectFile).arg(activeSession, jackMasterBus ? JackMasterBus : ""));
    const TProjectFileConversion3To4::Result result = TProjectFileConversion3To4::convert(doc, uuid);
    const QDomElement root = doc.documentElement();
    const QDomElement properties = root.firstChildElement("Properties");

    // 1. Version 4, nothing of the old ids left
    check(properties.attribute("projectfileversion") == "4", "1. version 4");
    check(result.leftOverIds == 0, "1. the converter reports no old id left");
    int oldAttributes = 0;
    for_each(root, [&](const QDomElement& e) {
        for (const char* attribute : {"id", "bus", "source", "sheet", "origsheetid", "currentsheetid", "activesessionid", "channelids", "clips"}) {
            oldAttributes += e.hasAttribute(attribute) ? 1 : 0;
        }
    });
    check(oldAttributes == 0, "1. no old attribute left anywhere");
    check(root.firstChildElement("AudioChannels").isNull(), "1. no AudioChannels");
    int softwareBuses = 0;
    for_each(root, [&](const QDomElement& e) { softwareBuses += (e.tagName() == "Bus" && e.attribute("bustype") != "hardware") ? 1 : 0; });
    check(softwareBuses == 0, "1. no software bus left: they are made again at load");

    // 2. Unique ids that fit their element, numbered in document order
    QSet<QString> ids;
    bool unique = true;
    bool fits = true;
    bool inOrder = true;
    uint64_t number = 0;
    for_each(root, [&](const QDomElement& e) {
        if (!e.hasAttribute("objectid")) {
            fits = fits && kind_for(e) == 0;
            return;
        }
        const QString id = e.attribute("objectid");
        unique = unique && !ids.contains(id);
        ids.insert(id);
        fits = fits && kind_of(id) == kind_for(e);
        inOrder = inOrder && TObjectIds::sequence_of(value_of(id)) == ++number;
    });
    check(unique, "2. every objectid is unique");
    check(fits, "2. every objectid's kind fits its element, and every element with an id has one");
    check(inOrder, "2. the numbers are 1..n in document order");
    check(properties.attribute("nextobjectid").toULongLong() == number + 1, "2. nextobjectid is the highest number + 1");

    // 3. Masters and the ruler are derived: no id, no owner
    bool derived = true;
    for_each(root, [&](const QDomElement& e) {
        if ((e.tagName() == "BusTrack" && parent_name(e) == "MasterOut") || e.tagName() == "TimeLine") {
            derived = derived && !e.hasAttribute("objectid") && !e.hasAttribute("owner");
        }
    });
    check(derived, "3. masters and the ruler have no objectid and no owner");
    const QDomElement songOne = first(root, [](const QDomElement& e) { return e.tagName() == "Sheet"; });
    const uint64_t songOneNumber = TObjectIds::sequence_of(value_of(songOne.attribute("objectid")));
    check(send_with_name(root, "audio one", 0).attribute("targettrack") == id_text(TrackKind, SongMasterSubtype, songOneNumber),
          "3. a send to a song master names the master derived from the song");
    check(send_with_name(root, "audio one", 2).attribute("targettrack") == id_text(TrackKind, ProjectMasterSubtype, 0),
          "3. a send to the project master names number 0");

    // 4. Every reference resolves to an object of its kind
    auto resolves = [&](const QString& id, uint8_t kind) {
        if (kind_of(id) != kind) {
            return false;
        }
        const uint8_t subtype = TObjectIds::subtype_of(TObjectIds::tag_of(value_of(id)));
        const bool master = kind == TrackKind && (subtype == SongMasterSubtype || subtype == ProjectMasterSubtype);
        return master || ids.contains(id);
    };
    bool references = true;
    int referenceCount = 0;
    for_each(root, [&](const QDomElement& e) {
        const QList<QPair<const char*, uint8_t>> attributes = {
            {"targettrack", TrackKind}, {"audiosource", AudioSourceKind}, {"originsong", SongKind},
            {"currentsong", SongKind}, {"activeworkspace", WorkspaceKind}};
        for (const auto& [attribute, kind] : attributes) {
            if (e.hasAttribute(attribute)) {
                references = references && resolves(e.attribute(attribute), kind);
                ++referenceCount;
            }
        }
        if (e.tagName() == "Clip" && parent_name(e) == "Clips") {
            references = references && resolves(e.attribute("clip"), ClipKind);
            ++referenceCount;
        }
        if (e.tagName() == "Track" && parent_name(e, 2) == "WorkSheet") {
            references = references && resolves(e.attribute("track"), TrackKind);
            ++referenceCount;
        }
        if (e.tagName() == "GlobalSelection") {
            for (const QString& id : e.attribute("selectedclips").split(';', Qt::SkipEmptyParts)) {
                references = references && resolves(id, ClipKind);
                ++referenceCount;
            }
        }
    });
    check(references && referenceCount > 15, "4. every reference resolves to an object of its kind");

    // 5. One target per send; dangling references dropped, one warning each
    bool oneTarget = true;
    int sends = 0;
    for_each(root, [&](const QDomElement& e) {
        if (e.tagName() == "Send") {
            oneTarget = oneTarget && (e.hasAttribute("targettrack") != e.hasAttribute("targetport"));
            ++sends;
        }
    });
    check(oneTarget, "5. every send has exactly one target");
    check(sends == 10, "5. the dangling send and the send to a track's jack bus are gone, two sends to a master added");

    // A song's track or master without a post send gets the one the version 3
    // load gave it; one with a send is left alone
    auto count_sends = [](const QDomElement& track) {
        int n = 0;
        for (QDomElement s = track.firstChildElement("Sends").firstChildElement("Send"); !s.isNull(); s = s.nextSiblingElement("Send")) {
            ++n;
        }
        return n;
    };
    const QDomElement trackTwo = first(root, [](const QDomElement& e) { return e.attribute("name") == "audio two"; });
    const QDomElement trackTwoSend = trackTwo.firstChildElement("Sends").firstChildElement("Send");
    check(count_sends(trackTwo) == 1 && trackTwoSend.attribute("type") == "post"
              && trackTwoSend.attribute("targettrack") == id_text(TrackKind, SongMasterSubtype, songOneNumber)
              && kind_of(trackTwoSend.attribute("objectid")) == SendKind,
          "default send: an audio track without a send gets one to its song master, with a counter id");
    const QDomElement songTwo = songOne.nextSiblingElement("Sheet");
    const QDomElement songTwoMaster = songTwo.firstChildElement("MasterOut").firstChildElement("BusTrack");
    check(count_sends(songTwoMaster) == 1
              && songTwoMaster.firstChildElement("Sends").firstChildElement("Send").attribute("targettrack")
                     == id_text(TrackKind, ProjectMasterSubtype, 0),
          "default send: a song master without a send gets one to the project master");
    check(count_sends(first(root, [](const QDomElement& e) { return e.attribute("name") == "audio one"; })) == 4
              && count_sends(first(root, [](const QDomElement& e) { return e.attribute("name") == "song bus"; })) == 1,
          "default send: a track with a post send is left alone");
    check(count_sends(first(root, [](const QDomElement& e) { return e.attribute("name") == "project bus"; })) == 0,
          "default send: a project bus track gets none, as the version 3 load gave it none");
    check(send_with_name(root, "audio one", 3).attribute("targetport") == "Playback 1-2", "5. a send to a hardware bus names it");
    const QDomElement master = root.firstChildElement("MasterOut").firstChildElement("BusTrack");
    check(master.firstChildElement("Sends").lastChildElement("Send").attribute("targetport") == "jackmaster",
          jackMasterBus ? "5. a send to the jack master's bus names jackmaster" : "5. the jack master's fixed id names jackmaster");
    const QDomElement trackOne = first(root, [](const QDomElement& e) { return e.attribute("name") == "audio one"; });
    int clipsOfTrack = 0;
    for (QDomElement c = trackOne.firstChildElement("Clips").firstChildElement("Clip"); !c.isNull(); c = c.nextSiblingElement("Clip")) {
        ++clipsOfTrack;
    }
    check(clipsOfTrack == 2, "5. the dangling clip of a track is gone, the clip after it stays");
    check(result.warnings.size() == 4, "5. four warnings: a send, a clip, a workspace track, a selected clip");

    // 6. Creation date and uuid
    check(properties.attribute("createdon") == "2023-11-14T22:13:20Z", "6. createdon from the old project id, UTC");
    check(properties.attribute("uuid") == "12345678-9abc-def0-1234-56789abcdef0", "6. the uuid given");

    // 7. Song and short names of sources
    const QDomElement take = first(root, [](const QDomElement& e) { return e.tagName() == "Source"; });
    QHash<QString, QDomElement> sources;
    for (QDomElement s = take; !s.isNull(); s = s.nextSiblingElement("Source")) {
        sources.insert(s.attribute("name"), s);
    }
    check(sources.value("take-1700000000123456789").attribute("originsong") == songOne.attribute("objectid"),
          "7. a take names its song");
    check(!sources.value("import.wav").hasAttribute("originsong") && !sources.value("Silence").hasAttribute("originsong"),
          "7. an import and the silent source name no song");
    check(sources.value("take-1700000000123456789").attribute("shortname") == "take", "7. a take's short name is cut");
    check(sources.value("old-1700000000123456789-ch0.wav").attribute("shortname") == "old",
          "7. a take of a version 2 project is cut with its channel suffix");
    check(sources.value("import.wav").attribute("shortname") == "import.wav", "7. an import keeps its whole name");

    // 9. Development ids replaced
    check(!properties.hasAttribute("objectid"), "9. a development id on the properties is gone");
    // Before the song: the project bus track, the master's two sends, four
    // sources and three clips
    check(songOne.attribute("objectid") == id_text(SongKind, SongSubtype, 11), "9. a development id is replaced by the number in document order");
}

static void test_active_session()
{
    const QUuid uuid = QUuid::createUuid();
    QDomDocument workspace = parse(QString(ProjectFile).arg("701", ""));
    TProjectFileConversion3To4::convert(workspace, uuid);
    const QDomElement w = workspace.documentElement().firstChildElement("Properties");
    check(w.hasAttribute("activeworkspace") && kind_of(w.attribute("activeworkspace")) == WorkspaceKind,
          "Q-B. an active workspace is saved as activeworkspace");

    QDomDocument song = parse(QString(ProjectFile).arg("102", ""));
    TProjectFileConversion3To4::convert(song, uuid);
    const QDomElement s = song.documentElement().firstChildElement("Properties");
    const QDomElement songTwo = song.documentElement().firstChildElement("Sheets").lastChildElement("Sheet");
    check(s.attribute("currentsong") == songTwo.attribute("objectid") && !s.hasAttribute("activeworkspace"),
          "Q-B. an active song other than the current one becomes the current song");

    QDomDocument project = parse(QString(ProjectFile).arg("1700000000123456789", ""));
    TProjectFileConversion3To4::convert(project, uuid);
    const QDomElement p = project.documentElement().firstChildElement("Properties");
    const QDomElement songOne = project.documentElement().firstChildElement("Sheets").firstChildElement("Sheet");
    check(p.attribute("currentsong") == songOne.attribute("objectid") && !p.hasAttribute("activeworkspace"),
          "Q-B. the project as active session is not saved, the current song is");
}

static bool has_warning(const QStringList& warnings, const QString& part)
{
    for (const QString& w : warnings) {
        if (w.contains(part)) {
            return true;
        }
    }
    return false;
}

// Dangling and damaged references beyond those of the main fixture: each is
// dropped with one warning of its own
static void test_dangling()
{
    QDomDocument doc = parse(QString(ProjectFile).arg("701", ""));
    QDomElement root = doc.documentElement();
    QDomElement properties = root.firstChildElement("Properties");
    properties.setAttribute("currentsheetid", "109");

    QDomElement clip = doc.createElement("Clip");
    clip.setAttribute("id", "604");
    clip.setAttribute("source", "598");
    clip.setAttribute("clipname", "lost");
    root.firstChildElement("ResourcesManager").firstChildElement("AudioClips").appendChild(clip);

    QDomElement tracks = root.firstChildElement("Sheets").firstChildElement("Sheet").firstChildElement("Tracks");
    QDomElement twin = doc.createElement("Track");
    twin.setAttribute("id", "1003");
    twin.setAttribute("name", "twin");
    tracks.appendChild(twin);

    QDomElement noId = doc.createElement("Track");
    noId.setAttribute("id", "");
    noId.setAttribute("name", "no id");
    QDomElement sends = doc.createElement("Sends");
    QDomElement noBus = doc.createElement("Send");
    noBus.setAttribute("id", "411");
    noBus.setAttribute("busname", "nowhere");
    sends.appendChild(noBus);
    noId.appendChild(sends);
    tracks.appendChild(noId);

    const TProjectFileConversion3To4::Result result = TProjectFileConversion3To4::convert(doc, QUuid::createUuid());

    check(!clip.hasAttribute("audiosource") && has_warning(result.warnings, "the source 598 of the clip lost"),
          "dangling: a clip whose source is gone names none, with a warning");
    check(has_warning(result.warnings, "the current song 109 is not in the project"),
          "dangling: a current song that is gone gives a warning");
    check(!properties.hasAttribute("currentsong"), "dangling: no current song is written, the load makes the first song current");
    check(has_warning(result.warnings, "the old id 1003 is used twice"), "dangling: an old id used twice gives a warning");
    check(has_warning(result.warnings, "names no bus (nowhere)"), "dangling: a send without a bus is dropped with a warning");
    const QDomElement left = noId.firstChildElement("Sends").firstChildElement("Send");
    check(left.attribute("busname") != "nowhere" && left.hasAttribute("targettrack") && left.nextSiblingElement("Send").isNull(),
          "dangling: the send without a bus is gone, the track gets the send to its master instead");
    check(result.leftOverIds == 0, "dangling: no old id is left");
}

int main()
{
    register_object_id_names();

    test_project("701", true);
    test_project("701", false);
    test_active_session();
    test_dangling();

    // 6. A template: no project id, so no uuid and no date, and no failure
    {
        QDomDocument doc = parse(TemplateFile);
        const TProjectFileConversion3To4::Result result = TProjectFileConversion3To4::convert(doc, QUuid::createUuid());
        const QDomElement properties = doc.documentElement().firstChildElement("Properties");
        check(result.leftOverIds == 0 && properties.attribute("projectfileversion") == "4", "6. a template converts");
        check(!properties.hasAttribute("uuid") && !properties.hasAttribute("createdon"), "6. a template gets no uuid and no date");
    }

    // 8. Deterministic
    {
        const QUuid uuid = QUuid::createUuid();
        QDomDocument a = parse(QString(ProjectFile).arg("701", JackMasterBus));
        QDomDocument b = parse(QString(ProjectFile).arg("701", JackMasterBus));
        TProjectFileConversion3To4::convert(a, uuid);
        TProjectFileConversion3To4::convert(b, uuid);
        check(a.toString() == b.toString(), "8. the same input converts the same way twice");
    }

    // An element with an old id the converter does not know: reported
    {
        QDomDocument doc = parse(QString(ProjectFile).arg("701", JackMasterBus));
        QDomElement unknown = doc.createElement("SomethingNew");
        unknown.setAttribute("id", "4242");
        doc.documentElement().appendChild(unknown);
        const TProjectFileConversion3To4::Result result = TProjectFileConversion3To4::convert(doc, QUuid::createUuid());
        check(result.leftOverIds == 1, "an unknown element with an old id is counted, the load refuses it");
    }

    if (failures > 0) {
        std::printf("%d project file conversion tests FAILED\n", failures);
        return 1;
    }
    std::printf("all project file conversion tests passed\n");
    return 0;
}
