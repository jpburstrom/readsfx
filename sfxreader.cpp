//
//  sfxReader.cpp
//  readsfx~
//
//  Created by Johannes Burström on 2014-06-05.
//  Copyright (c) 2014 Johannes Burström. 
//

//#define SFXDEBUG

#include "sfxReader.h"


#ifdef SFXDEBUG
static void postError(std::string pre, OSStatus error)
{
    char str[7];
    // see if it appears to be a 4-char-code
    *(UInt32 *)(str + 1) = CFSwapInt32HostToBig(error);
    if (isprint(str[1]) && isprint(str[2]) && isprint(str[3]) && isprint(str[4])) {
        str[0] = str[5] = '\'';
        str[6] = '\0';
    } else if ((int) error == 0) {
        sprintf(str, "OK");
    } else {
        // no, format it as an integer
        sprintf(str, "%d", (int)error);
    }
    post("%s: %s", pre.c_str(), str);
}
#define ERR postError
#else
#define ERR
#endif


using namespace std;

SfxReader::SfxReader()
{
    is_open = false;
}


SfxReader::~SfxReader()
{
    close();
}

void SfxReader::close()
{
    ExtAudioFileDispose(infile);
    is_open = false;
}

int SfxReader::openFile(char *dirname, char *filename)
{
    char path[MAXPDSTRING];
    CFURLRef infileURL;
    OSStatus err;
    
    close();
    
    int fd = readsfx_get_path(dirname, filename, path, MAXPDSTRING);
    if (fd < 0) {
        goto fail;
    }
    
    
    infileURL = CFURLCreateFromFileSystemRepresentation(NULL, (UInt8*)path, strlen(path), false);
    
    //We need inputFormat for channel count
    try {
        GetFormatFromInputFile(infileURL, inputFormat);
    } catch (...) {
         goto fail;
    }
    err = ExtAudioFileOpenURL (infileURL, &infile);
    if (err) goto fail;


    is_open = true;
    return 0;
    
fail:
    return -1;
    
}

int SfxReader::open(char* dirname, char* filename, int *p_bytespersamp, int *p_bigendian, int *p_nchannels, long *p_bytelimit, long skipframes)
{
    UInt32 size = 0;
    OSStatus err;
    
    if (-1 == openFile(dirname, filename))
        goto fail;
    
    //24-bit should be enough for our purposes. 32-bit float crashes.
    FillOutASBDForLPCM(clientFormat, sys_getsr(), inputFormat.mChannelsPerFrame, 24, 24, false, false);
    size = sizeof(clientFormat);
    
    *p_bytespersamp = clientFormat.mBytesPerFrame / clientFormat.mChannelsPerFrame;
    *p_bigendian = (int)(clientFormat.mFormatFlags & kAudioFormatFlagIsBigEndian);
    *p_nchannels = clientFormat.mChannelsPerFrame;
    *p_bytelimit = 0x7fffffff; //Needed?
    
    err = ExtAudioFileSetProperty(infile, kExtAudioFileProperty_ClientDataFormat, size, &clientFormat);
    
    if (err) goto fail;
    
    if (skipframes > 0)
        err = ExtAudioFileSeek(infile, skipframes);

    if (err) goto fail;
    
    return 0; //FIXME

fail:
    return -1;

}

int SfxReader::read(void *buf, int bytes)
{
 
    AudioBufferList fillBufList;
    fillBufList.mNumberBuffers = 1;
    fillBufList.mBuffers[0].mNumberChannels = inputFormat.mChannelsPerFrame;
    fillBufList.mBuffers[0].mDataByteSize = bytes;
    fillBufList.mBuffers[0].mData = buf;
    
    // client format is always linear PCM - so here we determine how many frames of lpcm
    // we can read/write given our buffer size
    UInt32 numFrames = (bytes / clientFormat.mBytesPerFrame);

    ExtAudioFileRead (infile, &numFrames, &fillBufList);
   
    if (!numFrames) {
        // this is our termination condition
        return 0; //EOF
    }
    
    return numFrames * clientFormat.mBytesPerFrame;

}

