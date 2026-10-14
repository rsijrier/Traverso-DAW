#ifndef TTIMEREF_H
#define TTIMEREF_H

#include <chrono>
#include "Utils.h"
#include "defines.h"
#include "qdatetime.h"



class TTimeRef {

public:
    // Universal samplerate for the frequences 22050, 8000, 32000, 44100, 48000, 88200, 96000 and 192000 Hz
    static const qint64 UNIVERSAL_SAMPLE_RATE = 28224000;
    static const qint64 ONE_HOUR_UNIVERSAL_SAMPLE_RATE = 101606400000;
    static const qint64 ONE_MINUTE_UNIVERSAL_SAMPLE_RATE = 1693440000;
    static const long long INVALID = LLONG_MIN;

    TTimeRef();
    explicit TTimeRef(int position) : TTimeRef(qint64(position)) {}
    explicit TTimeRef(qint64 position);
    explicit TTimeRef(double position);
    explicit TTimeRef(nframes_t frame, uint rate);
    // TODO
    // Upgrade internal data structure for nframes to uint64_t ?
    // Pipewire is using this type internally to track time
    // FIXME
    // m_universalFrame can overflow now easily...
    explicit TTimeRef(uint64_t frame, uint rate);

    explicit TTimeRef(qreal frame, uint rate);

    static TTimeRef max_length();

    static QString timeref_to_ms(const TTimeRef& ref);
    static QString timeref_to_ms_2 (const TTimeRef& ref);
    static QString timeref_to_ms_3 (const TTimeRef& ref);
    static QString timeref_to_hms(const TTimeRef& ref);

    static QString timeref_to_text(const TTimeRef& ref, qint64 scalefactor);

    static QString timeref_to_cd(const TTimeRef& ref);
    static QString timeref_to_cd_including_hours(const TTimeRef& ref);

    static TTimeRef msms_to_timeref(QString str);
    static TTimeRef cd_to_timeref(QString str);
    static TTimeRef cd_to_timeref_including_hours(QString str);

    static TTimeRef qtime_to_timeref(const QTime& time);
    // Rounds to the nearest universal frame. Out of range values are clamped.
    static TTimeRef seconds_to_timeref(double seconds);
    static QTime timeref_to_qtime(const TTimeRef& ref);


