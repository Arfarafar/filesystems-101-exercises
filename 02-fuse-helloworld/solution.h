#pragma once

#include <unistd.h>

/**
   Implement this function to to mount a "helloworld" FUSE filesystem to @mntp.
   The root of a filesystem must contain the following entries (the owner UID
   and GID may be different):

   $ ls -lha /helloworld
   drwxrwxr-x. 2 9091 9091 19 Oct  6 05:39 .
   drwxr-xr-x. 4 9091 9091 19 Oct  6 05:39 ..
   -r--------. 1 9091 9091 19 Oct  6 05:39 hello

   The content of "hello" must be (without quotes) "hello, ${PID}\n" where
   PID is the id of a process that reads "hello", and "\n" is the newline.
   It is OK to report the size of "hello" that does not match the content.

   Any attempt write to the FS must report EROFS.
*/
int helloworld(const char *mntp);
