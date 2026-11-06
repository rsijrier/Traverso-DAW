// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TCurveSnapshot.h"

#include <QtGlobal>
#include "TFloatCompare.h"

#include <algorithm>
#include <cmath>

std::shared_ptr<const TCurveSnapshot> TCurveSnapshot::create(std::vector<Point> points, double defaultValue)
{
    // The constructor is private, so std::make_shared can't be used
    return std::shared_ptr<const TCurveSnapshot>(new TCurveSnapshot(std::move(points), defaultValue));
}

TCurveSnapshot::TCurveSnapshot(std::vector<Point> points, double defaultValue)
    : m_points(std::move(points))
    , m_defaultValue(defaultValue)
{
    solve();
}

void TCurveSnapshot::solve()
{
    const int npoints = int(m_points.size());

    // Up to two points get_vector() interpolates linearly
    if (npoints <= 2) {
        return;
    }

    m_segments.resize(size_t(npoints - 1));

    auto x = [this](int i) { return m_points[size_t(i)].when; };
    auto y = [this](int i) { return m_points[size_t(i)].value; };

    // Constrained first derivatives, see Kruger
    double lp0 = (x(1) - x(0)) / (y(1) - y(0));
    double lp1 = (x(2) - x(1)) / (y(2) - y(1));

    double fpone;
    if ((lp0 * lp1) < 0) {
        fpone = 0;
    } else {
        fpone = 2 / (lp1 + lp0);
    }

    double fplast = (3 * (y(1) - y(0)) / (2 * (x(1) - x(0)))) - (fpone * 0.5);

    for (int i = 1; i < npoints; ++i) {
        const double xdelta = x(i) - x(i - 1);
        const double ydelta = y(i) - y(i - 1);

        // Two points at the same when: a vertical step, keep the value of the
        // first point instead of dividing by zero
        if (TFloatCompare::equals_0(xdelta)) {
            m_segments[size_t(i - 1)] = {y(i - 1), 0.0, 0.0, 0.0};
            fplast = 0.0;
            continue;
        }

        double fpi;
        if (i == npoints - 1) {
            // last segment
            fpi = ((3 * ydelta) / (2 * xdelta)) - (fplast * 0.5);
        } else {
            double slope_before = ((x(i + 1) - x(i)) / (y(i + 1) - y(i)));
            double slope_after = (xdelta / ydelta);

            if ((slope_after * slope_before) < 0.0) {
                // slope changes sign
                fpi = 0.0;
            } else {
                fpi = 2 / (slope_before + slope_after);
            }
        }

        // Second derivatives at the left and right end of the segment
        const double xdelta2 = xdelta * xdelta;
        const double fractal = ((6 * ydelta) / xdelta2);
        const double fppL = (((-2 * (fpi + (2 * fplast))) / (xdelta))) + fractal;
        const double fppR = (2 * ((2 * fpi) + fplast) / xdelta) - fractal;

        // The cubic through both points with these second derivatives, relative
        // to the start of the segment
        Segment& segment = m_segments[size_t(i - 1)];
        segment.a = y(i - 1);
        segment.c = fppL / 2;
        segment.d = (fppR - fppL) / (6 * xdelta);
        segment.b = (ydelta - (segment.c * xdelta2) - (segment.d * xdelta2 * xdelta)) / xdelta;

        fplast = fpi;
    }
}

double TCurveSnapshot::evaluate(double x, TCurveLookupCache& cache) const
{
    const int npoints = int(m_points.size());
    Q_ASSERT(npoints > 2);

    int segment = cache.segment;

    if (segment < 0 || segment >= npoints - 1
        || x < m_points[size_t(segment)].when || x > m_points[size_t(segment) + 1].when) {

        auto before = [](const Point& point, double when) { return point.when < when; };

        // x is a point of the curve: its value
        auto lower = std::lower_bound(m_points.begin(), m_points.end(), x, before);
        if (lower != m_points.end() && lower->when == x) {
            cache.segment = -1;
            return lower->value;
        }

        segment = int(lower - m_points.begin()) - 1;
        segment = std::clamp(segment, 0, npoints - 2);
        cache.segment = segment;
    }

    const Segment& s = m_segments[size_t(segment)];
    const double u = x - m_points[size_t(segment)].when;

    return s.a + u * (s.b + u * (s.c + u * s.d));
}

