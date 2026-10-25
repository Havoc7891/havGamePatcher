# Third-party notices

These notices cover the Windows x64 release of havGamePatcher built with
Microsoft Visual C++ and the dependency configuration in `CMakeLists.txt`.
Third-party components retain their own licenses and copyrights. The project
license applies to havGamePatcher's original code. It is provided as `LICENSE`
in the source repository and `LICENSE.txt` in the release package.

Keep this file and the accompanying `licenses` directory with the release
package.

## wxWidgets 3.3.4

havGamePatcher statically links the wxWidgets core and base libraries for
its Windows user interface.

Copyright (c) 1992-2026 Julian Smart, Vadim Zeitlin, Stefan Csomor, Robert
Roebling, and other members of the wxWidgets team.

Portions (c) 1996 Artificial Intelligence Applications Institute.

- Project: [wxWidgets](https://www.wxwidgets.org/)
- Source: [v3.3.4](https://github.com/wxWidgets/wxWidgets/tree/v3.3.4),
  commit `6a42a4b999b127cf28a8b5fe686da6b8ca93a9b8`
- License: wxWindows Library Licence, Version 3.1, which includes an
  exception for distributing binary works under the distributor's own terms
- Full text: [wxWidgetsLicense.txt](licenses/wxWidgetsLicense.txt)
- Referenced GNU Library General Public License, Version 2:
  [wxWidgetsLgpl.txt](licenses/wxWidgetsLgpl.txt)

These license files are unchanged copies of `docs/licence.txt`
and `docs/lgpl.txt` from the pinned wxWidgets source. The wxWidgets exception
allows this statically linked application to be distributed under its own
terms. havGamePatcher's original code remains MIT licensed.

### Additional notices within wxWidgets

Enabled core and base source files also contain code with the following
attributions. Their copyright, permission and warranty notices are reproduced
in [wxWidgetsAdditionalNotices.txt](licenses/wxWidgetsAdditionalNotices.txt).

- Calendar calculations from Scott E. Lee's sndcal library
- Scoped pointers adapted from work by Greg Colvin, Beman Dawes and Peter Dimov
- IEEE floating-point conversion routines by Ken Turkowski and Malcolm Slaney
- Spline drawing code from XFig by Supoj Sutanthavibul

The enabled image quantizer in `src/common/quantize.cpp` contains code adapted
from the Independent JPEG Group's software. This software is based in part on
the work of the Independent JPEG Group.

Its terms are reproduced in [ijgNotice.txt](licenses/ijgNotice.txt).
This attribution concerns the quantizer within wxWidgets. The separate JPEG
codec library is disabled in the release build.

## Microsoft C++ libraries

The release statically links the Microsoft release C and C++ runtime
libraries. Copyright (c) Microsoft Corporation.

The Microsoft C++ Standard Library is licensed under Apache License 2.0 with
the LLVM exception. The full license and accompanying upstream notices from
the Visual Studio 2022 17.14 release are included:

- [microsoftStlLicense.txt](licenses/microsoftStlLicense.txt)
- [microsoftStlNotice.txt](licenses/microsoftStlNotice.txt)
- [Upstream source](https://github.com/microsoft/STL/tree/vs-2022-17.14)

Other Microsoft runtime components are supplied under the applicable
[Visual Studio Build Tools license terms](https://visualstudio.microsoft.com/license-terms/vs2022-ga-diagnosticbuildtools/).
They are not relicensed under havGamePatcher's project license. Windows system
libraries remain part of Windows and are not included in the release package.

## Build scope

Optional wxWidgets codec and rendering dependencies including libpng,
libjpeg, libtiff, libwebp, NanoSVG and LunaSVG are disabled. Expat, PCRE2,
zlib, liblzma and curl are also disabled. The presence of their sources in a
developer's dependency checkout does not mean they are shipped in the release.

Build tools, test tools and diagnostic utilities are not part of the release
package. When changing the compiler, toolset or dependency configuration,
review the notices for that build. MinGW builds need their own runtime notices.
