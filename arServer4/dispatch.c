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
#include "cJSON.h"
#include "dispatch.h"
#include "utilities.h"
#include "mix_engine.h"
#include "data.h"
#include "session.h"
#include "tasks.h"
#include "data.h"
#include "database.h"
#include <stdio.h>
#include <ctype.h>
#include <pthread.h>
#include <signal.h>

#define queueMax 100

time_t logChangeTime;
uint32_t log_busses;

pthread_mutex_t notifyQueueLock;
pthread_mutex_t notifyMutex;
pthread_cond_t notifySemaphore;
pthread_t notifyThread;
notifyEntry *notifyQueue;

pthread_mutex_t srvLogQueueLock;
pthread_mutex_t srvLogMutex;
pthread_cond_t srvLogSemaphore;
pthread_t srvLogThread;
ServerLogRecord *svrLogQueue;

pthread_mutex_t pgmLogQueueLock;
pthread_mutex_t pgmLogMutex;
pthread_cond_t pgmLogSemaphore;
pthread_t pgmLogThread;
ProgramLogRecord *pgmLogQueue;

pthread_mutex_t lastsegMutex;
pthread_cond_t lastsegSemaphore;

pthread_t playerWatchThread;
pthread_t sipWatchThread;
char *svrLogFileName;
FILE *svrLogFile;
vuNContainer *vuRecord;
pthread_t vuUpdateThread;
unsigned char dispRun = 0;

pthread_t ctlInQueueWatchThread;
pthread_t jackWatchThread;

pthread_mutex_t sipMutex;

int sipStatus = 0;

int sip_ctl_sock = -1;

void *jackChangeWatcher(void *refCon);
void *playerChangeWatcher(void *refCon);
void *notifyWatcher(void *refCon);
void *serverLogWatcher(void *refCon);
void serverLogCloseFile(void);
void *metersUpdateThread(void *refCon);
void *programLogWatcher(void* refCon);
void *controlQueueInWatcher(void *refCon);
void *sipSessionWatcher(void *refCon);

unsigned char initDispatcherThreads(void){
	unsigned char vuRecCnt;
	unsigned char vuChanCnt;
	unsigned int bytes;

	dispRun = 1;
	
	pthread_mutex_init(&sipMutex, NULL);

	pthread_mutex_init(&lastsegMutex, NULL);
	pthread_cond_init(&lastsegSemaphore, NULL);
	
	notifyQueue = NULL;
	pthread_mutex_init(&notifyQueueLock, NULL);
	pthread_mutex_init(&notifyMutex, NULL);
	pthread_cond_init(&notifySemaphore, NULL);
	pthread_create(&notifyThread, NULL, &notifyWatcher, NULL);	

	svrLogFileName = NULL;
	str_setstr(&svrLogFileName, "");
	svrLogQueue = NULL;
	pthread_mutex_init(&srvLogQueueLock, NULL);
	pthread_mutex_init(&srvLogMutex, NULL);
	pthread_cond_init(&srvLogSemaphore, NULL);
	pthread_create(&srvLogThread, NULL, &serverLogWatcher, NULL);	
	
	logChangeTime = 0;
	pgmLogQueue = NULL;
	pthread_mutex_init(&pgmLogQueueLock, NULL);
	pthread_mutex_init(&pgmLogMutex, NULL);
	pthread_cond_init(&pgmLogSemaphore, NULL);
	pthread_create(&pgmLogThread, NULL, &programLogWatcher, NULL);	

	pthread_create(&playerWatchThread, NULL, &playerChangeWatcher, NULL);	
	
	pthread_create(&sipWatchThread, NULL, &sipSessionWatcher, NULL);	

	pthread_create(&ctlInQueueWatchThread, NULL, &controlQueueInWatcher, mixEngine);	

	vuRecCnt = mixEngine->busCount + mixEngine->inCount;
	vuChanCnt = mixEngine->chanCount * mixEngine->busCount;
	vuChanCnt = vuChanCnt + (mixEngine->inCount *  mixEngine->chanCount);
	/* allocate one big container for all the vu data */
	bytes = sizeof(vuNContainer) - 1;
	bytes = bytes + (vuRecCnt * (sizeof(vuNInstance) - 1));
	bytes = bytes + (vuChanCnt * sizeof(vuNData));	
	if(bytes < 0xFFFF)
		vuRecord = (vuNContainer *)calloc(1, bytes);
	else
		vuRecord = NULL;
	pthread_create(&vuUpdateThread, NULL, &metersUpdateThread, NULL);	
	
	pthread_create(&jackWatchThread, NULL, &jackChangeWatcher, NULL);	
	return 1;
}

void shutdownDispatcherThreads(void){
	dispRun = 0;
	/* wake Threads so they notice run state change end */
	pthread_cond_broadcast(&mixEngine->changedSemaphore); 
	pthread_cond_broadcast(&mixEngine->ctlInQueueSemaphore);
	
	pthread_cond_broadcast(&srvLogSemaphore); 
	pthread_cond_broadcast(&notifySemaphore); 
	pthread_cond_broadcast(&pgmLogSemaphore); 
	pthread_cond_broadcast(&lastsegSemaphore); 
	pthread_cond_broadcast(&mixEngine->cbQueueSemaphore);
	
	pthread_join(playerWatchThread, NULL);
	pthread_join(sipWatchThread, NULL);
	pthread_join(ctlInQueueWatchThread, NULL);
	pthread_join(srvLogThread, NULL);
	pthread_join(notifyThread, NULL);
	pthread_join(pgmLogThread, NULL);

	serverLogCloseFile();
	pthread_mutex_destroy(&srvLogMutex);
	pthread_cond_destroy(&srvLogSemaphore);
	pthread_mutex_destroy(&notifyMutex);
	pthread_cond_destroy(&notifySemaphore);
	pthread_mutex_destroy(&pgmLogMutex);
	pthread_cond_destroy(&pgmLogSemaphore);
	pthread_mutex_destroy(&lastsegMutex);
	pthread_cond_destroy(&lastsegSemaphore);
	pthread_mutex_destroy(&sipMutex);

	pthread_join(vuUpdateThread, NULL);
	if(vuRecord)
		free(vuRecord);
	pthread_join(jackWatchThread, NULL);

}

