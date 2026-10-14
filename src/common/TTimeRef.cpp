#include "TTimeRef.h"

#include <QLocale>
#include <QRegularExpression>

#include <limits.h>
#include <cmath>

TTimeRef::TTimeRef() {
    m_universalFrame = 0;
}

TTimeRef::TTimeRef(nframes_t frame, uint rate) {
    Q_ASSERT(rate != 0);
    m_universalFrame = (TTimeRef::UNIVERSAL_SAMPLE_RATE / rate) * frame;
}

TTimeRef::TTimeRef(uint64_t frame, uint rate) {
    Q_ASSERT(rate != 0);
    // FIXME
    // m_universalFrame is also 64 bit, now it can overflow!
    m_universalFrame = (TTimeRef::UNIVERSAL_SAMPLE_RATE / rate) * frame;
}

TTimeRef::TTimeRef(qreal frame, uint rate) {
    m_universalFrame = qint64((qreal(UNIVERSAL_SAMPLE_RATE) / rate) * frame);
}

TTimeRef TTimeRef::max_length()
{
    return TTimeRef(LLONG_MAX);
}

TTimeRef::TTimeRef(qint64 position)
    : m_universalFrame(position)
{

}

TTimeRef::TTimeRef(double position)
    : m_universalFrame(std::floor(position))
{

}
QString TTimeRef::timeref_to_hms(const TTimeRef& ref)
{
    if (ref == TTimeRef::INVALID) {
        return QString("-- : -- : --");
    }

    qint64 remainder;
    int hours, mins, secs;

    qint64 universalframe = ref.universal_frame();

    hours = (int) (universalframe / TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE);
    remainder = qint64(universalframe - (hours * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE));
    mins = (int) (remainder / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE ));
    remainder -= mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE;
    secs = (int) (remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE);
    QString spos("%1:%2:%3");
    return spos.arg(hours, 2, 10, QLatin1Char('0')).arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0'));

}

QString TTimeRef::timeref_to_ms(const TTimeRef& ref)
{
    if (ref == TTimeRef::INVALID) {
        return QString("-- : --");
    }

    qint64 remainder;
    int mins, secs;

    qint64 universalframe = ref.universal_frame();

    mins = (int) (universalframe / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE ));
    remainder = (long unsigned int) (universalframe - (mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE));
    secs = (int) (remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE);
    QString spos("%1:%2");
    return spos.arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0'));
}

