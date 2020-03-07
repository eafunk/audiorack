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

#include "automate.h"
#include "data.h"
#include "media.h"
#include "database.h"
#include "utilities.h"
#include "mix_engine.h"
#include "dispatch.h"
#include "tasks.h"
#include "arserver.h"

#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>

time_t live_event = 0;
time_t silent_event = 0;
char silent_tryseg = 0;
float silent_thresh = 0;
uint32_t silent_bus = 0;
uint32_t silent_timeout = 0;
unsigned int autoLiveTimeout = 0;

uint32_t plRev = 0;
uint32_t autoState = 0;
unsigned char plRunning = 0;
char *fillStr = NULL;		// thread protect by queueLock mutex

pthread_mutex_t mgrMutex;
pthread_cond_t mgrSemaphore;

unsigned char initAutomator(void){
	// call AFTER user config has been loaded
	char *tmp;
	live_event = time(NULL);
	silent_event = time(NULL);
	
	fillStr = strdup("");
	
	// set defaults if configuration load did not set values
	if(!silent_thresh)
		silent_thresh = 0.003;	// -50dB
	if(silent_bus)
		silent_bus = 2;			// main bus
	
	if(GetMetaInt(0, "auto_startup", NULL)){
		autoState = auto_unatt;
		serverLogMakeEntry("[automation] -:Switched to auto (startup action)");
	}else{
		autoState = auto_off;
		serverLogMakeEntry("[automation] -:Switched to off (startup action)");
	}
	
	pthread_mutex_init(&mgrMutex, NULL);
	pthread_cond_init(&mgrSemaphore, NULL);
}

void shutdownAutomator(void){
	// delete any unplayed playlist items from the database logs
	uint32_t logID;
	uint32_t *ptr;
	queueRecord *rec;
	
	pthread_rwlock_rdlock(&queueLock);
	rec = (queueRecord *)&queueList;
	while(rec = (queueRecord *)getNextNode((LinkedListEntry *)rec)){
		logID = GetMetaInt(rec->UID, "logID", NULL);
		if(logID){
			if(ptr = malloc(sizeof(uint32_t))){
				*ptr = logID;
				createTaskItem("Delete Log Entry", DeleteLogEntry, (void*)ptr, 0, -1, 180, 1); // time out in 3 minutes
			}
		}
	}	
	pthread_rwlock_unlock(&queueLock);

	free(fillStr);
	
	pthread_mutex_destroy(&mgrMutex);
	pthread_cond_destroy(&mgrSemaphore);
}

uint32_t AddItem(int pos, char *URLstr, char *adder, uint32_t adderUID){    
	// assumes queueLock is NOT locked on entry!
	queueRecord *instance, *prev;
	uint32_t newID, adderID;
	char *tmp;
	char buf[32];
	
	// create/get metadata for URL	
	newID = createMetaRecord(URLstr, NULL, 0);
	// fill the metadata record
	GetURLMetaData(newID, URLstr);
	
	//load the meta data into the item UID meta data.
	if(strlen(adder) > 0)
		SetMetaData(newID, "Owner", adder);
	adderID = 0;
	if(adderUID){
		adderID = GetMetaInt(adderUID, "ID", NULL);
		SetMetaData(newID, "Together", (tmp = GetMetaData(adderUID, "Together", 0)));
		free(tmp);
	}
	snprintf(buf, sizeof buf, "%u", adderID);
	SetMetaData(newID, "OwnerID", buf);
	
	// add handling below for missing file items and task items
	if(GetMetaInt(newID, "Missing", NULL)){
		// the file is missing... delete this meta record and fail
		releaseMetaRecord(newID);
		return 0;
	}
	
	if(instance = createQueueRecord(newID)){		
		// insert or append item into queue list
		pthread_rwlock_wrlock(&queueLock);
		if(pos < 0){
			// negative position indicates end of the list
			appendNode((LinkedListEntry *)&queueList, (LinkedListEntry *)instance);
		}else{
			if(prev = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, (unsigned)pos))
				insertAfterNode((LinkedListEntry *)prev, (LinkedListEntry *)instance);
			else
				appendNode((LinkedListEntry *)&queueList, (LinkedListEntry *)instance);
		}
		pthread_rwlock_unlock(&queueLock);

		tmp = GetMetaData(newID, "Type", 0);
		if(!strcmp(tmp, "task")){
			// try running the task
			if(!dbTaskRunner(newID, 0)){
				// run failed... delete this instance
				pthread_rwlock_wrlock(&queueLock);
				releaseQueueRecord((queueRecord	*)&queueList, instance, 0);
				pthread_rwlock_unlock(&queueLock);
				free(tmp);
				// and fail...
				return 0;
			}
		}
		free(tmp);
		
		plRev++;
		
		// create program log entry of added item
		programLogUIDEntry(newID, 1, 0);

		// send out notifications
		notifyData	data;
		data.reference = 0;
		data.senderID = getSenderID();
		data.value.iVal = 0;
		notifyMakeEntry(nType_status, &data, sizeof(data));	
		data.reference = htonl(newID);
		notifyMakeEntry(nType_mstat, &data, sizeof(data));
	}else{
		// failed to create new queue record
		releaseMetaRecord(newID);
		return 0;
	}
    return newID;
}

