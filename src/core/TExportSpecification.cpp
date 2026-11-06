#include "TExportSpecification.h"

#include "TAudioResampler.h"
#include "TInformUser.h"
#include "TProject.h"
#include "TSheet.h"
#include "TTimeLineRuler.h"

#include <bit>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include <algorithm>

#include "Debugger.h"

TExportSpecification::TExportSpecification()
{
    m_sampleRate = 44100;
    m_channelCount = 0;
    m_blockSize = 1024;

    m_writerType = TraversoDAW::WriterType::SNDFILE;
    m_fileFormat = TraversoDAW::FileFormat::WAV;
    m_resampler = {TResamplerChoice::Backend::LibSoxr,
                   qint16(TAudioResampler::default_quality(TResamplerChoice::Backend::LibSoxr))};

    m_exportStartLocation = TTimeRef::INVALID;
    m_exportEndLocation = TTimeRef::INVALID;
    m_exportLocation = TTimeRef();

    m_progress = 0;

    m_ditherType = GDitherShaped;

    set_data_format(TraversoDAW::DataFormat::FLOAT);

    m_cancelExportRequested = false;

    // This state is the default and used for conversion/copying of audio files.
    // Set to RecordingState::RECORDING to use this ExportSpecification
    // to record an audio file for an AudioClip.
    m_recordingState = RecordingState::NOT_RECORDING;
    m_exportDir = "";
    m_exportFileName = "";
    writeToc = false;
    m_isCdExport = false;
}


TExportSpecification::~TExportSpecification()
{
}

int TExportSpecification::is_valid()
{
    if (m_channelCount == 0) {
        TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "ExportSpecification: No channels configured!");
        return -1;
    }

    if (m_exportStartLocation == TTimeRef::INVALID) {
        TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "ExportSpecification: No start frame configured!");
        return -1;
    }

    if (m_exportEndLocation == TTimeRef::INVALID) {
        TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "ExportSpecification: No end frame configured!");
        return -1;
    }

    if (get_export_start_location() > get_export_end_location()) {
        tInformUser().warning(tr("Export start frame starts beyond export end frame!!"));
        return -1;
    }

    if (m_exportDir.isEmpty()) {
        TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "ExportSpecification: No export dir configured!");
        return -1;
    }

    if (m_exportFileName.isEmpty()) {
        TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "ExportSpecification: No name configured!");
        return -1;
    }
    if (m_fileFormat == TraversoDAW::FileFormat::UNKNOWN) {
        TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "ExportSpecification: No File Format set");
        return -1;
    }
#if !defined(M4A_ENCODE_SUPPORT)
    // Before the export starts, not halfway: this build has no AAC encoder
    // (the writer refuses it too, TAudioWriter::open())
    if (m_writerType == TraversoDAW::WriterType::M4A) {
        tInformUser().warning(tr("This build of Traverso cannot write M4A (AAC) files"));
        return -1;
    }
#endif

    return 1;
}

int TExportSpecification::prepare_export(TProject* project)
{
    Q_UNUSED(project);

    QDir dir(m_exportDir);
    if (!m_exportDir.isEmpty() && !dir.exists()) {
        if (!dir.mkpath(m_exportDir)) {
            QString message = tr("Creating Export Directory failed: %1").arg(m_exportDir);
            tInformUser().warning(message);
            emit exportMessage(message);
            return -1;
        }
    }

    m_cancelExportRequested = false;

    return 0;
}

void TExportSpecification::set_recording_state(int recordingState)
{
    PMESG("Setting Recording State to %d", recordingState);
    m_recordingState = recordingState;
}

void TExportSpecification::set_sample_rate(uint sampleRate)
{
    Q_ASSERT(sampleRate != 0);
    m_sampleRate = sampleRate;
}

void TExportSpecification::set_channel_count(uint channelCount)
{
    Q_ASSERT(channelCount != 0);
    m_channelCount = channelCount;
}

void TExportSpecification::set_block_size(uint blockSize)
{
    Q_ASSERT(blockSize > 0);
    // A power of two
    Q_ASSERT(std::has_single_bit(blockSize));
    m_blockSize = blockSize;
}

