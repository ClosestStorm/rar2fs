__Note that Google has moved hosted downloads from Google Code to Google Drive. For the latest releases of rar2fs check out the_[Download Area](https://drive.google.com/folderview?id=0B-2uEqYiZg3zalJ4ZnMzRG00eWM&usp=sharing)_from the external links or the Wiki_[Downloads](https://code.google.com/p/rar2fs/wiki/Downloads)_page (for direct links). Old releases will still be available through the deprecated_[Downloads](https://code.google.com/p/rar2fs/downloads/list)**section until further notice.**_


_rar2fs_ is a FUSE based file system that can mount a source RAR archive/volume or a directory containing any number of RAR archives and read the contents as regular files on-the-fly. Non-archived files located in the source directory are handled transparently. Both compressed and non-compressed archives/volumes are supported but full media seek support (aka. indexing) is only available for non-compressed plaintext archives. In general support for compressed and/or encrypted archives is "best effort", highly depending on what type of information the archive contains and by what method it is accessed.

Encrypted archives are supported but since _rar2fs_ is completely non-interactive it requires the archive password to be stored on the local file system in a file in plaintext format. This of course is a major security limitation in itself. If you really need to use this feature, use it wisely. _The author(s) of rar2fs will not be held responsible for encrypted information being exposed due to this limitation_. If security is really an issue some encryption on file system level should be considered instead. A FUSE based encryption file system, such as _[encfs](http://en.wikipedia.org/wiki/EncFS)_, has proven to work very well together with _rar2fs_.

**Latest released version of rar2fs is 1.20.0 and can be downloaded [here](https://docs.google.com/uc?export=download&id=0B-2uEqYiZg3zR1F0b0tmRktiaXc).**


---


This program takes use of the free _"Portable UnRAR"_ C++ library and some extensions to it. In order to build the API extensions the complete source tree of unrar is needed.

The latest supported version of the unrar source tree (by _Alexander Roshal_) can be found here http://www.rarlab.com/rar/unrarsrc-5.2.3.tar.gz. Later versions, especially minor revision steps, should still be possible to use (should you discover a bug) depending on the level of backward source compatibility it offers. If a new source version breaks the build please report it through the [issue tracking](http://code.google.com/p/rar2fs/issues/list) system or wait patiently for a new version of rar2fs to be released. New versions will be tested as soon as they are discovered. Also **be aware** that changing from one version of the source to another **always** will require that the current version of _libunrar.so_ on the target system is replaced.


---


To successfully build _rar2fs_ a copy of the fuse development files must also exist on the host system (build server). Any more recent version of FUSE should be possible to use, but versions earlier than 2.6 has not been tested and most probably does **not** work. A recent version of FUSE development files can be downloaded here http://sourceforge.net/projects/fuse/files/fuse-2.X/. For FreeBSD the FUSE ports [sysutils/fusefs-kmod](http://www.freshports.org/sysutils/fusefs-kmod/) and [sysutils/fusefs-libs](http://www.freshports.org/sysutils/fusefs-libs/) must be installed.

Version 1.11.3 and later of _rar2fs_ supports _MacFUSE_, the FUSE clone for Mac OS X. The latest installation package can be found on the _[MacFUSE](http://code.google.com/p/macfuse/)_ project site.

The more recent versions of _rar2fs_ also supports _Fuse4x_, the reference implementation of the Linux FUSE API for Mac OS X and successor to the non-proprietary version of _MacFUSE_. More information can be found on the _[Fuse4x](http://fuse4x.org)_ project site. The latest installation package of _Fuse4x_ can be downloaded _[here](https://github.com/fuse4x/fuse4x/downloads)_.

Version 1.15.2 and later includes support for FUSE for OS X (or _OSXFUSE_). _OSXFUSE_ is a successor to _MacFUSE_ which is no longer being maintained. _OSXFUSE_ has also been merged with _Fuse4x_. A recent installation of _OSXFUSE_ can be found _[here](http://osxfuse.github.com/)_.


---


For further information about building and using _rar2fs_ refer to the [README](https://code.google.com/p/rar2fs/source/browse/trunk/README) file or the [Wiki](http://code.google.com/p/rar2fs/w/list). If you still have questions or maybe even have a suggestion for improvement etc. do not hesitate posting it to the [rar2fs](http://groups.google.com/group/rar2fs) group.


---

[![](https://www.paypal.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=FZ8VF22AMASZS)
[![](http://www.gnu.org/graphics/gplv3-127x51.png)](http://www.gnu.org/licenses/licenses.html)