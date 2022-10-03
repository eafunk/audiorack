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

#include "data.h"
#include "dispatch.h"
#include "database.h"
#include "tasks.h"
#include "session.h"
#include "automate.h"
#include "arserver.h"

pthread_rwlock_t dataLock;
pthread_rwlock_t inputLock;
pthread_rwlock_t queueLock;
pthread_rwlock_t connLock;
uidRecord *metaList;
inputRecord *inputList;
queueRecord *queueList;
connRecord *connList;

void initDataLists(void){
	metaList = NULL;
	pthread_rwlock_init(&dataLock, NULL);
	
	inputList = NULL;
	pthread_rwlock_init(&inputLock, NULL);
	
	queueList = NULL;
	pthread_rwlock_init(&queueLock, NULL);
	
	connList = NULL;
	pthread_rwlock_init(&connLock, NULL);
}

void freeDataLists(void){
	uidRecord *rec;
	inputRecord *inRec;
	queueRecord *qRec;
	connRecord *cRec;
	
	while(qRec = queueList)
		releaseQueueRecord((queueRecord *)&queueList, qRec, 1);
	pthread_rwlock_destroy(&queueLock);
	while(rec = metaList)
		releaseUIDRecord((uidRecord *)&metaList, rec);
	pthread_rwlock_destroy(&dataLock);
	while(inRec = inputList)
		releaseInputRecord((inputRecord *)&inputList, inRec);
	pthread_rwlock_destroy(&inputLock);
	while(cRec = connList)
		releaseConnRecord((connRecord *)&connList, cRec);
	pthread_rwlock_destroy(&connLock);
}

void createSettingsRecord(const char *version){
	uidRecord *rec;

	pthread_rwlock_wrlock(&dataLock);
	rec = newUIDRecord((uidRecord *)&metaList, 0, &releaseAllKV);    
	rec->rev = 0;	// new record, revision zero
	setValueForKey((keyValueRecord *)&rec->child, "Version", version);
	setValueForKey((keyValueRecord *)&rec->child, "file_prefixes", DefPrefixList);
	pthread_rwlock_unlock(&dataLock);
}  
    
uint32_t createMetaRecord(const char *url, uint32_t *reqID, unsigned char silent){
	uint32_t theID;
	static uint32_t lastID = 0;
	uidRecord *rec = NULL;

	pthread_rwlock_wrlock(&dataLock);
	if(reqID){
		rec = newUIDRecord((uidRecord *)&metaList, *reqID, &releaseAllKV); 
		theID = *reqID;
	}
	if(!rec){
		do{
			// 20 bits only
			lastID++;
			theID = (lastID & 0x00FFFFFF);
			if(!theID)
				theID++; // zero is reserved for settings
			rec = newUIDRecord((uidRecord *)&metaList, theID, &releaseAllKV); 
		}while(!rec); 
		lastID = theID;
	}
	rec->rev = 0;	// new record, revision zero
	rec->silent = silent;
	if(url)
		setValueForKey((keyValueRecord *)&rec->child, "URL", url);	
	pthread_rwlock_unlock(&dataLock);
	if(!silent){
		notifyData	data;
		data.reference = htonl(theID);
		data.senderID = 0;
		data.value.iVal = 0;
		notifyMakeEntry(nType_mstat, &data, sizeof(data));
	}
	
	return theID;
}

void releaseMetaRecord(uint32_t uid){
	/* freed and removed from list when reference count is zero */
	uidRecord *rec;
	unsigned char silent;

	pthread_rwlock_wrlock(&dataLock);
    // find UID record
	if(rec = (uidRecord *)findNode((LinkedListEntry *)&metaList, uid, NULL, NULL)){
		silent = rec->silent;
		if(releaseUIDRecord((uidRecord *)&metaList, rec)){
			if(!silent){
				notifyData	data;
				data.reference = htonl(uid);
				data.senderID = 0;
				data.value.iVal = 0;
				notifyMakeEntry(nType_del, &data, sizeof(data));
			}
		}
	}
	pthread_rwlock_unlock(&dataLock);
}

