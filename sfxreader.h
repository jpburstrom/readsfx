//
//  sfxReader.h
//  readsfx
//
//  Created by Johannes Burström on 2014-06-05.
//  Copyright (c) 2014 Johannes Burström. All rights reserved.
//


#ifndef __readsfx__sfxReader__
#define __readsfx__sfxReader__

#ifdef __cplusplus

#include <iostream>
#include "AudioToolbox/AudioToolbox.h"
#include "m_pd.h"
//#include "CAAudioBufferList.h"


typedef struct SfxCallbackData
{
	AudioFileID *fileIDPtr;
    
	void *sourceBuffer;
	UInt64 packetOffset;
	bool moreData;
    
	UInt64 totalPacketCount;
	UInt32 maxPacketSize;
    
	AudioStreamPacketDescription aspd;
	UInt32 bytesCounter;
} SfxCallbackData;


class SfxReader {
private:
    
    
    bool is_playing;
    
    void GetFormatFromInputFile(CFURLRef infileURL, AudioStreamBasicDescription & inputFormat);
    
public:
    
    ExtAudioFileRef infile;
    AudioStreamBasicDescription inputFormat, clientFormat;
    
    SfxReader();
    ~SfxReader();
    
    int open(char* dirname, char* filename, int *p_bytespersamp, int *p_bigendian, int *p_nchannels, long *p_bytelimit, long skipframes);
    
    void close();
    
    int read(void* buf, int bytes);
    
    AudioBufferList* AllocateABL(UInt32 channelsPerFrame, UInt32 bytesPerFrame, bool interleaved, UInt32 capacityFrames);
    
    
};


extern "C" {
#endif //__cplusplus
    
    void* sfxreader_new();
    
    void sfxreader_free(void* o);
    int sfxreader_open(void* o, char* dirname, char* filename, int *p_bytespersamp, int *p_bigendian, int *p_nchannels, long *p_bytelimit, long skipframes);
    void sfxreader_close(void* o);
    
    int sfxreader_read(void* o, void *buf, int bytes);
    
#ifdef __cplusplus
}
#endif //


#endif /* defined(__readsfx__sfxReader__) */
