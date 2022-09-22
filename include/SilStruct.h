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

//error mask bits
#define SILPI_EIDLE_LVE      1
#define SILPI_EIDLE_RDYIRQ   2
#define SILPI_EIDLE_NOTIME   4
#define SILPI_EDEAD_LVE      8

//data structure to user space
struct Silevent { //128-bit events
	uint64_t ts; // event timestamp (in ns from 1/1/1970) registered at conversion start
	uint32_t dt; // total dead time: conversion time (due to Silena ADC) + reading time (due to Raspberry Pi)
	uint16_t val, emask; // event value and error bitmask;
};

struct Silshared {
	int size, flags;
	struct Silevent buffer[SIZE];
};

#endif