void retainMetaRecord(uint32_t uid){
	uidRecord *rec;

	pthread_rwlock_wrlock(&dataLock);
    // find UID record
	if(rec = (uidRecord *)findNode((LinkedListEntry *)&metaList, uid, NULL, NULL))
		retainListRecord((LinkedListEntry *)rec);
	pthread_rwlock_unlock(&dataLock);
}

unsigned char MetaDoesKeyExist(uint32_t uid, const char *key){
	char *result;
	const char *value;
	uidRecord *rec;
	
	value = NULL;
	pthread_rwlock_rdlock(&dataLock);
	// find UID record
	if(rec = (uidRecord *)findNode((LinkedListEntry *)&metaList, uid, NULL, NULL))
		// now find value for key
		value = getValueForKey((keyValueRecord *)&rec->child, key);
	pthread_rwlock_unlock(&dataLock);

	if(value)
		return 1;
	return 0;
}

char *GetMetaData(uint32_t uid, const char *key, unsigned char allowNull){
	char *result;
	const char *value;
	uidRecord *rec;
	
	result = NULL;
	value = NULL;
	pthread_rwlock_rdlock(&dataLock);
	// find UID record
	if(rec = (uidRecord *)findNode((LinkedListEntry *)&metaList, uid, NULL, NULL))
		// now find value for key
		value = getValueForKey((keyValueRecord *)&rec->child, key);
	if(value)
		result = strdup(value);
	pthread_rwlock_unlock(&dataLock);

	if(!result && !allowNull)
		result = strdup("");
	return result;
}

long GetMetaInt(uint32_t uid, const char *key, unsigned char *isEmpty){
	int result;
	const char *value;
	uidRecord *rec;
	
	result = 0;
	pthread_rwlock_rdlock(&dataLock);
	// find UID record
	if(rec = (uidRecord *)findNode((LinkedListEntry *)&metaList, uid, NULL, NULL))
		// now find value for key
		value = getValueForKey((keyValueRecord *)&rec->child, key);
	if(value){
		result = atol(value);
		if(isEmpty)
			*isEmpty = 0;
	}else if(isEmpty)
			*isEmpty = 1;
	pthread_rwlock_unlock(&dataLock);			
	return result;
}

double GetMetaFloat(uint32_t uid, const char *key, unsigned char *isEmpty){
	float result;
	const char *value;
	uidRecord *rec;
	
	result = 0.0;
	pthread_rwlock_rdlock(&dataLock);
	// find UID record
	if(rec = (uidRecord *)findNode((LinkedListEntry *)&metaList, uid, NULL, NULL))
		// now find value for key
		value = getValueForKey((keyValueRecord *)&rec->child, key);
	if(value){
		result = atof(value);
		if(isEmpty)
			*isEmpty = 0;
	}else if(isEmpty)
		*isEmpty = 1;
	pthread_rwlock_unlock(&dataLock);
	return result;
}

unsigned char SetMetaData(uint32_t uid, const char *key, const char *value){
	uidRecord *rec;
	
	rec = NULL;
	pthread_rwlock_wrlock(&dataLock);
	// find UID record
	if(rec = (uidRecord *)findNode((LinkedListEntry *)&metaList, uid, NULL, NULL)){
		// now find value for key
		setValueForKey((keyValueRecord *)&rec->child, key, value);
		rec->rev++;
	}
	pthread_rwlock_unlock(&dataLock);

	if(rec){
		if(uid == 0){
			// update local vars associated with certain settings values
			if(strcmp(key, "def_bus") == 0){
				def_busses = atoi(value);
				setAllUnloadedToDefault(mixEngine);
			}
			if(strcmp(key, "log_buses") == 0){
				log_busses = atoi(value);
				if(!log_busses)
					// no log busses specified... use default: Main bus only
					log_busses = 0x04;	
			}
			if(strcmp(key, "sys_silence_bus") == 0){
				silent_bus = atoi(value);
			}
			if(strcmp(key, "sys_silence_timeout") == 0){
				silent_timeout = atoi(value);
			}
			if(strcmp(key, "sys_silence_thresh") == 0){
				silent_thresh = atof(value);
			}
			if(strcmp(key, "auto_live_timeout") == 0){
				autoLiveTimeout = (unsigned)atoi(value);
			}
			if(strcmp(key, "db_server") == 0){
				clearCachedFingerprint();
			}
			if(strcmp(key, "db_name") == 0){
				clearCachedFingerprint();
			}
		}
		if(!rec->silent){
			notifyData	data;
			data.reference = htonl(uid);
			data.senderID = 0;
			data.value.iVal = 0;
			notifyMakeEntry(nType_mstat, &data, sizeof(data));
		}
		return 1;
	}
	return 0;
}

