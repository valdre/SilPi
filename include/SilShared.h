/*******************************************************************************
*                                                                              *
*                         Simone Valdre' - 21/09/2022                          *
*                  distributed under GPL-3.0-or-later licence                  *
*                                                                              *
*******************************************************************************/

#ifndef SILSHARED
#define SILSHARED

extern struct Silshared *shm_request(const char *, const int);
extern void shm_release(struct Silshared *, const char *, const int); 

#endif
