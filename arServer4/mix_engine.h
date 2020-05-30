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

#ifndef _MIXENGINE_H
#define _MIXENGINE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include "mixbuffers.h"

#define cbQsize	256		// must be a power of 2
#define cbMASK	cbQsize-1

typedef struct{
	jack_port_id_t portID[cbQsize];
	volatile unsigned int rdIdx;
	volatile unsigned int wrIdx;
	pthread_spinlock_t spinlock;
} callbackQueue;

#define sourceTypeLive 		0
#define sourceTypeCanRepos 	1

#define persistConnected 1
#define persistDisConn 2
#define persistOff 0

typedef struct{
	unsigned char sourceType;
	unsigned char isConnected;
	uint32_t UID;
	pid_t attached;
	FILE *aplFile;
	float nextAplEvent;		// changed flag change_aplEvent set by render when pos > this. Zero prevents render from checking.
	unsigned char aplFPmatch;	// true if apl files's library fingerprint match current library - apl logging can include item IDs.
	unsigned char persist;
	jack_port_t **in_jPorts;	// chanCount size array
	jack_port_t **mm_jPorts;	// chanCount size array
	vuData *VUmeters;		// chanCount array of vuData
	
	/* requested values, set by external threads */
	float reqVol;
	float reqBal;
	float reqPos;
	uint32_t reqBusses;
	uint32_t reqFeedBus;
	float reqFeedVol;
		
	/* current values, set by render thread */
	float vol;
	double pos;			// high res. for small sample chunks
	float bal;
	uint32_t busses;		// bits corispond to enabled bus numbers
	uint32_t feedBus;			// feed mix-minus bus number assignment + 1; zero for no feed bus
	float feedVol;
	unsigned int segNext;	// inNum+1; 0 for no next segue player
	float levelSeg;			// squared average VU level seg holdoff threshold
	float posSeg;
	time_t fadeTime;
	float fadePos;
	
	unsigned char managed;	// true only if this input is associated with a queue list item
	uint32_t requested;	// change bits set by app threads, cleared by render
	uint32_t changed;	// change bits set by Render, cleared app thread
	unsigned char posack;	// true when render should send a position ack control packet
	
	uint32_t status;	// status bits set or cleard by any thread
	unsigned char mutesGroup;
	uint32_t tmpFeedBus;	
} inChannel;

typedef struct {
	uint32_t nameHash;
	char *name;
	char *portList;
	jack_port_t **jPorts;	// chanCount size array
	float vol;
	float delay;			// in seconds
	unsigned int bus;
			
	float reqVol;
	float reqDelay;
	unsigned int reqBus;
	
	unsigned int requested;	// change bits set by app threads, cleared by render
	unsigned int changed;	// change bits set by Render, cleared app thread
	unsigned int muteLevels; // Cue (LSB), MuteA, B, C (MSB) levels -> gain / 255
} outChannel;

typedef struct{
	pthread_mutex_t jackMutex;
	unsigned int chanCount;
	unsigned int inCount;
	unsigned int outCount;
	unsigned int busCount;
	jack_nframes_t mixerSampleRate;
	mixbuffer_t *mixbuses;
	inChannel *ins;		// custom specific structure array
	outChannel *outs;	// custom specific structure array
	jack_client_t *client;
	const char *ourJackName;

	jack_port_t *ctlInPort;		// control data to attached peers
	jack_port_t *ctlOutPort;	// control data from attached peers
	jack_ringbuffer_t *ctlInQueue;
	jack_ringbuffer_t *ctlOutQueue;
	callbackQueue cbQueue;
	pthread_mutex_t cbQueueMutex;
	pthread_cond_t cbQueueSemaphore;
	pthread_mutex_t ctlOutQueueMutex;
	pthread_mutex_t ctlInQueueMutex;
	pthread_cond_t ctlInQueueSemaphore;
	
	pthread_rwlock_t outGrpLock;
	pthread_mutex_t changedMutex;
	pthread_cond_t changedSemaphore;
	uint32_t activeBus;
	unsigned char reqTalkBackBits;	// bits 2, 1, 0 enable Talkback to cue via corrisponding mute groups C, B, A.

/* NOTE: outGrpLock is for read/write locking output group name, nameHash, 
 * and portList string values of the mix engine "outs" list. This is not 
 * used for locking any other vaues in these records. */

} mixEngineRec;

typedef mixEngineRec* mixEngineRecPtr;

enum{
 	status_empty 		=0L,
	status_loading 	=(1L << 0),
	status_standby		=(1L << 1),
	status_playing		=(1L << 2),
	status_hasPlayed	=(1L << 3),
	status_finished	=(1L << 4),
	status_logged		=(1L << 5),
	status_waiting		=(1L << 6),
	status_cueing		=(1L << 7),
	status_running		=(1L << 8),
	status_remove		=(1L << 9),
	status_delete		=(1L << 10),
	status_talkback	=(1L << 11),
	status_deleteWhenDone =(1L << 12)
};

enum{
	change_stat			=(1L << 0),
	change_vol			=(1L << 1),	
	change_pos			=(1L << 2),
	change_bal			=(1L << 3),	
	change_bus			=(1L << 4),	
	change_delay		=(1L << 5),	
	change_play			=(1L << 6),
	change_stop			=(1L << 7),
	change_feedbus		=(1L << 8),
	change_feedvol		=(1L << 9),
	change_aplEvent	=(1L << 10),
	change_loaded		=(1L << 11),
	change_unloaded	=(1L << 12),
	change_type			=(1L << 13),
	change_mutes		=(1L << 14)
};

#define status_render (status_cueing | status_playing | status_talkback)

float def_vol;				// default scalar gain
unsigned int def_busses;	// default bus settings
float def_bal;				// default balance

void setInChanToDefault(inChannel *chrec);
void setAllUnloadedToDefault(mixEngineRecPtr mixRef);
unsigned char checkPnumber(int pNum);
char *initMixer(mixEngineRecPtr *mixEngineRef, unsigned int width, 
						unsigned int inputs, unsigned int outputs, 
						unsigned int buses, const char *server, 
						const char* reqName, jack_options_t options);
						
void shutdownMixer(mixEngineRecPtr mixEngineRef);
void updateOutputConnections(mixEngineRecPtr mixEngineRef, outChannel *rec, 
							unsigned char updatePorts, const char *portList, const char *matchOnly);
							
jack_port_id_t *getCBQitem(callbackQueue *Q);

#ifdef __cplusplus
}
#endif

#endif