unsigned char UpdateMetaData(uint32_t uid, const char *key, const char *value){
	uidRecord *rec;
	const char *tmp;
	
	rec = NULL;
	pthread_rwlock_wrlock(&dataLock);
	// find UID record
	if(rec = (uidRecord *)findNode((LinkedListEntry *)&metaList, uid, NULL, NULL)){
		// now find value for key
		tmp = getValueForKey((keyValueRecord *)&rec->child, key);
		if(!tmp || strcmp(tmp, value)){
			setValueForKey((keyValueRecord *)&rec->child, key, value);
			rec->rev++;
			pthread_rwlock_unlock(&dataLock);
			if(!rec->silent){
				notifyData	data;
				data.reference = htonl(uid);
				data.senderID = 0;
				data.value.iVal = 0;
				notifyMakeEntry(nType_mstat, &data, sizeof(data));
			}
			return 1;
		}
		pthread_rwlock_unlock(&dataLock);
	}else{
		pthread_rwlock_unlock(&dataLock);
		return SetMetaData(uid, key, value);
	}
	return 0;
}

unsigned char DelMetaData(uint32_t uid, const char *key){
	uidRecord *rec;
	keyValueRecord *kvp;
	
	kvp = NULL;
	/* this released the Key/Value record, resulting in it being removed
	 * only if the reference count is zero after the release */
	rec = NULL;
	pthread_rwlock_wrlock(&dataLock);
	// find UID record
	if(rec = (uidRecord *)findNode((LinkedListEntry *)&metaList, uid, NULL, NULL)){
		// now find kvp record for key
		if(kvp = getRecordForKey((keyValueRecord *)&rec->child, key)){
			releaseKeyValueRecord((keyValueRecord *)&rec->child, kvp);
			rec->rev++;
		}
	}
	pthread_rwlock_unlock(&dataLock);
	if(kvp){
		if(!rec->silent){
			notifyData	data;
			data.reference = htonl(uid);
			data.senderID = 0;
			data.value.iVal = 0;
			notifyMakeEntry(nType_mstat, &data, sizeof(data));
		}
		return 1;
	}
	return 0;
}

uint32_t GetMetaRev(uint32_t uid){
	uidRecord *rec;
	uint32_t rev;
	
	rev = 0;
	pthread_rwlock_rdlock(&dataLock);
	if(rec = (uidRecord *)findNode((LinkedListEntry *)&metaList, uid, NULL, NULL))
		// now find value for key
		rev = rec->rev;
	
	pthread_rwlock_unlock(&dataLock);
	return rev;
}

unsigned int GetMetaKeysAndValues(uint32_t uid, char ***keys, char ***values){
	unsigned int count, i;
	uidRecord *rec;
	keyValueRecord *pair;
	
	/* allocates two arrays of strings (keys and values) that are populated 
	 * with key and value pairs found for the specified UID.  The number of 
	 * entries in the arrays are returned. If non-zero, the arrays and each
	 * string need to be freed by the caller */
	count = 0;
	i = 0;
	pthread_rwlock_rdlock(&dataLock);
	// find UID record
	if(rec = (uidRecord *)findNode((LinkedListEntry *)&metaList, uid, NULL, NULL)){
		// count associated kV pairs
		if(count = countNodesAfter((LinkedListEntry *)&rec->child)){
			// allocate storage for results
			*keys = calloc(count, sizeof(char*));
			*values = calloc(count, sizeof(char*));
			// itterate through KV list (should already be in key alphebetical order)
			pair = (keyValueRecord *)&rec->child;
			while(pair = (keyValueRecord *)getNextNode((LinkedListEntry *)pair)){
				(*keys)[i] = strdup(pair->key);
				(*values)[i] = strdup(pair->value);
				i++;
			}
		}
	}
	pthread_rwlock_unlock(&dataLock);
	return count;
}

