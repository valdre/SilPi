/*******************************************************************************
*                                                                              *
*                         Simone Valdre' - 21/09/2022                          *
*                  distributed under GPL-3.0-or-later licence                  *
*                                                                              *
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>
#include <signal.h>
#include <sys/time.h>
#include <zmq.h>

#include "../include/ShellColors.h"
#include "../include/SilStruct.h"

int go=1;

void sigh(int sig) {
	if(sig == SIGINT) go=0;
	return;
}

int main(int argc, char *argv[]) {
	char fn[1000] = "SilCli.cfg";
	if(argc > 1) {
		strcpy(fn, argv[1]);
	}
	else printf(YEL "    main" NRM ": config file name not given. Using default (Silcli.cfg)!\n");
	
	FILE *f = fopen(fn, "r");
	if(f == NULL) printf(YEL "    main" NRM ": config file not found. Using default values!\n");
	
	char buffer[1000], par[1000], pardata[900];
	char host[1000] = "192.168.1.2", prefix[900] = "acq";
	int bits = 13, range = 0, comment;
	for(;f;) {
		if(fgets(buffer, 1000, f) == NULL) break;
		comment = 0;
		for(size_t i = 0; i < strlen(buffer); i++) {
			if(buffer[i] == '#') {
				comment = 1;
				break;
			}
			if(buffer[i] != ' ') break;
		}
		if(comment) continue;
		if(sscanf(buffer, "%s %[^\n]", par, pardata) < 2) continue;
		
		if(strcmp(par, "host") == 0) sprintf(host, "tcp://%s:4747", pardata);
		if(strcmp(par, "bits") == 0) bits = atoi(pardata);
		if(strcmp(par, "out") == 0) strcpy(prefix, pardata);
	}
	if(f) fclose(f);
	
	void *context   = zmq_ctx_new();
	void *requester = zmq_socket(context, ZMQ_REQ);
	if(zmq_connect(requester, host)) {
		perror(RED "    main" NRM);
		exit(EXIT_FAILURE);
	}
	
	do sprintf(par, "%s%05d.dat", prefix, range++);
	while((fopen(par, "r")));
	
	if(bits < 10 || bits > 16) {
		printf(YEL "    main" NRM ": bad number of ADC bits (10-16 range allowed). Set to 13 by default!\n");
		bits = 13;
	}
	range = (1 << bits);
	
	f = popen("/usr/bin/gnuplot -persist", "w");
	if(f) {
		fprintf(f, "set term x11 0\n");
		fprintf(f, "set styl data steps\n");
		fprintf(f, "set grid\n");
		fprintf(f, "set xrange [2:%d]\n", range);
		fprintf(f, "set xlabel 'ADC units'\n");
	}
	else printf(YEL "    main" NRM ": gnuplot not found!\n");
	
	printf("\n");
	printf("* Silena - Raspberry Pi acquisition - GNUPLOT client\n");
	printf("* version 1.0 (Simone Valdre', 2022)\n\n");
	printf("Starting with the following parameters:\n");
	printf(BLD "         Raspberry hostname" NRM " -> %s\n", host);
	printf(BLD "    Silena ADC bits (range)" NRM " -> %d (%d)\n", bits, range);
	printf(BLD "                Output file" NRM " -> %s\n\n", par);
	
	int n;
	zmq_send(requester, "start", 6, 0);
	n = zmq_recv(requester, buffer, 999, 0);
	buffer[n] = '\0';
	printf(BLD "    main" NRM ": START -> %s\n", buffer);
	
	signal(SIGINT,sigh);
	printf(GRN "***** Press CTRL+C to stop and close the acquisition client *****\n\n");
	
	int flags;
	struct Silevent data[SIZE];
	uint64_t t0 = 0, tall = 0, tdead = 0, lasttall = 0, lasttdead = 0;
	uint64_t spec[65536], N = 0, lastN = 0;
	for(int j = 0; j < 65536; j++) spec[j] = 0;
	
	printf("\n");
	struct timeval ti, tf, td;
	uint64_t usec, msec, lastmsec = 0;
	gettimeofday(&ti, NULL);
	for(;go;) {
		zmq_send(requester, "send", 5, 0); //5 byte: c'è il carattere terminatore '\0'!!
		n = zmq_recv(requester, data, SIZE * sizeof(struct Silevent), 0);
		if(!go) break;
		
		if(n % sizeof(struct Silevent)) {
			printf(UP RED "    main" NRM ": read fraction of event (size = %d)\n\n", n);
			break;
		}
		n /= sizeof(struct Silevent);
		
		for(int j = 0; j < n; j++) {
			if(t0 == 0) {
				//always skip first event (used as start time mark)
				t0 = data[j].ts + (uint64_t)(data[j].dt);
				continue;
			}
			tall = data[j].ts + (uint64_t)(data[j].dt) - t0;
			tdead += (uint64_t)(data[j].dt);
			spec[data[j].val]++;
			N++;
		}
		
		gettimeofday(&tf, NULL);
		timersub(&tf, &ti, &td);
		usec = (uint64_t)td.tv_usec + 1000000L * (uint64_t)td.tv_sec;
		msec = (usec + 500L) / 1000L;
		if(msec - lastmsec >= 1000L) {
			//ask status to server
			zmq_send(requester, "stat", 5, 0);
			n = zmq_recv(requester, &flags, sizeof(int), 0);
			
			//status update every second
			printf(UP BLD "   *****" NRM " uptime =%6lu s, tot.ev = %10lu, status =%s, i-rate =%6.0lf Hz, i-d.time =%3.0lf %%\n", msec / 1000L, N, (flags&F_PAUSE) ? (YEL "PAUSE" NRM) : ((flags&F_RUN) ? (GRN " RUN " NRM) : (RED " STOP" NRM)), 1000. * ((double)(N - lastN)) / ((double)(msec - lastmsec)), tall == lasttall ? 0 : 100. * ((double)(tdead - lasttdead)) / ((double)(tall - lasttall)));
			lastN = N;
			lasttall = tall;
			lasttdead = tdead;
			lastmsec = msec;
			
			if(f && N) {
				fprintf(f, "plot '-'\n");
				for(int j = 0; j < range; j++) fprintf(f, "%lu\n", spec[j]);
				fprintf(f, "e\n");
				fflush (f);
			}
		}
	}
	if(f) pclose(f);
	
	//receive remaining events
	zmq_recv(requester, data, SIZE * sizeof(struct Silevent), 0);
	
	//STOP Silena ADC
	zmq_send(requester, "stop", 5, 0);
	n = zmq_recv(requester, buffer, 999, 0);
	buffer[n] = '\0';
	printf(BLD "    main" NRM ":  STOP -> %s\n", buffer);
	zmq_close(requester);
	zmq_ctx_destroy(context);
	
	
	spec[0] = (tall + 50000000L) / 100000000L;
	spec[1] = (tall - tdead + 50000000L)  / 100000000L;
	printf(BLD "    main" NRM ": writing output on disk ed exiting...\n");
	f = fopen(par, "w");
	fprintf(f, "# BIN      count\n");
	for(int j = 0; j < range; j++) fprintf(f, " %4d %10lu\n", j, spec[j]);
	fclose(f);
	return 0;
}