void AddPlayer(int pos, int pNum){    
	// assumes queueLock is NOT locked!

	inChannel *instance;
	queueRecord *rec, *prev;
	
	if(checkPnumber(pNum)){
		instance = &mixEngine->ins[pNum];
	}else
		// invalid player number
		return;

	pthread_rwlock_rdlock(&queueLock);
	if(!instance->UID || findNode((LinkedListEntry *)&queueList, instance->UID, NULL, NULL)){
		// player's UID was found in the list already... bail out
		pthread_rwlock_unlock(&queueLock);
		return;
	}
	pthread_rwlock_unlock(&queueLock);
	
	if(rec = createQueueRecord(instance->UID)){
		retainMetaRecord(instance->UID);
		rec->player = pNum + 1;
		// insert or append item into queue list
		pthread_rwlock_wrlock(&queueLock);
		if(pos < 0){
			// negative position indicates end of the list
			appendNode((LinkedListEntry *)&queueList, (LinkedListEntry *)rec);
		}else{
			if(prev = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, (unsigned)pos))
				insertAfterNode((LinkedListEntry *)prev, (LinkedListEntry *)rec);
			else
				appendNode((LinkedListEntry *)&queueList, (LinkedListEntry *)rec);
		}
		pthread_rwlock_unlock(&queueLock);

		// clear needed status flags
		instance->status = instance->status & ~status_hasPlayed;
		instance->status = instance->status & ~status_logged;
		
		plRev++;

		// create program log entry of added item
		programLogUIDEntry(instance->UID, 1, 0);

		// send out notifications
		notifyData	data;
		data.reference = 0;
		data.senderID = getSenderID();
		data.value.iVal = 0;
		notifyMakeEntry(nType_status, &data, sizeof(data));	
		data.reference = htonl(instance->UID);
		notifyMakeEntry(nType_mstat, &data, sizeof(data));
    }
}

uint32_t SplitItem(uint32_t parent, char *URLstr, unsigned char last){    
	// assumes queueLock is NOT locked!

	queueRecord *instance, *parentNode;
	LinkedListEntry *prevNode;
	uint32_t newID;
	char *tmp;
	float idur, pdur;
	
	// create/get metadata for URL	
	newID = createMetaRecord(URLstr, NULL, 0);
	// fill the metadata record
	GetURLMetaData(newID, URLstr);			

	pthread_rwlock_wrlock(&queueLock);
	if(parentNode = (queueRecord *)findNode((LinkedListEntry *)&queueList, parent, NULL, &prevNode)){
		if(instance = createQueueRecord(newID)){
			insertAfterNode(prevNode, (LinkedListEntry *)instance);
			pthread_rwlock_unlock(&queueLock);
			// inherit meta data from parent
			SetMetaData(newID, "Owner", (tmp = GetMetaData(parent, "Name", 0)));
			free(tmp);
			SetMetaData(newID, "OwnerID", (tmp = GetMetaData(parent, "ID", 0)));
			free(tmp);
			tmp = GetMetaData(parent, "Together", 0);
			if(strlen(tmp))
				SetMetaData(newID, "Together", tmp);
			free(tmp);
			if(atof(tmp = GetMetaData(parent, "FadeOut", 0)) > 0.0)
				SetMetaData(newID, "FadeOut", tmp);
			free(tmp);
			if(atof(tmp = GetMetaData(parent, "SegIn", 0)) > 0.0)
				SetMetaData(newID, "SegIn", tmp);
			free(tmp);
			if(atof(tmp = GetMetaData(parent, "FadeTime", 0)) != 0.0)
				SetMetaData(newID, "FadeTime", tmp);
			free(tmp);
			if(atof(tmp = GetMetaData(parent, "Volume", 0)) != 0.0)
				SetMetaData(newID, "Volume", tmp);
			free(tmp);
			SetMetaData(newID, "fx_config", (tmp = GetMetaData(parent, "fx_config", 0)));
			free(tmp);
			SetMetaData(newID, "def_segout", (tmp = GetMetaData(parent, "def_segout", 0)));
			free(tmp);
			SetMetaData(newID, "def_seglevel", (tmp = GetMetaData(parent, "def_seglevel", 0)));
			free(tmp);
			SetMetaData(newID, "def_bus", (tmp = GetMetaData(parent, "def_bus", 0)));
			free(tmp);
			SetMetaData(newID, "TargetTime", (tmp = GetMetaData(parent, "TargetTime", 0)));
			free(tmp);
			SetMetaData(newID, "FillTime", (tmp = GetMetaData(parent, "FillTime", 0)));
			free(tmp);
			
			// inheritance of TRUE if TRUE in parent
			if(atoi(tmp = GetMetaData(parent, "NoPost", 0)))
				SetMetaData(newID, "NoPost", tmp);
			free(tmp);
			if(atoi(tmp = GetMetaData(parent, "NoLog", 0)))
				SetMetaData(newID, "NoLog", tmp);
			free(tmp);

			// parent gets/looses it's priority to the child
			SetMetaData(newID, "Priority", (tmp = GetMetaData(parent, "Priority", 0)));
			free(tmp);
			if(last)
				SetMetaData(parent, "Priority", "0");
				
			// parent gives it's duration to the child if child has no duration
			if(atof(tmp = GetMetaData(newID, "Duration", 0)) == 0.0)
				SetMetaData(newID, "Duration", tmp);
			free(tmp);
			
			// and parent loss of duration to child and segout is cleared
			if(last){
				SetMetaData(parent, "Duration", "0.0");
				SetMetaData(parent, "SegOut", "0.0");			
			}else{
				pdur = GetMetaFloat(parent, "Duration", NULL);
				idur = GetMetaFloat(newID, "Duration", NULL);
				pdur = pdur - idur;
				if(pdur < 0.0)
					pdur = 0.0;
				tmp = fstr(pdur, 1);
				SetMetaData(parent, "Duration", tmp);
				free(tmp);
			}

			tmp = GetMetaData(newID, "Type", 0);
			if(!strcmp(tmp, "task")){
				// try running the task
				if(!dbTaskRunner(newID, 0)){
					// run failed... delete this record and fail
					free(tmp);
					pthread_rwlock_wrlock(&queueLock);
					releaseQueueRecord((queueRecord *)&queueList, instance, 0);
					pthread_rwlock_unlock(&queueLock);
					goto fail;
				}
			}
			free(tmp);
			
			// Handling for missing file items
			if(GetMetaInt(newID, "Missing", NULL)){
				// the file is missing... delete this record and fail
				pthread_rwlock_wrlock(&queueLock);
				releaseQueueRecord((queueRecord *)&queueList, instance, 0);
				pthread_rwlock_unlock(&queueLock);
				goto fail;			
			}
			
			plRev++;
			
			// create program log entry of added item
			programLogUIDEntry(newID, 1, 0);

			// send out notifications
			notifyData	data;
			data.reference = 0;
			data.senderID = getSenderID();
			data.value.iVal = 0;
			notifyMakeEntry(nType_status, &data, sizeof(data));	
			data.reference = htonl(newID);
			notifyMakeEntry(nType_mstat, &data, sizeof(data));
			data.reference = htonl(parent);
			notifyMakeEntry(nType_mstat, &data, sizeof(data));			
			return newID;
		}
	}
	pthread_rwlock_wrlock(&queueLock);
	wakeQueManager();
	sched_yield();
	
fail:
	releaseMetaRecord(newID);
	return 0;
}