uint32_t FindUidForKeyAndValue(const char *key, const char *value, unsigned int index){
	uint32_t result;
	uidRecord *rec;
	keyValueRecord *pair;
	
	result = 0;
	pthread_rwlock_rdlock(&dataLock);
	// find UID record
	rec = (uidRecord *)&metaList;
	while(rec = (uidRecord *)getNextNode((LinkedListEntry *)rec)){
		if(rec->UID){
			if(pair = getRecordForKey((keyValueRecord *)&rec->child, key)){
				if(!strcmp(pair->value, value)){
					if(!index){
						result = rec->UID;
						break;
					}else
						index--;
				}
			}
		}
	}
	pthread_rwlock_unlock(&dataLock);
	return result;
}

void resolveStringMacros(char **theStr, uint32_t uid){
	// where uid is the metadata item to draw the requested metadata property from.
	// macro can be of the forms:
	// [Property=DefValue,comma,list,of,values,to,choose,from]
	// [Property=DefValue]
	// [Property]
	// Default value, if set, is used when the property is empty.
	// value list is ignored buy this function. It is intened for UI generation
	char *start, *end;
	char *prop, *defval, *tmp;
	unsigned int loc;
	
	if(theStr && *theStr){
		while(start = strchr(*theStr, '[')){
			if(end = strchr(*theStr, ']')){
				*end = 0;
				prop = strtok_r(start+1, "=", &tmp);
				defval = strtok_r(NULL, ",", &tmp);
				tmp = GetMetaData(uid, prop, 0);
				if(!strlen(tmp)){
					if(defval)
						str_setstr(&tmp, defval);
				}
				loc = start - *theStr;
				// fill start to end of string with spaces
				// clearing out NULLs we inserted above
				prop = start;
				while(prop <= end){
					*prop = ' ';
					prop++;
				}
				str_cutstr(theStr, loc, end-start+1);
				str_insertstr(theStr, tmp, loc);
				free(tmp);
			}else
				break;
		}
	}
}

// Queue functions should never be called while the data lock is held 
// unless noted otherwise
queueRecord *createQueueRecord(uint32_t uid){
	queueRecord *rec = NULL;

	if(rec = (queueRecord *)calloc(1, sizeof(queueRecord))){
		rec->refCnt = 1;
		rec->UID = uid;
	}
	return rec;
}

unsigned char releaseQueueRecord(queueRecord *root, queueRecord *rec, unsigned char force){
	queueRecord	*prev, *current; 
	inChannel *instance = NULL;
	uint32_t logID;	
	char *tmp;
	// This function is an exception:  The queue lock must be 
	// write locked prior to this function call.
	
	rec->refCnt--;
	if(rec->refCnt == 0){
		// check if playing
		if(rec->player && !force){
			if(checkPnumber(rec->player - 1)){
				instance = &mixEngine->ins[rec->player - 1];
				if(instance->status & status_playing){
					// do not remove playing items
					rec->refCnt++;
					return 0;
				}
			}
		}
		
		/* if reference count is zero, unhook and free the record */
		prev = root;
		while((current = prev->next) != NULL){ 
			if(current == rec){ 
				/* unhook from list */
				prev->next = current->next;
				break; 
			} 
			prev = current; 
		} 
		/* handle removal of associations */
		if(rec->UID){
			logID = GetMetaInt(rec->UID, "logID", NULL);
			// remove added only item from program logs
			if(logID){
				uint32_t *ptr;
				if(ptr = malloc(sizeof(uint32_t))){
					*ptr = logID;
					createTaskItem("Delete Log Entry", DeleteLogEntry, (void*)ptr, 0, -1, 180, 1); // time out in 3 minutes
				}
			}
			releaseMetaRecord(rec->UID);
		}
		
		if(instance && instance->managed){
			uint32_t c, cmax;
			jack_port_t **port;
			// this will trigger the standard player unload
			// after jack port disconnection
			port = instance->in_jPorts;
			cmax = mixEngine->chanCount;
			pthread_mutex_lock(&mixEngine->jackMutex);
			for(c=0; c<cmax; c++){
				jack_port_disconnect(mixEngine->client, *port);
				port++;
			}
			pthread_mutex_unlock(&mixEngine->jackMutex);
		}
		
		free(rec);
		return 1;
	}
	return 0;	
}

