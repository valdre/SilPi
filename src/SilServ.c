/*******************************************************************************
*                                                                              *
*                         Simone Valdre' - 21/09/2022                          *
*                  distributed under GPL-3.0-or-later licence                  *
*                                                                              *
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <zmq.h>

#include "../include/SilStruct.h"
#include "../include/SilShared.h"
#include "../include/ShellColors.h"

static int pstate=0, cstate=0;

void parsig(int num) {
	switch(num) {
		case SIGUSR1: if(pstate>=0) pstate++; break;
		case SIGUSR2: pstate=-1; break;
		case SIGINT: pstate=-1; printf("***** PARENT SIGINT CATCHED *****\n\n");
	}
}

void childsig(int num) {
	switch(num) {
		case SIGUSR1: if(cstate>=0) cstate++; break;
		case SIGUSR2: cstate=-1; break;
		case SIGINT: cstate=-1; printf("***** CHILD SIGINT CATCHED *****\n\n");
	}
}

int main() {
	struct Silshared *buf;
	
	printf(GRN "***** Silena - Raspberry Pi interface - event dispatcher *****\n" NRM);
	pid_t pid = fork();
	if(pid < 0) {
		perror(RED "fork" NRM);
		exit(EXIT_FAILURE);
	}
	if(pid) {
		//parent process, pid = child pid
		//event reading from device
		printf(BLD "parent" NRM ": PID = %d,  child PID = %d\n", getpid(), pid);
		signal(SIGUSR1, parsig);
		signal(SIGUSR2, parsig);
		signal(SIGINT, parsig);
		printf(BLD "parent" NRM ": signals registered, requesting shared memory.\n");
		sleep(1);
		buf = shm_request("/silsrvsh", 1);
		if(buf == NULL) {
			kill(pid, SIGUSR2);
			exit(EXIT_FAILURE);
		}
		kill(pid, SIGUSR1);
		buf->size = 0;
		buf->flags = 0;
		
		printf(BLD "parent" NRM ": shared memory allocated, waiting for child process...\n");
		sleep(5);
		if(pstate != 1) {
			kill(pid, SIGUSR2);
			shm_release(buf, "/silsrvsh", 1);
			exit(EXIT_FAILURE);
		}
		
		int fd = open("/dev/silena", O_RDWR);
		if(fd < 0) {
			perror(RED "parent" NRM);
			kill(pid, SIGUSR2);
			shm_release(buf, "/silsrvsh", 1);
			exit(EXIT_FAILURE);
		}
		
		const char off[2] = "0", on[2] = "1";
		struct timeval t0, ti, td;
		uint64_t N = 0, usec, msec, lastmsec = 0;
		ssize_t n;
		struct Silevent buffer[SIZE];
		int runflag = 0;
		
		usleep(10000);
		printf("\n");
		gettimeofday(&t0, NULL);
		while(pstate >= 0) {
			if(runflag) usleep(10000); // 10ms sleep between device readings
			else sleep(1); //if acquisition is stopped wait more
			
			if(runflag) {
				n = read(fd, buffer, sizeof(buffer));
				if(n < 0 || pstate < 0) break;
				
				if(n % sizeof(struct Silevent)) {
					printf(UP YEL "parent" NRM ": read fraction of event (size = %ld)\n\n", (long int)n);
				}
				n /= sizeof(struct Silevent);
			}
			else n = 0;
			
			if(n > 0) {
				while(buf->flags & F_RBUSY) usleep(1000);
				buf->flags |= F_WBUSY;
				if(buf->size + n >= SIZE) {
					printf(UP YEL "parent" NRM ": full buffer, possible data loss\n\n");
					buf->flags &= ~F_RUN;
					buf->flags |=  F_PAUSE;
					//TO DO: dead time calculation when acquisition is paused!!
					//TO DO: recover missing data when acquisition is paused!!
					n = SIZE - buf->size;
				}
				for(int i = 0; i < n; i++) buf->buffer[(buf->size)++] = buffer[i];
				N += n;
				buf->flags -= F_WBUSY;
			}
			
			if((buf->flags & F_RUN) == 0 && runflag == 1) {
				printf(UP YEL "parent" NRM ": stopping acquisition\n\n");
				if(write(fd, off, 2) < 0) {
					perror(RED "write" NRM);
					break;
				}
				runflag = 0;
				fsync(fd);
			}
			
			if((buf->flags & F_RUN) && runflag == 0) {
				printf(UP YEL "parent" NRM ": starting acquisition\n\n");
				if(write(fd, on, 2) < 0) {
					perror(RED "write" NRM);
					break;
				}
				buf->size = 0;
				runflag = 1;
				fsync(fd);
			}
			
			gettimeofday(&ti, NULL);
			timersub(&ti, &t0, &td);
			usec = (uint64_t)td.tv_usec + 1000000L * (uint64_t)td.tv_sec;
			msec = (usec + 500L) / 1000L;
			if(msec - lastmsec >= 1000) {
				//status update every second
				printf(UP BLD "parent" NRM ": uptime =%6lu s, status = %s, i-rate =%6.0lf Hz\n", (unsigned long)(msec / 1000L), runflag ? (GRN " RUN" NRM) : (RED "STOP" NRM), 1000. * ((double)N) / ((double)(msec - lastmsec)));
				N = 0;
				lastmsec = msec;
			}
		}
		
		printf(BLD "parent" NRM ": closing device and quitting acquisition\n");
		kill(pid, SIGUSR2);
		if(write(fd, off, 2) < 0) perror("write");
		fsync(fd); close(fd);
		shm_release(buf, "/silsrvsh", 1);
	}
	else {
		//child process, pid = parent pid
		//communication with clients via 0MQ
		pid = getppid();
		printf(BLD " child" NRM ": PID = %d, parent PID = %d\n", getpid(), pid);
		signal(SIGUSR1, childsig);
		signal(SIGUSR2, childsig);
		signal(SIGINT, childsig);
		printf(BLD " child" NRM ": signals registered, waiting for shared memory creation.\n");
		sleep(5);
		if(cstate != 1) {
			kill(pid, SIGUSR2);
			exit(EXIT_FAILURE);
		}
		printf(BLD " child" NRM ": requesting shared memory\n");
		buf = shm_request("/silsrvsh", 0);
		if(buf == NULL) {
			kill(pid, SIGUSR2);
			exit(EXIT_FAILURE);
		}
		kill(pid, SIGUSR1);
		
		void *context = zmq_ctx_new();
		void *responder = zmq_socket(context, ZMQ_REP);
		if(zmq_bind(responder, "tcp://*:4747")) {
			zmq_close(responder);
			zmq_ctx_destroy(context);
			perror(RED " child" NRM);
			shm_release(buf, "/silsrvsh", 0);
			kill(pid, SIGUSR2);
			exit(EXIT_FAILURE);
		}
		printf(BLD " child" NRM ": 0MQ context and socket opened. Listening at port 4747...\n");
		
		ssize_t n;
		char buffer[10];
		while(cstate >= 0) {
			usleep(10000); //10 ms sleep between command polling
			
			n = zmq_recv(responder, buffer, 10, ZMQ_DONTWAIT); //non blocking request
			if(n < 0 && errno == EAGAIN) continue;
			
			if(n < 0) {
				perror(RED " child" NRM);
				break;
			}
			else {
				if(n >= 10) n = 9;
				buffer[n]='\0';
			}
			
			if(strcmp(buffer, "stop") == 0) {
				zmq_send(responder, "ACK", 4, 0);
				printf(UP GRN " child" NRM ": STOP received\n\n");
				buf->flags &= ~F_RUN;
				buf->flags &= ~F_PAUSE;
				continue;
			}
			
			if(strcmp(buffer, "start") == 0) {
				zmq_send(responder, "ACK", 4, 0);
				printf(UP GRN " child" NRM ":  RUN received\n\n");
				buf->flags |= F_RUN;
				continue;
			}
			
			if(strcmp(buffer, "stat") == 0) {
				zmq_send(responder, &(buf->flags), sizeof(buf->flags), 0);
				continue;
			}
			
			if(strcmp(buffer, "check") == 0) {
				zmq_send(responder, "ACK", 4, 0);
				continue;
			}
			
			if(strcmp(buffer, "send") == 0) {
				while(buf->flags & F_WBUSY) usleep(1000);
				buf->flags |= F_RBUSY;
				zmq_send(responder, buf->buffer, buf->size * sizeof(struct Silevent), 0);
				buf->size = 0;
				if(buf->flags & F_PAUSE) {
					buf->flags |=  F_RUN;
					buf->flags &= ~F_PAUSE;
				}
				buf->flags -= F_RBUSY;
				continue;
			}
			//unknown command
			zmq_send(responder, "NAK", 4, 0);
		}
		
		shm_release(buf, "/silsrvsh", 0);
		printf(GRN " child" NRM ": quitting acquisition and closing 0MQ server\n");
		zmq_close(responder);
		zmq_ctx_destroy(context);
		kill(pid, SIGUSR2);
	}
	usleep(100000);
	return 0;
}