// TTimeRef to MM:SS.99 (hundredths)
QString TTimeRef::timeref_to_ms_2 (const TTimeRef& ref)
{
    if (ref == TTimeRef::INVALID) {
        return QString("-- : -- . --");
    }

    qint64 remainder;
    int mins, secs, frames;

    qint64 universalframe = ref.universal_frame();

    mins = universalframe / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    remainder = universalframe - ( mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    secs = remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    remainder -= secs * TTimeRef::UNIVERSAL_SAMPLE_RATE;
    frames = remainder * 100 / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    QString spos("%1:%2%3%4");
    return spos.arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0')).arg(QLocale::system().decimalPoint()).arg(frames, 2, 10, QLatin1Char('0'));
}

// TTimeRef to MM:SS.999 (ms)
QString TTimeRef::timeref_to_ms_3(const TTimeRef& ref)
{
    if (ref == TTimeRef::INVALID) {
        return QString("-- : -- . ---");
    }

    qint64 remainder;
    int mins, secs, frames;

    qint64 universalframe = ref.universal_frame();
    QString spos("%1:%2%3%4");
    if (universalframe < 0) {
        universalframe *= -1;
        spos.prepend("-");
    }

    mins = universalframe / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    remainder = universalframe - ( mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    secs = remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    remainder -= secs * TTimeRef::UNIVERSAL_SAMPLE_RATE;
    frames = remainder * 1000 / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    return spos.arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0')).arg(QLocale::system().decimalPoint()).arg(frames, 3, 10, QLatin1Char('0'));
}

// Frame to MM:SS,75 (75ths of a second, for CD burning)
QString TTimeRef::timeref_to_cd (const TTimeRef& ref)
{
    if (ref == TTimeRef::INVALID) {
        return QString("--:--:--");
    }

    qint64 remainder;
    int mins, secs, frames;

    qint64 universalframe = ref.universal_frame();

    mins = universalframe / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    remainder = universalframe - ( mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    secs = remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    remainder -= secs * TTimeRef::UNIVERSAL_SAMPLE_RATE;
    frames = remainder * 75 / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    QString spos("%1:%2:%3");
    return spos.arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0')).arg(frames, 2, 10, QLatin1Char('0'));
}

// Frame to HH:MM:SS,75 (75ths of a second, for CD burning)
QString TTimeRef::timeref_to_cd_including_hours (const TTimeRef& ref)
{
    if (ref == TTimeRef::INVALID) {
        return QString("-- : -- : -- , --");
    }

    qint64 remainder;
    int hours, mins, secs, frames;

    qint64 universalframe = ref.universal_frame();

    hours = int(universalframe / TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE);
    remainder = qint64(universalframe - (hours * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE));
    mins = (int) (remainder / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE ));
    remainder -= mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE;
    secs = (int) (remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE);
    remainder -= secs * TTimeRef::UNIVERSAL_SAMPLE_RATE;
    frames = remainder * 75 / TTimeRef::UNIVERSAL_SAMPLE_RATE;

    QString spos("%1:%2:%3,%4");
    return spos.arg(hours, 2, 10, QLatin1Char('0')).arg(mins, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0')).arg(frames, 2, 10, QLatin1Char('0'));
}

QString TTimeRef::timeref_to_text(const TTimeRef & ref, qint64 scalefactor)
{
    if (scalefactor >= 512*640) {
        return TTimeRef::timeref_to_ms_2(ref);
    } else {
        return TTimeRef::timeref_to_ms_3(ref);
    }
}

TTimeRef TTimeRef::qtime_to_timeref(const QTime & time)
{
    TTimeRef ref(time.hour() * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE + time.minute() * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE + time.second() * TTimeRef::UNIVERSAL_SAMPLE_RATE + (time.msec() * TTimeRef::UNIVERSAL_SAMPLE_RATE) / 1000);
    return ref;
}

TTimeRef TTimeRef::seconds_to_timeref(double seconds)
{
    Q_ASSERT(std::isfinite(seconds));
    if (!std::isfinite(seconds)) {
        return TTimeRef();
    }

    // double(LLONG_MAX) is exactly 2^63, every smaller double fits in a qint64.
    // LLONG_MIN is reserved for INVALID.
    const double universalFrame = seconds * UNIVERSAL_SAMPLE_RATE;
    if (universalFrame >= double(LLONG_MAX)) {
        return TTimeRef(qint64(LLONG_MAX));
    }
    if (universalFrame <= -double(LLONG_MAX)) {
        return TTimeRef(qint64(-LLONG_MAX));
    }

    return TTimeRef(qint64(std::llround(universalFrame)));
}

QTime TTimeRef::timeref_to_qtime(const TTimeRef& ref)
{
    qint64 remainder;
    int hours, mins, secs, msec;

    qint64 universalframe = ref.universal_frame();

    hours = universalframe / (TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE);
    remainder = universalframe - (hours * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE);
    mins = remainder / ( TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    remainder = remainder - (mins * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE );
    secs = remainder / TTimeRef::UNIVERSAL_SAMPLE_RATE;
    remainder -= secs * TTimeRef::UNIVERSAL_SAMPLE_RATE;
    msec = remainder * 1000 / TTimeRef::UNIVERSAL_SAMPLE_RATE;

    QTime time(hours, mins, secs, msec);
    return time;
}


TTimeRef TTimeRef::msms_to_timeref(QString str)
{
    TTimeRef out;
    static QRegularExpression expression("[;,.:]");
    QStringList lst = str.simplified().split(expression, Qt::SkipEmptyParts);

    if (lst.size() >= 1) out += TTimeRef(lst.at(0).toInt() * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 2) out += TTimeRef(lst.at(1).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 3) out += TTimeRef(lst.at(2).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE / 1000);

    return out;
}

TTimeRef TTimeRef::cd_to_timeref(QString str)
{
    TTimeRef out;
    static QRegularExpression expression("[;,.:]");
    QStringList lst = str.simplified().split(expression, Qt::SkipEmptyParts);

    if (lst.size() >= 1) out += TTimeRef(lst.at(0).toInt() * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 2) out += TTimeRef(lst.at(1).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 3) out += TTimeRef(lst.at(2).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE / 75);

    return out;
}

TTimeRef TTimeRef::cd_to_timeref_including_hours(QString str)
{
    TTimeRef out;
    static QRegularExpression expression("[;,.:]");
    QStringList lst = str.simplified().split(expression, Qt::SkipEmptyParts);

    if (lst.size() >= 1) out += TTimeRef(lst.at(0).toInt() * TTimeRef::ONE_HOUR_UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 2) out += TTimeRef(lst.at(1).toInt() * TTimeRef::ONE_MINUTE_UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 3) out += TTimeRef(lst.at(2).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE);
    if (lst.size() >= 4) out += TTimeRef(lst.at(3).toInt() * TTimeRef::UNIVERSAL_SAMPLE_RATE / 75);

    return out;
}