unsigned char releaseQueueEntry(uint32_t uid){
	/* freed and removed from list when reference count is zero */
	queueRecord *rec;

	pthread_rwlock_wrlock(&queueLock);
	// find UID record
	if(rec = (queueRecord *)findNode((LinkedListEntry *)&queueList, uid, NULL, NULL)){
		if(releaseQueueRecord((queueRecord *)&queueList, rec, 0)){
			plRev++;
			notifyData	data;
			data.reference = 0;
			data.senderID = getSenderID();
			data.value.iVal = 0;
			notifyMakeEntry(nType_status, &data, sizeof(data));	
		}else
			rec = NULL;
	}
	pthread_rwlock_unlock(&queueLock);
	if(rec)
		return 1;
	return 0;
}

void retainQueueEntry(uint32_t uid){
	queueRecord *rec;

	pthread_rwlock_wrlock(&queueLock);
	// find UID record
	if(rec = (queueRecord *)findNode((LinkedListEntry *)&queueList, uid, NULL, NULL))
		retainListRecord((LinkedListEntry *)rec);
	pthread_rwlock_unlock(&queueLock);
}

uint32_t getQueueRecStatus(queueRecord *rec, inChannel **input){
	inChannel *instance;
	
	// This function is an exception:  The queue lock must be 
	// either read or write locked prior to this function call
	if(rec->player && checkPnumber(rec->player-1)){
		instance = &mixEngine->ins[rec->player-1];
		if(input)
			*input = instance;
		return instance->status;
	}else{
		if(input)
			*input = NULL;
		return rec->status;
	}
}

unsigned int queueCount(void){
	unsigned int count; 
	
	pthread_rwlock_rdlock(&queueLock);
	count = countNodesAfter((LinkedListEntry *)&queueList);
	pthread_rwlock_unlock(&queueLock);
	
	return count;
} 

unsigned char getQueuePos(uint32_t *ref){
    // on entry *ref = meta-UID from the specified player
	LinkedListEntry *rec;
	if(*ref){
		pthread_rwlock_rdlock(&queueLock);
		if(rec = findNode((LinkedListEntry *)&queueList, *ref, ref, NULL)){ 
			pthread_rwlock_unlock(&queueLock);
			(*ref)++;
			// on return, *ref = index of found record
			return 1;
		}
		pthread_rwlock_unlock(&queueLock);
	}
	return 0;
}

unsigned int queueGetNextSegPos(int *thisP){
	uint32_t i, count, pos;
	queueRecord *rec;

	pos = 0;
	if(thisP)
		*thisP = -1;
	pthread_rwlock_rdlock(&queueLock);
	count = countNodesAfter((LinkedListEntry *)&queueList);
	if(count > mixEngine->inCount)
		count = mixEngine->inCount;
	i = count;
	while(i > 0){
		rec = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, i);
		if(rec && (getQueueRecStatus(rec, NULL) & status_hasPlayed)){
			pos = i;
			if(thisP)
				*thisP = rec->player-1;
			break;
		}
		i--;
	}
	pthread_rwlock_unlock(&queueLock);
	return pos;
}

double getTargetError(double start, double target, int priority){
	double result = 0;
	
	if(target != 0){
		result = start - target;
		if(priority > 9){ 
			// handle high priority (fade previous for exact time)
			if((start < target) && (result > 0)){
				result = 0.0;
			}
		}
	}else{
		result = 0.0;
	}
	return result;
}

