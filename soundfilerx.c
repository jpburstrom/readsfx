//
//  soundfilerx.c
//  readsfx~
//
//  Created by Johannes Burström on 2014-06-13.
//  Copyright (c) 2014 Johannes Burström. All rights reserved.
//

#include "xcommon.h"

/* ------- soundfilerx - reads and writes soundfiles to/from "garrays" ---- */
#define DEFMAXSIZE 4000000      /* default maximum 16 MB per channel */
#define SAMPBUFSIZE 16384


static t_class *soundfilerx_class;

typedef struct _soundfilerx
{
    t_object x_obj;
    t_canvas *x_canvas;
} t_soundfilerx;

static t_soundfilerx *soundfilerx_new(void)
{
    t_soundfilerx *x = (t_soundfilerx *)pd_new(soundfilerx_class);
    x->x_canvas = canvas_getcurrent();
    outlet_new(&x->x_obj, &s_float);
    return (x);
}

static void soundfile_xferin_float(int sfchannels, int nvecs, t_float **vecs,
                                   long itemsread, unsigned char *buf, int nitems, int bytespersamp,
                                   int bigendian, int spread)
{
    int i, j;
    unsigned char *sp, *sp2;
    t_float *fp;
    int nchannels = (sfchannels < nvecs ? sfchannels : nvecs);
    int bytesperframe = bytespersamp * sfchannels;
    for (i = 0, sp = buf; i < nchannels; i++, sp += bytespersamp)
    {
        if (bytespersamp == 2)
        {
            if (bigendian)
            {
                for (j = 0, sp2 = sp, fp=vecs[i] + spread * itemsread;
                     j < nitems; j++, sp2 += bytesperframe, fp += spread)
                    *fp = SCALE * ((sp2[0] << 24) | (sp2[1] << 16));
            }
            else
            {
                for (j = 0, sp2 = sp, fp=vecs[i] + spread * itemsread;
                     j < nitems; j++, sp2 += bytesperframe, fp += spread)
                    *fp = SCALE * ((sp2[1] << 24) | (sp2[0] << 16));
            }
        }
        else if (bytespersamp == 3)
        {
            if (bigendian)
            {
                for (j = 0, sp2 = sp, fp=vecs[i] + spread * itemsread;
                     j < nitems; j++, sp2 += bytesperframe, fp += spread)
                    *fp = SCALE * ((sp2[0] << 24) | (sp2[1] << 16)
                                   | (sp2[2] << 8));
            }
            else
            {
                for (j = 0, sp2 = sp, fp=vecs[i] + spread * itemsread;
                     j < nitems; j++, sp2 += bytesperframe, fp += spread)
                    *fp = SCALE * ((sp2[2] << 24) | (sp2[1] << 16)
                                   | (sp2[0] << 8));
            }
        }
        else if (bytespersamp == 4)
        {
            if (bigendian)
            {
                for (j = 0, sp2 = sp, fp=vecs[i] + spread * itemsread;
                     j < nitems; j++, sp2 += bytesperframe, fp += spread)
                    *(long *)fp = ((sp2[0] << 24) | (sp2[1] << 16)
                                   | (sp2[2] << 8) | sp2[3]);
            }
            else
            {
                for (j = 0, sp2 = sp, fp=vecs[i] + spread * itemsread;
                     j < nitems; j++, sp2 += bytesperframe, fp += spread)
                    *(long *)fp = ((sp2[3] << 24) | (sp2[2] << 16)
                                   | (sp2[1] << 8) | sp2[0]);
            }
        }
    }
    /* zero out other outputs */
    for (i = sfchannels; i < nvecs; i++)
        for (j = nitems, fp = vecs[i]; j--; )
            *fp++ = 0;
    
}

/* soundfilerx_read ...
 
 usage: read [flags] filename table ...
 flags:
 -skip <frames> ... frames to skip in file
 -onset <frames> ... onset in table to read into (NOT DONE YET)
 -raw <headersize channels bytes endian>
 -resize
 -maxsize <max-size>
 */

