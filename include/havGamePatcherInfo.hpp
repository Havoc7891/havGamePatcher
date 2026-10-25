// SPDX-License-Identifier: MIT

#ifndef HAVGAMEPATCHERINFO_HPP
#define HAVGAMEPATCHERINFO_HPP

// Shared by the About dialog and Windows executable properties.
// Keep the version aligned with assemblyIdentity in havGamePatcher.manifest.
#define HAVGAMEPATCHER_NAME "havGamePatcher"
#define HAVGAMEPATCHER_VERSION 0, 1, 0, 0
#define HAVGAMEPATCHER_DISPLAY_VERSION "0.1.0"
#define HAVGAMEPATCHER_VERSION_STRING HAVGAMEPATCHER_DISPLAY_VERSION ".0"
#define HAVGAMEPATCHER_AUTHOR "René Nicolaus"
#define HAVGAMEPATCHER_WEBSITE "https://havoc.de"
#define HAVGAMEPATCHER_SOURCE_URL "https://github.com/Havoc7891/havGamePatcher"
#define HAVGAMEPATCHER_EMAIL "havgamepatcher@havoc.de"
#define HAVGAMEPATCHER_COPYRIGHT "Copyright © 2026 " HAVGAMEPATCHER_AUTHOR
#define HAVGAMEPATCHER_DESCRIPTION HAVGAMEPATCHER_NAME " - Classic game compatibility patcher"
#define HAVGAMEPATCHER_COMMENTS                                                                    \
  "Creates patched copies of supported classic Windows game executables."

#endif // HAVGAMEPATCHERINFO_HPP
