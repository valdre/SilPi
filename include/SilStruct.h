/*******************************************************************************
*                                                                              *
*                         Simone Valdre' - 21/09/2022                          *
*                  distributed under GPL-3.0-or-later licence                  *
*                                                                              *
*******************************************************************************/

#ifndef SILPISTRUCT
#define SILPISTRUCT

#define SIZE 10000

//data flags
#define F_RUN   1
#define F_PAUSE 2
#define F_WBUSY 4
#define F_RBUSY 8

//data structure to user space
struct Silevent { //128-bit events
	uint64_t ts; // event timestamp (in ns from 1/1/1970) registered at conversion start
	uint32_t dt; // total dead time: conversion time (due to Silena ADC) + reading time (due to Raspberry Pi)
	uint16_t val, errcnt; // event value and error counter since previous event;
};

struct Silshared {
	int size, flags;
	struct Silevent buffer[SIZE];
};

#endif