void *sipSessionWatcher(void *refCon){
	char *baresipPort = NULL;
	char *tmp = NULL;
	char *src = NULL;
	char *dst = NULL;
	char *name = NULL;
	int lineno = 0;
	int len;
	char established;
	char block[256]; /* receive data buffer */
	char line[4096]; /* receive data buffer */
	char *fragment, *ptr, *remains;
	time_t lastRegCheck, now;
	const char *req = "/reginfo\n";
	
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	lastRegCheck = 0;
	established = 0;
	while(dispRun){
		if(sip_ctl_sock > -1){
			// connection open -- this is the only thread that reads from sip_ctl_sock, so no mutex is needed
			len = recv(sip_ctl_sock, block, sizeof(block)-1, 0);
			if(len < 0){
				if(errno != EAGAIN){
					// connection failure, not a time out
					close(sip_ctl_sock);
					sip_ctl_sock = -1;
					sipStatus = 0;
				}
			}else if(len){
				// data received
				block[len] = 0; // null at end of segment to make it a c-string
				remains = block;
				while(fragment = strpbrk(remains, "\n\r")){
					if(*fragment == '\r'){
						// echod back key... clear line
						remains = fragment+1;
						*line = 0;
						continue;
					}
					// found end-of-line
					*fragment = 0;
					strncat(line, remains, sizeof(line) - (strlen(line) + 1));
					remains = fragment+1;
					if(!strlen(line))
						continue;
					// process line
					if(strstr(line, "--- User Agents (")){
						sipStatus = 1;
					}else if(strstr(line, "> sip:")){
						if(strstr(line, "\x1b[32mOK ")){
							sipStatus++;
						}
					}else if(strstr(line, "Call established: ")){
						established = 1;
					}else if(strstr(line, "call: answering call")){
						if(tmp = strstr(line, "on line ")){
							lineno = atoi(tmp+8);
							if(name)
								free(name);
							name = istr(lineno);
							if(tmp = strstr(line, " from sip:")){
								tmp = tmp + 10;
								if(ptr = strchr(tmp, '@'))
									*ptr = 0;
								str_appendstr(&name, " - ");
								str_appendstr(&name, tmp);
							}
							established = 0;
							if(dst)
								free(dst);
							dst = NULL;
							if(src)
								free(src);
							src = NULL;
						}
					}else if(tmp = strstr(line, "jack: source unique name `")){
						tmp = tmp  + 26;
						if(ptr = strchr(tmp, '\''))
							*ptr = 0;
						str_setstr(&src, tmp);
					}else if(tmp = strstr(line, "jack: destination unique name `")){
						tmp = tmp + 31;
						if(ptr = strchr(tmp, '\''))
							*ptr = 0;
						str_setstr(&dst, tmp);
					}
					
					if(established && lineno && src && dst){
						// setup new call
						LoadSipPlayer(name, src, dst);
						if(name)
							free(name);
						name = NULL;
						if(dst)
							free(dst);
						dst = NULL;
						if(src)
							free(src);
						src = NULL;
						lineno = 0;
						established = 0;
					}
					// clear line
					*line = 0;
				}
				if(strlen(remains))
					strncat(line, remains, sizeof(line) - (strlen(line) + 1));
				// loop to recieve more
				continue;
			}
			now = time(NULL);
			if((now - lastRegCheck) > 10){
				// more than 10 seconds since last c registeration check
				pthread_mutex_lock(&sipMutex);
				send(sip_ctl_sock, req, strlen(req), 0);
				pthread_mutex_unlock(&sipMutex);
				lastRegCheck = now;
			}
		}else{
			// no connection... try to make one if baresipCtl is set
			if(baresipPort && strlen(baresipPort)){
				struct timeval tv;
				int sock;
				int trueVal = 1;
				struct sockaddr_in adrRec;
				
				pthread_mutex_lock(&sipMutex);
				sock = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
				if(sock > -1){
					// set up socket timeout for periodic polling of run status and dead child	
					tv.tv_sec = 1;		// seconds
					tv.tv_usec = 0;	// and microseconds
					setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
					setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
					setsockopt(sock, SOL_SOCKET,SO_REUSEADDR, &trueVal, sizeof(trueVal));
					setsockopt(sock, SOL_SOCKET,SO_KEEPALIVE, &trueVal, sizeof(trueVal));
					bzero(&adrRec, sizeof(adrRec));
					adrRec.sin_family = AF_INET;
					adrRec.sin_port = htons(atoi(baresipPort));
					adrRec.sin_addr.s_addr = inet_addr("127.0.0.1");
					if(connect(sock, (struct sockaddr *)&adrRec, sizeof(adrRec))){
						// connection failure
						close(sock);
						sipStatus = 0;
					}else{
						sip_ctl_sock = sock;
						sipStatus = 1;
						// clear leftovers from previous connection
						*line = 0;
						// connected, pass through the loop again
						pthread_mutex_unlock(&sipMutex);
						continue;
					}
				}
				pthread_mutex_unlock(&sipMutex);
			}
			sleep(1);	// check again in one second
		}
		
		// check for change in sip control port setting
		if(tmp = GetMetaData(0, "sip_ctl_port", 1)){
			if(baresipPort){
				if(strcmp(baresipPort, tmp)){
					// setting changed
					free(baresipPort);
					baresipPort = tmp;
					tmp = NULL;
					pthread_mutex_lock(&sipMutex);
					if(sip_ctl_sock > -1)
						close(sip_ctl_sock);
					sip_ctl_sock = -1;
					pthread_mutex_unlock(&sipMutex);
					sipStatus = 0;
				}
			}else if(strlen(tmp)){
				baresipPort = tmp;
				tmp = NULL;
			}
			if(tmp)
				free(tmp);
		}
	}
	pthread_mutex_lock(&sipMutex);
	if(sip_ctl_sock > -1)
		close(sip_ctl_sock);
	sip_ctl_sock = -1;
	pthread_mutex_unlock(&sipMutex);
	if(baresipPort)
		free(baresipPort);
	return NULL;
}

void *jackChangeWatcher(void *refCon){
	jack_port_id_t *IDptr;
	jack_port_t *port, **pptr;
	unsigned char isSource;
	unsigned int i, c, cmax;
	const char *name;
	char *pname, *plist, *clist;
	connRecord *jcrec;
	outChannel *orec;
	inChannel *inrec;
	unsigned char isConnected;

	while(dispRun){
		while(IDptr = getCBQitem(&mixEngine->cbQueue)){
			if(*IDptr == (unsigned)(-1)){
				/* jackd has quit or crashed.  We need to shutdown. */
				mixEngine->client = NULL;
				quit = 1;
			}else if(*IDptr == (unsigned)(-2)){
				// Check if any of our inputs
				// has had a change in connection status
				inrec = mixEngine->ins;
				cmax = mixEngine->chanCount;
				for(i=0; i<mixEngine->inCount; i++){
					pptr = inrec->in_jPorts;
					isConnected = 0;
					for(c=0; c<cmax; c++){
						if(jack_port_connected(*pptr))
							isConnected = 1;
						pptr++;
					}
					// update connected status
					inrec->isConnected = isConnected;
					inrec++;
				}
			}else if(port = jack_port_by_id(mixEngine->client, *IDptr)){
				pthread_mutex_lock(&mixEngine->jackMutex);
				if(jack_port_flags(port) & JackPortIsOutput)
					isSource = 1;
				else
					isSource = 0;
				name = jack_port_name(port);
				pthread_mutex_unlock(&mixEngine->jackMutex);

				if(name){
					/* search jackconn list for port name */
					pthread_rwlock_rdlock(&connLock);
					jcrec = (connRecord *)&connList;
					while(jcrec){
						if(isSource)
							jcrec = findConnRecord(jcrec, name, NULL);
						else
							jcrec = findConnRecord(jcrec, NULL, name);
						if(jcrec){
							/* found... reconnect */
							pthread_mutex_lock(&mixEngine->jackMutex);
							jack_connect(mixEngine->client, jcrec->src, jcrec->dest);
							pthread_mutex_unlock(&mixEngine->jackMutex);
						}
					}
					pthread_rwlock_unlock(&connLock);
					if(isSource){
						/* search mixer input channel list for port name */
						inrec = mixEngine->ins;
						cmax = mixEngine->chanCount;
						for(i=0; i<mixEngine->inCount; i++){
							if(inrec->persist && inrec->UID && (inrec->status & status_loading)){
								plist = GetMetaData(inrec->UID, "portList", 0);
								if(strlen(plist)){
									pptr = inrec->in_jPorts;
									for(c=0; c<cmax; c++){	
										if(clist = str_NthField(plist, "&", c)){
											i = 0;
											while(pname = str_NthField(clist, "+", i)){
												if(!strcmp(pname, name)){
													pthread_mutex_lock(&mixEngine->jackMutex);
													if(!jack_port_connected_to(*pptr, name))
														jack_connect(mixEngine->client, name, jack_port_name(*pptr));
													pthread_mutex_unlock(&mixEngine->jackMutex);
												}
												free(pname);
												i++;
											}
											free(clist);
										}
										pptr++;
									}
								}
								free(plist);
							}
							inrec++;
						}
					}else{
						/* check for mix-minus output lists on inputs */
						inrec = mixEngine->ins;
						cmax = mixEngine->chanCount;
						for(i=0; i<mixEngine->inCount; i++){
							if(inrec->UID){
								plist = GetMetaData(inrec->UID, "MixMinusList", 0);
								if(strlen(plist)){
									pptr = inrec->mm_jPorts;
									for(c=0; c<cmax; c++){	
										if(clist = str_NthField(plist, "&", c)){
											i = 0;
											while(pname = str_NthField(clist, "+", i)){
												if(!strcmp(pname, name)){
													pthread_mutex_lock(&mixEngine->jackMutex);
													if(!jack_port_connected_to(*pptr, name))
														jack_connect(mixEngine->client, jack_port_name(*pptr), pname);
													pthread_mutex_unlock(&mixEngine->jackMutex);
												}
												free(pname);
												i++;
											}
											free(clist);
										}
										pptr++;
									}			
								}
								free(plist);
							}
							inrec++;
						}
						/* itterate though the output groups, attempting to reconnect unconnected ports. */
						orec = mixEngine->outs;
						pthread_rwlock_rdlock(&mixEngine->outGrpLock);
						for(i=0; i<mixEngine->outCount; i++){
							updateOutputConnections(mixEngine, orec, 0, NULL, name);
							orec++;
						}
						pthread_rwlock_unlock(&mixEngine->outGrpLock);							
					}			
				}
			}
		}
	
		pthread_mutex_lock(&mixEngine->cbQueueMutex);
		pthread_cond_wait(&mixEngine->cbQueueSemaphore, &mixEngine->cbQueueMutex);
		pthread_mutex_unlock(&mixEngine->cbQueueMutex);
	}
	return NULL;
}