double ItemEndTime(queueRecord *instance, double startTime, double lastStart, int pos, double *error, int *priority, double *targetTime){
	inChannel *mixrec;
	double totalTime;
	float segInT;
	float segOutT;
	float fadeT;
	uint32_t stat;
	char *tmp;

	totalTime = startTime;
	*priority = 0;
	*error = 0.0;
	if(instance){
		stat = getQueueRecStatus(instance, &mixrec);

		segOutT = GetMetaFloat(instance->UID, "SegOut", NULL);
		if(segOutT == 0.0)
				segOutT = GetMetaFloat(instance->UID, "Duration", NULL);
		fadeT = GetMetaFloat(instance->UID, "FadeOut", NULL); // pos FadeTome is time
		if((fadeT > 0.0) && (fadeT < segOutT))
			segOutT = fadeT;
		
		if(mixrec){
			fadeT = mixrec->fadePos;
			if((fadeT > 0.0) && (fadeT < segOutT))
				segOutT = fadeT;
		}
		
		segInT = GetMetaFloat(instance->UID, "SegIn", NULL);
		 
		if(!(stat & status_hasPlayed)){
			if(pos > 0)
				totalTime = totalTime - segInT;
		}
		
		if(mixrec && (segOutT > mixrec->pos))
			segOutT = segOutT - mixrec->pos;

		if((stat & status_delete) == 0){  // not flaged for deletion
			if((stat & status_playing) != 0){
				totalTime = segOutT + (double)time(NULL);
			}else{
				totalTime = totalTime + segOutT;
			}
		}else{ 
			// flagged for deletion
			totalTime = startTime;
		}
		*priority = GetMetaInt(instance->UID, "Priority", NULL);
		*targetTime = GetMetaFloat(instance->UID, "TargetTime", NULL);
		if(*targetTime != 0){
			*error = startTime - *targetTime;
			if(*priority > 9){ 
				// handle high priority (fade previous for exact time)
				if((lastStart < *targetTime) && (*error > 0)){
					if(*targetTime > 0)
						totalTime = totalTime - *error;
					*error = 0.0;
				}
			}
		}else{
			*error = 0.0;
		}
	}
	return totalTime;
}

void checkSwapReorder(itemGroupRec *firstGroup, itemGroupRec *nextGroup, double prior_start_time){
	// This function is an exception:  The queue lock must be write 
	// locked prior to calling.

	// this function compares the current target time errors of the two groups
	// to the errors if they were swapped.  If the swapped error is better by at
	// least 10 seconds, then it actually swaps the groups in the playlist,
	// updates the item end times to reflect the new order and updates the 
	// passed firstGroup and nextGroup records (excluding highest_priority members)
	// to reflect the order change.
	
	queueRecord *instance;
	double first_err, next_err, first_swap_err, next_swap_err;
	double firstDur, nextDur, this_start_time, endTime, target;
	int firstgroup_end_index, i, priority;
	
	// each priority level amplifies error by a factor of 1.5.
	first_err = fabs(powf(1.5, firstGroup->highest_priority_value) * firstGroup->highest_priority_error);
	next_err = fabs(powf(1.5, nextGroup->highest_priority_value) * nextGroup->highest_priority_error);
	// select largest error
	if(next_err > first_err)
		first_err = next_err;
	
	if(first_err){
		// see if the largest error (after priority scaling) is lower the with groups swapped
		firstDur = firstGroup->last_end_time - firstGroup->first_start_time;
		nextDur = nextGroup->last_end_time - nextGroup->first_start_time;
		first_swap_err = getTargetError(firstGroup->highest_priority_start_time + nextDur, 
											firstGroup->highest_priority_target, firstGroup->highest_priority_value);
		first_swap_err = fabs(powf(1.5, firstGroup->highest_priority_value) * first_swap_err);
		next_swap_err = getTargetError(nextGroup->highest_priority_start_time - firstDur, 
											nextGroup->highest_priority_target, nextGroup->highest_priority_value);
		next_swap_err = fabs(powf(1.5, nextGroup->highest_priority_value) * next_swap_err);
		// select largest error
		if(next_swap_err > first_swap_err)
			first_swap_err = next_swap_err;
						
		if((first_swap_err + 10) < first_err){
			// swap group order in list if more than 10 seconds (after priority weighting)
			// of target time hit improvement
			firstgroup_end_index = firstGroup->last_index;
			while(firstgroup_end_index >= firstGroup->first_index){
				firstgroup_end_index--;
				MoveItem(firstGroup->first_index, nextGroup->last_index, 0);
			}
			// Recalculated item end times
			this_start_time = firstGroup->first_start_time;
			for(i=firstGroup->first_index; i<=nextGroup->last_index; i++){
				if(instance = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, i+1)){
					endTime = ItemEndTime(instance, this_start_time, prior_start_time, i, &first_err, &priority, &target);
					instance->endTime = endTime;
					prior_start_time = this_start_time;
					this_start_time = endTime;
				}
			}
			// update passed recrords
			firstGroup->last_index = firstGroup->first_index + (nextGroup->last_index - nextGroup->first_index);
			firstGroup->last_end_time = firstGroup->first_start_time + nextDur;
			
			nextGroup->first_start_time = firstGroup->last_end_time;
			nextGroup->first_index = firstGroup->last_index + 1;
		}
	}
}