void TExportSpecification::set_export_start_location(const TTimeRef &startLocation)
{
    m_exportStartLocation = startLocation;
    m_exportLocation = startLocation;
    m_progress = 0;
    PMESG("Setting Start Location to (minutes:seconds) %s", qUtf8Printable(TTimeRef::timeref_to_ms_3(m_exportStartLocation)));
}

void TExportSpecification::set_export_end_location(const TTimeRef &endLocation)
{
    m_exportEndLocation = endLocation;
    PMESG("Setting End Location to (minutes:seconds) %s", qUtf8Printable(TTimeRef::timeref_to_ms_3(m_exportEndLocation)));
}

void TExportSpecification::add_exported_range(const TTimeRef& time)
{
    m_exportLocation += time;

    int progress = int(((get_export_location() - get_export_start_location()) / get_export_length()) * 100);
    if (progress > m_progress) {
        m_progress = progress;
        emit progressChanged(m_progress);
    }
}

void TExportSpecification::add_sheet_to_export(TSheet *sheet)
{
    if (m_sheetsToExport.contains(sheet)) {
        return;
    }
    m_sheetsToExport.append(sheet);
}

void TExportSpecification::clear_sheets_to_export()
{
    m_sheetsToExport.clear();
}

void TExportSpecification::set_file_format(TraversoDAW::FileFormat fileFormat)
{
    m_fileFormat = fileFormat;

    switch (m_fileFormat) {
    case TraversoDAW::FileFormat::WAVPACK:
        m_writerType = TraversoDAW::WriterType::WAVPACK;
        // WavPack natively encapsulates floating point streams, enforce it as default
        set_data_format(TraversoDAW::DataFormat::FLOAT);
        break;

    case TraversoDAW::FileFormat::WAV:
    case TraversoDAW::FileFormat::AIFF:
    case TraversoDAW::FileFormat::W64:
    case TraversoDAW::FileFormat::FLAC:
    case TraversoDAW::FileFormat::OGG:
    case TraversoDAW::FileFormat::MP3:
    case TraversoDAW::FileFormat::RAW:
    default:
        m_writerType = TraversoDAW::WriterType::SNDFILE;
        break;
    }

    PMESG("TExportSpecification: FileFormat updated, automatically synced WriterType to %s",
          qUtf8Printable(writer_type_to_string(m_writerType)));
}

QString TExportSpecification::get_file_extension() const
{
    switch (m_fileFormat) {
    case TraversoDAW::FileFormat::WAV:      return ".wav";
    case TraversoDAW::FileFormat::AIFF:     return ".aiff";
    case TraversoDAW::FileFormat::W64:      return ".w64";
    case TraversoDAW::FileFormat::FLAC:     return ".flac";
    case TraversoDAW::FileFormat::OGG:      return ".ogg";
    case TraversoDAW::FileFormat::MP3:      return ".mp3";
    case TraversoDAW::FileFormat::WAVPACK:  return ".wv";
    default:                                return ".raw";
    }
}

void TExportSpecification::set_data_format(TraversoDAW::DataFormat format)
{
    m_dataFormat = format;
}

int TExportSpecification::get_bit_depth() const
{
    return TAudioEncodeSettings::bit_depth(m_dataFormat);
}

QString TExportSpecification::format_to_string(TraversoDAW::BitrateMode format)
{
    switch (format) {
    case TraversoDAW::BitrateMode::CONSTANT:
        return QStringLiteral("cbr");
    case TraversoDAW::BitrateMode::AVERAGE:
        return QStringLiteral("abr");
    case TraversoDAW::BitrateMode::VARIABLE:
        return QStringLiteral("vbr");
    case TraversoDAW::BitrateMode::UNKNOWN:
    default:
        return QStringLiteral("Unknown Format");
    }
}