int LoadItem(int pos, queueRecord *qrec){    
	// assumes queueLock is alread write locked!
	uint32_t UID, result;
	inChannel *instance;
	int i;
	char *tmp;

	if(!qrec){
		// use index to find record
		if(pos < 0)
			return -2;
		qrec = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, pos);
		if(!qrec)
			return -2; // missing delete
	}

	if(qrec->status & status_running){
		// it's a task or play list that is running... do nothing and don't delete it
		return -3;  //don't delete
	}
	if(qrec->player){
		// already in a player!
		return (-4 - (qrec->player-1));  //don't delete
	}
    
	if(!qrec->UID)
		// No meta Data to base load on...
		return -2;  //delete
    
	tmp = GetMetaData(qrec->UID, "Type", 0);
	if(!strcmp(tmp, "stop")){
		// it's a playlist stop item... don't do anything!
		free(tmp);
		return -3;  //don't delete
	}
	if(!strcmp(tmp, "filepl") || !strcmp(tmp, "playlist")){
		// it's a playlist
		plTaskRunner(qrec->UID);
		return -3;  //don't delete
	}
	free(tmp);

	// load player
	tmp = GetMetaData(qrec->UID, "URL", 0);
	i = -1;  // load from next available player
	if(result = LoadPlayer(&i, tmp, qrec->UID, 0)){
		if(i > -1){
			if(checkPnumber(i)){
				instance = &mixEngine->ins[i];
				// set playlist queue record player number
				qrec->player = i+1;
				// flag the palyer as being managed (since we loaded it and not a user)
				instance->managed = 1;
				instance->status =  instance->status | status_deleteWhenDone;
				// new rev number since the list changed
				plRev++;    
				// send out notifications
				notifyData	data;
				data.reference = 0;
				data.senderID = getSenderID();
				data.value.iVal = 0;
				notifyMakeEntry(nType_status, &data, sizeof(data));	
			
				free(tmp);
				return i;
			}else{
				free(tmp);
				return -2;   // delete item
			}
		}else{
			// no available players: leave it in the list to try again later
			free(tmp);
			return -3; 
		}
	}else{
		if(i > -1){
			free(tmp);
			return -2;  // delete item
		}else{
			// no available players: leave it in the list to try again later
			free(tmp);
			return -3; 
		}
	}
}

void UnloadItem(int pos, queueRecord *qrec){    
	// assumes queueLock is alread write locked!

	uint32_t c, cmax;
	inChannel *instance;
	jack_port_t **port;
	
	if(!qrec){
		// use index to find record
		if(pos < 0)
			return;
		qrec = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, pos);
		if(!qrec)
			return;
	}

	if(checkPnumber(qrec->player-1)){
		instance = &mixEngine->ins[qrec->player-1];
		if(!instance->managed || (instance->status & status_playing))
			// don't unload an item that is playing or is not "managed" 
			// i.e. the user laoded it into a player, not automation.
			return;
		// retain the queue record (reference count++) so that
		// the player unload doesn't also remove from the queue list
		retainListRecord((LinkedListEntry *)qrec);
	
		qrec->status = 0;
		qrec->player = 0;
		port = instance->in_jPorts;
		cmax = mixEngine->chanCount;
		pthread_mutex_lock(&mixEngine->jackMutex);
		for(c=0; c<cmax; c++){
			jack_port_disconnect(mixEngine->client, *port);
			port++;
		}
		pthread_mutex_unlock(&mixEngine->jackMutex);
	}
}

