//
//  readsfx.c
//  readsfx
//
//  Created by Johannes Burström on 2014-06-05.
//  Most of this is borrowed from Pure Data, d_soundfile.c, which is

/* Copyright (c) 1997-1999 Miller Puckette.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */


//  Copyright (c) 2014 Johannes Burström.
//

#include "readsfx.h"

/* g_array.c */
int garray_ambigendian(void)
{
    unsigned short s = 1;
    unsigned char c = *(char *)(&s);
    return (c==0);
}

static void soundfile_xferin_sample(int sfchannels, int nvecs, t_sample **vecs,
                                    long itemsread, unsigned char *buf, int nitems, int bytespersamp,
                                    int bigendian, int spread)
{
    int i, j;
    unsigned char *sp, *sp2;
    t_sample *fp;
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


static t_class *readsfx_tilde_class;

typedef struct _readsfx
{
    t_object x_obj;
    t_canvas *x_canvas;
    t_clock *x_clock;
    char *x_buf;                            /* soundfile buffer */
    int x_bufsize;                          /* buffer size in bytes */
    int x_noutlets;                         /* number of audio outlets */
    t_sample *(x_outvec[MAXSFCHANS]);       /* audio vectors */
    int x_vecsize;                          /* vector size for transfers */
    t_outlet *x_bangout;                    /* bang-on-done outlet */
    int x_state;                            /* opened, running, or idle */
    t_float x_insamplerate;   /* sample rate of input signal if known */
    /* parameters to communicate with subthread */
    int x_requestcode;      /* pending request from parent to I/O thread */
    char *x_filename;       /* file to open (string is permanently allocated) */
    int x_fileerror;        /* slot for "errno" return */
     int x_bytespersample;   /* bytes per sample (2 or 3) */
    int x_bigendian;        /* true if file is big-endian */
    int x_sfchannels;       /* number of channels in soundfile */
    t_float x_samplerate;     /* sample rate of soundfile */
    long x_onsetframes;     /* number of sample frames to skip */
    long x_bytelimit;       /* max number of data bytes to read */
    int x_fd;               /* filedesc */
    int x_fifosize;         /* buffer size appropriately rounded down */
    int x_fifohead;         /* index of next byte to get from file */
    int x_fifotail;         /* index of next byte the ugen will read */
    int x_eof;              /* true if fifohead has stopped changing */
    int x_sigcountdown;     /* counter for signalling child for more data */
    int x_sigperiod;        /* number of ticks per signal */
    int x_filetype;         /* writesf~ only; type of file to create */
    int x_itemswritten;     /* writesf~ only; items writen */
    int x_swap;             /* writesf~ only; true if byte swapping */
    t_float x_f;              /* writesf~ only; scalar for signal inlet */
    pthread_mutex_t x_mutex;
    pthread_cond_t x_requestcondition;
    pthread_cond_t x_answercondition;
    pthread_t x_childthread;
    
    //void* x_reader_obj; // Our file reader
    
} t_readsfx;


/************** the child thread which performs file I/O ***********/

#if 1
static void pute(char *s)   /* debug routine */
{
    write(2, s, strlen(s));
}
#define DEBUG_SOUNDFILE
#endif

#if 1
#define sfread_cond_wait pthread_cond_wait
#define sfread_cond_signal pthread_cond_signal
#else
#include <sys/time.h>    /* debugging version... */
#include <sys/types.h>
static void readsfx_fakewait(pthread_mutex_t *b)
{
    struct timeval timout;
    timout.tv_sec = 0;
    timout.tv_usec = 1000000;
    pthread_mutex_unlock(b);
    select(0, 0, 0, 0, &timout);
    pthread_mutex_lock(b);
}

#define sfread_cond_wait(a,b) readsfx_fakewait(b)
#define sfread_cond_signal(a)
#endif

static void *readsfx_child_main(void *zz)
{
    t_readsfx *x = zz;
    void * reader_obj = sfxreader_new();
#ifdef DEBUG_SOUNDFILE
    pute("1\n");
#endif
    pthread_mutex_lock(&x->x_mutex);
    int fd, fifohead;
    char *buf;
    while (1)
    {

#ifdef DEBUG_SOUNDFILE
        pute("0\n");
#endif
        if (x->x_requestcode == REQUEST_NOTHING)
        {
#ifdef DEBUG_SOUNDFILE
            pute("wait 2\n");
#endif
            sfread_cond_signal(&x->x_answercondition);
            sfread_cond_wait(&x->x_requestcondition, &x->x_mutex);
#ifdef DEBUG_SOUNDFILE
            pute("3\n");
#endif
        }
        else if (x->x_requestcode == REQUEST_OPEN)
        {
#ifdef DEBUG_SOUNDFILE
            char boo[80];
#endif
            int sysrtn, wantbytes;
            
            //---------START OPEN SOUNDFILE-------------------
            
            /* copy file stuff out of the data structure so we can
             relinquish the mutex while we're in open_soundfile(). */
            long onsetframes = x->x_onsetframes;
            long bytelimit = 0x7fffffff;
            int bytespersample = x->x_bytespersample;
            int sfchannels = x->x_sfchannels;
            int bigendian = x->x_bigendian;
            //void* reader_obj = x->x_reader_obj;

            char *filename = x->x_filename;
            char *dirname = canvas_getdir(x->x_canvas)->s_name;
            
#ifdef DEBUG_SOUNDFILE
            pute("4\n");
#endif
            /* alter the request code so that an ensuing "open" will get
noticed. */
            x->x_requestcode = REQUEST_BUSY;
            x->x_fileerror = 0;
            
            /* if there's already a file open, close it */
            if (x->x_fd >= 0)
            {
                fd = x->x_fd;
                pthread_mutex_unlock(&x->x_mutex);
                sfxreader_close(reader_obj);
                pthread_mutex_lock(&x->x_mutex);
                x->x_fd = -1;
                if (x->x_requestcode != REQUEST_BUSY)
                    goto lost;
            }
            /* open the soundfile with the mutex unlocked */
            pthread_mutex_unlock(&x->x_mutex);
            
#ifdef DEBUG_SOUNDFILE
            pute("Pre-open\n");
#endif

            fd = sfxreader_open(reader_obj, dirname, filename, &bytespersample, &bigendian,
                                &sfchannels, &bytelimit, onsetframes);
            
            pthread_mutex_lock(&x->x_mutex);
         
            
#ifdef DEBUG_SOUNDFILE
            pute("5\n");
#endif
            /* copy back into the instance structure. */
            x->x_bytespersample = bytespersample;
            x->x_sfchannels = sfchannels;
            x->x_bigendian = bigendian;
            x->x_fd = fd;
            x->x_bytelimit = bytelimit;
            if (fd < 0)
            {
                x->x_fileerror = errno;
                x->x_eof = 1;
#ifdef DEBUG_SOUNDFILE
                pute("open failed\n");
                pute(filename);
                pute(dirname);
#endif
                goto lost;
            }
            //---------END OPEN SOUNDFILE-------------------
            
            /* check if another request has been made; if so, field it */
            if (x->x_requestcode != REQUEST_BUSY)
                goto lost;
#ifdef DEBUG_SOUNDFILE
            pute("6\n");
#endif
            
            x->x_fifohead = 0;
            /* set fifosize from bufsize.  fifosize must be a
             multiple of the number of bytes eaten for each DSP
             tick.  We pessimistically assume MAXVECSIZE samples
             per tick since that could change.  There could be a
             problem here if the vector size increases while a
             soundfile is being played...  */
            x->x_fifosize = x->x_bufsize - (x->x_bufsize %
                                            (x->x_bytespersample * x->x_sfchannels * MAXVECSIZE));
            /* arrange for the "request" condition to be signalled 16
             times per buffer */
#ifdef DEBUG_SOUNDFILE
            sprintf(boo, "fifosize %d\n",
                    x->x_fifosize);
            pute(boo);
#endif
            x->x_sigcountdown = x->x_sigperiod =
            (x->x_fifosize /
             (16 * x->x_bytespersample * x->x_sfchannels *
              x->x_vecsize));
            /* in a loop, wait for the fifo to get hungry and feed it */
            
            while (x->x_requestcode == REQUEST_BUSY)
            {
                int fifosize = x->x_fifosize;
#ifdef DEBUG_SOUNDFILE
                pute("77\n");
#endif
                if (x->x_eof)
                    break;
                if (x->x_fifohead >= x->x_fifotail)
                {
                    /* if the head is >= the tail, we can immediately read
                     to the end of the fifo.  Unless, that is, we would
                     read all the way to the end of the buffer and the
                     "tail" is zero; this would fill the buffer completely
                     which isn't allowed because you can't tell a completely
                     full buffer from an empty one. */
                    if (x->x_fifotail || (fifosize - x->x_fifohead > READSIZE))
                    {
                        wantbytes = fifosize - x->x_fifohead;
                        if (wantbytes > READSIZE)
                            wantbytes = READSIZE;
                        if (wantbytes > x->x_bytelimit)
                            wantbytes = (int)x->x_bytelimit;
#ifdef DEBUG_SOUNDFILE
                        sprintf(boo, "head %d, tail %d, size %d\n",
                                x->x_fifohead, x->x_fifotail, wantbytes);
                        pute(boo);
#endif
                    }
                    else
                    {
#ifdef DEBUG_SOUNDFILE
                        pute("wait 7a ...\n");
#endif
                        sfread_cond_signal(&x->x_answercondition);
#ifdef DEBUG_SOUNDFILE
                        pute("signalled\n");
#endif
                        sfread_cond_wait(&x->x_requestcondition,
                                         &x->x_mutex);
#ifdef DEBUG_SOUNDFILE
                        pute("7a done\n");
#endif
                        continue;
                    }
                }
                else
                {
                    /* otherwise check if there are at least READSIZE
                     bytes to read.  If not, wait and loop back. */
                    wantbytes =  x->x_fifotail - x->x_fifohead - 1;
                    if (wantbytes < READSIZE)
                    {
#ifdef DEBUG_SOUNDFILE
                        pute("wait 7...\n");
#endif
                        sfread_cond_signal(&x->x_answercondition);
                        sfread_cond_wait(&x->x_requestcondition,
                                         &x->x_mutex);
#ifdef DEBUG_SOUNDFILE
                        pute("7 done\n");
#endif
                        continue;
                    }
                    else wantbytes = READSIZE;
                    if (wantbytes > x->x_bytelimit)
                        wantbytes = (int)x->x_bytelimit;
                }
#ifdef DEBUG_SOUNDFILE
                pute("8\n");
#endif
                fd = x->x_fd;
                buf = x->x_buf;
                fifohead = x->x_fifohead;
                pthread_mutex_unlock(&x->x_mutex);
                
                //Här läser från filen.
                //Denna skulle kanske gå att översätta?
                //sysrtn = read(fd, buf + fifohead, wantbytes);
                
                sysrtn = sfxreader_read(reader_obj, buf + fifohead, wantbytes);
                
                pthread_mutex_lock(&x->x_mutex);
                if (x->x_requestcode != REQUEST_BUSY)
                    break;
                if (sysrtn < 0)
                {
#ifdef DEBUG_SOUNDFILE
                    pute("fileerror\n");
#endif
                    
                    x->x_fileerror = errno;
                    break;
                }
                else if (sysrtn == 0)
                {
                    x->x_eof = 1;
                    break;
                }
                else
                {
                    x->x_fifohead += sysrtn;
                    x->x_bytelimit -= sysrtn;
                    if (x->x_fifohead == fifosize)
                        x->x_fifohead = 0;
                    if (x->x_bytelimit <= 0)
                    {
                        x->x_eof = 1;
                        break;
                    }
                }
#ifdef DEBUG_SOUNDFILE
                sprintf(boo, "after: head %d, tail %d\n",
                        x->x_fifohead, x->x_fifotail);
                pute(boo);
#endif
                /* signal parent in case it's waiting for data */
                sfread_cond_signal(&x->x_answercondition);
            }
        lost:
            
            if (x->x_requestcode == REQUEST_BUSY)
                x->x_requestcode = REQUEST_NOTHING;
            /* fell out of read loop: close file if necessary,
             set EOF and signal once more */
            if (x->x_fd >= 0)
            {
                fd = x->x_fd;
                pthread_mutex_unlock(&x->x_mutex);
                close (fd);
                pthread_mutex_lock(&x->x_mutex);
                x->x_fd = -1;
            }
            sfread_cond_signal(&x->x_answercondition);
            
        }
        else if (x->x_requestcode == REQUEST_CLOSE)
        {
            

            if (x->x_fd >= 0)
            {
//                void* reader_obj = x->x_reader_obj;
                fd = x->x_fd;
                pthread_mutex_unlock(&x->x_mutex);
                sfxreader_close(reader_obj);
                pthread_mutex_lock(&x->x_mutex);
                x->x_fd = -1;
            }
            if (x->x_requestcode == REQUEST_CLOSE)
                x->x_requestcode = REQUEST_NOTHING;
            sfread_cond_signal(&x->x_answercondition);
        }
        else if (x->x_requestcode == REQUEST_QUIT)
        {
            

            if (x->x_fd >= 0)
            {
//                void* reader_obj = x->x_reader_obj;
                fd = x->x_fd;
                pthread_mutex_unlock(&x->x_mutex);
                sfxreader_free(reader_obj);
                pthread_mutex_lock(&x->x_mutex);
                x->x_fd = -1;
            }
            x->x_requestcode = REQUEST_NOTHING;
            sfread_cond_signal(&x->x_answercondition);
            break;
        }
        else
        {
#ifdef DEBUG_SOUNDFILE
            pute("13\n");
#endif
        }
    }
#ifdef DEBUG_SOUNDFILE
    pute("thread exit\n");
#endif
    pthread_mutex_unlock(&x->x_mutex);
    return (0);
}

/******** the object proper runs in the calling (parent) thread ****/

static void readsfx_tick(t_readsfx *x);

 void *readsfx_new(t_floatarg fnchannels, t_floatarg fbufsize)
{
    t_readsfx *x;
    int nchannels = fnchannels, bufsize = fbufsize, i;
    char *buf;
    
    if (nchannels < 1)
        nchannels = 1;
    else if (nchannels > MAXSFCHANS)
        nchannels = MAXSFCHANS;
    if (bufsize <= 0) bufsize = DEFBUFPERCHAN * nchannels;
    else if (bufsize < MINBUFSIZE)
        bufsize = MINBUFSIZE;
    else if (bufsize > MAXBUFSIZE)
        bufsize = MAXBUFSIZE;
    buf = getbytes(bufsize);
    if (!buf) return (0);
    
    x = (t_readsfx *)pd_new(readsfx_tilde_class);
    
    for (i = 0; i < nchannels; i++)
        outlet_new(&x->x_obj, gensym("signal"));
    x->x_noutlets = nchannels;
    x->x_bangout = outlet_new(&x->x_obj, &s_bang);
    pthread_mutex_init(&x->x_mutex, 0);
    pthread_cond_init(&x->x_requestcondition, 0);
    pthread_cond_init(&x->x_answercondition, 0);
    x->x_vecsize = MAXVECSIZE;
    x->x_state = STATE_IDLE;
    x->x_clock = clock_new(x, (t_method)readsfx_tick);
    x->x_canvas = canvas_getcurrent();
    x->x_bytespersample = 2;
    x->x_sfchannels = 1;
    x->x_fd = -1;
    x->x_buf = buf;
    x->x_bufsize = bufsize;
    x->x_fifosize = x->x_fifohead = x->x_fifotail = x->x_requestcode = 0;
//    x->x_reader_obj = sfxreader_new();
    pthread_create(&x->x_childthread, 0, readsfx_child_main, x);
    return (x);
}

 void readsfx_tick(t_readsfx *x)
{
    outlet_bang(x->x_bangout);
}

 t_int *readsfx_perform(t_int *w)
{
    t_readsfx *x = (t_readsfx *)(w[1]);
    int vecsize = x->x_vecsize, noutlets = x->x_noutlets, i, j,
    bytespersample = x->x_bytespersample,
    bigendian = x->x_bigendian;
    t_sample *fp;
    if (x->x_state == STATE_STREAM)
    {
        int wantbytes, sfchannels = x->x_sfchannels;
        
        pthread_mutex_lock(&x->x_mutex);
        wantbytes = sfchannels * vecsize * bytespersample;
        while (
               !x->x_eof && x->x_fifohead >= x->x_fifotail &&
               x->x_fifohead < x->x_fifotail + wantbytes-1)
        {
            
#ifdef DEBUG_SOUNDFILE
            pute("wait...\n");
#endif
            sfread_cond_signal(&x->x_requestcondition);
            sfread_cond_wait(&x->x_answercondition, &x->x_mutex);
            /* resync local cariables -- bug fix thanks to Shahrokh */
            vecsize = x->x_vecsize;
            bytespersample = x->x_bytespersample;
            sfchannels = x->x_sfchannels;
            wantbytes = sfchannels * vecsize * bytespersample;
            bigendian = x->x_bigendian;
#ifdef DEBUG_SOUNDFILE
            pute("done\n");
#endif
        }
        if (x->x_eof && x->x_fifohead >= x->x_fifotail &&
            x->x_fifohead < x->x_fifotail + wantbytes-1)
        {
            int xfersize;
            if (x->x_fileerror)
            {
                pd_error(x, "dsp: %s: %s", x->x_filename,
                         (x->x_fileerror == EIO ?
                          "unknown or bad header format" :
                          strerror(x->x_fileerror)));
            }
            clock_delay(x->x_clock, 0);
            x->x_state = STATE_IDLE;
            
            /* if there's a partial buffer left, copy it out. */
            xfersize = (x->x_fifohead - x->x_fifotail + 1) /
            (sfchannels * bytespersample);
            if (xfersize)
            {
                soundfile_xferin_sample(sfchannels, noutlets, x->x_outvec, 0,
                                        (unsigned char *)(x->x_buf + x->x_fifotail), xfersize,
                                        bytespersample, bigendian, 1);
                vecsize -= xfersize;
            }
            /* then zero out the (rest of the) output */
            for (i = 0; i < noutlets; i++)
                for (j = vecsize, fp = x->x_outvec[i] + xfersize; j--; )
                    *fp++ = 0;
            
            sfread_cond_signal(&x->x_requestcondition);
            pthread_mutex_unlock(&x->x_mutex);
            return (w+2);
        }
        
        soundfile_xferin_sample(sfchannels, noutlets, x->x_outvec, 0,
                                (unsigned char *)(x->x_buf + x->x_fifotail), vecsize,
                                bytespersample, bigendian, 1);
        
        x->x_fifotail += wantbytes;
        if (x->x_fifotail >= x->x_fifosize)
            x->x_fifotail = 0;
        if ((--x->x_sigcountdown) <= 0)
        {
            sfread_cond_signal(&x->x_requestcondition);
            x->x_sigcountdown = x->x_sigperiod;
        }
        pthread_mutex_unlock(&x->x_mutex);
    }
    else
    {
        for (i = 0; i < noutlets; i++)
            for (j = vecsize, fp = x->x_outvec[i]; j--; )
                *fp++ = 0;
    }
    return (w+2);
}

 void readsfx_start(t_readsfx *x)
{
    /* start making output.  If we're in the "startup" state change
     to the "running" state. */
    if (x->x_state == STATE_STARTUP)
        x->x_state = STATE_STREAM;
    else pd_error(x, "readsfx: start requested with no prior 'open'");
}

 void readsfx_stop(t_readsfx *x)
{
    /* LATER rethink whether you need the mutex just to set a variable? */
    pthread_mutex_lock(&x->x_mutex);
    x->x_state = STATE_IDLE;
    x->x_requestcode = REQUEST_CLOSE;
    sfread_cond_signal(&x->x_requestcondition);
    pthread_mutex_unlock(&x->x_mutex);
}

 void readsfx_float(t_readsfx *x, t_floatarg f)
{
    if (f != 0)
        readsfx_start(x);
    else readsfx_stop(x);
}

/* open method.  Called as:
 open filename [skipframes headersize channels bytespersamp endianness]
 (if headersize is zero, header is taken to be automatically
 detected; thus, use the special "-1" to mean a truly headerless file.)
 */

 void readsfx_open(t_readsfx *x, t_symbol *s, int argc, t_atom *argv)
{
    t_symbol *filesym = atom_getsymbolarg(0, argc, argv);
    t_float onsetframes = atom_getfloatarg(1, argc, argv);
    t_symbol *endian = atom_getsymbolarg(5, argc, argv);
    if (!*filesym->s_name)
        return;
    pthread_mutex_lock(&x->x_mutex);
    x->x_requestcode = REQUEST_OPEN;
    x->x_filename = filesym->s_name;
    x->x_fifotail = 0;
    x->x_fifohead = 0;
    if (*endian->s_name == 'b')
        x->x_bigendian = 1;
    else if (*endian->s_name == 'l')
        x->x_bigendian = 0;
    else if (*endian->s_name)
        pd_error(x, "endianness neither 'b' nor 'l'");
    else x->x_bigendian = garray_ambigendian();
    x->x_onsetframes = (onsetframes > 0 ? onsetframes : 0);
    
    //These should be set when opening the file
    x->x_sfchannels = 1;
    x->x_bytespersample = 2;
    
    x->x_eof = 0;
    x->x_fileerror = 0;
    x->x_state = STATE_STARTUP;
    sfread_cond_signal(&x->x_requestcondition);
    pthread_mutex_unlock(&x->x_mutex);
}

 void readsfx_dsp(t_readsfx *x, t_signal **sp)
{
    int i, noutlets = x->x_noutlets;
    pthread_mutex_lock(&x->x_mutex);
    x->x_vecsize = sp[0]->s_n;
    
    x->x_sigperiod = (x->x_fifosize /
                      (x->x_bytespersample * x->x_sfchannels * x->x_vecsize));
    for (i = 0; i < noutlets; i++)
        x->x_outvec[i] = sp[i]->s_vec;
    pthread_mutex_unlock(&x->x_mutex);
    dsp_add(readsfx_perform, 1, x);
}

 void readsfx_print(t_readsfx *x)
{
    post("state %d", x->x_state);
    post("fifo head %d", x->x_fifohead);
    post("fifo tail %d", x->x_fifotail);
    post("fifo size %d", x->x_fifosize);
    post("fd %d", x->x_fd);
    post("eof %d", x->x_eof);
}

 void readsfx_free(t_readsfx *x)
{
    /* request QUIT and wait for acknowledge */
    void *threadrtn;
    pthread_mutex_lock(&x->x_mutex);
    x->x_requestcode = REQUEST_QUIT;
    sfread_cond_signal(&x->x_requestcondition);
    while (x->x_requestcode != REQUEST_NOTHING)
    {
        sfread_cond_signal(&x->x_requestcondition);
        sfread_cond_wait(&x->x_answercondition, &x->x_mutex);
    }
    pthread_mutex_unlock(&x->x_mutex);
    if (pthread_join(x->x_childthread, &threadrtn))
        error("readsfx_free: join failed");
    
    pthread_cond_destroy(&x->x_requestcondition);
    pthread_cond_destroy(&x->x_answercondition);
    pthread_mutex_destroy(&x->x_mutex);
    freebytes(x->x_buf, x->x_bufsize);
    clock_free(x->x_clock);
}

void readsfx_tilde_setup(void)
{
    readsfx_tilde_class = class_new(gensym("readsfx~"), (t_newmethod)readsfx_new,
                             (t_method)readsfx_free, sizeof(t_readsfx), 0, A_DEFFLOAT, A_DEFFLOAT, 0);
    class_addfloat(readsfx_tilde_class, (t_method)readsfx_float);
    class_addmethod(readsfx_tilde_class, (t_method)readsfx_start, gensym("start"), 0);
    class_addmethod(readsfx_tilde_class, (t_method)readsfx_stop, gensym("stop"), 0);
    class_addmethod(readsfx_tilde_class, (t_method)readsfx_dsp, gensym("dsp"), 0);
    class_addmethod(readsfx_tilde_class, (t_method)readsfx_open, gensym("open"),
                    A_GIMME, 0);
    class_addmethod(readsfx_tilde_class, (t_method)readsfx_print, gensym("print"), 0);
}



