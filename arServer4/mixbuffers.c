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

#include "mixbuffers.h"

void mixbuffer_free(mixbuffer_t *mb_rec){
	size_t i;
	jack_port_t **port;
	
	if(mb_rec){
		if(mb_rec->mlocked_flags & 0x8)
			munlock(mb_rec->VUmeters, sizeof(vuData) * mb_rec->arrayCount);	
		if(mb_rec->mlocked_flags & 0x4)
			munlock(mb_rec->busout_jPorts, sizeof(jack_port_t *) * mb_rec->arrayCount);
		if(mb_rec->mlocked_flags & 0x2)
			munlock(mb_rec->buf, mb_rec->totalSizeBytes);
		if(mb_rec->mlocked_flags & 0x1)
			munlock(mb_rec, sizeof(mixbuffer_t));		
		/* free ports */
		if(port = mb_rec->busout_jPorts){
			for(i=0; i < mb_rec->arrayCount; i++){				
				if(*port)
					jack_port_unregister(mb_rec->client, *port);
				port++;
			}
		}
		free(mb_rec->busout_jPorts);	
			
		/* free VU meters array */		
		if(mb_rec->VUmeters)
			free(mb_rec->VUmeters);
			
		/* free buffer */		
		if(mb_rec->buf)
			free(mb_rec->buf);
			
		/* free record */
		free(mb_rec);	
	}
}

mixbuffer_t *mixbuffer_create(size_t sizeSamples, unsigned int chanCount, 
						unsigned int busCount , jack_client_t *client){
    unsigned int i, c;
	size_t size;
	size_t bitDepth;
	mixbuffer_t *mb_rec;
	jack_port_t **port;
	char pname[32];


	if((mb_rec = (mixbuffer_t *)malloc(sizeof(mixbuffer_t))) == NULL)
		return NULL;
	
	mb_rec->mlocked_flags = 0;	
	mb_rec->busses = busCount;
	mb_rec->channelsPerBus = chanCount;
	mb_rec->arrayCount = busCount * chanCount;
	mb_rec->index = 0;
	mb_rec->client = client;

	for(bitDepth = 1; (1 << bitDepth) < sizeSamples; bitDepth++);

	mb_rec->bufSizeSamples = 1 << bitDepth;
	mb_rec->bufIndexMask = mb_rec->bufSizeSamples;
	mb_rec->bufIndexMask -= 1;
	mb_rec->totalSizeBytes = mb_rec->arrayCount * mb_rec->bufSizeSamples 
							* sizeof(jack_default_audio_sample_t);

	if((mb_rec->buf = (jack_default_audio_sample_t*)malloc(mb_rec->totalSizeBytes)) == NULL) {
		mixbuffer_free(mb_rec);
		return NULL;
	}
	
	/* Create Jack ports for output groups */
	if(mb_rec->busout_jPorts = (jack_port_t**)calloc(mb_rec->arrayCount, 
											sizeof(jack_port_t *))){
		port = mb_rec->busout_jPorts;
		for(i=0; i<busCount; i++){
			for(c=0; c<chanCount; c++){
				snprintf(pname, sizeof pname, "mixBus%dch%d", i, c);
				*port = jack_port_register(mb_rec->client, pname,
					JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
				if(*port == NULL){
					mixbuffer_free(mb_rec);
					return NULL;
				}
				port++;
			}
		}
	}else{
		mixbuffer_free(mb_rec);
		return NULL;
	}
	
	mb_rec->VUmeters = (vuData*)calloc(mb_rec->arrayCount, sizeof(vuData));
	if(mb_rec->VUmeters == NULL){
		mixbuffer_free(mb_rec);
		return NULL;
	}

	if(mlock(mb_rec, sizeof(mixbuffer_t)) == 0)
		mb_rec->mlocked_flags = 1;
	if(mlock(mb_rec->buf, mb_rec->totalSizeBytes) == 0)
		mb_rec->mlocked_flags += 2;
	if(mlock(mb_rec->busout_jPorts, sizeof(jack_port_t*) * mb_rec->arrayCount))
		mb_rec->mlocked_flags += 4;
	if(mlock(mb_rec->VUmeters, sizeof(vuData) * mb_rec->arrayCount))
		mb_rec->mlocked_flags += 8;	

	return mb_rec;						
}

void mixbuffer_read(mixbuffer_t *mb, size_t sampCnt, 
					unsigned int offset, jack_default_audio_sample_t *dest, 
					unsigned int chan, unsigned int bus){
	
	size_t i, i_end, i_start, head, tail;
	unsigned int b;
	jack_default_audio_sample_t *ptrA, *ptrB;
	
	b = (mb->channelsPerBus * bus) + chan;
	ptrA = mb->buf + (mb->bufSizeSamples * b);
	
	i = mb->index - offset;
	i_start = i & mb->bufIndexMask;
	i = i_start + sampCnt;
	i_end = i & mb->bufIndexMask;
	if(i == i_end){
		/* single segment: i_start to i_end */
		ptrA += i_start;
		memcpy(dest, ptrA, sampCnt * sizeof(jack_default_audio_sample_t));
	}else{
		/* wrap around: two part copy */
		/* i_start through mb_rec->bufIndexMask */
		head = mb->bufIndexMask - i_start;
		ptrB = ptrA + i_start;
		memcpy(dest, ptrB, head * sizeof(jack_default_audio_sample_t));
		/* 0 to i_end */
		tail = sampCnt - head;
		memcpy(dest, ptrA, tail * sizeof(jack_default_audio_sample_t));
	}
}

void mixbuffer_sum(mixbuffer_t *mb, size_t sampCnt, 
					jack_default_audio_sample_t *source, 
					unsigned int chan, unsigned int bus, unsigned char zero){
	/* if zero is true, soure is ignored and the buffer is zeroed for 
	 *  sampCnt samples */
	size_t i, i_end, i_start, head, tail;
	unsigned int b;
	jack_default_audio_sample_t *ptrA, *ptrB;
	
	b = (mb->channelsPerBus * bus) + chan;
	ptrA = mb->buf + (mb->bufSizeSamples * b);
	
	i_start = mb->index;
	i = i_start + sampCnt;
	i_end = i & mb->bufIndexMask;
	if(i == i_end){
		/* single segment: i_start to i_end */
		ptrA += i_start;
		if(zero)
			memset(ptrA, 0, sampCnt * sizeof(jack_default_audio_sample_t));
		else{
			for(i=0; i<sampCnt; i++){
				*ptrA += *source;
				ptrA++;
				source++;
			}
		}
		
	}else{
		/* wrap around: two part copy */
		/* i_start through mb_rec->bufIndexMask */
		head = mb->bufIndexMask - i_start;
		tail = sampCnt - head;
		ptrB = ptrA + i_start;
		if(zero){
			memset(ptrB, 0, head * sizeof(jack_default_audio_sample_t));
			memset(ptrA, 0, tail * sizeof(jack_default_audio_sample_t));
		}else{
			for(i=0; i<head; i++){
				*ptrB += *source;
				ptrB++;
				source++;
			}
			/* 0 to i_end */
			for(i=0; i<tail; i++){
				*ptrA += *source;
				ptrA++;
				source++;
			}
		}
	}					
}
					
void mixbuffer_advance(mixbuffer_t *mb, size_t sampCnt){
	size_t newIndex;
	
	newIndex = mb->index + sampCnt;
	mb->index = newIndex & mb->bufIndexMask;
}