void MoveItem(int sourcePos, int destPos, unsigned char clearTimes){    
	// assumes queueLock is alread write locked!

	LinkedListEntry *fromRec, *toRec;
	queueRecord *rec;
	uint32_t UID;
	inChannel *instance;
    
	if(sourcePos < 0)
		return;
     
	if(sourcePos == destPos)
		return;
        
	fromRec = getNthNode((LinkedListEntry *)&queueList, sourcePos+1);
	if(!fromRec)
		return;
	
	rec = (queueRecord *)fromRec;
	if(rec->player && (checkPnumber(rec->player-1))){
		instance = &mixEngine->ins[rec->player-1];
		if(instance->status & status_playing)
			// don't move playing items
			return;
	}
	
	toRec = NULL;
	if(destPos >= 0)
		if(destPos <= sourcePos)
			// toRec is the record just before the index specified
			toRec = getNthNode((LinkedListEntry *)&queueList, destPos);
		else
			// toRec is the record at the index specified
			toRec = getNthNode((LinkedListEntry *)&queueList, destPos+1);
			
	if(!toRec)
		//use last record if toRec is out of bounds or invalid
		toRec = getNthNode((LinkedListEntry *)&queueList, countNodesAfter((LinkedListEntry *)&queueList));
		
	if(fromRec = moveAfterNode(fromRec, toRec, (LinkedListEntry *)&queueList)){
		if(destPos >= mixEngine->inCount){
			// unload from player if loaded and move up past the number of mixer inputes
			UnloadItem(destPos, NULL);
		}
		plRev++;  // new rev number since the list changed 

		if(clearTimes){
			if(UID = rec->UID){
				// clear all scheduled time properties
				SetMetaData(UID, "TargetTime", "0");
				SetMetaData(UID, "Priority", "0");
				SetMetaData(UID, "FillTime", "0");
			}
		}
	}
}

void setSegTimes(inChannel *thisp, inChannel *nextp, int nextNum){
	double seginT, segoutT, dur, defseg;
	int t_priority, n_priority;
	char *tmp;
	   
	if(thisp == NULL)
		return;
	if(nextp == NULL)
		return;  
        
	// NOTE: Negative number forces imediate segue.
	
	// set seg level
	thisp->levelSeg = GetMetaFloat(thisp->UID, "def_seglevel", NULL);
	if(thisp->levelSeg == 0.0)
		thisp->levelSeg = GetMetaFloat(0, "def_seglevel", NULL);
	//get segout time and duration of the source item
	segoutT = GetMetaFloat(thisp->UID, "SegOut", NULL);
	if(segoutT)
		// disable level segue if a specific segOut was set
		thisp->levelSeg = 0.0;
	if(segoutT < 0.0) 
		segoutT = 0.0;
	dur = GetMetaFloat(thisp->UID, "Duration", NULL);
	if(dur){
		if(segoutT == 0.0){
			// no segout time specified, use default segout time from the end of the item
			defseg = GetMetaFloat(thisp->UID, "def_segout", NULL);
			if(defseg <= 0.0){
				defseg = GetMetaFloat(0, "def_segout", NULL);
			}
			segoutT = dur - defseg;
			if(segoutT < 0.0)
				segoutT = 0.0;
		}
	}
	if(segoutT > dur)
		segoutT = dur;
	//get segin time of the next item
	seginT = GetMetaFloat(nextp->UID, "SegIn", NULL);
	if(seginT)
		// disable level segue if a specific segIn was set
		thisp->levelSeg = 0.0;
	
	// adjust segout time to account for the segin time of the next item
	segoutT = segoutT - seginT;
	if(segoutT < 0.0) 
		segoutT = 0.0;

	n_priority = GetMetaInt(nextp->UID, "Priority", NULL);
	t_priority = GetMetaInt(thisp->UID, "Priority", NULL);
	if((n_priority > 9) && (t_priority < n_priority)){ // priority 10 or higher forces fade into next at exact time
		thisp->fadeTime = GetMetaFloat(nextp->UID, "TargetTime", NULL);
		if(thisp->fadeTime < 0)
			thisp->fadeTime = 0.1;
	}else
		thisp->fadeTime = GetMetaFloat(thisp->UID, "FadeTime", NULL);
	thisp->segNext = nextNum+1;
	thisp->posSeg = (float)segoutT;
	thisp->fadePos = GetMetaFloat(thisp->UID, "FadeOut", NULL);
}

