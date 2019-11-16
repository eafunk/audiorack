/*
 Copyright (c) 2019 Ethan Funk
 
 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated 
 documentation files (the "Software"), to deal in the Software without restriction, including without limitation 
 the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, 
 and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all copies or substantial portions 
 of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED 
 TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
 THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF 
 CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 DEALINGS IN THE SOFTWARE.
*/

#ifndef _MIXBUFFER_H
#define _MIXBUFFER_H

#ifdef __cplusplus
extern "C"
{
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <jack/jack.h>

typedef struct{
	// values are scalar magnitude
	float	peak;		
	float	avr;
} vuData;

typedef struct {
    jack_default_audio_sample_t	*buf; // bufSizeSamples of audio sample data 
    jack_port_t **busout_jPorts;	// points to arrayCount array of bus output port pointers
	vuData *VUmeters;				// points to arrayCount array of vuData    
    size_t arrayCount;
    size_t index;
    size_t bufSizeSamples;
    size_t totalSizeBytes;
    size_t bufIndexMask;
    unsigned int busses;
    unsigned int channelsPerBus;
   	unsigned char mlocked_flags;
   	jack_client_t *client;
} mixbuffer_t;

void mixbuffer_free(mixbuffer_t *mb_rec);

mixbuffer_t *mixbuffer_create(size_t sizeSamples, 
					unsigned int chanCount, unsigned int busCount, jack_client_t *client);

void mixbuffer_read(mixbuffer_t *mb, size_t sampCnt, 
					unsigned int offset, jack_default_audio_sample_t *dest, 
					unsigned int chan, unsigned int bus);

void mixbuffer_sum(mixbuffer_t *mb, size_t sampCnt, 
					jack_default_audio_sample_t *source, 
					unsigned int chan, unsigned int bus, unsigned char zero);
					
void mixbuffer_advance(mixbuffer_t *mb, size_t sampCnt);

#ifdef __cplusplus
}
#endif

#endif
