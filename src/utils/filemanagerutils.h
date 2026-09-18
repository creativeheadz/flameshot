// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QString>

namespace FileManagerUtils {

/**
 * @brief Open the system file manager with `path` selected.
 *
 * Selecting the file (as opposed to only opening the folder that contains it)
 * is what makes it possible to immediately drag the capture into another
 * application. If the file manager cannot select it, the containing folder is
 * opened instead.
 */
void revealFile(const QString& path);

} // namespace FileManagerUtils