void *playerChangeWatcher(void *refCon){
	inChannel *instance;
	outChannel *outstance;
	uint32_t changed;
	uint32_t lastBusses, curBusses;
	uint32_t state;
	notifyData	data;
	char *portName, *chanList, *mmList;
	jack_port_t **port;
	int i, c, cmax;
	char *triggerFile, *triggerDir, *type, *name, *end;

	triggerFile = NULL;
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	lastBusses = 0;
	while(dispRun){
		triggerDir = GetMetaData(0, "file_trigger_dir", 0);
		if(strlen(triggerDir)){
			if(triggerDir[strlen(triggerDir)-1] != directoryToken){
				// add trailing slash
				str_appendstr(&triggerDir, directoryTokenStr);
			}
		}
		
		/* check input players */
		instance = mixEngine->ins;
		for(i=0; i<mixEngine->inCount; i++){
			/* check change flags */
			if(changed = instance->changed){
				if(changed & change_stat){
					if(instance->status & status_deleteWhenDone){
						if((instance->status & status_finished) || 
								((instance->status & status_hasPlayed) && 
								!(instance->status & status_playing))){
							instance->persist = 0;
							jack_port_t **port;
							port = instance->in_jPorts;
							cmax = mixEngine->chanCount;
							pthread_mutex_lock(&mixEngine->jackMutex);
							for(c=0; c<cmax; c++){
								jack_port_disconnect(mixEngine->client, *port);
								port++;
							}
							pthread_mutex_unlock(&mixEngine->jackMutex);
char logstr[64];
snprintf(logstr, sizeof logstr, "[debug] -:player done, jack discon; Player %d.", i);
serverLogMakeEntry(logstr);
						}
					}
					data.senderID = 0;
					data.reference = htonl(i);
					data.value.iVal = htonl(instance->status);
					notifyMakeEntry(nType_pstat, &data, sizeof(data));
				}
				if(changed & change_pos){
					data.senderID = 0;
					data.reference = htonl(i);
					data.value.fVal = (float)instance->pos;
					data.value.iVal = htonl(data.value.iVal);
					notifyMakeEntry(nType_pos, &data, sizeof(data));
					if(instance->aplFile){
						// player has an associated playlist file (apl)
						// reset the next item position to next after the
						// new current position.  We need to take the
						// apl file position back to zero incase the new
						// play position is a move back in play time.
						rewind(instance->aplFile);
						float pos = instance->pos;
						if(pos <= 0.0)
							pos = 0.1;	// prevent disallowed apl offset
						instance->nextAplEvent = associatedPLNext(instance->aplFile, pos);
					}
				}
				if(changed & change_vol){
					data.senderID = 0;
					data.reference = htonl(i);
					data.value.fVal = instance->vol;
					data.value.iVal = htonl(data.value.iVal);
					notifyMakeEntry(nType_vol, &data, sizeof(data));
				}
				if(changed & change_bal){
					data.senderID = 0;
					data.reference = htonl(i);
					data.value.fVal = instance->bal;
					data.value.iVal = htonl(data.value.iVal);
					notifyMakeEntry(nType_bal, &data, sizeof(data));
				}
				if(changed & (change_bus | change_mutes)){
					data.senderID = 0;
					data.reference = htonl(i);
					data.value.iVal = htonl(instance->busses);
					notifyMakeEntry(nType_bus, &data, sizeof(data));
				}
				if(changed & change_stop){
					/* handle stop player */
					if(instance->UID){
						name = GetMetaData(instance->UID, "Name", 0);
						type = GetMetaData(instance->UID, "Type", 0);
						if(strlen(triggerDir) && strlen(name) && !strcmp(type, "input")){
							str_setstr(&triggerFile, triggerDir);
							str_appendstr(&triggerFile, name);
							str_appendstr(&triggerFile, ".stop");
							createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, i, 0, 0);
							free(triggerFile);
							triggerFile = NULL;
						}
						free(name);
						free(type);
					}
				}
				if(changed & change_play){
					/* handle start player */
					if(instance->UID){
						if(!(instance->busses & 2L)){
							// NOT in cue... proceed with logging the item play
							if(instance->sourceType != sourceTypeCanRepos){
								// Live items should be logged whenever played.
								// File items (can reposition) are logged only
								// once. After an item is logged, it's logID 
								// will be set preventing further logging. Since 
								// this is a live item, we clear the logID, if 
								// any, so it can be relogged.
								SetMetaData(instance->UID, "logID", "0");
								instance->status = instance->status & ~status_logged;
							}
							
							pthread_mutex_lock( &lastsegMutex);
							pthread_cond_broadcast(&lastsegSemaphore);
							pthread_mutex_unlock( &lastsegMutex);
							
							// create program log entry
							if((instance->status & status_logged) == 0){
								instance->status = instance->status | status_logged;
								programLogUIDEntry(instance->UID, 0, (instance->busses & 0xFF));
							}
						}
						
						name = GetMetaData(instance->UID, "Name", 0);
						type = GetMetaData(instance->UID, "Type", 0);
						if(strlen(triggerDir) && strlen(name) && !strcmp(type, "input")){
							str_setstr(&triggerFile, triggerDir);
							str_appendstr(&triggerFile, name);
							str_appendstr(&triggerFile, ".start");
							createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, i, 0, 0);
							free(triggerFile);
							triggerFile = NULL;
						}
						free(name);
						free(type);
					}
				}
				if(changed & change_loaded){
					/* handle loaded player */
					uint32_t locID;
					if(locID = instance->UID){
						if(instance->managed){
							if(!getQueuePos(&locID)){
								// doesn't apprear to be in the queue anymore... unload it.
								// this can happen if an item is deleted from the list while it is loading.
								instance->persist = 0;
								jack_port_t **port;
								port = instance->in_jPorts;
								cmax = mixEngine->chanCount;
								pthread_mutex_lock(&mixEngine->jackMutex);
								for(c=0; c<cmax; c++){
									jack_port_disconnect(mixEngine->client, *port);
									port++;
								}
								pthread_mutex_unlock(&mixEngine->jackMutex);
								// clear loaded flag
								changed = changed & ~change_loaded;
char logstr[64];
snprintf(logstr, sizeof logstr, "[debug] -:no longer in queue; Player %d.", i);
serverLogMakeEntry(logstr);
							}
						}
						if(changed & change_loaded){
							// still loaded after above test
							name = GetMetaData(instance->UID, "Name", 0);
							type = GetMetaData(instance->UID, "Type", 0);
							if(strlen(triggerDir) && strlen(name) && !strcmp(type, "input")){
								str_setstr(&triggerFile, triggerDir);
								str_appendstr(&triggerFile, name);
								str_appendstr(&triggerFile, ".load");
								createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, i, 0, 0);
								free(triggerFile);
								triggerFile = NULL;
							}
							free(name);
							free(type);
						}
					}
					if(changed & change_loaded){
						// again, still loaded after above tests
						notifyData data;
						data.reference = 0;
						data.senderID = 0;
						data.value.iVal = 0;
						notifyMakeEntry(nType_status, &data, sizeof(data));
						
						data.reference = htonl(i);
						data.value.fVal = instance->vol;
						data.value.iVal = htonl(data.value.iVal);
						notifyMakeEntry(nType_vol, &data, sizeof(data));
						
						data.value.fVal = instance->bal;
						data.value.iVal = htonl(data.value.iVal);
						notifyMakeEntry(nType_bal, &data, sizeof(data));

						data.value.iVal = htonl(instance->busses);
						notifyMakeEntry(nType_bus, &data, sizeof(data));
		
						data.value.iVal = htonl(instance->status);
						notifyMakeEntry(nType_pstat, &data, sizeof(data));
char logstr[64];
snprintf(logstr, sizeof logstr, "[debug] -:player loaded; Player %d.", i);
serverLogMakeEntry(logstr);
					}
				}
				if(changed & change_unloaded){
					/* handle unloaded player */
					mmList = NULL;
					if(instance->UID){
						// remove from playlist queue if it is in there
						releaseQueueEntry(instance->UID);
						// execute unload trigger file script, if any...
						name = GetMetaData(instance->UID, "Name", 0);
						type = GetMetaData(instance->UID, "Type", 0);
						mmList = GetMetaData(instance->UID, "MixMinusList", 0);
						if(!strlen(mmList)){
							free(mmList);
							mmList = NULL; 
						}
						if(strlen(triggerDir) && strlen(name) && !strcmp(type, "input")){
							str_setstr(&triggerFile, triggerDir);
							str_appendstr(&triggerFile, name);
							str_appendstr(&triggerFile, ".unload");
							createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, i, 0, 0);
							free(triggerFile);
							triggerFile = NULL;
						}
						free(name);
						free(type);
					}
					if(instance->aplFile){
						fclose(instance->aplFile);
						instance->aplFile = NULL;
					}
					notifyData	data;
					data.reference = 0;
					data.senderID = 0;
					data.value.iVal = 0;
					notifyMakeEntry(nType_status, &data, sizeof(data));
					
					data.reference = htonl(i);
					notifyMakeEntry(nType_vol, &data, sizeof(data));
					
					notifyMakeEntry(nType_bal, &data, sizeof(data));
					
					notifyMakeEntry(nType_bus, &data, sizeof(data));
					
					notifyMakeEntry(nType_pstat, &data, sizeof(data));
					
					/* if mmList is set, disconnect list, and clear */
					if(mmList){
						/* disconnect requested monitor mix-minus ports */
						port = instance->mm_jPorts;
						cmax = mixEngine->chanCount;
						for(c=0; c<cmax; c++){
							if(chanList = str_NthField(mmList, "&", c)){
								i = 0;
								while(portName = str_NthField(chanList, "+", i)){
									if(strlen(portName)){
										pthread_mutex_lock(&mixEngine->jackMutex);
										jack_disconnect(mixEngine->client, jack_port_name(*port), portName);
										pthread_mutex_unlock(&mixEngine->jackMutex);
									}
									free(portName);
									i++;
								}
								free(chanList);
							}
							port++;
						}
						free(mmList);
					}
char logstr[64];
snprintf(logstr, sizeof logstr, "[debug] -:player unloaded; Player %d.", i);
serverLogMakeEntry(logstr);
				}
				if((changed & change_type) && (instance->UID)){
					uint32_t cVal;
					type = GetMetaData(instance->UID, "Controls", 0);
					cVal = strtoul(type, &end, 16);
					free(type);
					if(instance->sourceType == sourceTypeCanRepos){
						type = hstr(cVal | ctl_pos, 8);
						SetMetaData(instance->UID, "Controls", type); 
						free(type);
					}
				}
				
				if((changed & change_aplEvent) && (instance->aplFile)){
					if(associatedPLLog(instance->aplFile, instance->UID, instance->busses, instance->aplFPmatch)){
						instance->nextAplEvent = associatedPLNext(instance->aplFile, instance->nextAplEvent);
					}
				}
				
				if((changed & change_feedbus) && (instance->UID)){
					type = ustr(instance->feedBus);
					SetMetaData(instance->UID, "MixMinusBus", type);
					free(type);
				}
				
				if((changed & change_feedvol) && (instance->UID)){
					type = fstr(3, instance->feedVol);
					SetMetaData(instance->UID, "MixMinusVol", type);
					free(type);
				}
				
				/* all handled: clear flags */
				instance->changed = 0;
			}
			
			/* we need to check for failed player loads here */
			if((instance->status & status_loading) && instance->attached){
				if(kill(instance->attached, 0) < 0){
					/* the PID no loger is running... change status
					 * to remove, and the next render cycle will 
					 * set handle it. */
					instance->status = status_remove; 
					instance->attached = 0;	// to prevent doing this again.
					char *urlstr = NULL;
					char *logstr = NULL;
					if(instance->UID){
						urlstr = GetMetaData(instance->UID, "URL", 0);
						releaseQueueEntry(instance->UID);
					}
					str_setstr(&logstr, "[media] -:player load failed; ");
					if(urlstr){
						str_appendstr(&logstr, urlstr);
						free(urlstr);
					}
					serverLogMakeEntry(logstr);
					free(logstr);
				}
			}
			/* we need to check for left-over UIDs from unload player here
			 * since releasing the UID may block */
			if((instance->status == status_empty) && instance->UID){
				releaseMetaRecord(instance->UID);
				instance->UID = 0;
char logstr[64];
snprintf(logstr, sizeof logstr, "[debug] -:leftover UID cleared; Player %d.", i);
serverLogMakeEntry(logstr);
			}
			/* Likewise, if a player staus is not remove or loading, but 
			 * it's UID is zero, then an external connection was made, 
			 * and we should creat a UID for it here. */
			if((instance->status & ~(status_remove | status_loading)) && !instance->UID){
				const char** conList;
				char *url, *name;
				unsigned int c;
char logstr[64];
snprintf(logstr, sizeof logstr, "[debug] -:new jack connection; Player %d.", i);
serverLogMakeEntry(logstr);
				url = NULL;
				name = NULL;
				str_setstr(&url, "");
				for(c=0; c<mixEngine->chanCount; c++){
					pthread_mutex_lock(&mixEngine->jackMutex);
					conList = jack_port_get_connections(instance->in_jPorts[c]);
					pthread_mutex_unlock(&mixEngine->jackMutex);
					if(conList){	
						if(conList[0]){
							// first port connection name only
							if(strlen(url))
								str_appendstr(&url, "&");
							if(!name){
								if(name = str_NthField(conList[0], ":", 0))
									name = strdup(name);
							}
							str_appendstr(&url, conList[0]);
						}
						jack_free(conList);
					}
				}
				str_insertstr(&url, "jack:///", 0);
				instance->UID = createMetaRecord(url, NULL, 0);
				SetMetaData(instance->UID, "Type", "jack");
				if(name){
					SetMetaData(instance->UID, "Name", name);
					free(name);
				}
				free(url);
			}
			
			instance++;
		}
		
		/* check output groups */
		outstance = mixEngine->outs;
		for(i=0; i<mixEngine->outCount; i++){
			/* chech change flags */
			if(changed = outstance->changed){		
				/* note: UID with top byte set to 0xC0 indicates output
				 * group ID, not a UID.  Lower 24 bits are the output index */
				if(changed & change_delay){
					data.senderID = 0;
					data.reference = htonl((i & 0x00ffffff) | 0xC0000000);
					data.value.fVal = outstance->delay;
					data.value.iVal = htonl(data.value.iVal);
					notifyMakeEntry(nType_dly, &data, sizeof(data));
				}
				if(changed & change_bus){
					data.senderID = 0;
					data.reference = htonl((i & 0x00ffffff) | 0xC0000000);
					data.value.iVal = htonl(outstance->bus);
					notifyMakeEntry(nType_bus, &data, sizeof(data));
				}
				if(changed & change_vol){
					data.senderID = 0;
					data.reference = htonl((i & 0x00ffffff) | 0xC0000000);
					data.value.fVal = outstance->vol;
					data.value.iVal = htonl(data.value.iVal);
					notifyMakeEntry(nType_vol, &data, sizeof(data));
				}
				/* all handled: clear flags */
				outstance->changed = 0;
			}
			outstance++;
		}
		
		/* handle mute group activations */
		curBusses = mixEngine->activeBus;
		if(lastBusses != curBusses){
			if(strlen(triggerDir)){
				// check cue
				state = curBusses & (1L << 24);
				if(state != (lastBusses & (1L << 24))){
					str_setstr(&triggerFile, triggerDir);	
					if(state){
						str_appendstr(&triggerFile, "cue.start");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}else{
						str_appendstr(&triggerFile, "cue.stop");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}
					free(triggerFile);
					triggerFile = NULL;
				}
			
				// check muteA	
				state = curBusses & (1L << 25);
				if(state != (lastBusses & (1L << 25))){
					str_setstr(&triggerFile, triggerDir);	
					if(state){
						str_appendstr(&triggerFile, "muteA.start");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}else{
						str_appendstr(&triggerFile, "muteA.stop");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}
					free(triggerFile);
					triggerFile = NULL;
				}
			
				// check muteB
				state = curBusses & (1L << 26);
				if(state != (lastBusses & (1L << 26))){
					str_setstr(&triggerFile, triggerDir);	
					if(state){
						str_appendstr(&triggerFile, "muteB.start");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}else{
						str_appendstr(&triggerFile, "muteB.stop");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}
					free(triggerFile);
					triggerFile = NULL;
				}
			
				// check muteC
				state = curBusses & (1L << 27);
				if(state != (lastBusses & (1L << 27))){
					str_setstr(&triggerFile, triggerDir);	
					if(state){
						str_appendstr(&triggerFile, "muteC.start");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}else{
						str_appendstr(&triggerFile, "muteC.stop");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}
					free(triggerFile);
					triggerFile = NULL;
				}
				
				// check TalkBack1	
				state = curBusses & (1L << 29);
				if(state != (lastBusses & (1L << 29))){
					str_setstr(&triggerFile, triggerDir);	
					if(state){
						str_appendstr(&triggerFile, "talkback1.start");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}else{
						str_appendstr(&triggerFile, "talkback1.stop");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}
					free(triggerFile);
					triggerFile = NULL;
				}
			
				// check TalkBack2
				state = curBusses & (1L << 30);
				if(state != (lastBusses & (1L << 30))){
					str_setstr(&triggerFile, triggerDir);	
					if(state){
						str_appendstr(&triggerFile, "talkback2.start");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}else{
						str_appendstr(&triggerFile, "talkback2.stop");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}
					free(triggerFile);
					triggerFile = NULL;
				}
			
				// check TalkBack3
				state = curBusses & (1L << 31);
				if(state != (lastBusses & (1L << 31))){
					str_setstr(&triggerFile, triggerDir);	
					if(state){
						str_appendstr(&triggerFile, "talkback3.start");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}else{
						str_appendstr(&triggerFile, "talkback3.stop");
						createTaskItem(triggerFile, loadConfigFromTask, NULL, instance->UID, 0, 0, 0);
					}
					free(triggerFile);
					triggerFile = NULL;
				}
			}
			lastBusses = curBusses;
		}
		
		free(triggerDir);
		pthread_mutex_lock(&mixEngine->changedMutex);
		pthread_cond_wait(&mixEngine->changedSemaphore, &mixEngine->changedMutex);
		pthread_mutex_unlock(&mixEngine->changedMutex);
	}
	return NULL;
}

