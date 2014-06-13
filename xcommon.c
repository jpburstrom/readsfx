//
//  xcommon.c
//  readsfx~
//
//  Created by Johannes Burström on 2014-06-13.
//  Copyright (c) 2014 Johannes Burström. All rights reserved.
//

#include "xcommon.h"

static void slashit(char *path)
{
    if (*path && path[strlen(path)-1] != '/')
        strcat(path, "/");
}



/*
 Take a dirname/filename pair and put the absolute path in filename char
 This doesn't work with declared paths, which seems to need canvas_open
 */
int readsfx_get_path(const char *dirname, char *filename, char *path, unsigned int size)
{
    int fd;
    char tmp[size];
    char *bufptr;
    fd = open_via_path(dirname, filename, "", tmp, &bufptr, MAXPDSTRING, 1);
    if (!*tmp) {
        
        
        if (sys_isabsolutepath(filename)) {
            strcpy(path, filename);
        } else if (sys_isabsolutepath(dirname)) {
            strcpy(path, dirname);
            slashit(path);
            strcat(path, filename);
        }
    } else {
        strcpy(path, tmp);
        slashit(path);
        strcat(path, bufptr);
    }
    
    if (fd >= 0)
        close(fd);
    
    return fd;
    
}