static void soundfilerx_read(t_soundfilerx *x, t_symbol *s,
                            int argc, t_atom *argv)
{
    int channels = 0, bytespersamp = 0, bigendian = 0,
    resize = 0, i, j;
    long skipframes = 0, finalsize = 0,
    maxsize = DEFMAXSIZE, itemsread = 0, bytelimit  = 0x7fffffff;
    int status = -1;
    char *filename;
    char *dirname = canvas_getdir(x->x_canvas)->s_name;
    t_garray *garrays[MAXSFCHANS];
    t_word *vecs[MAXSFCHANS];
    char sampbuf[SAMPBUFSIZE];
    int bufframes, nitems;
    void * sfxreader = sfxreader_new();
    
    while (argc > 0 && argv->a_type == A_SYMBOL &&
           *argv->a_w.w_symbol->s_name == '-')
    {
        char *flag = argv->a_w.w_symbol->s_name + 1;
        if (!strcmp(flag, "resize"))
        {
            resize = 1;
            argc -= 1; argv += 1;
        }
        else if (!strcmp(flag, "skip"))
        {
            if (argc < 2 || argv[1].a_type != A_FLOAT ||
                ((skipframes = argv[1].a_w.w_float) < 0))
                goto usage;
            argc -= 2; argv += 2;
        }
        else if (!strcmp(flag, "maxsize"))
        {
            if (argc < 2 || argv[1].a_type != A_FLOAT ||
                ((maxsize = argv[1].a_w.w_float) < 0))
                goto usage;
            resize = 1;     /* maxsize implies resize. */
            argc -= 2; argv += 2;
        }
        else goto usage;
    }
    if (argc < 2 || argc > MAXSFCHANS + 1 || argv[0].a_type != A_SYMBOL)
        goto usage;
    filename = argv[0].a_w.w_symbol->s_name;
    argc--; argv++;
    
    for (i = 0; i < argc; i++)
    {
        int vecsize;
        if (argv[i].a_type != A_SYMBOL)
            goto usage;
        if (!(garrays[i] =
              (t_garray *)pd_findbyclass(argv[i].a_w.w_symbol, garray_class)))
        {
            pd_error(x, "%s: no such table", argv[i].a_w.w_symbol->s_name);
            goto done;
        }
        else if (!garray_getfloatwords(garrays[i], &vecsize,
                                       &vecs[i]))
            error("%s: bad template for tabwrite",
                  argv[i].a_w.w_symbol->s_name);
        if (finalsize && finalsize != vecsize && !resize)
        {
            post("soundfilerx_read: arrays have different lengths; resizing...");
            resize = 1;
        }
        finalsize = vecsize;
    }
    status = sfxreader_open(sfxreader, dirname, filename, &bytespersamp, &bigendian,
                        &channels, &bytelimit, skipframes);
    
    if (status < 0)
    {
        pd_error(x, "soundfilerx_read: %s: %s", filename, (errno == EIO ?
                                                          "unknown or bad header format" : strerror(errno)));
        goto done;
    }
    
    if (resize)
    {
        long framesinfile = sfxreader_get_nframes(sfxreader);
        if (framesinfile > maxsize)
        {
            pd_error(x, "soundfilerx_read: truncated to %ld elements", maxsize);
            framesinfile = maxsize;
        }
        if (framesinfile > bytelimit / (channels * bytespersamp))
            framesinfile = bytelimit / (channels * bytespersamp);
        finalsize = framesinfile;
        for (i = 0; i < argc; i++)
        {
            int vecsize;
            
            garray_resize_long(garrays[i], finalsize);
            /* for sanity's sake let's clear the save-in-patch flag here */
            garray_setsaveit(garrays[i], 0);
            garray_getfloatwords(garrays[i], &vecsize,
                                 &vecs[i]);
            /* if the resize failed, garray_resize reported the error */
            if (vecsize != framesinfile)
            {
                pd_error(x, "resize failed");
                goto done;
            }
        }
    }
    
    //Final size in frames
    if (!finalsize) finalsize = 0x7fffffff;
    if (finalsize > bytelimit / (channels * bytespersamp))
        finalsize = bytelimit / (channels * bytespersamp);
    
    bufframes = SAMPBUFSIZE / (channels * bytespersamp);
    
    
    for (itemsread = 0; itemsread < finalsize; )
    {
        int thisread = finalsize - itemsread;
        thisread = (thisread > bufframes ? bufframes : thisread);
        nitems = sfxreader_read(sfxreader, sampbuf, thisread * channels * bytespersamp);
        nitems = nitems / (channels * bytespersamp);
        //fread(sampbuf, channels * bytespersamp, thisread, fp);
        if (nitems <= 0) break;
        soundfile_xferin_float(channels, argc, (t_float **)vecs, itemsread,
                               (unsigned char *)sampbuf, nitems, bytespersamp, bigendian,
                               sizeof(t_word)/sizeof(t_sample));
        itemsread += nitems;
    }
    /* zero out remaining elements of vectors */
    
    for (i = 0; i < argc; i++)
    {
        int vecsize;
        garray_getfloatwords(garrays[i], &vecsize, &vecs[i]);
        for (j = (int)itemsread; j < vecsize; j++)
            vecs[i][j].w_float = 0;
    }
    /* zero out vectors in excess of number of channels */
    for (i = channels; i < argc; i++)
    {
        int vecsize;
        t_word *foo;
        garray_getfloatwords(garrays[i], &vecsize, &foo);
        for (j = 0; j < vecsize; j++)
            foo[j].w_float = 0;
    }
    /* do all graphics updates */
    for (i = 0; i < argc; i++)
        garray_redraw(garrays[i]);
    
    sfxreader_close(sfxreader);
    status = -1;
    goto done;
usage:
    pd_error(x, "usage: read [flags] filename tablename...");
    post("flags: -skip <n> -resize -maxsize <n> ...");
    post("-raw <headerbytes> <channels> <bytespersamp> <endian (b, l, or n)>.");
done:
    if (status >= 0)
        sfxreader_close(sfxreader);
    outlet_float(x->x_obj.ob_outlet, (t_float)itemsread);
}

void soundfilerx_setup(void)
{
    soundfilerx_class = class_new(gensym("soundfilerx"), (t_newmethod)soundfilerx_new,
                                 0, sizeof(t_soundfilerx), 0, 0);
    class_addmethod(soundfilerx_class, (t_method)soundfilerx_read, gensym("read"),
                    A_GIMME, 0);
}