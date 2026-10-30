// SPDX-FileCopyrightText: 2026 Remon Sijrier
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The platform macros (Q_OS_*), in every translation unit: the build includes
// this header before anything else (traverso_compiler_settings). Without it a
// file that tests Q_OS_* before its first Qt header takes the wrong branch
// and nothing says so.
#include <QtCore/qsystemdetection.h>

#if !defined(Q_OS_LINUX) && !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
#error "Traverso builds on Linux, macOS and Windows"
#endif
