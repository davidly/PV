# PV Photo Viewer
### Windows application for displaying photos

PV is a Windows application that displays photos. PV supports nearly all image formats.
It was designed for fast triage of both newly captured RAW files and edited JPG files
ready for sharing.

PV.EXE is built in github and available as an artifact under the most recent build
under Actions, in a zip file. Otherwise, build it yourself.

PV uses WIC for opening images and D2D for display. It was tested with files
including 3fr, arw, bmp, cr2, cr3, dng, flac, gif, heic, hif, ico, jfif/jpeg/jpg,
mp3, nef, orf, png, raf, rw2, tif/tiff.

PV can rotate images via updating EXIF Orientation, adding EXIF Orientation, or by
using WIC to actually rotate the pixels for file formats that don't support EXIF
(PNG, ICO, BMP, etc.). PV only fails to rotate an image when it's a RAW format that
hasn't already allocated EXIF Orientation (e.g. Nikon D100).

PV can optionally be built to use LibRaw, a RAW processing library. PV can be
configured to always use LibRaw for RAW files, use LibRaw for RAW files with
tiny embedded JPGs, or only use LibRaw for RAW files with no embedded JPG.

LibRaw can be found here: https://github.com/LibRaw/LibRaw

Building LibRaw:

    Version 0.20.0 of LibRaw was used. No sources for LibRaw were modified, though
    makefile.msvc was changed to add these compiler options:

        /D_OPENMP /DLIBRAW_FORCE_OPENMP /openmp

    The COPT flag was changed from /MD to /MT to remove the dependency on msvcrt.dll

    Also, these two lines were uncommented to enable building a static library:

        CFLAGS2=/DLIBRAW_NODLL
        LINKLIB=$(LIBSTATIC)

    To build LibRaw, use "nmake makefile.msvc" from a 64-bit VS CMD prompt.

To build PV, use m.bat in a "x64 Native Tools Command Prompt for VS 2019", or the
equivalent for your version of Visual Studio.

In m.bat, define or undefine PV_USE_LIBRAW for both rc and cl commands to enable use
of LibRaw. Also, add the LibRaw include files to your INCLUDE environment variable and
libraw_static.lib to your LIB environment variable (if using LibRaw).

There is no install for PV -- just copy pv.exe to a folder in your path. You can also
right-click on image files in Explorer to open files with PV and/or make PV the default
app for various image formats.

PV is distributed for free using the GNU General Public License v3. Information can
be found here: https://www.gnu.org/licenses/gpl-3.0.en.html