void serverLogCloseFile(void)
{
	pthread_mutex_lock(&srvLogQueueLock);
	if(svrLogFile){
		fclose(svrLogFile);
		svrLogFile = NULL;
	}
	pthread_mutex_unlock(&srvLogQueueLock);
}

void *serverLogWatcher(void *refCon){
	ServerLogRecord *instance;
	int i;
	char timeStr[32];
	char *fullMSG;
	char *localName;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	fullMSG = NULL;
	while(dispRun){
		pthread_mutex_lock(&srvLogQueueLock);
		while(dispRun && svrLogQueue){
			instance = (ServerLogRecord *)getNextNode((LinkedListEntry *)&svrLogQueue);
			unlinkNode((LinkedListEntry *)instance, (LinkedListEntry *)&svrLogQueue, 0);
			pthread_mutex_unlock(&srvLogQueueLock);
			
			// something in the queue to put in the log file
			localName = GetMetaData(0, "file_log", 0);

			if(svrLogFile){
				// check that the file name has not changed
				if(strcmp(localName, svrLogFileName)){
					fclose(svrLogFile);
					svrLogFile = NULL;
				}
			}
			if(!svrLogFile && strlen(localName)){		
					svrLogFile = fopen(localName, "a+");
					if(svrLogFile)
						str_setstr(&svrLogFileName, localName);
			}
			free(localName);
			
			if(svrLogFile && instance->message){
				// file open... write to it.
				strftime(timeStr, sizeof(timeStr), "%b %d, %Y %H:%M:%S", &instance->when);
				str_setstr(&fullMSG, timeStr);
				str_appendstr(&fullMSG, " | ");
				str_appendstr(&fullMSG, mixEngine->ourJackName);
				str_appendstr(&fullMSG, " | ");
				str_appendstr(&fullMSG, instance->message);
				fprintf(svrLogFile, "%s\n", fullMSG);
				fflush(svrLogFile);
			}
			if(instance->message)
				free(instance->message);
			free(instance);
			pthread_mutex_lock(&srvLogQueueLock);
		}
		pthread_mutex_unlock(&srvLogQueueLock);
		
		pthread_mutex_lock(&srvLogMutex);
		pthread_cond_wait(&srvLogSemaphore, &srvLogMutex);
		pthread_mutex_unlock(&srvLogMutex);
	}
	if(fullMSG)
		free(fullMSG);
	free(svrLogFileName);
	return NULL;
}