void NextListItem(uint32_t lastStat, queueRecord *curQueRec, int *firstp, float *sbtime, float remtime, unsigned char *isPlaying){
	// assumes queueLock is alread write locked!
	inChannel *thisIn, *nextIn;
	queueRecord *instance, *next;
	int result, nextp, priority;
	uint32_t status, busses;
	double targetTime, dur;
	unsigned char force;
	char *tmp, *type;

	*firstp = -1;
	force = 0;
	next = NULL;
	priority = 0;
	targetTime = 0.0;
		
	instance = (queueRecord *)getNextNode((LinkedListEntry *)curQueRec);
	if(instance == NULL)
		return;		// bad item or end of list trap
		
	if(curQueRec == (queueRecord *)&queueList){
		tmp = GetMetaData(instance->UID, "Type", 0);
		if(!strcmp(tmp, "stop")){
			// it's a playlist stop item... stop the playlist and delete it!
			plRunning = 0;
			releaseQueueRecord((queueRecord	*)&queueList, instance, 0);
			// send out notifications
			notifyData	data;
			data.reference = 0;
			data.senderID = getSenderID();
			data.value.iVal = 0;
			notifyMakeEntry(nType_status, &data, sizeof(data));	
			free(tmp);
			return;
		}
		free(tmp);
	}
	
	// our current item has not been deleted... continue
	status = getQueueRecStatus(instance, &thisIn);
	if(thisIn)
		busses = thisIn->busses;
	else
		busses = 0;
	if(!checkPnumber(instance->player-1) || (((status & status_playing) == 0) && ((busses & 2L) == 0))){
		// not in a player or in a player and not playing
		// check the next one to see if it is playing
		if(next = (queueRecord *)getNextNode((LinkedListEntry *)instance)){
			if(checkPnumber(next->player-1)){
				status = getQueueRecStatus(next, &nextIn);
				if((nextIn && nextIn->managed) && (status & status_playing) && ((nextIn->busses & 2L) == 0)){
					// next item IS playing, and IS NOT in cue... and it WAS NOT  put there manually - delete this item
					releaseQueueRecord((queueRecord	*)&queueList, instance, 0);
					return;
				}
			}
		}
	}
	// stuff to do for items that are NOT marked to be deleted or removed
	force = 0;
	// special case for priority > 9 items...
	priority = GetMetaInt(instance->UID, "Priority", NULL);
	if(priority > 9){
		targetTime = GetMetaFloat(instance->UID, "TargetTime", NULL);
		if(targetTime != 0){ 
			// High priority (fade previous for exact time)
			// if previous item is loaded, force this one to load too regardless of the amount of time previously cued!
			if(lastStat & status_standby)
				force = 1;
//				*sbtime = 0;
		}
	}

	if(!instance->player){
		// Not in a player... load the item if needed
		if(force || (*sbtime < 60)){
			// less than 60 seconds cued up in players... load this items too
			tmp = GetMetaData(instance->UID, "Type", 0);
			if(strlen(tmp)){
				result = LoadItem(0, instance);
				if(result < 0){
					if(result == -1){
						// waiting for a player
						instance->status = status_waiting;
					}else if(result == -2){
						// can't load it... delete
						releaseQueueRecord((queueRecord	*)&queueList, instance, 0);
						free(tmp);
						return;
					} 
					// -3 or less, leave it alone		
				}else{
					// exit after loading - times are invalid now.
					free(tmp);
					return;
				}
			}
			free(tmp);
		}
	} 
	
	*firstp = instance->player-1;
	status = getQueueRecStatus(instance, &thisIn);
	if(status & status_standby){
		if(status & status_hasPlayed){
			dur = GetMetaFloat(instance->UID, "Duration", NULL);
			if((status & status_finished) || (dur <= 0)){
				if((status & status_playing) == 0){
					// has played but is now either finished or has zero duration and has been stoped
					releaseQueueRecord((queueRecord	*)&queueList, instance, 0);
					*firstp = -1;
					return;
				}else{
					*isPlaying = 1;
				}
			}else{
				*isPlaying = 1;
			}
			if(*isPlaying && thisIn){
				remtime = GetMetaFloat(instance->UID, "Duration", NULL) - thisIn->pos;
				if(remtime < 0.0)
					remtime = 0.0;
			}
		}else{
			if(!force && (*sbtime > 60)){
				if(thisIn && thisIn->managed){
					// already have more than 60 seconds already loaded in standby... unload this one
					UnloadItem(0, instance);
					*firstp = -1;
				}
			}else{
				// less than 60 seconds loaded in standby before this one... add this one to the count 
				float dur;
				
				dur = GetMetaFloat(instance->UID, "SegOut", NULL);
				if(dur <= 0.0)
					dur = GetMetaFloat(instance->UID, "Duration", NULL);
				*sbtime = *sbtime + dur;
				remtime = remtime + dur;
			}
		}
	}
			
	NextListItem(status, instance, &nextp, sbtime, remtime, isPlaying);

	type = GetMetaData(instance->UID, "Type", 0);
	if(!strcmp(type, "task")){
		// If this is a task, skip it... use the the next returned current player as the returned current player 
		*firstp = nextp;
	}
	
	// set seg times into next item, if next item is loaded
	if(checkPnumber(nextp)){
		nextIn = &mixEngine->ins[nextp];
		if(checkPnumber(*firstp)){
			thisIn = &mixEngine->ins[*firstp];
			if(nextIn->status & status_standby){
				if(!(nextIn->status & status_playing)){
					if(plRunning){
						priority = GetMetaInt(nextIn->UID, "Priority", NULL);
						targetTime = GetMetaFloat(nextIn->UID, "TargetTime", NULL);
						if(((targetTime < 0) && (priority > 9)) || (thisIn->segNext != (nextp+1))){
								setSegTimes(thisIn, nextIn, nextp);
						}
					}else{
						if(thisIn->segNext){
							// unhook any segue times that have been set if the list is not running
							thisIn->segNext = -1;
						}
					}
				}
			}
		}else{
			if(remtime < 30){
				// next item is loaded, this one is not, less than 30 seconds until something needs to play! 
				// Swap this one with the next one if this is not a stop and next is managed
				if(strcmp(type, "stop") && nextIn->managed){
					if(next = (queueRecord *)getNextNode((LinkedListEntry *)instance))
						moveAfterNode((LinkedListEntry *)instance, (LinkedListEntry *)next, (LinkedListEntry *)&queueList);
				}
			}
		}
	}else{
		// Next item is not in a player.  Check if this item thinks 
		// is in a player and set to seg.
		if(checkPnumber(*firstp)){
			thisIn = &mixEngine->ins[*firstp];
			if(thisIn->segNext){
				// unhook any segue times that have been set previously
				// further clean up will occur if next next time this function
				// is called though this point in the queue list
				thisIn->segNext = 0;
			}
		}
	}
	free(type);
}