unsigned char getNextMovableGroup(int PLsize, itemGroupRec *firstGroup, itemGroupRec *nextGroup, double last_start_time){
	// After first moving past any items in a together groups that is playing and updating firstGroup->first_Index to 
	// the first such non-playing item, we return the last item index of a together group in the list that has a 
	// different non-null together group than item of thisIndex, or the next item of a null together group.
	// All items encountered along the way will have their endTime property updated based on thisTime passed in initialy.

	// on entry, the first_index and first_start_time members of firstGroup must be set.
	// all firstGroup and nextGroup members will be filled in prior to exit if the function
	// was sucessful, otherwise, if the end of the list is encountered, zero is returned and
	// and firstGroup and nextGroup should be ignored.
	
	// The queue lock must be either read or write locked prior to calling.
	unsigned char result;
	int i;
	queueRecord *item;
	itemGroupRec *curGroup;
	char *itemTog;
	char *lastTog;
	double cur_err, target, startTime, endTime;
	int cur_prio;
	uint32_t stat;
			
	result = 0;
	itemTog = strdup("");
	lastTog = strdup("");
	
	while((i = firstGroup->first_index) < PLsize){
		// skip playing loop
		if(strlen(itemTog))
			str_setstr(&itemTog, "");
			
		if((item = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, i+1)) == NULL){
			// no first item
			result = 0;
			goto finish;
		}
			
		if(item->UID){
			free(itemTog);
			itemTog = GetMetaData(item->UID, "Together", 0);
		}
				
		endTime = ItemEndTime(item, firstGroup->first_start_time, last_start_time, i, &cur_err, &cur_prio, &target);
		item->endTime = endTime;
		
		firstGroup->highest_priority_value = cur_prio;
		firstGroup->highest_priority_error = cur_err;
		firstGroup->highest_priority_target = target;
		firstGroup->highest_priority_start_time = firstGroup->first_start_time;
		
		last_start_time = firstGroup->first_start_time;
		firstGroup->last_end_time = endTime;
		firstGroup->last_index = i;
		
		stat = getQueueRecStatus(item, NULL);
		if(stat & status_hasPlayed){
			// skip items that have played; note any together group
			str_setstr(&lastTog, itemTog); 
			firstGroup->first_index++;
			firstGroup->first_start_time = firstGroup->last_end_time;
			continue;
		}			
		if((stat & (status_remove | status_delete)) || (strlen(lastTog) && (!strcmp(itemTog,lastTog)))){
			// skip items that have same together group as played items or deleted items
			firstGroup->first_index++;
			firstGroup->first_start_time = firstGroup->last_end_time;
			continue;
		}
		str_setstr(&lastTog, itemTog); 
		startTime = firstGroup->last_end_time;
		curGroup = firstGroup;
		nextGroup->highest_priority_value = -1;

		while(++i < PLsize){
			// find next loop
			if((item = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, i+1)) == NULL){
				result = 0;
				goto finish;
			}
			endTime = ItemEndTime(item, startTime, last_start_time, i, &cur_err, &cur_prio, &target);
			item->endTime = endTime;
			
			last_start_time = startTime;
			startTime = endTime;
			
			if(strlen(itemTog))
				str_setstr(&itemTog, "");
			if(item->UID){
				free(itemTog);
				itemTog = GetMetaData(item->UID, "Together", 0);
			}
			
			stat = getQueueRecStatus(item, NULL);
			if(stat & status_hasPlayed){
				// kick back out to skip loop for items that have played, noting any together group
				str_setstr(&lastTog, itemTog);
				firstGroup->first_index = ++i;
				firstGroup->first_start_time = endTime;
				break;
			}			
			if((stat & (status_remove | status_delete)) == 0){
				// not flagged for deletion
				if(strcmp(itemTog, lastTog) || (!strlen(lastTog) && !strlen(itemTog))){
					if(curGroup == firstGroup){
						// past end of first group... set first record of next group to this
						str_setstr(&lastTog, itemTog);
						nextGroup->first_index = i;
						nextGroup->first_start_time = last_start_time;
						curGroup = nextGroup;
					}else{
						// past next group: done
						result = 1;
						goto finish;
					}
				}
				curGroup->last_index = i;
				curGroup->last_end_time = endTime;
				// note the highest priority item in the bunch
				if(cur_prio > curGroup->highest_priority_value){
					curGroup->highest_priority_value = cur_prio;
					curGroup->highest_priority_error = cur_err;
					curGroup->highest_priority_target = target;
					curGroup->highest_priority_start_time = last_start_time;
				}
				if((curGroup == nextGroup) && (itemTog == "")){
					// done
					result = 1;
					goto finish;
				}
			}
		}
		if(curGroup == nextGroup){
			// reached the end of the list while looking for the next item's last member
			// done
			result = 1;
			goto finish;
		}else{
			// reached the end of the list while looking for the first item's last member
			// no next item group: fail
			result = 0;
			goto finish;
		}
	}
	// reached the end of the list while looking for a non-playing group item
	