QString TExportSpecification::writer_type_to_string(TraversoDAW::WriterType writerType)
{
    switch (writerType) {
    case TraversoDAW::WriterType::SNDFILE:
        return QStringLiteral("sndfile");
    case TraversoDAW::WriterType::WAVPACK:
        return QStringLiteral("wavpack");
    case TraversoDAW::WriterType::M4A:
        return QStringLiteral("m4a");
    default:
        return QStringLiteral("unknown");
    }
}

TraversoDAW::BitrateMode TExportSpecification::string_to_format(const QString &option)
{
    if (option == QStringLiteral("cbr")) {
        return TraversoDAW::BitrateMode::CONSTANT;
    }
    if (option == QStringLiteral("abr")) {
        return TraversoDAW::BitrateMode::AVERAGE;
    }
    if (option == QStringLiteral("vbr")) {
        return TraversoDAW::BitrateMode::VARIABLE;
    }

    return TraversoDAW::BitrateMode::UNKNOWN;
}

void TExportSpecification::set_resampler(TResamplerChoice resampler)
{
    Q_ASSERT(TAudioResampler::qualities(resampler.backend).contains(resampler.quality));
    m_resampler = resampler;
}

void TExportSpecification::set_is_cd_export(bool cdExport)
{
    m_isCdExport = cdExport;
}

void TExportSpecification::set_export_dir(const QString &dir)
{
    m_exportDir = dir;
}

void TExportSpecification::set_export_file_name(const QString &fileName)
{
    m_exportFileName = fileName;
}

void TExportSpecification::set_dither_type(const GDitherType &ditherType)
{
    m_ditherType = ditherType;
}

void TExportSpecification::cancel_export()
{
    m_cancelExportRequested = true;
}

void TExportSpecification::print_export_data() const
{
    PMESG("Starting export, samplerate %d, bitdepth %d, file extension %s",
          get_sample_rate(),
          get_bit_depth(),
          qUtf8Printable(get_file_extension()) );
    PMESG("Export State:");
    PMESG("Start Location %s", qUtf8Printable(TTimeRef::timeref_to_hms(m_exportStartLocation)));
    PMESG("End Location (minutes:seconds) %s", qUtf8Printable(TTimeRef::timeref_to_ms(m_exportEndLocation)));
    PMESG("Export Location (minutes:seconds) %s", qUtf8Printable(TTimeRef::timeref_to_ms(m_exportLocation)));
    PMESG("Sample Rate %d", m_sampleRate);
    PMESG("Channel Count %d", m_channelCount);
    PMESG("Block Size %d", m_blockSize);
    PMESG("Writer Type %s", qUtf8Printable(writer_type_to_string(m_writerType)));
    PMESG("Is CD Export %s", m_isCdExport ? "Yes" : "No");
    PMESG("Export Directory %s", qUtf8Printable(m_exportDir));
    PMESG("Export File Name %s", qUtf8Printable(m_exportFileName));
    PMESG("Progress %d", m_progress);
}


TTimeRef TExportSpecification::get_export_length() const
{
    return m_exportEndLocation - m_exportStartLocation;
}

TAudioEncodeSettings TExportSpecification::get_encode_settings() const
{
    TAudioEncodeSettings settings;
    settings.writerType = get_writer_type();
    settings.fileFormat = get_file_format();
    settings.dataFormat = get_data_format();
    settings.channelCount = get_channel_count();
    settings.sampleRate = get_sample_rate();
    settings.blockSize = get_block_size();
    settings.resampler = get_resampler();
    settings.ditherType = get_dither_type();

    // The writer's options, as the export dialog and the recording settings
    // name them
    if (get_writer_type() == TraversoDAW::WriterType::WAVPACK) {
        const QString compression = get_extra_format(QStringLiteral("quality"), QStringLiteral("high"));
        settings.wavpackCompression = compression == QLatin1String("fast")        ? TAudioEncodeSettings::WavPackCompression::Fast
                                      : compression == QLatin1String("very_high") ? TAudioEncodeSettings::WavPackCompression::VeryHigh
                                                                                  : TAudioEncodeSettings::WavPackCompression::High;
        settings.wavpackSkipWvx = get_extra_format(QStringLiteral("skip_wvx")) == QLatin1String("true");
    }
    return settings;
}

