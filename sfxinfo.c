//
//  sfxinfo.c
//  readsfx~
//
//  Created by Johannes Burström on 2014-06-15.
//  Copyright (c) 2014 Johannes Burström. All rights reserved.
//

#include "xcommon.h"

static t_class *sfxinfo_class;


typedef struct _sfxinfo {
    t_object  x_obj;
    t_canvas *x_canvas;
    t_atom x_output[SFXINFO_SIZE];
    void *x_list;
    sfxdata x_data;
    
} t_sfxinfo;

static void sfxinfo_reset(t_sfxinfo *x)
{
    x->x_data = (sfxdata){0, -1, -1, -1, -1};
}

void sfxinfo_read(t_sfxinfo *x, t_symbol *path)
{
    char *filename = path->s_name;
    char *dirname = canvas_getdir(x->x_canvas)->s_name;
    
    sfxinfo_reset(x);
    
    sfxreader_info(dirname, filename, &(x->x_data));
    
    if (x->x_data.channels == -1)
        goto err;
    
    
    SETFLOAT(x->x_output, (t_float)x->x_data.frames);
    SETFLOAT(x->x_output+1, (t_float)x->x_data.samplerate);
    SETFLOAT(x->x_output+2, (t_float)x->x_data.channels);
    SETFLOAT(x->x_output+3, (t_float)x->x_data.bits);
    SETFLOAT(x->x_output+4, (t_float)x->x_data.bigendian);
    
    outlet_list(x->x_list, &s_list, SFXINFO_SIZE, x->x_output);
    
    return;
    
err:
    pd_error(x, "Couldn't open file: %s", filename);
}

void *sfxinfo_new(void)
{
    t_sfxinfo *x = (t_sfxinfo *)pd_new(sfxinfo_class);
    x->x_canvas = canvas_getcurrent();
    x->x_list = outlet_new(&x->x_obj, &s_list);
    
    sfxinfo_reset(x);
    
    return (void *)x;
}

void sfxinfo_setup(void) {
    sfxinfo_class = class_new(gensym("sfxinfo"),
                                 (t_newmethod)sfxinfo_new,
                                 0, sizeof(t_sfxinfo),
                                 CLASS_DEFAULT, 0);
    class_addmethod(sfxinfo_class, (t_method)sfxinfo_read, gensym("read"), A_DEFSYM, 0);
}