finish:
	free(itemTog);
	free(lastTog);
	return result;
}

void UpdateQueueEndTimes(unsigned char sort){
	// This function is an exception:  The queue lock must be 
	// either read or write locked if sort is false and write locked
	// if sort is true, prior to calling this function.

	queueRecord *rec, *nextr;
	int i;
	double startTime, fillTime, locFillTime, lastStartTime, targetTime;
	int size;
	uint32_t flags, stat;
	itemGroupRec first, next;
	char *tmp;
	
	flags = status_hasPlayed | status_cueing | status_remove | status_delete;
	
	size = countNodesAfter((LinkedListEntry *)&queueList);
	lastStartTime = (double)time(NULL);
	first.first_start_time = lastStartTime;
	first.first_index = 0;
	
	while(getNextMovableGroup(size, &first, &next, lastStartTime)){
		if(sort)
			checkSwapReorder(&first, &next, lastStartTime);
		lastStartTime = first.first_start_time;
		first = next;
	}
	
	if(sort){
		// check for fill times overlap deletion and for old items now 10 minutes past target time
		fillTime = 0.0;
		for(i = size; i > 0; i--){
			if(rec = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, i)){
				stat = getQueueRecStatus(rec, NULL);
				if(!(stat  & flags)){
					// instance hasn't played or isn't flagged to delete/remove			
					if(i > 1){
						if(nextr = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, i-1))
							startTime = nextr->endTime;
						else
							startTime = time(NULL);
					}else
						startTime = time(NULL);
						 
					locFillTime = GetMetaFloat(rec->UID, "FillTime", NULL);
					targetTime = GetMetaFloat(rec->UID, "TargetTime", NULL);
					if(((targetTime > 0) && (stat & status_hasPlayed) == 0) && ((targetTime + 600) < (double)time(NULL))){ 
						// target time set to 10 or more minutes BEFORE now... delete!
						// Deleting is OK because we are itterating through the list from end to start,
						// so he previous item (next) will still be there after we delete this one.
						releaseQueueRecord((queueRecord *)&queueList, rec, 0);

					}else if((locFillTime > 0) && (fillTime > locFillTime)){
						if(fillTime < startTime){
							// Again, Deleting is OK because we are itterating backwards through the list.
							releaseQueueRecord((queueRecord *)&queueList, rec, 0);
						}else
							fillTime = locFillTime;
					}else
						fillTime = locFillTime;
				}
			}
		}
	}
}

time_t queueGetEndTime(void){
	time_t	endTime;
	int	listSize;
	queueRecord *rec;

	pthread_rwlock_rdlock(&queueLock);
	listSize = countNodesAfter((LinkedListEntry *)&queueList);
	// update end time estimates
	UpdateQueueEndTimes(0);
	// get end time of last item in list
	endTime = 0;
	rec = NULL;
	if(listSize > 0){
		if(rec = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, listSize))
			endTime = (time_t)rec->endTime;
	}
	if(!rec)
		endTime = time(NULL);
	pthread_rwlock_unlock(&queueLock);
	return endTime;
}
