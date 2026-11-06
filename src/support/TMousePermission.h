// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QtGlobal>

#ifdef Q_OS_MAC
#include <ApplicationServices/ApplicationServices.h>
#endif

// Whether Traverso may move the mouse pointer: on macOS only with the user's
// accessibility permission; the first refusal opens the settings to ask for
// it, once
inline bool t_may_move_mouse()
{
#ifdef Q_OS_MAC
    static bool didShowAlert = false;

    if (AXIsProcessTrusted()) {
        return true;
    }
    qWarning("Accessibility permissions required to move the mouse position.");
    if (!didShowAlert) {
        CFStringRef keys[] = { kAXTrustedCheckOptionPrompt };
        CFBooleanRef values[] = { kCFBooleanTrue };
        CFDictionaryRef options = CFDictionaryCreate(NULL, (const void **)keys, (const void **)values, 1, &kCFCopyStringDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        AXIsProcessTrustedWithOptions(options);
        didShowAlert = true;
    }
    return false;
#else
    return true;
#endif
}
