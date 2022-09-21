/*******************************************************************************
*                                                                              *
*                         Simone Valdre' - 21/09/2022                          *
*                  distributed under GPL-3.0-or-later licence                  *
*                                                                              *
*******************************************************************************/

#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <unistd.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include "../include/SilStruct.h"
#include "../include/ShellColors.h"

struct Silshared *shm_request(const char *memname, const int create) {
	int fd;
	struct stat statbuf;
	struct Silshared *buf = NULL;
	
	if(create) {
		fd = shm_open(memname, O_RDWR|O_CREAT|O_EXCL, 0644);
		if(fd < 0 && errno == EEXIST) {
			printf(YEL "  shm_req" NRM ": file exists -> closing it!\n");
			if(shm_unlink(memname)) perror(RED "shm_unlink" NRM);
			fd = shm_open(memname, O_RDWR|O_CREAT|O_EXCL, 0644);
		}
	}
	else fd = shm_open(memname, O_RDWR, 0644);
	
	if(fd < 0) {
		perror(RED "shm_open" NRM);
		return NULL;
	}
	
	if(create) {
		if(ftruncate(fd, sizeof(struct Silshared))) {
			perror(RED "ftruncate" NRM);
			goto err;
		}
	}
	else {
		if(fstat(fd, &statbuf)) {
			perror(RED "fstat" NRM);
			goto err;
		}
		if(statbuf.st_size != sizeof(struct Silshared)) {
			printf(RED "shm_get" NRM ": wrong shared file size\n");
			goto err;
		}
	}
	
	buf = mmap(NULL, sizeof(struct Silshared), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if(buf == MAP_FAILED) {
		perror(RED "fstat" NRM);
		goto err;
	}
	if(create) memset(buf, 0, sizeof(struct Silshared));
	return buf;
	
	err:
	if(close(fd)) perror(RED "close" NRM);
	if(shm_unlink(memname)) perror(RED "shm_unlink" NRM);
	return NULL;
}

void shm_release(struct Silshared *buf, const char *memname, const int shunlink) {
	if(munmap(buf, sizeof(struct Silshared))) perror(RED "munmap" NRM);
	if(shunlink) {
		if(shm_unlink(memname)) perror(RED "shm_unlink" NRM);
	}
}