void TCurveSnapshot::get_vector(double x0, double x1, float* vec, nframes_t veclen, TCurveLookupCache& cache) const T_REALTIME_SAFE
{
    const int npoints = int(m_points.size());

    if (npoints == 0) {
        for (nframes_t i = 0; i < veclen; ++i) {
            vec[i] = float(m_defaultValue);
        }
        return;
    }

    const Point& firstPoint = m_points.front();
    const Point& lastPoint = m_points.back();

    const double max_x = lastPoint.when;
    const double min_x = firstPoint.when;

    const double lx = std::max(min_x, x0);

    if (x1 < 0) {
        x1 = lastPoint.when;
    }

    const double hx = std::min(max_x, x1);

    const nframes_t original_veclen = veclen;

    if (x0 < min_x) {
        // Before the first point: the value of the first point
        double frac = (min_x - x0) / (x1 - x0);
        nframes_t subveclen = std::min(nframes_t(std::floor(veclen * frac)), veclen);

        for (nframes_t i = 0; i < subveclen; ++i) {
            vec[i] = float(firstPoint.value);
        }

        veclen -= subveclen;
        vec += subveclen;
    }

    if (veclen && x1 > max_x) {
        // After the last point: the value of the last point
        double frac = (x1 - max_x) / (x1 - x0);
        nframes_t subveclen = std::min(nframes_t(std::floor(original_veclen * frac)), veclen);

        for (nframes_t i = veclen - subveclen; i < veclen; ++i) {
            vec[i] = float(lastPoint.value);
        }

        veclen -= subveclen;
    }

    if (veclen == 0) {
        return;
    }

    if (npoints == 1) {
        for (nframes_t i = 0; i < veclen; ++i) {
            vec[i] = float(firstPoint.value);
        }
        return;
    }

    if (npoints == 2) {
        // Linear interpolation between the two points
        const double dx = (veclen > 1) ? (hx - lx) / (veclen - 1) : 0.0;
        const double whenDelta = lastPoint.when - firstPoint.when;
        const double slope = TFloatCompare::equals_0(whenDelta) ? 0.0 : (lastPoint.value - firstPoint.value) / whenDelta;
        const double yfrac = dx * slope;

        vec[0] = float(firstPoint.value + slope * (lx - firstPoint.when));

        for (nframes_t i = 1; i < veclen; ++i) {
            vec[i] = float(vec[i - 1] + float(yfrac));
        }
        return;
    }

    if (veclen == 1) {
        vec[0] = float(evaluate(lx, cache));
        return;
    }

    const double dx = (hx - lx) / veclen;
    double rx = lx;

    for (nframes_t i = 0; i < veclen; ++i, rx += dx) {
        vec[i] = float(evaluate(rx, cache));
    }
}

double TCurveSnapshot::value_at(double x, TCurveLookupCache& cache) const
{
    const int npoints = int(m_points.size());

    if (npoints == 0) {
        return m_defaultValue;
    }

    const Point& firstPoint = m_points.front();
    const Point& lastPoint = m_points.back();

    if (x <= firstPoint.when) {
        return firstPoint.value;
    }
    if (x >= lastPoint.when) {
        return lastPoint.value;
    }

    if (npoints == 2) {
        // Linear between the two points, their whens differ since x lies between them
        const double slope = (lastPoint.value - firstPoint.value) / (lastPoint.when - firstPoint.when);
        return firstPoint.value + slope * (x - firstPoint.when);
    }

    return evaluate(x, cache);
}

// The lowest and highest value seen in one column, with their positions
struct TColumnExtremes
{
    double low;
    double lowAt;
    double high;
    double highAt;

    TColumnExtremes(double value, double at) : low(value), lowAt(at), high(value), highAt(at) {}

    void add(double value, double at)
    {
        if (value < low) {
            low = value;
            lowAt = at;
        }
        if (value > high) {
            high = value;
            highAt = at;
        }
    }
};

void TCurveSnapshot::get_columns(double x0, double columnWidth, int count, TCurveColumn* columns, TCurveLookupCache& cache) const
{
    Q_ASSERT(columnWidth > 0.0);

    const int npoints = int(m_points.size());
    auto before = [](const Point& point, double when) { return point.when < when; };

    // The first point at or after the current column start
    int nextPoint = int(std::lower_bound(m_points.begin(), m_points.end(), x0, before) - m_points.begin());

    double entry = value_at(x0, cache);

    for (int i = 0; i < count; ++i) {
        const double start = x0 + i * columnWidth;
        const double end = start + columnWidth;
        const double exit = value_at(end, cache);

        // The constrained spline never turns between two points: the slope at
        // a point is at most twice the smaller slope of the segments next to
        // it, which keeps every segment monotone. So the lowest and highest
        // value of a column are at its edges or at points inside it.
        TColumnExtremes extremes(entry, 0.0);
        extremes.add(exit, 1.0);

        // The points inside the column
        while (nextPoint < npoints && m_points[size_t(nextPoint)].when < end) {
            const Point& point = m_points[size_t(nextPoint)];
            extremes.add(point.value, (point.when - start) / columnWidth);
            ++nextPoint;
        }

        TCurveColumn& column = columns[i];
        column.entry = entry;
        column.exit = exit;

        if (extremes.lowAt <= extremes.highAt) {
            column.firstExtreme = extremes.low;
            column.firstExtremeAt = extremes.lowAt;
            column.secondExtreme = extremes.high;
            column.secondExtremeAt = extremes.highAt;
        } else {
            column.firstExtreme = extremes.high;
            column.firstExtremeAt = extremes.highAt;
            column.secondExtreme = extremes.low;
            column.secondExtremeAt = extremes.lowAt;
        }

        entry = exit;
    }
}