void watchdogReset(void){	
	// check main bus VU meters for silence
	if(silent_timeout && (silent_bus < mixEngine->busCount)){
		float sum = 0.0;
		for(int i=silent_bus; i<mixEngine->chanCount; i++)
			sum += mixEngine->mixbuses->VUmeters[i].peak;

		if(sum < silent_thresh){
			if((difftime(time(NULL), silent_event) > silent_timeout)){
				if(!silent_tryseg){
					// first try to seg all
					serverLogMakeEntry("[automation] -:Silence detection timed out; trying seg all");
					silent_tryseg = 1;
					silent_event = time(NULL);
					
					inChannel *instance;
					int i;
					// search all players, fade items that are found to be playing and managed
					for(i=0;i<mixEngine->inCount;i++){
						instance = &mixEngine->ins[i];
						if(instance->managed && (instance->status & status_playing) && ((instance->status & status_cueing) == 0)){
							// currently playing and not in cue... fade it!
							instance->fadePos = instance->pos;
						}
					}	
				}else{
					serverLogMakeEntry("[automation] -:Silence detection timed out; trying restart");
					sleep(5);	// wait 5 seconds for log entry to be made
					write(STDOUT_FILENO, "#", 1);
					return;
				}
			}
		}else{
			silent_tryseg = 0;
			silent_event = time(NULL);
		}
	}else{
		silent_event = time(NULL);
		silent_tryseg = 0;
	}
	// signal our parent (launcher) process that we are still alive and well
	write(STDOUT_FILENO, "W", 1); 	
}

void SchedulerInserter(time_t *lastSchedTime, unsigned char highOnly){
	queueRecord *instance;
	void *dbresult;
	int listSize;
	short priority;
	short lastPriority;
	uint32_t UID, item;
	time_t endTime, firstTime, target, lastTarget;
	struct tm lastTimeRec, endTimeRec;
	char *tmp;

	listSize = queueCount();

	// update end time estimates
	pthread_rwlock_rdlock(&queueLock);
	UpdateQueueEndTimes(0);
	// get end time of last item in list
	firstTime = 0;
	endTime = 0;
	if(listSize > 0){
		if(instance = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, 1))
			firstTime = (time_t)instance->endTime;
		if(instance = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, listSize))
			endTime = (time_t)instance->endTime;
	}
	pthread_rwlock_unlock(&queueLock);
	if(firstTime == 0)
		return;
	if(endTime == 0)
		return;
	if(*lastSchedTime >= endTime)
		return;
	if(*lastSchedTime == 0)
		// sheduling just turned on... start at list end
		*lastSchedTime = endTime;
	if(*lastSchedTime < firstTime)
		*lastSchedTime = firstTime;	

	priority = 0;
	target = -1;
	lastTarget = -1;
	lastPriority = 0;
	localtime_r(&endTime, &endTimeRec);
	localtime_r(lastSchedTime, &lastTimeRec);
	if(lastTimeRec.tm_min != endTimeRec.tm_min){
		// minutes of time last and end are different... check for schedule inserts
		dbresult = NULL;
		while(item = dbGetNextScheduledItem(&dbresult, &target, &priority, *lastSchedTime, endTime, highOnly)){
			// item priorities of 8 or greater will supress all lesser priority items targeted for the same time from being added
			// otherwise , items are added in descending order of priority for the same target time.
			if((lastPriority < 8) || (target != lastTarget)){
				tmp = ustr(item);
				str_insertstr(&tmp, "item:///", 0);
				if(UID = AddItem(-1, tmp, "Schedule Insert", 0)){
					// set target time properties
					free(tmp);
					if(target != -1){
						tmp = fstr((double)target, 0);
						SetMetaData(UID, "TargetTime", tmp);
						free(tmp);
						tmp = istr(priority);
						SetMetaData(UID, "Priority", tmp);
						free(tmp);
						lastTarget = target;
						lastPriority = priority;
					}
				}else{
					free(tmp);
				}
			}
		}
		*lastSchedTime = endTime;
	}
}

