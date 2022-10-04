#include <solution.h>


#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>


#define MAXPATHLEN 4096

long piddir(struct dirent *d){

	if (d -> d_type != DT_DIR)
		return 0;

	char* extstr;
    long N = strtol(d -> d_name, &extstr, 0);

	if (*extstr != '\0')
		return 0;

	return N;
}

void listFiles(char* dirname){

	DIR *fd = opendir(dirname);
	if (fd == NULL) {
		report_error(dirname, errno);
		return;
	}

	struct dirent *d;
	errno = 0;

	char flname[MAXPATHLEN]; 
	char path[MAXPATHLEN];

	while ((d = readdir(fd))){

		char* extstr;
	    strtol(d -> d_name, &extstr, 0);

		if (*extstr != '\0')
			continue;


		sprintf(path, "%s/%s", dirname, d -> d_name);


		int sz = readlink(path, flname, MAXPATHLEN);
		if (sz == -1) {
			report_error(path, errno);
			errno = 0;
			continue;
		}
		if (sz == MAXPATHLEN){
			report_error(path, E2BIG);
			errno = 0;
			continue;
		}

		flname[sz] = '\0';

		report_file(flname);
	}

	if (errno) {
			report_error(dirname, errno);
		}
	closedir(fd);

}

void lsof(void)
{
	DIR *proc = opendir("/proc");
	if (proc == NULL) {
		report_error("/proc", errno);
		return;
	}


	struct dirent *d;
	errno = 0;

	char path[MAXPATHLEN];

	while ((d = readdir(proc))){

		long N = piddir(d);
		if (!N){
			errno = 0;
			continue;
		}

		
		sprintf(path, "/proc/%ld/fd", N);
		listFiles(path);

		errno = 0;
	}

	if (errno) {
			report_error("/proc", errno);
		}
	closedir(proc);
}
