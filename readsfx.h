//
//  readsfx.h
//  readsfx
//
//  Created by Johannes Burström on 2014-06-10.
//  Copyright (c) 2014 Johannes Burström. All rights reserved.
//

#ifndef readsfx_readsfx_h
#define readsfx_readsfx_h

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>



#define SFXDEBUG

#include "m_pd.h"

#ifndef MAXSFCHANS //d_soundfile.c
#define MAXSFCHANS 64

#define MAXBYTESPERSAMPLE 4
#define MAXVECSIZE 128

#define READSIZE 65536
#define WRITESIZE 65536
#define DEFBUFPERCHAN 262144
#define MINBUFSIZE (4 * READSIZE)
#define MAXBUFSIZE 16777216     /* arbitrary; just don't want to hang malloc */

#define REQUEST_NOTHING 0
#define REQUEST_OPEN 1
#define REQUEST_CLOSE 2
#define REQUEST_QUIT 3
#define REQUEST_BUSY 4

#define STATE_IDLE 0
#define STATE_STARTUP 1
#define STATE_STREAM 2

#endif //define MAXFSCHANS

#include "sfxReader.h"

#define SCALE (1./(1024. * 1024. * 1024. * 2.))




#endif //readsfx_readsfx_h