void PlayListFiller(uint32_t *lastFillID, int *listPos){
	queueRecord *instance;
	taskRecord *task;
	char buf[32];
	int listSize, taskCount, thresh, missing;
	time_t	endTime, fillTime;
	uint32_t ID, localUID, itemUID;
	struct tm timeRec;
	char *url, *type, *tmp;
	unsigned char err;

	url = NULL;
	taskCount = 0;
	pthread_rwlock_rdlock(&taskLock);
	task = (taskRecord *)&taskList;
	while(task = (taskRecord *)getNextNode((LinkedListEntry *)task)){
		if((task->pid == 0) && (task->timeOut)){
			// NOT an external to arserver process and has a timeout time set
			if((task->Proc == (void (*)(void *))dbPick) || (task->Proc == (void (*)(void *))folderPick))
				// and if the task is a folder or database pick, count it as running
				taskCount++;
		}
	}
	pthread_rwlock_unlock(&taskLock);
	
	listSize = queueCount();
	thresh = GetMetaInt(0, "auto_thresh", NULL);
	if(!thresh)
		thresh = 8;
	/* fill until pick task count >=3 or queue is already filled to thresh count. */
	if(thresh <= listSize)
		return;
	if(taskCount > 2){
		return;
	}
	pthread_rwlock_rdlock(&queueLock);
	// update end time estimates
	UpdateQueueEndTimes(0);
	// get end time of last item in list
	endTime = 0;
	if(listSize > 0){
		if(instance = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, listSize))
			endTime = (time_t)instance->endTime;
	}
	if(endTime == 0)
		endTime = time(NULL);
	pthread_rwlock_unlock(&queueLock);

	// playlist filling is enabled
	fillTime = endTime;
	if(ID = dbGetFillID(&fillTime)){
		// update filling string
		localtime_r(&endTime, &timeRec);
		if(strftime(buf, sizeof buf, "%H:%M--", &timeRec)){
			tmp = dbGetItemName(ID);
			pthread_rwlock_wrlock(&queueLock);
			str_setstr(&fillStr, buf);
			str_appendstr(&fillStr, tmp);
			pthread_rwlock_unlock(&queueLock);
			free(tmp);
		}

		// check item type
		str_setstr(&url, "item:///");
		tmp = ustr(ID);
		str_appendstr(&url, tmp);
		free(tmp);
		
		localUID = createMetaRecord(url, NULL, 0);
		// fill the metadata record
		GetURLMetaData(localUID, url);

		missing = GetMetaInt(localUID, "Missing", NULL);
		type = GetMetaData(localUID, "Type", 0);
		if(missing || (!strlen(type))){
			// no type or missing error!
			localtime_r(&endTime, &timeRec);
			if(strftime(buf, sizeof buf, "%H:%M--", &timeRec)){
				pthread_rwlock_wrlock(&queueLock);
				str_setstr(&fillStr, buf);
				str_appendstr(&fillStr, "[error]");
				pthread_rwlock_unlock(&queueLock);
			}
			str_setstr(&url, "");
		}else if(!strcmp(type, "playlist")){
			// it's a database play list... cycle through one by one
			if(ID != *lastFillID){
				// new fill item from last time through... reset the plist position to zero.
				// NOTE: First item (0) is intro
				*listPos = 0;
			}
			itemUID = createMetaRecord("", NULL, 0);
			if(err = dbPLGetNextMeta(*listPos, ID, itemUID)){
				// reached the end of the list... loop back to start.
				// NOTE: First item (0) is intro, second item (1) is start of loop point in list.
				*listPos = 1;
				if(err = dbPLGetNextMeta(*listPos, ID, itemUID)){
					// got an error looping to the second item... try the first item just incase there is no second item
					*listPos = 0;
					err = dbPLGetNextMeta(*listPos, ID, itemUID);
				}
			}
			if(err == 0){
				tmp = istr(*listPos);
				pthread_rwlock_wrlock(&queueLock);
				str_appendstr(&fillStr, " (");
				str_appendstr(&fillStr, tmp);
				str_appendstr(&fillStr, ")");
				pthread_rwlock_unlock(&queueLock);
				free(tmp);
				free(url);
				url = FindFromMeta(itemUID);
				if(!strlen(url)){
					// failed to resolve
					tmp = GetMetaData(itemUID, "Name", 0);
					str_insertstr(&tmp, "[automation] PlayListFiller-", 0);
					str_appendstr(&tmp, ": couldn't resolve item");
					serverLogMakeEntry(tmp);
					free(tmp);
				}
				*listPos = *listPos + 1;
			}else{
				tmp = GetMetaData(localUID, "Name", 0);
				str_insertstr(&tmp, "[automation] PlayListFiller-", 0);
				str_appendstr(&tmp, ": couldn't read playlist");
				serverLogMakeEntry(tmp);
				free(tmp);
				if(strlen(url))
					str_setstr(&url, "");
			}
			releaseMetaRecord(itemUID);
		}
		free(type);
		
		*lastFillID = ID;
		if(strlen(url) > 0){
			itemUID = AddItem(-1, url, "Automation Filler", localUID);
			if(itemUID){
				// set fill time propoerty
				tmp = fstr((double)fillTime, 0);
				SetMetaData(itemUID, "FillTime", tmp);
				free(tmp);
			}
		}
		releaseMetaRecord(localUID);
	}else{
		// nothing scheduled error!
		localtime_r(&endTime, &timeRec);
		if(strftime(buf, sizeof buf, "%H:%M--", & timeRec)){
			pthread_rwlock_wrlock(&queueLock);
			str_setstr(&fillStr, buf);
			str_appendstr(&fillStr, "[Nothing scheduled to fill the playlist]");
			pthread_rwlock_unlock(&queueLock);
		}
	}
	if(url)
		free(url);
}

void AutomatorTask(void){        
	static int listPos = 0;
	static uint32_t lastFillID = 0;
	static time_t lastSchedTime = 0;
	uint32_t flags;
	
	// Play List Filling
	flags = GetMetaInt(0, "auto_live_flags", NULL);
	if((autoState == auto_unatt) || ((autoState == auto_live) && (flags & live_fill))){
		PlayListFiller(&lastFillID, &listPos);
	}else{
		pthread_rwlock_wrlock(&queueLock);
		if(strlen(fillStr))
			str_setstr(&fillStr, "");
		pthread_rwlock_unlock(&queueLock);
	}
	
	// Schedule Inserts
	if(autoState == auto_unatt){
		SchedulerInserter(&lastSchedTime, 0);
	}else if(autoState == auto_live){
		if(flags & live_schedule){
			SchedulerInserter(&lastSchedTime, 0);
		}else{ 
			SchedulerInserter(&lastSchedTime, 1);
		}
	}else{
		// not doing schedule checks...
		lastSchedTime = 0;
	}

	// Target Time adjustments
	if((autoState == auto_unatt) || (autoState == auto_live && (flags & live_target))){
		pthread_rwlock_wrlock(&queueLock);
		UpdateQueueEndTimes(1);
		pthread_rwlock_unlock(&queueLock);
	}
}


void checkRecorders(void){
	unsigned int i;
	uint32_t UID;
	long ts;
	unsigned char empty;

	i = 0;
	while(UID = FindUidForKeyAndValue("Type", "encoder", i)){
		ts = GetMetaInt(UID, "TimeStamp", &empty);
		if(!empty && ((time(NULL) - ts) > 30)){
			// We have not heard from this recorder for more than 30 seconds, delete it
			releaseMetaRecord(UID);
			continue;
		}
		i++;
	}
}

