// SPDX-FileCopyrightText: 2007-2026 Ben Levitt, Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TAudioDecoder.h"

#include <expected>
#include <memory>

// Which decoder reads a file, and opening it: the one place that knows all
// the decoders.

/**
 * @brief What every decoder's open() does: make the decoder and start it, the
 * decoder or why not. A friend of each decoder, whose constructor and start()
 * are its own.
 */
struct TAudioDecoderFactory
{
    template <typename Decoder>
    static std::expected<std::unique_ptr<Decoder>, TAudioFileError> open(const QString& fileName, std::unique_ptr<TByteSource> source, const TByteSourceOpener& opener)
    {
        // Not make_unique: the constructor is private, this struct is its friend
        std::unique_ptr<Decoder> decoder(new Decoder(fileName, std::move(source), opener));
        if (TAudioFileStatus started = decoder->start(); !started) {
            return std::unexpected(std::move(started.error()));
        }
        return decoder;
    }

    // Whether one of the decoders can read fileName. The file is only probed: no
    // decoder is kept, nothing is logged.
    static bool can_read(const QString& fileName);

    // The decoder for fileName, open, or why there is none: NotFound when the
    // file is not there, else CannotOpen (no permission, no decoder for its
    // format, the decoder's own reason). Every file goes by its content, see
    // find_decoder(). opener gives the file's bytes, also to the probes (tests).
    [[nodiscard]] static std::expected<std::unique_ptr<TAudioDecoder>, TAudioFileError> open_file(const QString& fileName, const TByteSourceOpener& opener = {});
};
