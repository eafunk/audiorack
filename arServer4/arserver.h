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

/*	Default file locations

User startup config file: 		~/.audiorack/ars_startup.conf
	if file above is not found, a default startup is loaded at: 
								/opt/audiorack/support/user_startup.conf
								
	The following are set in the startup config file: 
	Other config files in:		~/.audiorack/
	Unified server log file:	/var/opt/audiorack/logs/arserver.log
	
Daemon startup config file: 	Specified with the -c command line parameter.
								typically set to:
									/etc/opt/audiorack/ars_startup.conf
								A Template file can be found at:
									/opt/audiorack/support/root_startup.conf

	The following are set in the startup config file:
	Other config files in:		/etc/opt/audiorack/
	Server log file:			/var/log/arserver.log
								 
lock/run file:					/var/run/ars{TCPport#}.pid
supporting files in:			/opt/audiorack/support/
help file:						/opt/audiorack/support/ars_help.txt
bin files in:					/opt/audiorack/bin/
	
*/
#ifndef _ARSERVER_H
#define _ARSERVER_H

#ifdef __cplusplus
extern "C"
{
#endif

// Mount: /private/var/automount/Network/Vault
// URLfile://localhost/private/var/automount/Network/Vault/Music/2NU/Unknown%20Album/Frank's%20Chair.mp3

// /run/user/1000/gvfs/smb-share:server=192.168.15.6,share=vault/Music/2NU/Unknown Album/Frank's Chair.mp3
// vault/Music/2NU/Unknown Album/Frank's Chair.mp3

#include "mix_engine.h"
#include "dispatch.h"
#include "stdbool.h"

#define UserSettingsDirectory ".audiorack/"
#define AppSupportDirectory	"/opt/audiorack/support/"
#define DefLockfileDirectory "/var/run/"
#define directoryToken '/'
#define directoryTokenStr "/"

#if defined(__APPLE__) && defined(__MACH__)
	#define DefPrefixList "/Volumes/,/private/var/automount/Network/,/Network/"
/*#elif _WIN32
    Fuck Microsoft! */
#else
	#define DefPrefixList "/mnt/,/media/*/,/run/user/*/gvfs/*share="
#endif

enum{
	ctl_vol	=(1L << 0),
	ctl_pos		=(1L << 1),
	ctl_skipf	=(1L << 2),
	ctl_skipb	=(1L << 3),
	ctl_fade	=(1L << 4),
	ctl_feed	=(1L << 5)
};

// rec status bits: b0-14 status, b16-31 are error code, if any 
enum{
 	rec_uninit			=0L,
	rec_ready			=(1L << 0),
	rec_start			=(1L << 1),
	rec_running			=(1L << 2),		// note: same as status_playing 
	rec_stop			=(1L << 3),
	rec_done			=(1L << 5),		// recorder time limit exceeded
	rec_locked			=(1L << 7),
	rec_err_write		=(1L << 8),		// recorder encoder write error
	rec_err_keepup		=(1L << 9),		// recorder encoder can't keep up with audio
	rec_err_con_fail	=(1L << 10),	// recorder connection failure (stream encoder)
	rec_err_comp		=(1L << 11),	// recorder compression error
	rec_conn			=(1L << 12),	// recorder connecting
	rec_wait			=(1L << 13),	// recorder time record waiting
	rec_err_other		=(1L << 14)		// error number in high 16 bit word
};

enum{
	auto_off = 0,
	auto_live,
	auto_unatt
};

enum{
	live_fill 		=(1L << 0),
	live_schedule	=(1L << 1),
	live_stop		=(1L << 2),
	live_target		=(1L << 3)
};

typedef union { 
	float_t			fVal;
	int32_t			iVal;
	int8_t			cVal[4];
} valuetype;

typedef struct __attribute__((packed)){		/* top level structure of notify packet */
	int8_t				marker;	// NULL value as a packet marker in session text stream
	int8_t				type;
	uint16_t				dataSize;	// excluding the 4 bytes of this header
	int8_t				data[1];
} notifyConatiner;

typedef struct __attribute__((packed)){	
	uint32_t			senderID;
	int32_t				reference;
	union {
		float_t			fVal;
		int32_t			iVal;
		uint8_t			cVal[4];
	} value;
} notifyData;

typedef struct __attribute__((packed)){
	// values are saclar magnitude
	uint8_t			count;	// number of vuInstances in data
	int8_t			data[1];
} vuNContainer;

typedef struct __attribute__((packed)){		/* data structures inside of vuNContainer */
	uint32_t uid;	// 0 for output buses or uid of player, recorder, etc.
	uint8_t	count;		// number of channels in vuData array... sequencial from 0
	int8_t	data[1];	
} vuNInstance;

typedef struct __attribute__((packed)){		/* data structures for each channel inside of vuInstance */
	// values are saclar magnitude
	uint8_t	peak;		// value is 255 times the sqrt of scalar magnitude (VU metere like scaling)
	uint8_t	avr;		// value is 255 times the sqrt of scalar magnitude (VU metere like scaling)
} vuNData; 

typedef struct {		/* entry in notify queue */
	void *next;			// next record in list, or NULL for end
	int32_t				spare;
	notifyConatiner		container;
} notifyEntry;

/* Global Variables */
extern const char *versionStr;	// set in main.c
extern const char *versionCR;	// set in main.c
extern mixEngineRecPtr mixEngine;
extern unsigned char run;	// signal by program to threads/processes to stop
extern unsigned char quit;	// set by user action to exit program
extern unsigned char restart;
extern unsigned int lastp;
extern time_t live_event;
extern unsigned int autoLiveTimeout;
extern char *startup_path;
extern char *dbi_path;
extern char *wdir_path;

#ifdef __cplusplus
}
#endif

#endif
