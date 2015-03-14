
```
RAR2FS(1)                                  User Commands                                  RAR2FS(1)



NAME
       rar2fs - FUSE file system for reading RAR archives

SYNOPSIS
       rar2fs [options] source target

DESCRIPTION
       rar2fs is a FUSE based file system that can mount a source RAR archive/volume or a directory
       containing any number of RAR archives on target and access (read only) the contents as plain
       files/directories.  Other  files  located in the source directory are handled transparently.
       Both compressed and non-compressed (store) archives/volumes are  supported  but  full  media
       seek  support  (aka. indexing) is only available for non-compressed plaintext archives. If a
       RAR volume is selected as source the file specified must be the first  in  the  set.   Since
       rar2fs  is non-interactive, passwords that are required to decrypt encrypted archives should
       be stored in a file with the same name as the main  archive/volume  file  but  with  a  .pwd
       extension.  Be aware that a password must be stored in plaintext format and is thus not pro-
       tected in any way from unauthorized access.

       This program is free software: you can redistribute it and/or modify it under the  terms  of
       the  GNU General Public License as published by the Free Software Foundation, either version
       3 of the License, or (at your option) any later version.

       This program is distributed in the hope that it will be useful, but  WITHOUT  ANY  WARRANTY;
       without  even  the  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
       See the GNU General Public License for more details.  You should have received a copy of the
       GNU    General    Public    License    along    with    this    program.    If    not,   see
       <http://www.gnu.org/licenses/>


OPTIONS
       Besides the standard FUSE options rar2fs accepts the following options that can be passed to
       the program.

       --img-type=".<ext>[;.<ext>;...]"
              additional image file type extensions

              The default image file types recognized by rar2fs is .img, .nrg and .iso. This option
              will allow more file extensions to be added. It affects the behavior of  the  --show-
              comp-img , --preopen-img and --fake-iso options.

       --show-comp-img
              show image file types also for compressed/encrypted archives

              Image  media  files  in  compressed/encrypted archives usually does not playback very
              well, if at all. This is because imaged media, such as DVD images, is implementing  a
              file access pattern that can not be fully supported by a partly buffered decoded data
              stream with limited history. For that reason the default is to not show such files.

       --preopen-img
              prefetch volume file descriptors for image file types

              This option will force all volume files to be  opened  before  playback  is  started.
              Specifying this option might help in some rare situations to overcome transient play-
              back disturbances at switch of volume files when mounted across a lossy/slow network.
              Note that this option only affects playback of image file media read in raw mode (not
              compressed/encrypted).

       --fake-iso=".<ext>[;.<ext>;...]"

       --fake-iso
              fake .iso extension for specified image file types

              Some media players does not support display of image files other than  those  with  a
              .iso  extension.  However, .img files are usually 100% compatible with .iso and so is
              .nrg files, even though the .nrg format specification says otherwise. For that reason
              playback of .nrg might fail. Specifying this option will remove the need for renaming
              certain file types to .iso just to make them display properly. If playback  works  or
              not  is all about the player software from here. Each file extension/type in the list
              should be separated by a semi-colon ';' character. It is also possible not to provide
              any  image file type extensions for which the default .img and .nrg will be displayed
              as .iso together with what was specified in --img-type. Note though that image  files
              are  treated  somewhat  differently depending on where they are located. If the image
              file is not part of a RAR archive, then there will be a virtual  clone  made  of  the
              original file but with a .iso extension, provided that the file/link does not already
              exist. This to keep the consistency of the  back-end  file  system,  especially  when
              links are involved.

       --seek-length=n
              set number of volume files that are traversed in search for headers [0=All]

              Normally  the  RAR  specific  header(s) describing the files contained in a volume is
              located in the first volume file. Providing a value of 1 here should thus  be  suffi-
              cient to cover most cases. The default is 0, meaning that the complete set of volumes
              files are searched for headers.  The lower the value (starting from 1) the faster the
              display  of  folders containing a lot of RAR volumes (or volumes with a lot of files)
              will become since the number of open/search/close requests can be reduced.

       --seek-depth=n

       --flat-only
              control number of levels down RAR files are parsed inside main archive

              Currently one instance of a rar2fs file system can expand up to two levels of  nested
              RAR  archives.  The  --flat-only option can be used to tell rar2fs to expand only the
              first level. In some specific situations enabling this option might  increase  folder
              load  time  but typically will not have any other effect than loosing the rar-inside-
              rar feature. The --flat-only replaces the obsoleted --seek-depth=0 option which is no
              longer  supported.  The  --seek-depth option is however still accepted as a dummy for
              backwards compatibility but provides no functionality. To aquire more levels of nest-
              ing stacking of rar2fs file systems is needed.


       --exclude="<file>[;<file>;...]"

       --exclude=<path>
              exclude file filter

              When  file access is requested and the file can not be found on the local file system
              all RAR archives in target folder are also searched for it. Not until that last oper-
              ation  fails  the  file  is  considered missing. On some platforms certain files (eg.
              .lock files) are always accessed but are usually never to expect  within  a  RAR  ar-
              chive.   Specifying this option will treat the listed files differently. If not found
              on local file system they will never be searched for in the local RAR archives.  This
              dramatically  decrease  the  folder  load/display time during 'ls' like operations on
              such systems.  Each file in the list should be separated by a semi-colon ';'  charac-
              ter.

              It is also possible to use this option in such a way that it instead points to a file
              that lists the actual exclude filter. This is done by  specifying  an  absolute  file
              path  (starting  with  '/') instead of a semi-colon separated list of file names. The
              file pointed to may contain more than one line but for each line files should be sep-
              arated by a semi-colon ';' character.

       --no-smp
              disable SMP support (bind to CPU #0)

              Note that this option is only available on Linux based platforms with support for the
              cpu_set_t type (GNU extension).

       --save-eof
              force creation of .r2i files (end-of-file chunk) [EXPERIMENTAL]

              Index information is usually populated by the media player  at  the  beginning  of  a
              playback  session.  Since  the  index table in most cases is stored at the end of the
              file, retrieving this information without the use of some post-processing is more  or
              less  impossible  to perform in real-time for compressed/encrypted video streams. The
              mkr2i tool is intended to be used in such cases to make the index table available  in
              a separate .r2i file.

              Enabling this option will instead tell rar2fs to guess where the index information is
              located by analyzing the access pattern of the media player and then write  the  end-
              of-file chunk to an .r2i file automatically. This method is however less precise than
              when using the mkr2i tool. Expect an increase in size of the generated .r2i file com-
              pared  to using the mkr2i tool directly. Start of playback will also be delayed since
              almost the entire archive needs to be extracted in order to access the  data  towards
              the end of the file and make it available for playback.

              This option is only supported for AVI 1.0 and multi-part OpenDML (AVI 2.0) files cre-
              ated by a properly configured muxer. Badly configured muxers will  expose  themselves
              by generating invalid frame counts. The latter is automatically detected by rar2fs.


       --no-lib-check
              disable dynamic library consistency check

              At startup rar2fs validates that the dynamic libraries libfuse.so and libunrar.so are
              compatible/consistent with what was used during compilation.  Use this option to  by-
              pass this check. Use of this option is discouraged.

       --iob-size=n
              tune the size of the I/O buffer

              The  I/O buffer is used to prefetch data at extraction of compressed or encrypted ar-
              chives to make sure streaming is possible without delay due to disk or  network  I/O.
              Depending  on the current system resources and network latency this buffer might need
              to be adjusted. A small buffer takes less resources  but  increase  the  chance  that
              rar2fs must wait for data to arrive during a read request. On the other hand, a large
              buffer will increase memory footprint which may not always be desired. Also  keep  in
              mind  that  every  file  being extracted requires its own buffer. So the total memory
              resources required are always the buffer size multiplied  by  the  number  of  active
              extraction threads. Be careful when choosing buffer size. There is no cap on the size
              itself. The only requirement is that it is a 'power of  2'  Megabytes,  eg.  1,2,4,8,
              etc. The default size is 4MiB.

       --hist-size=n
              tune the size of I/O buffer history

              The  I/O  buffer history is a sliding window within the I/O buffer that is guaranteed
              to never be overwritten until future data has been consumed passed this  limit.  This
              means that, even though an extraction process can never be reversed, this part of the
              buffer can still deliver "historic" data within this window (eg.  skipping  backwards
              during  movie  playback). The size of the history buffer is expressed as a percentage
              of the total I/O buffer size between 0% and 75%. Specifying 0  here  will  completely
              disable this function. The default size is 50% of the total I/O buffer size.

       --no-expand-cbr
              disable support for comic book RAR archives

              Default  is  to  always  expand  comic book RAR archives. In the case that comic book
              readers are used that expect to find the orignal .cbr archive this option can be used
              to keep such files intact.

BUGS
       -

SEE ALSO
       Project home page <http://code.google.com/p/rar2fs/>

AUTHOR
       Hans Beckérus
       <hans.beckerus#AT#gmail.com>




Wed, May 22, 2013                                v                                        RAR2FS(1)
```