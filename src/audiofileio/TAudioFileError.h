// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TTypes.h"

#include <QString>

#include <expected>
#include <optional>

/**
 * @brief Why a call into the audio file library failed
 * (doc/design/audiofileio-errors.md).
 *
 * The code is what a caller decides on and builds the user's message from
 * (translated); the detail is the library's own words, English, a technical
 * addition. Made on the IO threads only: it allocates (the detail), so it
 * never comes near the audio thread.
 */
struct TAudioFileError
{
    enum class Code {
        // The file is not there (only open() of a reader)
        NotFound,
        // It is there but cannot be opened: no permission, no decoder or
        // encoder for its format, a damaged header, no resampler
        CannotOpen,
        ReadFailed,
        SeekFailed,
        WriteFailed
    };

    Code code;
    QString detail;
};

/**
 * @brief What a call that reads or writes a block did: the frames, and the
 * error of this call when it failed.
 *
 * Both at once: a read can give its first frames and then fail. A caller
 * that only needs the count writes `.frames`; reading the error is the
 * caller's responsibility:
 *
 *     auto [frames, error] = reader->read(buffer, location, count);
 */
struct [[nodiscard]] TAudioFileResult
{
    nframes_t frames{0};
    std::optional<TAudioFileError> error;
};

// A call that either succeeds or fails, and why (open, close, seek, the
// output rate)
using TAudioFileStatus = std::expected<void, TAudioFileError>;