void serverLogMakeEntry(char *message)
{
	time_t now;
	ServerLogRecord *instance;

	if(instance = calloc(1, sizeof(ServerLogRecord))){
		now = time(NULL);
		localtime_r(&now, &instance->when);
		str_setstr(&instance->message, message);
		pthread_mutex_lock(&srvLogQueueLock);
		if(countNodesAfter((LinkedListEntry *)&svrLogQueue) < queueMax){
			appendNode((LinkedListEntry *)&svrLogQueue, (LinkedListEntry *)instance);
			pthread_mutex_unlock(&srvLogQueueLock);
			pthread_cond_signal(&srvLogSemaphore);
		}else{
			// no room in the queue... drop record
			pthread_mutex_unlock(&srvLogQueueLock);
			free(instance);
		}
	}
}

unsigned char serverLogRotateLogFile(void)
{
	unsigned char result;
	char *mvName;
	
	result = 0;
	if(strlen(svrLogFileName) && svrLogFile){
		mvName = NULL;
		str_setstr(&mvName, svrLogFileName);
		str_appendstr(&mvName, ".old");
		pthread_mutex_lock(&srvLogQueueLock);
		if(rename(svrLogFileName, mvName) == 0){
			fclose(svrLogFile);
			svrLogFile = NULL;
			result = 1;
		}
		pthread_mutex_unlock(&srvLogQueueLock);
		free(mvName);
	}
	return result;
}

