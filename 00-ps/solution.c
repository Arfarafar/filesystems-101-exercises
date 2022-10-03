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


#define MAXFILEN 32*4096
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


char** parsefile(char* filename){

	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		report_error(filename, errno);
		return NULL;
	}

	
	char* content = (char*) malloc (MAXFILEN + 2);

	int sz = read(fd, content, MAXFILEN);
	if(sz == -1){
		report_error(filename, errno);
		free(content);
		close(fd);
		return NULL;
	}

	content[sz] = '\0';
	content[sz+1] = '\0';

	int numstr = 0;
	for (int i = 0; (content+i)[0] != '\0' && (content+i)[1] != '\0' ; i += strlen(content+i) + 1)
	{
		numstr++;
	}
	//printf("%d number of str\n", numstr);
	char** args = (char**) malloc ((numstr+1) * sizeof(char*));

	int j = 0;
	for (int i = 0; (content+i)[0] != '\0' && (content+i)[1] != '\0' ; i += strlen(content+i) + 1)
	{
		args[j++] = content+i; 
	}
	args[j] = NULL;

	if(j == 0){
		free(content);
	}

	close(fd);

	return args;

}


void ps(void)
{
	DIR *proc = opendir("/proc");
	if (proc == NULL) {
		report_error("/proc", errno);
		return;
	}


	struct dirent *d;
	errno = 0;

	char exe[MAXPATHLEN]; 
	char path[MAXPATHLEN];

	while ((d = readdir(proc))){

		long N = piddir(d);
		if (!N){
			errno = 0;
			continue;
		}

		
		sprintf(path, "/proc/%ld/exe", N);
		int sz = readlink(path, exe, MAXPATHLEN);
		if (sz == -1) {
			report_error(path, errno);
			errno = 0;
			continue;
		}
		exe[sz] = '\0';
		
		sprintf(path,"/proc/%ld/cmdline", N);
		char **argv = parsefile(path);
		if (argv == NULL){
			errno = 0;
			continue;
		}

	
		sprintf(path, "/proc/%ld/environ", N);
		char **envp = parsefile(path);
		if(envp == NULL){
			free(argv[0]);
			free(argv);
			errno = 0;
			continue;
		}

		
		report_process(N, exe, argv, envp);
		
		
		free(argv[0]);
		free(argv);
		free(envp[0]);
		free(envp);
		

		errno = 0;
	}

	if (errno) {
			report_error("/proc", errno);
		}
	closedir(proc);
}