void QueManagerTask(unsigned char *stop){    
	unsigned char isPlaying, lastState;
	int firstp;
	float sbtime;
	char *tmp;
	inChannel *instance;
	struct timespec abstime;
	ProgramLogRecord *entryRec;
	taskRecord *prevt, *trec;

	lastState = 0;

	initAutomator();

	// loop until st
	while(!(*stop)){
		// check for auto-live mode timeout
		if(autoLiveTimeout == 0)
			autoLiveTimeout = 1200;	// default time out is 20 minutes
		if((autoState == auto_live) && (difftime(time(NULL), live_event) > autoLiveTimeout)){
			autoState = auto_unatt;
			// send out notifications
			serverLogMakeEntry("[automation] -Switched to auto (live time-out)");
			
			notifyData	data;
			data.reference = 0;
			data.senderID = getSenderID();
			data.value.iVal = 0;
			notifyMakeEntry(nType_status, &data, sizeof(data));	
		}
				
		/* when we are run in keep-alive mode, signal the watchdog 
		 * process that launched us that we are still running. */
		watchdogReset();
		
		/* clean up any finish child processes */
		waitpid(-1, NULL, WNOHANG);
		
		isPlaying = 0;
		firstp = -1;
		sbtime = 0;

		pthread_rwlock_wrlock(&queueLock);
		// recursively iterate through each playlist item doing what need to be done with each	
		NextListItem(status_standby, (queueRecord *)&queueList, &firstp, &sbtime, 0.0, &isPlaying);
		pthread_rwlock_unlock(&queueLock);

		// perform automation functions: inserts, fills, re-orders
		AutomatorTask();

		if(!plRunning){
			if((autoState == auto_unatt) || ((autoState == auto_live) && 
					((GetMetaInt(0, "auto_live_flags", NULL) & live_stop) == 0))){
				plRunning = 1;
				// send out notifications
				notifyData	data;
				data.reference = 0;
				data.senderID = getSenderID();
				data.value.iVal = 0;
				notifyMakeEntry(nType_status, &data, sizeof(data));
			}
		}
		
		if(plRunning){
			// stuff to do while the play list is running
			if(queueCount() < 1){	
				plRunning = 0;
				// send out notifications
				notifyData	data;
				data.reference = 0;
				data.senderID = getSenderID();
				data.value.iVal = 0; 
				notifyMakeEntry(nType_status, &data, sizeof(data));
			}else{
				if(!lastState){
					// playlist has started - log it.
					if(entryRec = calloc(1, sizeof(ProgramLogRecord))){
						entryRec->name = strdup("--- Play List Started ---");
						entryRec->ID = 0;
						entryRec->location = GetMetaInt(0, "db_loc", NULL);
						entryRec->albumID = 0;
						entryRec->artistID = 0;
						entryRec->ownerID = 0;
						entryRec->owner = strdup("List Manager");
						entryRec->added = 0;
						entryRec->played = 0xff;
						entryRec->post = 0;
						entryRec->UID = 0;
						programLogMakeEntry(entryRec);
					}
					lastState = 1;
				}
				if(!isPlaying){
					// nothing is playing!  Get going...
					// it's possible that firstp may have changed by another thread.
					// if so, we will start the wrong player.  Oh well.  No crash.
					if(checkPnumber(firstp)){
						instance = &mixEngine->ins[firstp];
						instance->requested = instance->requested | change_play;
					}
				}
			}
		}else{

			if(!isPlaying && lastState){
				// playlist has stoped - log it.
				if(entryRec = calloc(1, sizeof(ProgramLogRecord))){
					entryRec->name = strdup("--- Play List Stoped ---");
					entryRec->ID = 0;
					entryRec->location = GetMetaInt(0, "db_loc", NULL);
					entryRec->albumID = 0;
					entryRec->artistID = 0;
					entryRec->ownerID = 0;
					entryRec->owner = strdup("List Manager");
					entryRec->added = 0;
					entryRec->played = 0xff;
					entryRec->post = 0;
					entryRec->UID = 0;
					programLogMakeEntry(entryRec);
				}
				lastState = 0;
			}
		}
		
		// check for timed-out tasks
		pthread_rwlock_rdlock(&taskLock);
		prevt = (taskRecord *)&taskList;
		while(trec = (taskRecord *)getNextNode((LinkedListEntry *)prevt)){
			if(trec->timeOut > 0){
				if((int)(time(NULL) - trec->started) > trec->timeOut){
					if(trec->pid)
						kill(trec->pid, SIGKILL);
					trec->cancelThread = 1;
					trec->timeOut = 0;
					char buf[96];
					snprintf(buf, sizeof buf, "[task] -%08x, thread sig# %08x: timed out.", (unsigned int)trec->UID, (unsigned int)trec->thread);
					serverLogMakeEntry(buf); 
					trec->timeOut = 0;
				}
			}
			prevt = trec;
		}
		pthread_rwlock_unlock(&taskLock);

		// check for external recorders that are no longer connected (probebly crashed)
		checkRecorders();
		
		// Wait for a signal to check the list again or a time out
		abstime.tv_nsec = 0;
		abstime.tv_sec = time(NULL) + 2; // set time-out for 2 seconds from now

		pthread_mutex_lock(&mgrMutex);
		pthread_cond_timedwait(&mgrSemaphore, &mgrMutex, &abstime);
		pthread_mutex_unlock(&mgrMutex);
	}
	shutdownAutomator();
}

void wakeQueManager(void){
	pthread_mutex_lock( &mgrMutex );
	pthread_cond_signal( &mgrSemaphore );
	pthread_mutex_unlock( &mgrMutex );
}