void notifyMakeEntry(char type, void *data, unsigned short size)
{
	unsigned int length;
	notifyEntry *record;

	// format and fill a notify packet
	length = size + sizeof(notifyEntry) - 1;
	if(record = (notifyEntry *)calloc(1, length)){
		record->container.marker = 0;
		record->container.type = type;
		record->container.dataSize = size;
		memcpy(record->container.data, data, size);
		pthread_mutex_lock(&notifyQueueLock);
		if(countNodesAfter((LinkedListEntry *)&notifyQueue) < queueMax){
			appendNode((LinkedListEntry *)&notifyQueue, (LinkedListEntry *)record);
			pthread_mutex_unlock(&notifyQueueLock);
			pthread_cond_broadcast(&notifySemaphore);
		}else{
			// no room in the queue... drop record
			pthread_mutex_unlock(&notifyQueueLock);
			serverLogMakeEntry("[dispatch] notifyMakeEntry-notifyQueue:Queue full, entry dropped.");
			free(record);
		}
	}
}

void *notifyWatcher(void *refCon){
	notifyEntry *record;
	int i, size;
	unsigned char isVU;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	while(dispRun){
		pthread_mutex_lock(&notifyQueueLock);
		while(notifyQueue){
			record = (notifyEntry *)getNextNode((LinkedListEntry *)&notifyQueue);
			unlinkNode((LinkedListEntry *)record, (LinkedListEntry *)&notifyQueue, 0);
			pthread_mutex_unlock(&notifyQueueLock);

			size = record->container.dataSize + 4;
			record->container.dataSize = htons(record->container.dataSize); // size in network byte order
			if(record->container.type == nType_vu)
				isVU = 1;
			else
				isVU = 0;
			noticeSend((const char *)&record->container, size, isVU);
			free(record);

			pthread_mutex_lock(&notifyQueueLock);
		}
		pthread_mutex_unlock(&notifyQueueLock);

		pthread_mutex_lock(&notifyMutex);
		pthread_cond_wait(&notifySemaphore, &notifyMutex);
		pthread_mutex_unlock(&notifyMutex);
	}
	return NULL;
}

void* metersUpdateThread(void *refCon){        
	struct timespec timeout;
	vuNContainer *record = NULL;
	vuNInstance *instance;
	vuNData *values;
	inChannel *inchrec;
	vuData *meters;
	unsigned char vuRecCnt;
	float total;
	int i, c, imax, bmax, ccount; 
	unsigned short size;
	unsigned char delay;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	size = 0;
		
	delay = 0;
	while(dispRun && vuRecord){
		/* handle sending VU meters data to all clients registered for it.
		 * Start by counting the total number of ins & out buses, and
		 * associated channels */
		
		vuRecCnt = 0;
		size = 0;
		instance = (vuNInstance *)vuRecord->data;
		imax = mixEngine->inCount;
		ccount = mixEngine->chanCount;
		inchrec = mixEngine->ins;
		for(i=0; i < imax; i++){
			if(inchrec->UID){
				instance->uid = htonl(inchrec->UID);
				instance->count = ccount;
				values = (vuNData *)instance->data;
				meters = inchrec->VUmeters;
				for(c=0; c<ccount; c++){
					values->avr = ftovu(meters->avr);
					values->peak = ftovu(meters->peak);
					values++;
					meters++;
				}
				instance = (vuNInstance *)values;
				vuRecCnt++;
			}
			inchrec++;
		}	
		/* mix bus meters */
		vuRecCnt++;
		instance->uid = 0;
		instance->count = bmax = (mixEngine->busCount * mixEngine->chanCount);
		meters = mixEngine->mixbuses->VUmeters;
		values = (vuNData *)instance->data;
		for(c=0; c<bmax; c++){
			values->avr = ftovu(meters->avr), 
			values->peak = ftovu(meters->peak);
			values++;
			meters++;
		}
		instance = (vuNInstance *)values;
		vuRecord->count = vuRecCnt;
		size = (char *)instance - (char *)vuRecord;
		notifyMakeEntry(nType_vu, vuRecord, size);
		
		// send realtime render CPU load info every 10 seconds
		if(--delay == 0){
			delay = 100;
			notifyData	data;
			data.senderID = 0;
			data.reference = 0;
			pthread_mutex_lock(&mixEngine->jackMutex);
			total = jack_cpu_load(mixEngine->client);
			pthread_mutex_unlock(&mixEngine->jackMutex);
			data.value.iVal = 0;
			data.value.cVal[0] = (uint8_t)roundf(total);
			notifyMakeEntry(nType_load, &data, sizeof(data));
		}
		
		// Wait for a time out to check again
		timeout.tv_nsec= 100 * 1000 * 1000;  // 100 ms
		timeout.tv_sec=0;	// 0 seconds
		nanosleep(&timeout, NULL);
	}
	if(record)
		free(record);
	return NULL;
}

void programLogMakeEntry(ProgramLogRecord *entry){
	time_t now;
	
	pthread_mutex_lock(&pgmLogQueueLock);
	if(countNodesAfter((LinkedListEntry *)&pgmLogQueue) < queueMax){
		// NOTE: non-zero strings will be freed by the queue when done.
		entry->when = time(NULL);
		if(entry->UID)
			// if non-zero, will be release by log watcher
			retainMetaRecord(entry->UID);
		appendNode((LinkedListEntry *)&pgmLogQueue, (LinkedListEntry *)entry);
		pthread_mutex_unlock(&pgmLogQueueLock);
		pthread_cond_signal(&pgmLogSemaphore);
	}else{
		// no room in the queue... drop record
		pthread_mutex_unlock(&pgmLogQueueLock);
		serverLogMakeEntry("[dispatch] programLogMakeEntry-programQueue:Queue full, entry dropped.");
		if(entry->name)
			free(entry->name);
		if(entry->artist)
			free(entry->artist);
		if(entry->album)
			free(entry->album);
		if(entry->source)
			free(entry->source);
		if(entry->comment)
			free(entry->comment);
		if(entry->owner)
			free(entry->owner);
		if(entry->webURL)
			free(entry->webURL);	
		free(entry);
	}
}

void programLogUIDEntry(uint32_t passUID, unsigned char Added, unsigned char Played){
	ProgramLogRecord *instance;
	
	if(instance = calloc(1, sizeof(ProgramLogRecord))){
		instance->added = Added;
		instance->played = Played;
		instance->UID = passUID;
		instance->post = 1;
		programLogMakeEntry(instance);
	}
}

void executeLogScript(uint32_t logID){
	uint32_t UID;
	char *script, *logIDStr;
	
	script = GetMetaData(0, "sys_logscript", 0);
	if(strlen(script)){
		logIDStr = ustr(logID);
		// set up parameters to pass the script:
		// previous item played log ID;
		// path to arserver settings config file;
		str_appendchr(&script, ' ');
		str_appendstr(&script, logIDStr);
		free(logIDStr);
		str_appendchr(&script, ' ');
		str_appendstr(&script, startup_path);

		// run it as an arserver task
		ExecuteProcess(script, 0, 60);
	}
	free(script);
}
 