    static inline long get_milliseconds_since_epoch()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() / 1000000;
    }

    static inline long get_microseconds_since_epoch()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count() / 1000;
    }

    static inline long get_nanoseconds_since_epoch()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }

    static nframes_t to_frame(const TTimeRef& timeRef, uint sampleRate) {
        long double duration = timeRef.universal_frame() * sampleRate;
        return  std::round(duration / UNIVERSAL_SAMPLE_RATE);
    }


    void add_frames(nframes_t frames, uint rate) {
        m_universalFrame += ((UNIVERSAL_SAMPLE_RATE / rate) * frames);
    }

    qint64 universal_frame() const {return m_universalFrame;}

    TTimeRef& operator =(const qint64 value) {
        m_universalFrame = value;
        return *this;
    }

    TTimeRef& operator =(double value) {
        m_universalFrame = qint64(value);
        return *this;
    }

    friend int operator!=(const TTimeRef& left, const TTimeRef& right) {
        return left.m_universalFrame != right.m_universalFrame;
    }

    friend int operator!=(const TTimeRef& left, qint64 right) {
        return left.m_universalFrame != right;
    }

    friend int operator!=(const TTimeRef& left, double right) {
        return left.m_universalFrame != qint64(right);
    }

    friend TTimeRef operator-(const TTimeRef& left, const TTimeRef& right) {
        return TTimeRef(left.m_universalFrame - right.m_universalFrame);
    }

    friend TTimeRef operator-(const TTimeRef& left, qint64 right) {
        return TTimeRef(left.m_universalFrame - right);
    }

    friend TTimeRef operator-(const TTimeRef& left, double right) {
        return TTimeRef(left.m_universalFrame - qint64(right));
    }

    friend TTimeRef& operator-=(TTimeRef& left, const TTimeRef& right) {
        left.m_universalFrame -= right.m_universalFrame;
        return left;
    }

    friend TTimeRef& operator-=(TTimeRef& left, qint64 right) {
        left.m_universalFrame -= right;
        return left;
    }

    friend TTimeRef& operator-=(TTimeRef& left, double right) {
        left.m_universalFrame -= qint64(right);
        return left;
    }

    friend TTimeRef operator+(const TTimeRef& left, const TTimeRef& right) {
        return TTimeRef(left.m_universalFrame + right.m_universalFrame);
    }

    friend TTimeRef operator+(const TTimeRef& left, qint64 right) {
        return TTimeRef(left.m_universalFrame + right);
    }

    friend TTimeRef operator+(const TTimeRef& left, double right) {
        return TTimeRef(left.m_universalFrame + qint64(right));
    }

    friend TTimeRef& operator+=(TTimeRef& left, const TTimeRef& right) {
        left.m_universalFrame += right.m_universalFrame;
        return left;
    }

    friend TTimeRef& operator+=(TTimeRef& left, qint64 right) {
        left.m_universalFrame += right;
        return left;
    }

    friend TTimeRef& operator+=(TTimeRef& left, double right) {
        left.m_universalFrame += qint64(right);
        return left;
    }

    friend qreal operator/(const TTimeRef& left, const TTimeRef& right) {
        Q_ASSERT(right.m_universalFrame != 0);
        return qreal(left.m_universalFrame) / right.m_universalFrame;
    }

    friend qreal operator/(const TTimeRef& left, const qint64 right) {
        Q_ASSERT(right != 0);
        return qreal(left.m_universalFrame) / right;
    }

    friend qreal operator/(const TTimeRef& left, double right) {
        Q_ASSERT(!TraversoDAW::Float::equals_0(right));
        return qreal(left.m_universalFrame) / qint64(right);
    }

    friend TTimeRef operator*(const qint64 left, TTimeRef& right) {
        return TTimeRef(left * right.m_universalFrame);
    }

    friend TTimeRef operator*(const TTimeRef& left, const TTimeRef& right) {
        return TTimeRef(left.m_universalFrame * right.m_universalFrame);
    }

    friend TTimeRef operator*(const TTimeRef& left, double right) {
        return TTimeRef(left.m_universalFrame * qint64(right));
    }

    friend int operator<(const TTimeRef& left, const TTimeRef& right) {
        return left.m_universalFrame < right.m_universalFrame;
    }

    friend int operator<(const TTimeRef& left, qint64 right) {
        return left.m_universalFrame < right;
    }

    friend int operator<(const TTimeRef& left, double right) {
        return left.m_universalFrame < qint64(right);
    }

    friend int operator<(double left, const TTimeRef& right) {
        return (qint64(left) < right.m_universalFrame);
    }

    friend int operator>(const TTimeRef& left, const TTimeRef& right) {
        return left.m_universalFrame > right.m_universalFrame;
    }

    friend int operator>(const TTimeRef& left, qint64 right) {
        return left.m_universalFrame > right;
    }

    friend int operator>(const TTimeRef& left, double right) {
        return left.m_universalFrame > qint64(right);
    }

    friend int operator>(double left, const TTimeRef& right) {
        return left > right.m_universalFrame;
    }

    friend int operator<=(const TTimeRef& left, const TTimeRef& right) {
        return left.m_universalFrame <= right.m_universalFrame;
    }

    friend int operator<=(const TTimeRef& left, qint64 right) {
        return left.m_universalFrame <= right;
    }

    friend int operator<=(const TTimeRef& left, double right) {
        return left.m_universalFrame <= qint64(right);
    }

    friend int operator>=(const TTimeRef& left, const TTimeRef& right) {
        return left.m_universalFrame >= right.m_universalFrame;
    }

    friend int operator>=(const TTimeRef& left, qint64 right) {
        return left.m_universalFrame >= right;
    }

    friend int operator>=(const TTimeRef& left, double right) {
        return left.m_universalFrame >= qint64(right);
    }

    friend int operator==(const TTimeRef& left, const TTimeRef& right) {
        return left.m_universalFrame == right.m_universalFrame;
    }

    friend int operator==(const TTimeRef& left, qint64 right) {
        return left.m_universalFrame == right;
    }

    friend int operator==(const TTimeRef& left, double right) {
        return left.m_universalFrame == qint64(right);
    }

private:
    qint64 m_universalFrame;
};

Q_DECLARE_METATYPE(TTimeRef);

#endif // TTIMEREF_H
