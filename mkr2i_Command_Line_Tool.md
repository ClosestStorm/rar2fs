_mkr2i_ is a small command line tool delivered and built together with rar2fs. This tool may be used to generate meta data files that provide experimental support for indexing in .avi and .mkv files contained in compressed/encrypted rar archives.

_mkr2i_ will create a _.r2i_ file that should be placed in the root of the compressed archive and be named exactly as the file inside the archive with an _.r2i_ extension (eg. _foo.avi_ should be named _foo.r2i_). In order to use _mkr2i_ a second tool, AVI-Mux GUI, is also required (only link provided here http://www.alexander-noe.com/video/amg/).
To create the _.r2i_ file, the archive needs to be decompressed once and then opened in AVI-Mux GUI. From the AVI-Mux GUI file window right click and choose EBML tree (for eg. .mkv files) or RIFF tree (for eg. .avi files). Once the tree pops up, select "complete" and **wait** for the grayed out button to light up again. Then you know the tree has finished expanding. Once this is done, save the tree somewhere and feed it to the _mkr2i_ program together with the uncompressed raw video file. The latter can then be deleted.

How does it work? Well it is quite simple really. _mkr2i_ locates and extracts the index table data from the video file using the EBML/RIFF tree and adds a custom header with information about start offset and length. Then _rar2fs_ will check every read request towards this offset and length and if it is within that region data will be returned from the _.r2i_ file instead of the archive. Index information is usually populated by the media player at the beginning of a playback session. Since the index table in most cases is stored at the end of the file, retrieving this information without the use of some post-processing is more or less impossible to perform in real-time for compressed/encrypted video streams.