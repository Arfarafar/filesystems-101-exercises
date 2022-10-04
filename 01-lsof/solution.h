#pragma once

#include <unistd.h>

/**
   Implement this function to list processes. It must call report_file()
   for each open file. If an error occurs when accessing a file or
   a directory, it must call report_error().
*/
void lsof(void);

/**
   lsof() must call this function to report each open file.

   @path is the absolute path to the file
*/
void report_file(const char *path);
/**
   lsof() must call this function whenever it detects an error when accessing
   a file or a directory.
*/
void report_error(const char *path, int errno_code);