void *programLogWatcher(void* refCon){	
	ProgramLogRecord *rec;
	char *tmp;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	while(dispRun){
		pthread_mutex_lock(&pgmLogQueueLock);
		while(pgmLogQueue){
			rec = (ProgramLogRecord *)getNextNode((LinkedListEntry *)&pgmLogQueue);
			unlinkNode((LinkedListEntry *)rec, (LinkedListEntry *)&pgmLogQueue, 0);
			pthread_mutex_unlock(&pgmLogQueueLock);
			if(rec->UID){ 
				// fill in data from UID, if available
				if(rec->name)
					free(rec->name);
				rec->name = GetMetaData(rec->UID, "Name", 0);
				if(rec->artist)
					free(rec->artist);
				rec->artist = GetMetaData(rec->UID, "Artist", 0);
				if(rec->album)
					free(rec->album);
				rec->album = GetMetaData(rec->UID, "Album", 0);
				if(rec->source)
					free(rec->source);
				rec->source = GetMetaData(rec->UID, "URL", 0);
				if(rec->comment)
					free(rec->comment);
				rec->comment = GetMetaData(rec->UID, "Comment", 0);
				if(rec->owner)
					free(rec->owner);
				rec->owner = GetMetaData(rec->UID, "Owner", 0);
				if(rec->webURL)
					free(rec->webURL);
				rec->webURL = GetMetaData(rec->UID, "WebURL", 0);
				// set up Integers
				rec->artistID = GetMetaInt(rec->UID, "ArtistID", NULL);
				rec->albumID = GetMetaInt(rec->UID, "AlbumID", NULL);
				rec->ownerID = GetMetaInt(rec->UID, "OwnerID", NULL);
				rec->ID = GetMetaInt(rec->UID, "ID", NULL);
				rec->logID = GetMetaInt(rec->UID, "logID", NULL);
				rec->location = GetMetaInt(0, "db_loc", NULL);
				if(GetMetaInt(rec->UID, "NoLog", NULL)){
					rec->post = 0;
					rec->added = rec->added | 2;
				}else{
					if(GetMetaInt(rec->UID, "NoPost", NULL)){
						rec->added = rec->added | 4;
						rec->post = 0;
					}else
						rec->post = 1;
				}
				releaseMetaRecord(rec->UID);
			}
			// fill NULL strings with empty string
			if(!rec->name)
				rec->name = strdup("");
			if(!rec->artist)
				rec->artist = strdup("");
			if(!rec->album)
				rec->album = strdup("");
			if(!rec->source)
				rec->source = strdup("");
			if(!rec->comment)
				rec->comment = strdup("");
			if(!rec->owner)
				rec->owner = strdup("");
			if(!rec->webURL)
				rec->webURL = strdup("");
			if((rec->added & 0x01) || (rec->played & log_busses)){
				// update database program log
				if(MakeLogEntry(rec)){
					if(rec->played || !(rec->played || rec->added)){
						// change the log changed time if the item was played AND the db was updated
						logChangeTime = rec->when;
						// new item started playing - execute the log script, if specified
						if(rec->logID)
							executeLogScript(rec->logID);
						// send out notifications
						notifyData data;
						data.senderID = 0;
						data.reference = htonl(0);
						data.value.iVal = htonl(0);
						notifyMakeEntry(nType_status, &data, sizeof(data));
					}
				}
			}
			if(rec->played && rec->post){
				// update all the recorder/encoder now-playing meta data.
				// Note: control packet peer = play busses
				cJSON *obj, *ar;
				char *jstr;
				size_t len;
				
				if(obj = cJSON_CreateObject()){
					cJSON_AddStringToObject(obj, "Name", rec->name);
					if(strlen(rec->artist))
						cJSON_AddStringToObject(obj, "Artist", rec->artist);
					if(strlen(rec->album))
						cJSON_AddStringToObject(obj, "Album", rec->album);

					if((rec->ID) && (ar = cJSON_CreateObject())){
						cJSON_AddNumberToObject(ar, "FP", getFingerprint());
						cJSON_AddNumberToObject(ar, "ID", rec->ID);
						if(rec->artistID)
							cJSON_AddNumberToObject(ar, "ArtistID", rec->artistID);
						if(rec->albumID)
							cJSON_AddNumberToObject(ar, "AlbumID", rec->albumID);
						if(rec->ownerID)
							cJSON_AddNumberToObject(ar, "OwnerID", rec->ownerID);
						if(rec->location)	
							cJSON_AddNumberToObject(ar, "db_loc", rec->location);
						if(strlen(rec->source))
							cJSON_AddStringToObject(ar, "Source", rec->source);
						if(strlen(rec->comment))
							cJSON_AddStringToObject(ar, "Comment", rec->comment);
						if(strlen(rec->owner))
							cJSON_AddStringToObject(ar, "Owner", rec->owner);
						cJSON_AddItemToObject(obj, "AR", ar);
					}

					if(jstr = cJSON_PrintUnformatted(obj)){
						queueControlOutPacket(mixEngine, cPeer_bus | cType_tags, rec->played, strlen(jstr), jstr);
						free(jstr);
					}	
					cJSON_Delete(obj);	
				}
			}
			// all done with the record and child strings
			if(rec->name)
				free(rec->name);
			if(rec->artist)
				free(rec->artist);
			if(rec->album)
				free(rec->album);
			if(rec->source)
				free(rec->source);
			if(rec->comment)
				free(rec->comment);
			if(rec->owner)
				free(rec->owner);
			if(rec->webURL)
				free(rec->webURL);
			free(rec);
			pthread_mutex_lock(&pgmLogQueueLock);
		}
		pthread_mutex_unlock(&pgmLogQueueLock);
		pthread_mutex_lock(&pgmLogMutex);
		pthread_cond_wait(&pgmLogSemaphore, &pgmLogMutex);
		pthread_mutex_unlock(&pgmLogMutex);
	}
	return NULL;
}

unsigned char queueControlOutPacket(mixEngineRecPtr mixRef, char type, uint32_t peer, size_t size, char *data){	
	pthread_mutex_lock(&mixRef->ctlOutQueueMutex);
	if((size <= 2041) && (jack_ringbuffer_write_space(mixRef->ctlOutQueue) >= (size + 7))){
		// enque packet in midi/control queue ring buffer
		controlPacket header;
		header.type = type;
		header.peer = htonl(peer);
		header.dataSize = htons(size);
		jack_ringbuffer_write(mixRef->ctlOutQueue, (char*)&header, 7);
		if(size)
			jack_ringbuffer_write(mixRef->ctlOutQueue, data, size);
		pthread_mutex_unlock(&mixRef->ctlOutQueueMutex);
		pthread_mutex_unlock(&mixRef->ctlOutQueueMutex);
		return 1;	
	}else{
		pthread_mutex_unlock(&mixRef->ctlOutQueueMutex);
		return 0;
	}
}