UInt64 SfxReader::getNumFrames()
{
    UInt64 frames = 0;
    UInt32 size = sizeof(frames);
    OSStatus err = ExtAudioFileGetProperty(infile, kExtAudioFileProperty_FileLengthFrames, &size, &frames);
    
    if (err)
        return -1;
    
    return frames;
        
    
}

void SfxReader::getInfo(char* dirname, char* filename, sfxdata *data)
{
    int status;
    bool do_close = false;
    
    if (!is_open) {
        status = openFile(dirname, filename);
        do_close = true;
    } else {
        status = 0;
    }
    
    if (status >= 0) {
        data->frames = getNumFrames();
        data->channels = inputFormat.mChannelsPerFrame;
        data->samplerate = inputFormat.mSampleRate;
        data->bits = 8 * inputFormat.mBytesPerFrame / inputFormat.mChannelsPerFrame;
        data->bigendian = (int)(inputFormat.mFormatFlags & kAudioFormatFlagIsBigEndian);

        data->ms = (double)((data->samplerate > 0) ? 1000.0 * ((double)data->frames / (double) data->samplerate): 0.0);
    }
    
    if (do_close)
        close();
    
}


void    SfxReader::GetFormatFromInputFile (CFURLRef infileURL, AudioStreamBasicDescription & inputFormat)
{
    AudioFileID inputFile;
    UInt32 size;
    OSStatus err;
    
    err = AudioFileOpenURL(infileURL, kAudioFileReadPermission, 0, &inputFile);
    
//    ERR("GetFormat", err);
    
    AudioFileGetPropertyInfo(inputFile, kAudioFilePropertyFormatList, &size, NULL);
    UInt32 numFormats = size / sizeof(AudioFormatListItem);
    AudioFormatListItem *formatList = new AudioFormatListItem [ numFormats ];
    
	
    
    AudioFileGetProperty(inputFile, kAudioFilePropertyFormatList, &size, formatList);
    numFormats = size / sizeof(AudioFormatListItem); // we need to reassess the actual number of formats when we get it
    if (numFormats == 1) {
        // this is the common case
        inputFormat = formatList[0].mASBD;
    } else {
          // now we should look to see which decoders we have on the system
        AudioFormatGetPropertyInfo(kAudioFormatProperty_DecodeFormatIDs, 0, NULL, &size);
        UInt32 numDecoders = size / sizeof(OSType);
        OSType *decoderIDs = new OSType [ numDecoders ];
        AudioFormatGetProperty(kAudioFormatProperty_DecodeFormatIDs, 0, NULL, &size, decoderIDs);
        unsigned int i = 0;
        for (; i < numFormats; ++i) {
            OSType decoderID = formatList[i].mASBD.mFormatID;
            bool found = false;
            for (unsigned int j = 0; j < numDecoders; ++j) {
                if (decoderID == decoderIDs[j]) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        delete [] decoderIDs;
        
        if (i >= numFormats) {
            fprintf (stderr, "Cannot play any of the formats in this file\n");
            throw kAudioFileUnsupportedDataFormatError;
        }
        inputFormat = formatList[i].mASBD;
    }
    delete [] formatList;
    AudioFileClose(inputFile);
}


//------ C interface ------------

void* sfxreader_new() {
    return (void *) new SfxReader();
}

void sfxreader_free(void* o) {
    //ERR("sfxreader: free", 0);
    delete (SfxReader*) o;
}

int sfxreader_open(void* o, char* dirname, char* filename, int *p_bytespersamp, int *p_bigendian, int *p_nchannels, long *p_bytelimit, long skipframes) {
    SfxReader* sp = (SfxReader*) o;
    return sp->open(dirname, filename, p_bytespersamp, p_bigendian, p_nchannels, p_bytelimit, skipframes);
}

void sfxreader_close(void* o)
{
    SfxReader* sp = (SfxReader*) o;
    sp->close();
}

int sfxreader_read(void* o, void* buf, int bytes) {
    SfxReader* sp = (SfxReader*) o;
    return sp->read(buf, bytes);
    
}

long sfxreader_get_nframes(void* o) {
    SfxReader* sp = (SfxReader*) o;
    return (long)sp->getNumFrames();
}

void sfxreader_info(char* dirname, char* filename, sfxdata *data) {
    SfxReader sf;
    sf.getInfo(dirname, filename, data);
    
}