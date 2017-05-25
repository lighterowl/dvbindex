# Introduction

`dvbindex` is a program which is meant to read a number of MPEG-TS files, 
probably with DVB metadata embedded inside them, and put this metadata along 
with stream information inside an SQLite database.

It uses ffmpeg for obtaining information about the streams, and libdvbpsi for 
reading the DVB metadata.

# Why?

This program is mostly meant to be used by people working on STBs and/or TVs 
which support DVB. Quite often, in order to reproduce a specific issue, a 
specific stream is needed, for example one which has a number of table version 
changes, or which has a service with a subtitle/audio stream in a very specific 
language accompanied with a video stream in a particular resolution.

People who do this kind of work usually have lots of different streams, however 
finding one with a particular combination of parameters is a tedious task : it 
needs opening each file in a stream analyzer and looking at its parameters. 
`dvbindex` is meant to make this task easier : by making appropriate queries 
against the database it creates, one can significantly reduce the number of 
streams that need to be inspected manually.

Since stream files can be quite large sometimes, care was taken to ensure that 
the whole file is never read twice : instead, the file being read is analyzed 
simultaneously by ffmpeg and libdvbpsi.

# Compiling

Compilation was only tested under Linux, with ffmpeg 3.2.2, libdvbpsi 1.3.0, 
and sqlite 3.16.2. Your mileage may vary with other OSes and/or library 
versions : please submit bug reports if something doesn't work with your 
configuration.

Since `dvbindex` uses CMake, the whole process is limited to generating the 
project on your platform and building it. If you're using make as the build 
backend, just do :

```
mkdir build
cd build
cmake ..
make
```

# Usage

`dvbindex` should be handed the name of the database file to create (or modify) 
as its first parameter. All the following parameters are paths to directories 
and/or files for indexing. Very brief status information is output to the 
standard error stream. After the program exits, the database file will contain 
all information it could capture.

Any following invocations of the program don't cause it to rebuild the database 
from scratch : it skips all files that have already been indexed based on their 
name and size.

# Testing

Testing consists of running `test/dvbindex-test.sh` and passing the path to the
executable as one of its arguments. The stream repository that's used to create
the reference database is available upon request.

# Example queries

I'm not a SQL wizard, so I'm sure much more complicated (and useful) queries 
can be thought of.

Get the number of services declared as non-scrambled in each of the indexed
files :

```sql
SELECT f.name, count(0) AS num_non_scrambled FROM services s
JOIN sdts sdt ON s.sdt_rowid = sdt.rowid
JOIN pats pat ON sdt.pat_rowid = pat.rowid
JOIN files f ON pat.file_rowid = f.rowid
WHERE s.scrambled = 0
GROUP BY f.rowid;
```

Get the list of files which have at least one 6-channel audio track :

```sql
SELECT f.name FROM aud_streams a
JOIN files f ON a.file_rowid = f.rowid
WHERE a.channels = 6
GROUP BY f.rowid;
```

Get the list of services which have a HD video track :

```sql
SELECT s.name FROM vid_streams v
JOIN pats pat ON v.file_rowid = pat.file_rowid
JOIN pmts pmt ON pmt.pat_rowid = pat.rowid AND pmt.program_number = (
  SELECT program_number FROM elem_streams es
  JOIN pmts pmt_for_es ON es.pmt_rowid = pmt_for_es.rowid
  WHERE es.pid = v.pid
)
JOIN sdts sdt ON sdt.pat_rowid = pat.rowid
JOIN services s ON s.sdt_rowid = sdt.rowid AND s.program_number = pmt.program_number
WHERE v.width = 1920 AND v.height = 1080
```

# Missing features

Lots. Currently, `dvbindex` only reads PAT and PMT, and SDT tables. Support for 
all the other tables will be added in the future, along with their export to 
the database.

There is no way to obtain any information about scrambled streams, so expect 
these to have mostly NULLs in place of actual data.

Logging could probably be a bit more verbose.

# Bugs

Lots, probably. I have about 10 streams that I used for testing, but 
practically every stream is a totally different world. If you encounter any 
incorrect behaviour, like crashes or missing metadata, please submit a bug 
report along with the stream that was being read.