void *controlQueueInWatcher(void *refCon){
	mixEngineRec *mixEngine = (mixEngineRec*)refCon;
	controlPacket header;
	controlPacket *packet;
	vuNContainer *vuRecord;
	size_t vuSize;
	size_t size;
	inChannel *inchrec;
	char *tmp, *sval;
	time_t now;
	
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	vuSize = 0;
	vuRecord = NULL;
	while(dispRun){
		while(1){	// loop until we break -> nothing else in queue
			now = time(NULL);
			size = jack_ringbuffer_peek(mixEngine->ctlInQueue, (char*)&header, 7);
			if(size == 7){
				size = 7 + ntohs(header.dataSize);
				if(jack_ringbuffer_read_space(mixEngine->ctlInQueue) >= size){
					
					
					if(packet = calloc(1, size+1)){ // +1 for added null termination
						jack_ringbuffer_read(mixEngine->ctlInQueue, (char*)packet, size);
						// convert endia-ness from network to host
						packet->peer = ntohl(packet->peer);
						packet->dataSize = ntohs(packet->dataSize);
						packet->data[packet->dataSize] = 0; // null terminate data: maybe it's a string.
						if(((packet->type & cType_MASK) == cType_tags) && ((packet->type & cPeer_MASK) == cPeer_player)){
							// live tag data from player to be logged, if LiveTags is enabled
							if(checkPnumber(packet->peer)){
								inchrec = &mixEngine->ins[packet->peer];
								if(inchrec->status && inchrec->UID){
									if(GetMetaInt(inchrec->UID, "LiveTags", NULL)){
										ProgramLogRecord *entryRec; 
										cJSON *tags, *item, *ar;
										if(tags = cJSON_Parse(packet->data)){
											entryRec->location = GetMetaInt(0, "db_loc", NULL);
											entryRec->source = GetMetaData(inchrec->UID, "Source", 0);
											entryRec->owner = GetMetaData(inchrec->UID, "Name", 0);
											entryRec->ownerID = GetMetaInt(inchrec->UID, "ID", NULL);
											if((item = cJSON_GetObjectItem(tags, "Name")) && (item->valuestring))
												entryRec->name = strdup(item->valuestring);
											if((item = cJSON_GetObjectItem(tags, "Artist")) && (item->valuestring))
												entryRec->artist = strdup(item->valuestring);
											if((item = cJSON_GetObjectItem(tags, "Album")) && (item->valuestring))
												entryRec->album = strdup(item->valuestring);
											if(ar = cJSON_GetObjectItem(tags, "AR")){
												unsigned int fp = 0;
												if(item = cJSON_GetObjectItem(ar, "FP"))
													fp = item->valueint;
												if(fp == GetMetaInt(inchrec->UID, "Fingerprint", NULL)){
													if((item = cJSON_GetObjectItem(ar, "ID")) && (item->valueint))
														entryRec->ID = item->valueint;
													if((item = cJSON_GetObjectItem(ar, "AlbumID")) && (item->valueint))
														entryRec->albumID = item->valueint;	
													if((item = cJSON_GetObjectItem(ar, "ArtistID")) && (item->valueint))
														entryRec->artistID = item->valueint;
													if((item = cJSON_GetObjectItem(ar, "OwnerID")) && (item->valueint))
														entryRec->ownerID = item->valueint;
													if((item = cJSON_GetObjectItem(ar, "db_loc")) && (item->valueint))
														entryRec->location = item->valueint;
												}
											}	
											
											if(GetMetaInt(inchrec->UID, "NoLog", NULL)){
												entryRec->added = 2;
												entryRec->post = 0;
											}else{
												entryRec->added = 0;
												if(GetMetaInt(inchrec->UID, "NoPost", NULL))
													entryRec->post = 0;
												else
													entryRec->post = 1;
											}
											
											entryRec->played = (inchrec->busses & 0xFF);
											entryRec->UID = 0;
											
											programLogMakeEntry(entryRec);
											cJSON_Delete(tags);
										}
									}
									free(tmp);
								}
							}
						}
						if(((packet->type & cType_MASK) == cType_anc) && 
								(packet->dataSize) && ((packet->type & cPeer_MASK) == cPeer_recorder)){
							// recorders send an announcement at lease once every 10 seconds
							cJSON *parent, *item;
							packet->data[packet->dataSize] = 0; // null terminate data
							if(parent = cJSON_Parse(packet->data)){
								if((item = cJSON_GetObjectItem(parent, "Name")) && (item->valuestring)){
									tmp = GetMetaData(packet->peer, "Name", 0);
									sval = GetMetaData(packet->peer, "Type", 0);
									if((!strlen(tmp) || !strcmp(tmp, item->valuestring))
																	&& !strcmp(sval, "encoder")){
										/* name is empty or matches, and type match... update this uid item */
										free(tmp);
										tmp = istr(now);
										SetMetaData(packet->peer, "TimeStamp", tmp);
										/* itterate through the jSON list, updating metadata */
										unsigned char notify = 0;
										item = parent->child;
										do{
											if(item->string && strlen(item->string) && !item->child){
												if(item->type == cJSON_String){
													if(UpdateMetaData(packet->peer, item->string, item->valuestring))
														notify = 1;
												}else if(item->type == cJSON_Number){
													free(tmp);
													tmp = fstr(item->valuedouble, 2);
													if(UpdateMetaData(packet->peer, item->string, tmp)){
														if(strcmp(item->string, "Position"))
															// any change except position will trigger a notify packet send
															notify = 1;
													}
												}else if(item->type == cJSON_True){
													if(UpdateMetaData(packet->peer, item->string, "1"))
														notify = 1;
												}else if(item->type == cJSON_False){
													if(UpdateMetaData(packet->peer, item->string, "0"))
														notify = 1;
												}
											}
										}while(item = item->next);
										// a change was made... send out notice.
										if(notify){
											notifyData data;
											data.senderID = 0;
											data.reference = htonl(0);
											data.value.iVal = htonl(0);
											notifyMakeEntry(nType_rstat, &data, sizeof(data));
										}
									}else{
										/* no match... UID either doesn't exist, or it does, but name mismatch */
										if((item = cJSON_GetObjectItem(parent, "Name")) && (item->valuestring) && (item = parent->child)){
											uint32_t newUID, tmpUID;
											tmpUID = packet->peer; 
											newUID = createMetaRecord(NULL, &tmpUID, 0);
											if(newUID != tmpUID){
												/* persistant recorder's previous UID is not available anymore
												 * a new metadata record has been created with a different ID.
												 * We need to send a message to the recorder to change it's peer
												 * ID to match the new UID number */
												 tmpUID = htonl(newUID);
												 queueControlOutPacket(mixEngine, cPeer_recorder | cType_reid, packet->peer, 4, (char *)&tmpUID);
											}
											do{
												if(item->string && strlen(item->string) && !item->child){
													if(item->type == cJSON_String){
														SetMetaData(packet->peer, item->string, item->valuestring);
													}else if(item->type == cJSON_Number){
														free(tmp);
														tmp = fstr(item->valuedouble, 2);
														SetMetaData(packet->peer, item->string, tmp);
													}else if(item->type == cJSON_True){
														SetMetaData(packet->peer, item->string, "1");
													}else if(item->type == cJSON_False){
														SetMetaData(packet->peer, item->string, "0");
													}
												}
											}while(item = item->next);
											free(tmp);
											tmp = istr(now);
											SetMetaData(newUID, "TimeStamp", tmp);
											SetMetaData(newUID, "Type", "encoder");
											
											notifyData data;
											data.senderID = 0;
											data.reference = htonl(0);
											data.value.iVal = htonl(0);
											notifyMakeEntry(nType_rstat, &data, sizeof(data));
										}
									}
									free(tmp);
									free(sval);

								}
								cJSON_Delete(parent);
							}

						}
						if(((packet->type & cType_MASK) == cType_vu) && ((packet->type & cPeer_MASK) == cPeer_recorder)){
							vuNInstance *instance;
							uint8_t chanCnt;
							size_t bytes, size;
							/* allocate container for all the vu data */
							bytes = sizeof(vuNContainer) - 1;
							bytes = bytes + (sizeof(vuNInstance) - 1);
							chanCnt = packet->dataSize / sizeof(vuNData);
							bytes = bytes + (chanCnt * sizeof(vuNData));
							if(bytes < 0xFFFF){
								if(vuSize < bytes){
									// resize the reusable vuRecorder if needed
									if(vuRecord)
										free(vuRecord);
									vuRecord = (vuNContainer *)calloc(1, bytes);
									vuSize = bytes;
								}
								vuRecord->count = 1;
								instance = (vuNInstance *)vuRecord->data;
								instance->uid = htonl(packet->peer);
								instance->count = chanCnt;
								memcpy(instance->data, packet->data, packet->dataSize);
								size = packet->dataSize + (sizeof(vuNInstance) + sizeof(vuNContainer) - 2);
								notifyMakeEntry(nType_vu, vuRecord, size);
							}

						}
						if(((packet->type & cType_MASK) == cType_end) && ((packet->type & cPeer_MASK) == cPeer_recorder)){
							releaseMetaRecord(packet->peer);
						}

						free(packet);
					}
				}else{
					// nothing else to read
					break;
				}
			}else{
				// nothing else to read
				break;
			}
		}
		pthread_mutex_lock(&mixEngine->ctlInQueueMutex);
		pthread_cond_wait(&mixEngine->ctlInQueueSemaphore, &mixEngine->ctlInQueueMutex);
		pthread_mutex_unlock(&mixEngine->ctlInQueueMutex);
	}
	if(vuRecord)
		free(vuRecord);

	return NULL;
}
