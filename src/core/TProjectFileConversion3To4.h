// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Project file version 3 to version 4 (doc/design/object-ids.md, § Version
// 4): every object gets a new id, every reference names it by that id, the
// old ids leave the file. Pure DOM work, no file and no project, so a test
// can run it headless.

#include <QDomDocument>
#include <QStringList>
#include <QUuid>

struct TProjectFileConversion3To4
{
    struct Result
    {
        // Dropped references and other repairs, one line each, for the log
        QStringList warnings;
        // "id" attributes still in the document after the conversion: a file
        // the converter does not know, not to be loaded
        int leftOverIds{0};
    };

    // Precondition (assert): the document is version 3. The UUID is given so
    // that a test is deterministic. A template (no project id) gets no uuid
    // and no creation date
    static Result convert(QDomDocument& doc, const QUuid& uuid);
};
