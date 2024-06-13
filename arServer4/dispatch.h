/*
  Copyright (C) 2019 Ethan Funk
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.
  
  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, write to the Free Software 
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*/

#ifndef _DISPATCH_H
#define _DISPATCH_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "arserver.h"
#include "utilities.h"
#include "mix_engine.h"

#define 	nType_vol		0x01	// player or output volume change, ref=0xC0000000 + index for output or 0x00 + index for input/player
#define 	nType_bal		0x02	// player balance change
#define 	nType_bus		0x03	// player or output  bus change
#define 	nType_pstat		0x04	// player status change
#define 	nType_status	0x05	// over all status change - use stat to get info
#define 	nType_mstat		0x06	// meta data record change - use dumpmeta to syncronize
#define 	nType_rstat		0x07	// recorder status change - use rstat to get info
#define 	nType_vu			0x08	// vu levels packet
#define 	nType_rgain		0x09	// recorder gain changed, ref=UID
#define 	nType_pos		0x0a	// player position change
#define 	nType_del		0x0b	// item deleted, ref=UID
#define 	nType_dly		0x0c	// delay setting changed - sync with current delay setting
#define 	nType_load		0x10	// Processor load, cVal[0] = % realtime JACK load, 0.8 format
									
// the following are no longer used in audiorack4 

#define 	nType_fxparam	0x0d	// AudioUnit effects processing parameter changed - notifyFXParam struct sent
#define 	nType_fxchain	0x0e	// Audio effects processing chain change - use fxslots to get info, 
									//	iVal = bus number
									//	reference = UID/OID of item
#define 	nType_fxcue		0x0f	// Audio effects cue change - use fxslots to get info.
									//	iVal = bus number
									//	reference = UID/OID of item or zero for fxcue disable
#define 	nType_outvol	0x11	// Change made to output volume use dumpout to update


// cType (control packet) real-time packets are handled directly
// in the jack process function.  Others are queued for handing in a 
// non-realtime thread.

#define cPeer_MASK		0x70
#define cPeer_none		0x00
#define cPeer_player		0x10
#define cPeer_recorder	0x20
#define cPeer_bus			0x30
#define cPeer_allrec		0x40

#define cType_MASK		0x0f
#define cType_tags		0	// tags - data is counted json string of track tags
#define cType_pos			1	// position - data is a float (seconds)
#define cType_start		2	// start - data is empty
#define cType_stop		3	// stop - data is empty
#define cType_end			4	// reached media end - data is empty.  Closes recorder
#define cType_anc			5	// anounce (recorder) - data is json collection of recorder settings.
#define cType_vu			6	// vU meters (recorder) - data is array of vuNData type (see below)
#define cType_err			7	// error message - data is uint32 error code
#define cType_reid		8	// change UID (recorder) - data is a new uint32 UID
#define cType_vol			9	// change volume - data is a float
#define cType_lock		10	// set recorder to locked - data is empty
#define cType_unlock		11	// set recorder to unlocked - data is empty
#define cType_posack		12	// arServer acknowlage of pos change back to a player - data is empty

typedef struct __attribute__((packed)){
						/* Structure of contol packet: used to control/communicate with recorders and players
						 * NOTE: packets passed as midi data via jackaudio midi API, even though the data format
						 * is not midi.  Jack is fine with this, but don't try to connect the arserver control midi
						 * ports to other applications that are expecting actual midi data. */
	uint8_t			sysExFlag;
	uint8_t			topBits;
	uint8_t			type;
	uint32_t			peer;		// network byte order - player input number [0,N] or recorder UID.
	uint16_t			dataSize;	// network byte order - size, in bytes, of the raw data prior to 7 bit endoding, if any.
	int8_t			data[1];	// network byte order or text.  Text need NOT to be null terminated
									// due to the size (length) specified above.
} controlPacket;

typedef struct {
		void *next;			// next record in list, or NULL for end
		struct tm when;
		char *message;
} ServerLogRecord;

typedef struct {
		void *next;			// next record in list, or NULL for end
		uint32_t ID;
		uint32_t UID;
		time_t when;
		char *name;
		char *artist;
		char *album;
		char *source;
		char *comment;
		char *owner;
		char *webURL;
		uint32_t location;
		uint32_t albumID;
		uint32_t artistID;
		uint32_t ownerID;
		uint32_t logID;
		unsigned char added;
		unsigned char played;
		unsigned char post;
} ProgramLogRecord;

extern time_t logChangeTime;
extern uint32_t	log_busses;
extern pthread_mutex_t lastsegMutex;
extern pthread_cond_t lastsegSemaphore;

extern int sip_ctl_sock;
extern pthread_mutex_t sipMutex;

extern int sipStatus;

unsigned char initDispatcherThreads(void);
void shutdownDispatcherThreads(void);
void serverLogMakeEntry(char *message);
unsigned char serverLogRotateLogFile(void);

void notifyMakeEntry(char type, void *data, unsigned short size);

void programLogMakeEntry(ProgramLogRecord *entry);
void programLogUIDEntry(uint32_t passUID, unsigned char Added, unsigned char Played);

unsigned char queueControlOutPacket(mixEngineRecPtr mixRef, char type, uint32_t peer, size_t size, char *data);

uint16_t controlDataSizeFromRaw(uint16_t byteCount);
unsigned char decodeControlPacket(controlPacket *packet, char headerOnly);
char *encodeControlPacket(controlPacket *header, char *data, uint16_t *size, char *extraPtr);

#ifdef __cplusplus
}
#endif

#endif