GDitherSize TExportSpecification::get_dither_size() const
{
    return TAudioEncodeSettings::dither_size(m_dataFormat);
}

void TExportSpecification::add_extra_format(const QString& key, const QString& value)
{
    Q_ASSERT(!key.isEmpty());
    m_extraFormat[key] = value;
}

QString TExportSpecification::get_extra_format(const QString& key, const QString& defaultValue) const
{
    return m_extraFormat.value(key, defaultValue);
}

bool TExportSpecification::has_extra_format(const QString& key) const
{
    return m_extraFormat.contains(key);
}

void TExportSpecification::clear_extra_formats()
{
    m_extraFormat.clear();
}


nframes_t TExportSpecification::get_remaining_export_frames() const
{
    // None left also when the location went past the end (a rate that does
    // not divide the universal rate adds up rounded per block)
    return TTimeRef::to_nframes(std::max<qint64>(0, TTimeRef::frame_nearest(m_exportEndLocation - m_exportLocation, m_sampleRate)));
}

int TExportSpecification::create_cdrdao_toc(TProject* project, TExportSpecification* spec)
{
    QList<TSheet* > sheets = spec->get_sheets_to_export();
    if (sheets.isEmpty()) {
        return -1;
    }

    QString filename = spec->get_export_dir();

    if (sheets.size() > 1) {
        // filename of the toc file is "project-name.toc"
        filename += project->get_title() + ".toc";
    } else {
        // filename of the toc file is "sheet-name.toc"
        filename += project->get_title() + ".toc";
    }

    QString output;

    output += "CD_DA\n\n";
    output += "CD_TEXT {\n";

    output += "  LANGUAGE_MAP {\n    0 : EN\n  }\n\n";

    output += "  LANGUAGE 0 {\n";
    output += "    TITLE \"" + project->get_title() +  "\"\n";
    output += "    PERFORMER \"" + project->get_performer() + "\"\n";
    output += "    DISC_ID \"" + project->get_discid() + "\"\n";
    output += "    UPC_EAN \"" + project->get_upc_ean() + "\"\n\n";

    output += "    ARRANGER \"" + project->get_arranger() + "\"\n";
    output += "    SONGWRITER \"" + project->get_songwriter() + "\"\n";
    output += "    MESSAGE \"" + project->get_message() + "\"\n";
    output += "    GENRE \"" + QString::number(project->get_genre()) + "\"\n  }\n}\n\n";


    bool pregap = true;

    foreach(TSheet* sheet, sheets) {
        output += sheet->get_timeline_ruler()->get_cdrdao_tracklist(spec, spec->get_export_file_name(), pregap);
        pregap = false; // only add the pregap at the first sheet
    }


    if (spec->writeToc) {
        spec->tocFileName = filename;

        QFile file(filename);

        if (file.open(QFile::WriteOnly)) {
            TLog::write_printf(TLogLevel::Debug, TLOG_FILE_CATEGORY, "Saving cdrdao toc-file to %s", qUtf8Printable(spec->tocFileName));
            QTextStream out(&file);
            out << output;
            file.close();
        }
    }

    spec->cdrdaoToc = output;

    return 1;
}

/* returns the total time of the data that will be written to CD */
TTimeRef TExportSpecification::get_cd_totaltime(TExportSpecification* spec)
{
    Q_UNUSED(spec);
    // TODO
    // Used to be called from CDWritingDialog::sheet_mode_changed(bool b)
    // but that one needs investigation as well on usefullness.
    TTimeRef totalTime = TTimeRef();

    // spec->renderpass = TExportSpecification::CREATE_CDRDAO_TOC;

    // if (spec->allSheets) {
    //     foreach(Sheet* sheet, m_sheets) {
    //         sheet->prepare_export(spec);
    //         totalTime += spec->totalTime;
    //     }
    // } else {
    //     Sheet* sheet = qobject_cast<Sheet*>(get_current_session());
    //     if (sheet) {
    //         sheet->prepare_export(spec);
    //         totalTime += spec->totalTime;
    //     }
    // }

    return totalTime;
}
