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

#include "session.h"
#include "data.h"
#include "tasks.h"
#include "database.h"
#include "automate.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <glob.h>
#include <pwd.h>
#include <signal.h>
#include <pwd.h>
#include <ctype.h>
#include <sched.h>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

short listenSocket;
pthread_mutex_t sMutex;
ctl_session *sessionList;
pthread_t listen_thread;
unsigned int sessionListSize;
const char *constPrompt = "ars>";

// local function prototypes
unsigned char handle_lastuid(ctl_session *session);
unsigned char handle_lastaid(ctl_session *session);
unsigned char handle_help(ctl_session *session);
unsigned char handle_clients(ctl_session *session);
unsigned char handle_echo(ctl_session *session);
unsigned char handle_config(ctl_session *session);
unsigned char handle_info(ctl_session *session);
unsigned char handle_attach(ctl_session *session);
unsigned char handle_external(ctl_session *session);
unsigned char handle_tbon(ctl_session *session);
unsigned char handle_tboff(ctl_session *session);
unsigned char handle_play(ctl_session *session);
unsigned char handle_stop(ctl_session *session);
unsigned char handle_pstat(ctl_session *session);
unsigned char handle_meters(ctl_session *session);
unsigned char handle_fade(ctl_session *session);
unsigned char handle_vol(ctl_session *session);
unsigned char handle_sender(ctl_session *session);
unsigned char handle_next(ctl_session *session);
unsigned char handle_bus(ctl_session *session);
unsigned char handle_showbus(ctl_session *session);
unsigned char handle_showmutes(ctl_session *session);
unsigned char handle_mutes(ctl_session *session);
unsigned char handle_mmbus(ctl_session *session);
unsigned char handle_mmvol(ctl_session *session);
unsigned char handle_bal(ctl_session *session);
unsigned char handle_pos(ctl_session *session);	
unsigned char handle_unload(ctl_session *session);
unsigned char handle_load(ctl_session *session);
unsigned char handle_notify(ctl_session *session);
unsigned char handle_vuon(ctl_session *session);
unsigned char handle_vuoff(ctl_session *session);
unsigned char handle_settings(ctl_session *session);
unsigned char handle_get(ctl_session *session);
unsigned char handle_set(ctl_session *session);
unsigned char handle_saveset(ctl_session *session);
unsigned char handle_metalist(ctl_session *session);
unsigned char handle_dumpmeta(ctl_session *session);
unsigned char handle_getmeta(ctl_session *session);
unsigned char handle_setmeta(ctl_session *session);
unsigned char handle_delmeta(ctl_session *session);
unsigned char handle_srcports(ctl_session *session);
unsigned char handle_dstports(ctl_session *session);
unsigned char handle_rotatelog(ctl_session *session);
unsigned char handle_savein(ctl_session *session);
unsigned char handle_dumpin(ctl_session *session);
unsigned char handle_getin(ctl_session *session);
unsigned char handle_setin(ctl_session *session);
unsigned char handle_delin(ctl_session *session);
unsigned char handle_saveout(ctl_session *session);
unsigned char handle_dumpout(ctl_session *session);
unsigned char handle_outvol(ctl_session *session);
unsigned char handle_outbus(ctl_session *session);
unsigned char handle_setdly(ctl_session *session);
unsigned char handle_getdly(ctl_session *session);
unsigned char handle_dump(void);
unsigned char handle_getout(ctl_session *session);
unsigned char handle_setout(ctl_session *session);
unsigned char handle_delout(ctl_session *session);
unsigned char handle_jconlist(ctl_session *session);
unsigned char handle_savejcons(ctl_session *session);
unsigned char handle_jackconn(ctl_session *session);
unsigned char handle_jackdisc(ctl_session *session);
unsigned char handle_setmm(ctl_session *session);
unsigned char handle_getmm(ctl_session *session);
unsigned char handle_execute(ctl_session *session);
unsigned char handle_deltask(ctl_session *session);
unsigned char handle_task(ctl_session *session);
unsigned char handle_tasks(ctl_session *session);
unsigned char handle_urlmeta(ctl_session *session);
unsigned char handle_dblist(ctl_session *session);
unsigned char handle_cue(ctl_session *session);
unsigned char handle_playnow(ctl_session *session);
unsigned char handle_logmeta(ctl_session *session);
unsigned char handle_stat(ctl_session *session);

unsigned char handle_list(ctl_session *session);
unsigned char handle_delete(ctl_session *session);
unsigned char handle_move(ctl_session *session);
unsigned char handle_add(ctl_session *session);
unsigned char handle_uadd(ctl_session *session);
unsigned char handle_split(ctl_session *session);
unsigned char handle_expand(ctl_session *session);
unsigned char handle_getuid(ctl_session *session);
unsigned char handle_inuid(ctl_session *session);
unsigned char handle_waitseg(ctl_session *session);
unsigned char handle_segnow(ctl_session *session);
unsigned char handle_segall(ctl_session *session);
unsigned char handle_fadeprior(ctl_session *session);

unsigned char handle_modbuspoll(ctl_session *session);
unsigned char handle_coilset(ctl_session *session, unsigned char val);

unsigned char handle_rtemplates(ctl_session *session);
unsigned char handle_rstat(ctl_session *session);
unsigned char handle_startrec(ctl_session *session);
unsigned char handle_stoprec(ctl_session *session);
unsigned char handle_recgain(ctl_session *session);
unsigned char handle_lockrec(ctl_session *session);
unsigned char handle_unlockrec(ctl_session *session);
unsigned char handle_closerec(ctl_session *session);	
unsigned char handle_newrec(ctl_session *session);	
unsigned char handle_initrec(ctl_session *session);	
unsigned char handle_jsonpost(ctl_session *session);	
      
uint32_t getSenderID(void){
	uint32_t sender;
	pthread_t thisThread;
	int i;
	
	sender = 0;
	thisThread = pthread_self();
	for(i=0; i<sessionListSize;i++){
		if(pthread_equal(sessionList[i].sessionThread, thisThread))
			break;
	}
	if(i < sessionListSize)
		sender = sessionList[i].sender;
	/* convert to network byte order */
	return htonl(sender);
}

unsigned char getPlayerUID(uint32_t *resolve){
	inChannel *instance;
	uint32_t uid;

	if(checkPnumber(*resolve)){
		instance = &mixEngine->ins[*resolve];
		if(instance->status){
			if(uid = instance->UID){
				*resolve = uid;
				return 1;
			}
		}
	}
	return 0;
}
int noticeSend(const char *buf, int tx_length, unsigned char isVU){
	int i, count;
	ctl_session *recPtr;

	count = 0;
	// send to all registered for noticies
	for(i=0; i<sessionListSize;i++){
		recPtr = &sessionList[i];
		if(recPtr->cs){
			if((!isVU && recPtr->use_tcp) || (isVU && recPtr->notify_meters))
				// set the non-block flag, so the send is skipped if there isn't room in the send buffer.
				count = my_send(recPtr, buf, tx_length, 0, MSG_DONTWAIT);
		}
	}
	return count;
}

int my_send(ctl_session *session, const char *buf, int tx_length, unsigned char silent, int flags){
	int count = 0;

	if(!silent){
		if(session->cs == 0){
			count = fprintf(stdout, "%s", buf);
			fflush(stdout);
			return count;
		}
		else if(session->cs > 0){
			count = send(session->cs, buf, tx_length, flags);
			return count;
		}
	}
	return 0;
}

int my_recv(ctl_session *session, char *buf, int buf_length){
	if(session->cs > 0){
		return recv(session->cs, buf, buf_length, 0);
	}
	return -1;
}

unsigned char processCommand(ctl_session *session, char *command, unsigned char *passResult){
	char buf[256]; /* send data buffer */
	int tx_length;
	unsigned char result;
	char *arg;
	int i;

	//remove LF and CR at end of command
	strtok_r(command, "\r", &session->save_pointer);
	strtok_r(command, "\n", &session->save_pointer);
	arg = strtok_r(command, " ", &session->save_pointer);
	
	result = rError;
	session->errMSG = "";
	if((arg == NULL) || !strlen(arg))
		goto finish;
	session->errMSG = "Huh?\n";
	// Check the arguments
	if(!strcmp(arg, "close")){
		// first parameter, connection number is in save_pointer
		i = atoi(session->save_pointer) - 1;
		// cancel the specified TCP thread
		if((i >= 0) && (i < sessionListSize)){
			pthread_mutex_lock(&sMutex);
			if(sessionList[i].cs > 0){
				shutdown(sessionList[i].cs, SHUT_RDWR); 
				close(sessionList[i].cs);
				/* just incase the thread has become cancelable */
				pthread_cancel(sessionList[i].sessionThread);
			}
			pthread_mutex_unlock(&sMutex);
			result = rOK;
		}else{
			session->errMSG = "bad client number.\n";
			result = rError;          
		}
		goto finish;
	}

	if(!strcmp(arg, "lastuid")){
		result = handle_lastuid(session);
		goto finish;
	}
	if(!strcmp(arg, "lastaid")) {
		result = handle_lastaid(session);
		goto finish;
	}
	if(!strcmp(arg, "help")) {
		result = handle_help(session);
		goto finish;
	}
	if(!strcmp(arg, "exit"))
		return 1;
	if(!strcmp(arg, "shutdown")){
		quit = 1;
		result = rOK;
	}
	if(!strcmp(arg, "restart")){
		quit = 1;
		restart = 1;
		result = rOK;
	}
	if(!strcmp(arg, "clients")){
		result = handle_clients(session);
		goto finish;
	}
	if(!strcmp(arg, "echo")){ 
		result = handle_echo(session);
		goto finish;
	}
	if(!strcmp(arg, "config")){ 
		result = handle_config(session);
		goto finish;
	}
	if(!strcmp(arg, "info")){
		result = handle_info(session);
		goto finish;
	}
	if(!strcmp(arg, "attach")) {
		result = handle_attach(session);
		goto finish;
	}
	if(!strcmp(arg, "external")) {
		result = handle_external(session);
		goto finish;
	}
	if (!strcmp(arg, "tbon")) {
		result = handle_tbon(session);
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "tboff")) {
		result = handle_tboff(session);
		live_event = time(NULL);
		goto finish;
	}
	if(!strcmp(arg, "play")) {
		result = handle_play(session);
		live_event = time(NULL);
		goto finish;
	}
	if(!strcmp(arg, "stop")) {
		result = handle_stop(session);
		live_event = time(NULL);
		goto finish;
	}
	if(!strcmp(arg, "pstat")) {
		result = handle_pstat(session);
		goto finish;
	}
	if(!strcmp(arg, "meters")) {
		result = handle_meters(session);
		goto finish;
	}
	if(!strcmp(arg, "fade")) {
		result = handle_fade(session);
		goto finish;
	}
	if(!strcmp(arg, "vol")) {
		result = handle_vol(session);
		live_event = time(NULL);
		goto finish;
	}
	if(!strcmp(arg, "sender")) {
		result = handle_sender(session);
		goto finish;
	}
	if(!strcmp(arg, "next")) {
		result = handle_next(session);
		goto finish;
	}   
	if(!strcmp(arg, "bus")) {
		result = handle_bus(session);
		live_event = time(NULL);
		goto finish;
	}
		if(!strcmp(arg, "showbus")) {
		result = handle_showbus(session);
		goto finish;
	}
	if(!strcmp(arg, "mutes")) {
		result = handle_mutes(session);
		live_event = time(NULL);
		goto finish;
	}
	if(!strcmp(arg, "mmbus")) {
		result = handle_mmbus(session);
		live_event = time(NULL);
		goto finish;
	}
	if(!strcmp(arg, "mmvol")) {
		result = handle_mmvol(session);
		live_event = time(NULL);
		goto finish;
	}
	if(!strcmp(arg, "bal")) {
		result = handle_bal(session);
		live_event = time(NULL);
		goto finish;
	}
	if(!strcmp(arg, "showmutes")){
		result = handle_showmutes(session);
		goto finish;
	}
    if(!strcmp(arg, "pos")) {
		result = handle_pos(session);
		goto finish;
	}   
	if (!strcmp(arg, "unload")) {
		result = handle_unload(session);
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "load")) {
		result = handle_load(session);
		live_event = time(NULL);
		goto finish;
	} 
	if (!strcmp(arg, "notify")) {
		result = handle_notify(session);
		goto finish;
	}
	if (!strcmp(arg, "vuon")) {
		result = handle_vuon(session);
		goto finish;
	}
	if (!strcmp(arg, "vuoff")) {
		result = handle_vuoff(session);
		goto finish;
    }
	if (!strcmp(arg, "settings")) {
		result = handle_settings(session);
		goto finish;
	}
	if (!strcmp(arg, "get")) {
		result = handle_get(session);
		goto finish;
	}
	if (!strcmp(arg, "set")) {
		result = handle_set(session);
		goto finish;
	}
	if (!strcmp(arg, "saveset")) {
		result = handle_saveset(session);
		goto finish;
	}
	if (!strcmp(arg, "metalist")) {
		result = handle_metalist(session);
		goto finish;
	}
	if (!strcmp(arg, "dumpmeta")) {
		result = handle_dumpmeta(session);
		goto finish;
	}
	if (!strcmp(arg, "getmeta")) {
		result = handle_getmeta(session);
		goto finish;
	}
	if (!strcmp(arg, "setmeta")) {
		result = handle_setmeta(session);
		goto finish;
	}
	if (!strcmp(arg, "delmeta")) {
		result = handle_delmeta(session);
		goto finish;
	} 
	if (!strcmp(arg, "srcports")) {
		result = handle_srcports(session);
		goto finish;
	}    
	if (!strcmp(arg, "dstports")) {
		result = handle_dstports(session);
		goto finish;
	}
	if (!strcmp(arg, "logrotate")) {
		result = handle_rotatelog(session);
		goto finish;
	}
	if (!strcmp(arg, "savein")) {
		result = handle_savein(session);
		goto finish;
	}
	if (!strcmp(arg, "dumpin")) {
		result = handle_dumpin(session);
		goto finish;
	}
	if (!strcmp(arg, "getin")) {
		result = handle_getin(session);
		goto finish;
	}
	if (!strcmp(arg, "setin")) {
		result = handle_setin(session);
		goto finish;
	}
	if (!strcmp(arg, "delin")) {
		result = handle_delin(session);
		goto finish;
	}
	if (!strcmp(arg, "setout")) {
		result = handle_setout(session);
		goto finish;
	}
	if (!strcmp(arg, "saveout")) {
		result = handle_saveout(session);
		goto finish;
	}
	if (!strcmp(arg, "dumpout")) {
		result = handle_dumpout(session);
		goto finish;
	}
	if (!strcmp(arg, "outvol")) {
		result = handle_outvol(session);
		goto finish;
	}
	if (!strcmp(arg, "outbus")) {
		result = handle_outbus(session);
		goto finish;
	}
	if (!strcmp(arg, "setdly")) {
		result = handle_setdly(session);
		goto finish;
	}
	if (!strcmp(arg, "getdly")) {
		result = handle_getdly(session);
		goto finish;
	}	
	if (!strcmp(arg, "dump")) {
		result = handle_dump();
		goto finish;
	}
	if (!strcmp(arg, "delout")) {
		result = handle_delout(session);
		goto finish;
	}
	if (!strcmp(arg, "getout")) {
		result = handle_getout(session);
		goto finish;
	}
	if (!strcmp(arg, "jconlist")) {
		result = handle_jconlist(session);
		goto finish;
	}  
	if (!strcmp(arg, "savejcons")) {
		result = handle_savejcons(session);
		goto finish;
	} 
	if (!strcmp(arg, "jackconn")) {
		result = handle_jackconn(session);
		goto finish;
	}   
	if (!strcmp(arg, "jackdisc")) {
		result = handle_jackdisc(session);
		goto finish;
	}
	if (!strcmp(arg, "setmm")) {
		result = handle_setmm(session);
		goto finish;
	}
	if (!strcmp(arg, "getmm")) {
		result = handle_getmm(session);
		goto finish;
	}  
	if (!strcmp(arg, "execute")) {
		result = handle_execute(session);
		goto finish;
	}
	if (!strcmp(arg, "deltask")) {
		result = handle_deltask(session);
		goto finish;
	}
	if (!strcmp(arg, "task")) {
		result = handle_task(session);
		goto finish;
	}
	if (!strcmp(arg, "tasks")) {
		result = handle_tasks(session);
		goto finish;
	}
	if (!strcmp(arg, "urlmeta")){
		result = handle_urlmeta(session);
		goto finish;
	}
	if (!strcmp(arg, "dblist")) {
		result = handle_dblist(session);
		goto finish;
	}	
	if (!strcmp(arg, "dbsync")) {
		dbFileSync(session, session->silent);
		result = rNone;
		goto finish;
	}	
	if (!strcmp(arg, "dbinit")) {
		if(db_initialize(NULL))
			result = rOK;
		else{
			session->errMSG = "Problem initializing the database.  See log file for details.\n";
			result = rError;
		}
		goto finish;
	}
	if(!strcmp(arg, "dbfilesearch")) {
		uint32_t pace;
		char *tmp;
		
		pace = GetMetaInt(0, "db_file_search_pace", NULL);
		if(pace < 1)
			pace = 250;	// 250 mS delay between file traversing by default
		dbFileSearch(session, session->silent, session->save_pointer, pace);
		result = rNone;
		goto finish;
	}
	if (!strcmp(arg, "cue")) {
		result = handle_cue(session);
		live_event = time(NULL);
		goto finish;
	}    
	if (!strcmp(arg, "playnow")) {
		result = handle_playnow(session);
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "logmeta")) {
		result = handle_logmeta(session);
		goto finish;
	}
	if (!strcmp(arg, "logsync")) {
		if(time(NULL) > logChangeTime)
			logChangeTime = time(NULL);
		else
			logChangeTime = logChangeTime + 1;
		// send out notifications
		notifyData	data;
		data.reference = 0;
		data.senderID = 0;
		data.value.iVal = 0;
		notifyMakeEntry(nType_status, &data, sizeof(data));
		result = rOK;
		goto finish;
	}
	if(!strcmp(arg, "stat")) {
		result = handle_stat(session);
		goto finish;
	}
	if (!strcmp(arg, "list")) {
		result = handle_list(session);
		goto finish;
	}
	if (!strcmp(arg, "delete")) {
		result = handle_delete(session);
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "move")) {
		result = handle_move(session);
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "add")) {
		result = handle_add(session);
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "uadd")) {
		result = handle_uadd(session);
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "split")) {
		result = handle_split(session);
		goto finish;
	}
	if (!strcmp(arg, "autoon")) {   
		autoState = auto_unatt;
		// send out notifications
		serverLogMakeEntry("[automation] -:Switched to auto (user action)");

		notifyData	data;
		data.reference = 0;
		data.senderID = getSenderID();
		data.value.iVal = 0;
		notifyMakeEntry(nType_status, &data, sizeof(data));
		live_event = time(NULL);
		result = rOK;
		goto finish;
	}
	if (!strcmp(arg, "autooff")) {   
		autoState = auto_off;
		// send out notifications
		serverLogMakeEntry("[automation] -:Switched to off (user action)");

		notifyData	data;
		data.reference = 0;
		data.senderID = getSenderID();
		data.value.iVal = 0;
		notifyMakeEntry(nType_status, &data, sizeof(data));
		live_event = time(NULL);
		result = rOK;
		goto finish;
	}
	if (!strcmp(arg, "autolive")) {   
		autoState = auto_live;
		// send out notifications
		serverLogMakeEntry("[automation] -:Switched to live (user action)");

		notifyData	data;
		data.reference = 0;
		data.senderID = getSenderID();
		data.value.iVal = 0;
		notifyMakeEntry(nType_status, &data, sizeof(data));
		result = rOK;
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "run")) {
		plRunning = 1;
		wakeQueManager(); // force the playist manager to process the playlist - get things going
		// send out notifications
		notifyData	data;
		data.reference = 0;
		data.senderID = getSenderID();
		data.value.iVal = 0;
		notifyMakeEntry(nType_status, &data, sizeof(data));
		result = rOK;
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "halt")) {   
		plRunning = 0;
		wakeQueManager(); // force the playist manager to process the playlist - clean up
		notifyData	data;
		data.reference = 0;
		data.senderID = getSenderID();
		data.value.iVal = 0;
		notifyMakeEntry(nType_status, &data, sizeof(data));
		result = rOK;
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "inuid")) {
		result = handle_inuid(session);
		goto finish;
	}
	if (!strcmp(arg, "getuid")) {
		result = handle_getuid(session);
		goto finish;
	} 
	if (!strcmp(arg, "waitseg")) {
		result = handle_waitseg(session);
		goto finish;
	}
	if (!strcmp(arg, "segnow")) {
		result = handle_segnow(session);
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "segall")) {
		result = handle_segall(session);
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "fadeprior")) {
		result = handle_fadeprior(session);
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "expand")) {
		result = handle_expand(session);
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "modbuspoll")) {
		result = handle_modbuspoll(session);
		goto finish;
	}
	if (!strcmp(arg, "modbusset")) {
		result = handle_coilset(session, 1);
		goto finish;
	}
	if (!strcmp(arg, "modbusclear")) {
		result = handle_coilset(session, 0);
		goto finish;
	}
	if (!strcmp(arg, "rtemplates")) {
		result = handle_rtemplates(session);
		goto finish;
	}
	if (!strcmp(arg, "rstat")) {
		result = handle_rstat(session);
		goto finish;
	}
	if (!strcmp(arg, "startrec")) {   
		result = handle_startrec(session);
		goto finish;
	}
	if (!strcmp(arg, "stoprec")) {   
		result = handle_stoprec(session);
		goto finish;
	}
	if (!strcmp(arg, "recgain")) {   
		result = handle_recgain(session);
		goto finish;
	}
	if (!strcmp(arg, "lockrec")) {   
		result = handle_lockrec(session);
		goto finish;
	}
	if (!strcmp(arg, "unlockrec")) {   
		result = handle_unlockrec(session);
		goto finish;
	}
	if (!strcmp(arg, "closerec")) {   
		result = handle_closerec(session);
		goto finish;
	}	
	if (!strcmp(arg, "newrec")) {   
		result = handle_newrec(session);
		goto finish;
	}
	if (!strcmp(arg, "initrec")) { 
		result = handle_initrec(session);
		if((result == rError) && session->silent){
			// silent mode with an error... delete the recorder instance
			char *tempPtr = session->save_pointer;
			result = handle_closerec(session);
			session->save_pointer = tempPtr;
		}
		goto finish;
	}
	if (!strcmp(arg, "jsonpost")) {
		result = handle_jsonpost(session);
		goto finish;
	}
/*
	if (!strcmp(arg, "mutex")){
		result = handle_mutex(session);
		goto finish;
	}
	if (!strcmp(arg, "getdm")) {
		result = handle_getdm(session);
		goto finish;
	}
	if (!strcmp(arg, "setdm")) {
		result = handle_setdm(session);
		goto finish;
	}
	if (!strcmp(arg, "listdm")) {
		result = handle_listdm(session);
		goto finish;
	}

	if (!strcmp(arg, "iaxstream")) {
		result = handle_iaxstream(session);
		goto finish;
	}	
	if (!strcmp(arg, "back")) {
		result = handle_back(session);
		live_event = time(NULL);
		goto finish;
	}
	if (!strcmp(arg, "ahead")) {
		result = handle_ahead(session);
		live_event = time(NULL);
		goto finish;
	}

	if (!strcmp(arg, "setstat")) {
		result = handle_setstat(session);
		goto finish;
	}
	if (!strcmp(arg, "feed")) {
		result = handle_feed(session);
		goto finish;
	}
	
	if (!strcmp(arg, "fxcue")) {   
		result = handle_fxcue(session);
		goto finish;
	}

	if(!strcmp(arg, "iaxinit")) { 
		unsigned char err;
		err = iaxp_initialize(); 
		if(err){
			if(err == 1)
				session->errMSG = "Call in progress: couldn't re-initialize iax system.\n";
			else
				session->errMSG = "Failed to initialize iax system.\n";
			result = rError;
		}else
			result = rOK;
		goto finish;
	}
	if (!strcmp(arg, "fxlist")) {
		result = handle_fxlist(session);
		goto finish;
	}
	if (!strcmp(arg, "fxslots")) {
		result = handle_fxslots(session);
		goto finish;
	}
	if (!strcmp(arg, "fxinsert")) {
		result = handle_fxinsert(session);
		goto finish;
	}
	if (!strcmp(arg, "fxparam")) {
		result = handle_fxparam(session);
		goto finish;
	}
	if (!strcmp(arg, "fxbypass")) {
		result = handle_fxbypass(session);
		goto finish;
	}
	if (!strcmp(arg, "fxvalstr")) {
		result = handle_fxvalstr(session);
		goto finish;
	}
	if (!strcmp(arg, "fxsave")) {
		result = handle_fxsave(session);
		goto finish;
	}
	if (!strcmp(arg, "fxpreset")) {
		result = handle_fxpreset(session);
		goto finish;
	}
	if (!strcmp(arg, "fxwatch")) {
		result = handle_fxwatch(session);
		goto finish;
	}
    if (!strcmp(arg, "fxls")) {
		result = lsFXConfigDir(session);
		goto finish;
	}

	if (!strcmp(arg, "debug")){
        debug = session->cs;
		sleep(1);
		debug = 0;
		result = rOK;
	}

	if (!strcmp(arg, "getcodecs")){
		result = handle_getcodecs(session);
		goto finish;
	}
*/

finish:
	if(result == rError){
		if(arg && (strlen(arg) > 1)){
			tx_length = snprintf(buf, sizeof buf, "%s", session->errMSG);
			my_send(session, buf, tx_length, session->silent, 0);
		}
	}
	if(result == rOK){
		tx_length = snprintf(buf, sizeof buf, "OK\n");
		my_send(session, buf, tx_length, session->silent, 0);
	}
	if(passResult)
		*passResult = result;
	return 0;
}

unsigned char loadConfiguration(ctl_session *session, char *file_path){
	FILE *fp;
	char *result, line[4096];
	
	if((fp = fopen(file_path, "r")) == NULL)
		return 0;
	result = fgets(line, sizeof(line), fp);
	while(result != NULL){
		if((line[0] != ';') && (line[0] != '-')){ // not a commented or pre-config line (';' or '-' as first char in line)
			processCommand(session, line, NULL);
		}
		result = fgets(line, sizeof line, fp);
	}
	fclose(fp);
	return 1;
}

void* sessionThread(ctl_session *session){
	char command[4096]; /* receive data buffer */
	char block[1024]; /* receive data buffer */
	char *fragment, *save_pointer;
	int rx_length, tx_length;
	int i, old;
	ctl_session *recPtr;
	
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	
	// display version
	tx_length = snprintf(command, sizeof command, "AudioRack Server, version %s\n", versionStr);

	if(my_send(session, command, tx_length, 0, 0) < 0) goto finish;
	tx_length = snprintf(command, sizeof command, "Copyright (C) %s\n\n", versionCR);
	if(my_send(session, command, tx_length, 0, 0) < 0)goto finish;
	tx_length = snprintf(command, sizeof command, "AudioRack Server comes with ABSOLUTELY NO WARRANTY; for details\n");
	if(my_send(session, command, tx_length, 0, 0) < 0) goto finish;
	tx_length = snprintf(command, sizeof command, "type `info'.  This is free software, and you are welcome\n");
	if(my_send(session, command, tx_length, 0, 0) < 0) goto finish;
	tx_length = snprintf(command, sizeof command, "to redistribute it under certain conditions; See the\n");
	if(my_send(session, command, tx_length, 0, 0) < 0) goto finish;
	tx_length = snprintf(command, sizeof command, "GNU General Public License included with this program for details.\n\n");
	if(my_send(session, command, tx_length, 0, 0) < 0) goto finish;
	tx_length = snprintf(command, sizeof command, "==================================================================\n");
	if(my_send(session, command, tx_length, 0, 0) < 0) goto finish;

	// send prompt
	tx_length = strlen(constPrompt);
	if(my_send(session, constPrompt, tx_length, 0, 0) < 0) goto finish;
	*command = 0;
	
	// wait for a client command to arrive
	rx_length = my_recv(session, block, sizeof(block) - 1);
	if(rx_length >= 0)
		block[rx_length] = 0; // null at end of segment to make it a c-string
	else 
		block[0] = 0;
	
	session->silent = 0;
	while(rx_length > 0){
		// "\n" is our command delimitor, but we need to handle \r too.
		save_pointer = block;
		while(fragment = strpbrk(save_pointer, "\n\r")){
			// replace found /n or /r with null string termination
			*fragment = 0;
			strncat(command, save_pointer, sizeof(command) - (strlen(command) + 1));
			char nxt = *(fragment+1);
			// It's possible that a /r/n set will be broken across a recvd block, and
			// then we wont catch is as a pair.  Oh well.  Not likely, and would
			// only result in a double prompt being sent the client.
			if((nxt == '\n') || (nxt == '\r'))
				// next char is also a \r or \n... move past it too
				save_pointer = fragment+2;
			else
				// next char is not a \r or \n... just move past the first one
				save_pointer = fragment+1;

			if(strlen(command)){
				if(processCommand(session, command, NULL))
					goto finish;
			}else{
				// send \n
				if(my_send(session, "\n", 1, 0, 0) < 0) 
					goto finish;
			}
			// send prompt
			tx_length = strlen(constPrompt);
			if(my_send(session, constPrompt, tx_length, 0, 0) < 0) 
				goto finish;
			*command = 0;
		}
		// no delimitor left in the string... save whats left, the delimitor my show up in the next round
		if(strlen(save_pointer))
			strncat(command, save_pointer, sizeof(command) - (strlen(command) + 1));
		// wait for a client command to arrive
		rx_length = my_recv(session, block, sizeof(block) - 1);
		if(rx_length >= 0)
			block[rx_length] = 0; // null at end of segment to make it a c-string
		else
			block[0] = 0;
	}
finish:
	pthread_mutex_lock(&sMutex);
	if(session->cs > 0){
		shutdown(session->cs, SHUT_RDWR);
		close(session->cs); /* close client socket connection */
	}
	session->cs = 0;
	session->sessionThread = 0;
	pthread_mutex_unlock(&sMutex);

	if(GetMetaInt(0,"sys_autoexit", NULL)){
		// check for auto exit on loss of last control connection
		pthread_mutex_lock(&sMutex);
		recPtr = &sessionList[i];
		for(i=0; i<sessionListSize; i++){
				if(recPtr->cs)
					break;
				recPtr++;
		}
		pthread_mutex_unlock(&sMutex);
		if(i == sessionListSize){
			restart = 0;
			quit = 1;
		}
	}
	return NULL;
}

void* TCPListener(void *refCon){
	ctl_session *recPtr;
	socklen_t namelen; /* length of client name */
	int ns; /* client socket */
	int i;
	struct sockaddr_in6 client; /* client address information */

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	/* when listenSocket is closed, ns will be negative (error),
	 * and function loop will exit */
	do{
		namelen = sizeof(client); 
		ns = accept(listenSocket, (struct sockaddr *)&client, &namelen); /* wait for connection request */
		if(ns > 0) {
			// Set new socket options -- keep-alive for broken connection detection
			int trueval = 1;
			setsockopt(ns, SOL_SOCKET, SO_KEEPALIVE, &trueval, sizeof(trueval)); // if this fails, not much can be done
			pthread_mutex_lock(&sMutex);
			for(i=0; i<sessionListSize;i++){
				recPtr = &sessionList[i];
				if(recPtr->cs == 0){
					recPtr->cs = ns;
					recPtr->client = client;
					recPtr->use_tcp = 0;
					recPtr->notify_meters = 0;
					recPtr->sender = 0;
					recPtr->lastUID = 0;
					recPtr->lastPlayer = -1;
					recPtr->lastAID = 0;
					pthread_create(&recPtr->sessionThread, NULL, (void*(*)(void*))&sessionThread, recPtr);
					pthread_detach(recPtr->sessionThread);
					break;
				}
			}
			pthread_mutex_unlock(&sMutex);
			
			if(i == sessionListSize){
				serverLogMakeEntry("[session] TCPListener-new connections: requests exceed max number of allowed connections");
				send( ns, "maximum number of connection exceeded. Try again later.\n", 57, 0);
				close(ns);
			}
		}
	}while(ns > 0);
	return NULL;
}

char *initSessions(unsigned int maxSessions, short *tcpPort){
	short s;
	struct sockaddr_in6 server; /* server address information */
	socklen_t namelen; /* length of client name */
	int trueVal = 1;
	
	/* create a session list mutex */
	pthread_mutex_init(&sMutex, NULL);  

	listen_thread = 0;
	listenSocket = -1;
	sessionList = (ctl_session *)calloc(maxSessions, sizeof(ctl_session));
	if(sessionList == NULL)
		return "failed to allocate memory for session list.";
	sessionListSize = maxSessions;
	
	s = socket(AF_INET6, SOCK_STREAM, 0); /* create stream socket using TCP */
	if(s == -1) {
		free(sessionList);
		return "TCP listen socket creation failed";
	}

	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &trueVal, sizeof(trueVal));
	memset(&server, 0, sizeof(server));
	server.sin6_family = AF_INET6; 
	server.sin6_port = htons(*tcpPort); 
	server.sin6_addr = in6addr_any;

	if(bind(s, (struct sockaddr *) &server, sizeof( server )) < 0 ) { /* bind server address to socket */
		free(sessionList);
		close(s);
		return  "bind() Error binding server to port";
	}
	
	/* find out what port was assigned */
	namelen = sizeof(server);
	if(getsockname(s, (struct sockaddr*) &server, &namelen) < 0 ) {
		free(sessionList);
		close(s);
		return "getsockname() failed to get port number";
	}
	*tcpPort = ntohs(server.sin6_port);

	if(listen(s, 1) != 0 ) { /* listen for a connection */
		free(sessionList);
		close(s);
		return "listen() failed";
	}

	listenSocket = s;

	/* create a listener thread to listen for TCP connection requests 
	 * and spaun off additional threads for those sessions */
	pthread_create(&listen_thread, NULL, &TCPListener, NULL);
	/* sucess! */
	return NULL;
}

void shutdownSessions(void){
	int i;
			
	if(listenSocket > -1){
		/* closing the socket will cause the the listen thread to finish */
		shutdown(listenSocket, SHUT_RDWR);
		close(listenSocket); 
	}			
	if(sessionList){
		for(i=0; i<sessionListSize; i++){
			pthread_mutex_lock(&sMutex);
			if(sessionList[i].cs > 0){
				shutdown(sessionList[i].cs, SHUT_RDWR);
				close(sessionList[i].cs);
			}
			if(sessionList[i].sessionThread){
				/* just incase the thread has become cancelable */
				pthread_cancel(sessionList[i].sessionThread);
				while(sessionList[i].sessionThread){
					pthread_mutex_unlock(&sMutex);
					sched_yield();	// give thread time to end from cancel or socket closure
					pthread_mutex_lock(&sMutex);
				}
			}
			pthread_mutex_unlock(&sMutex);
			
		}			
		free(sessionList);
	}

	if(listen_thread)
		pthread_join(listen_thread, NULL);
			
	pthread_mutex_destroy(&sMutex);
}

unsigned char handle_lastuid(ctl_session *session){
	char *end;
	uint32_t aLong;
	
	// first parameter, UID in hex format
	if(session->save_pointer){
		// hex number
		if(aLong = strtoul(session->save_pointer, &end, 16)){
			session->lastUID = aLong;
			return rOK;
		}
	}
	session->errMSG = "Missing or bad parameter.\n";
	return rError;
}

unsigned char handle_lastaid(ctl_session *session){
	char *end;
	uint32_t aLong;
	
	// first parameter, UID in hex format
	if(session->save_pointer){
		// hex number
		if(aLong = strtoul(session->save_pointer, &end, 16)){
			session->lastAID = aLong;
			return rOK;
		}
	}
	session->errMSG = "Missing or bad parameter.\n";
	return rError;
}

unsigned char handle_help(ctl_session *session){
	FILE *fp;
	char *result, buf[256];

	if((fp = fopen("/opt/audiorack/support/help.txt", "r")) == NULL){
		session->errMSG = "The help file is missing: /opt/audiorack/support/help.txt\n";
		return rError;	
	}
	while(result = fgets(buf, sizeof(buf), fp)){
		while(result = strchr(buf, '\r'))
			// convert CR to LF chars
			*result = '\n';
		my_send(session, buf, strlen(buf), session->silent, 0);
	}
	fclose(fp);
	return rNone;
}

unsigned char handle_clients(ctl_session *session){
	char buf[4096]; /* send data buffer */
	char str[256];
	int tx_length;

	// list the current connected clients
	tx_length = snprintf(buf, sizeof buf - 1, "Connected clients\n");
	my_send(session, buf, tx_length, session->silent, 0);
	pthread_mutex_lock(&sMutex);
	for(int cn=0; cn<sessionListSize; cn++){
		if(sessionList[cn].cs > 0){
			if(inet_ntop(AF_INET6, &sessionList[cn].client.sin6_addr, str, sizeof(str)))
			tx_length = snprintf(buf, sizeof buf - 1, "#%d from %s\n", cn+1, str);
			my_send(session, buf, tx_length, session->silent, 0);
		}
	}
	pthread_mutex_unlock( &sMutex );
	return rNone;
}

unsigned char handle_echo(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;

	if(session->save_pointer){
		// first parameter, message to echo is in save_pointer
		tx_length = snprintf(buf, sizeof buf, "%s\n", session->save_pointer);
		// ignore silent... always echo.
		my_send(session, buf, tx_length, 0, 0);
	}
	return rNone;
}

unsigned char handle_config(ctl_session *session){
	// first parameter, file path is in save_pointer
	if(session->save_pointer != NULL){
		if(!loadConfiguration(session, session->save_pointer)){
			session->errMSG = "Could not open configuartion file for reading.\n";
		}else{
			return rOK;
		}
	}else{
			session->errMSG = "Configuration file path not specified.\n";
	}
	return rError;
}

unsigned char handle_info(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;
	
	if(!session->silent){
		tx_length = snprintf(buf, sizeof buf, "AudioRack Server, version %s\n", versionStr);
		my_send(session, buf, tx_length, session->silent, 0);
		tx_length = snprintf(buf, sizeof buf, "Copyright (C) %s\n\n", versionCR);
		my_send(session, buf, tx_length, session->silent, 0);
		
		
		
		tx_length = snprintf(buf, sizeof buf, "Mixer Settings:\n\tcore sample rate = %u\n"
					"\tmatrix input count = %d x %d\n"
					"\tmatrix bus count = %d x %d\n"
					"\toutput group count = %d x %d\n"
					"\tJACK-Audio name = %s\n\n", 
					mixEngine->mixerSampleRate, mixEngine->inCount, mixEngine->chanCount, 
					mixEngine->busCount, mixEngine->chanCount,
					mixEngine->outCount, mixEngine->chanCount, mixEngine->ourJackName);
		my_send(session, buf, tx_length, session->silent, 0);
		
		tx_length = snprintf(buf, sizeof buf, "============================================================================\n");
		my_send(session, buf, tx_length, session->silent, 0);
		tx_length = snprintf(buf, sizeof buf, "This program is free software; you can redistribute it and/or\n");
		my_send(session, buf, tx_length, session->silent, 0);
		tx_length = snprintf(buf, sizeof buf, "modify it under the terms of the GNU General Public License\n");
		my_send(session, buf, tx_length, session->silent, 0);
		tx_length = snprintf(buf, sizeof buf, "as published by the Free Software Foundation; either version 2\n");
		my_send(session, buf, tx_length, session->silent, 0);
		tx_length = snprintf(buf, sizeof buf, "of the License, or (at your option) any later version.\n\n");
		my_send(session, buf, tx_length, session->silent, 0);
		tx_length = snprintf(buf, sizeof buf, "This program is distributed in the hope that it will be useful,\n");
		my_send(session, buf, tx_length, session->silent, 0);
		tx_length = snprintf(buf, sizeof buf, "but WITHOUT ANY WARRANTY; without even the implied warranty of\n");
		my_send(session, buf, tx_length, session->silent, 0);
		tx_length = snprintf(buf, sizeof buf, "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n");
		my_send(session, buf, tx_length, session->silent, 0);
		tx_length = snprintf(buf, sizeof buf, "GNU General Public License for more details.\n\n");
		my_send(session, buf, tx_length, session->silent, 0);
		tx_length = snprintf(buf, sizeof buf, "You should have received a copy of the GNU General Public License\n");
		my_send(session, buf, tx_length, session->silent, 0);
		tx_length = snprintf(buf, sizeof buf, "with this program; if not, write to the Free Software Foundation, Inc.,\n");
		my_send(session, buf, tx_length, session->silent, 0);
		tx_length = snprintf(buf, sizeof buf, "59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.\n");
		my_send(session, buf, tx_length, session->silent, 0);
		tx_length = snprintf(buf, sizeof buf, "============================================================================\n");
		my_send(session, buf, tx_length, session->silent, 0);
	}
	return rNone;
}

unsigned char handle_attach(ctl_session *session){
	struct execRec{
		char **argv;
		char *str;
		pid_t child;
	} *recPtr;
	
	char *command;
	char *wdir;
	int i, sockpair[2];
	
	// input error checking
	if(session->cs == 0){
		session->errMSG = "This commandcan can not be used on stdin/out connection.\n";
		return rError;
	}
	if(session->save_pointer == NULL){
		session->errMSG = "Shell command to execute is missing.\n";
		return rError;
	}
	command = session->save_pointer;
	i = strlen(command);
	if(i <= 0){
		session->errMSG = "Shell command to execute is missing.\n";
		return rError;
	}
	
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockpair) < 0) {
		session->errMSG = "Failed to create socket pair.\n";
		return rError;
	}
	
	// all is well: allocate record for execution var's
	recPtr = (struct execRec *)malloc(sizeof(struct execRec));
	
	// make copy of c string
	recPtr->str = strdup(command);
	// make array for holding arguments
	i = str_CountFields(command, " ");
	recPtr->argv = (char **)malloc(sizeof(char*) * (i+2));
	//parse string for spaces, replace with nulls, and fill argv array with pointers to each parsed segment
	i = 0;
	recPtr->argv[i] = strtok_r(recPtr->str, " ", &session->save_pointer);
	while(recPtr->argv[i]){
		i++;
		recPtr->argv[i] = strtok_r(NULL, " ", &session->save_pointer);
	}
	
	// execute it;
	if((recPtr->child = fork()) < 0){
		session->errMSG = "process fork failure.\n";
		return rError;
	}else if(recPtr->child == 0){
		// if we are the forked child
		// set working dir if specified or user home directory
		struct passwd pwrec;
		struct passwd* pwPtr;
		char pwbuf[1024];
		if(strlen(wdir_path) == 0){
			// Get our effective user's home directory
			if(getpwuid_r(geteuid(), &pwrec, pwbuf, sizeof(pwbuf), &pwPtr) != 0)
				wdir = pwPtr->pw_dir;
			else
				wdir = "";
		}else
			wdir = wdir_path;
		if(strlen(wdir) > 0)
			chdir(wdir);
		
		// Redirect standard input from socketpair
		if(dup2(sockpair[0], STDIN_FILENO) == STDIN_FILENO){ 
			// Redirect standard output to socketpair
			if(dup2(sockpair[0], STDOUT_FILENO) == STDOUT_FILENO){
				// Redirect standard err to socketpair
				if(dup2(sockpair[0], STDERR_FILENO) == STDERR_FILENO){
					for(i=(getdtablesize()-1); i >= 0; --i){
						if((i != STDERR_FILENO) && (i != STDIN_FILENO) && (i != STDOUT_FILENO))
							close(i); // close all descriptors we are not interested in
					}
					// unblock all signals and set to default handlers
					sigset_t sset;
					sigemptyset(&sset);
					pthread_sigmask(SIG_SETMASK, &sset, NULL);
					// obtain a new process group 
					setsid();
					// and run...	
					execvp(recPtr->str, recPtr->argv);
				}
			}
		}
		exit(0);
		
	}else{
		// parent continues here...
		
		char buff[256];
		fd_set read_fds, exc_fds;
		struct timeval tv;
		int h = 0;
		
		do{
			/* Wait up to one second. */
			tv.tv_sec = 1;
			tv.tv_usec = 0;
			
			FD_ZERO(&read_fds);
			FD_ZERO(&exc_fds);
			FD_SET(sockpair[1], &read_fds);
			if(sockpair[1] > h) h = sockpair[1];
			FD_SET(session->cs, &read_fds);
			FD_SET(session->cs, &exc_fds);
			if(session->cs > h) h = session->cs;
			if(select(h+1, &read_fds, NULL, &exc_fds, &tv) > 0){
				if(FD_ISSET(session->cs, &exc_fds))
					// control socket error (closed?) Kill attached process
					kill(recPtr->child, 9);
				if(FD_ISSET(sockpair[1], &read_fds)) {
					if(i = read(sockpair[1], buff, sizeof(buff)))
						write(session->cs, buff, i);
				}
				if(FD_ISSET(session->cs, &read_fds)){
					if((i = read(session->cs, buff, sizeof(buff))) > 0)
						write(sockpair[1], buff, i);
					else
						// control socket error (closed?) Kill attached process
						kill(recPtr->child, 9);
				}
			}
			if(run == 0)
				kill(recPtr->child, 9);
		}while(waitpid(recPtr->child, NULL, WNOHANG) == 0);
		close(sockpair[0]);
		close(sockpair[1]);
		free(recPtr->str);
		free(recPtr->argv);
		free(recPtr);
		return rOK;
	}
	return rNone;
}

unsigned char handle_external(ctl_session *session){
	struct execRec{
		char **argv;
		pid_t child;
	} *recPtr;
	char *wdir;
	int i, sockpair[2];
	
	if(session->save_pointer == NULL){
		session->errMSG = "Shell command to execute is missing.\n";
		return rError;
	}
	i = strlen(session->save_pointer);
	if(i <= 0){
		session->errMSG = "Shell command to execute is missing.\n";
		return rError;
	}
	
	if(socketpair(AF_UNIX, SOCK_STREAM, 0, sockpair) < 0) {
		session->errMSG = "Failed to create socket pair.\n";
		return rError;
	}
	
	// all is well: allocate record for execution var's
	recPtr = (struct execRec *)malloc(sizeof(struct execRec));
	
	// make array for holding arguments
	i = str_CountFields(session->save_pointer, " ");
	recPtr->argv = (char **)malloc(sizeof(char*) * (i+2));
	//parse string for spaces, replace with nulls, and fill argv array with pointers to each parsed segment
	i = 0;
	recPtr->argv[i] = strtok_r(session->save_pointer, " ", &session->save_pointer);
	while(recPtr->argv[i]){
		i++;
		recPtr->argv[i] = strtok_r(NULL, " ", &session->save_pointer);
	}
	
	// execute it;
	if((recPtr->child = fork()) < 0){
		session->errMSG = "process fork failure.\n";
		return rError;
	}else if(recPtr->child == 0){
		// if we are the forked child
		
		int fd;
		// if we are the forked child
		// set working dir if specified or user home directory
		struct passwd pwrec;
		struct passwd* pwPtr;
		char pwbuf[1024];
		if(strlen(wdir_path) == 0){
			// Get our effective user's home directory
			if(getpwuid_r(geteuid(), &pwrec, pwbuf, sizeof(pwbuf), &pwPtr) != 0)
				wdir = pwPtr->pw_dir;
			else
				wdir = "";
		}else
			wdir = wdir_path;
		if(strlen(wdir) > 0)
			chdir(wdir);
		
		// Redirect standard input from socketpair
		if(dup2(sockpair[0], STDIN_FILENO) == STDIN_FILENO){ 
			// Redirect standard output to socketpair
			if(dup2(sockpair[0], STDOUT_FILENO) == STDOUT_FILENO){
				// Redirect standard err to socketpair
				if(dup2(sockpair[0], STDERR_FILENO) == STDERR_FILENO){
					for(fd=(getdtablesize()-1); fd >= 0; --fd){
						if((fd != STDERR_FILENO) && (fd != STDIN_FILENO) && (fd != STDOUT_FILENO))
							close(fd); // close all descriptors we are not interested in
					}
					// unblock all signals and set to default handlers
					sigset_t sset;
					sigemptyset(&sset);
					pthread_sigmask(SIG_SETMASK, &sset, NULL);
					// obtain a new process group 
					setsid();
					// and run...	
					execvp(recPtr->argv[0], recPtr->argv);
				}
			}
		}
		exit(0);
		
	}else{
		// parent continues here...
		char *fragment, *save_pointer;
		char buff[1024];
		char command[4096];
		struct timeval tv;
		unsigned char was_silent;
		int was_cs;
		
		was_cs = session->cs;
		was_silent = session->silent;
		session->cs = sockpair[1];
		session->silent = 0;
		
		// set up socket timeout for periodic polling of run status and dead child	
		tv.tv_sec = 1;		// seconds
		tv.tv_usec = 0;		// and microseconds
		setsockopt(session->cs, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));	
		
		// send prompt
		i = snprintf(buff, (sizeof buff) - 1, "%s", constPrompt);
		strcat(buff, "\n");
		send(session->cs, buff, i+1, 0);
		
		command[0] = 0;
		while(waitpid(recPtr->child, NULL, WNOHANG) == 0){
			// wait for a client command to arrive or a socket error
			while((i = read(session->cs, buff, sizeof(buff)-1)) > 0){
				// null at end of segment to make it a c-string
				buff[i] = 0;
				// "\n" is our command delimitor
				save_pointer = buff;
				while(fragment = strpbrk(save_pointer, "\n\r")){
					// found end-of-line
					*fragment = 0;
					strncat(command, save_pointer, sizeof(command) - (strlen(command) + 1));
					save_pointer = fragment+1;
					// process command, ignoring return value... disallow quit/shutdown from external programs
					processCommand(session, command, NULL);
					// send prompt
					i = snprintf(command, (sizeof command) - 1, "%s", constPrompt);
					strcat(command, "\n");
					send(session->cs, command, i+1, 0);
					*command = 0;
				}
				// no delimitor left in the string... save whats left, the delimitor my show up in the next round
				if(strlen(save_pointer))
					strncat(command, save_pointer, sizeof(command) - (strlen(command) + 1));
			}
			if((i < 0) && (errno != EAGAIN))
				// control socket error (closed?) Kill attached process
				kill(recPtr->child, 9);
			if(run == 0)
				kill(recPtr->child, 9);
		}
		close(sockpair[0]);
		close(sockpair[1]);
		free(recPtr->argv);
		free(recPtr);
		// restore session setting
		session->cs = was_cs;
		session->silent = was_silent;
		return rOK;
	}
	return rNone;
}

unsigned char handle_play(ctl_session *session){
	char *param;
	uint32_t aInt;
	inChannel *instance;

	// first parameter, player number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			aInt = session->lastPlayer;
		else
			aInt = atoi(param);
		if(!checkPnumber(aInt)){
			session->errMSG = "Bad player number.\n";
			return rError;            
		}
		session->lastPlayer = aInt;
		instance = &mixEngine->ins[aInt];
		instance->requested = instance->requested | change_play;
		return rOK;
	}
	session->errMSG = "Missing parameter player number.\n";
	return rError;
}

unsigned char handle_tbon(ctl_session *session){
	char *param;
	int aInt;
	inChannel *instance;

	// first parameter, talkback number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		aInt = atoi(param);
		if((aInt < 0) || (aInt > 2)){
			session->errMSG = "Bad talkback number.\n";
			return rError;            
		}
		mixEngine->reqTalkBackBits = mixEngine->reqTalkBackBits | (1<<aInt);
		return rOK;
	}
	session->errMSG = "Missing parameter talkback number.\n";
	return rError;
}

unsigned char handle_tboff(ctl_session *session){
	char *param;
	int aInt;
	inChannel *instance;

	// first parameter, talkback number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		aInt = atoi(param);
		if((aInt < 0) || (aInt > 2)){
			session->errMSG = "Bad talkback number.\n";
			return rError;            
		}
		mixEngine->reqTalkBackBits = mixEngine->reqTalkBackBits & ~(1<<aInt);
		return rOK;
	}
	session->errMSG = "Missing parameter talkback number.\n";
	return rError;
}

unsigned char handle_stop(ctl_session *session){
	char *param;
	uint32_t aInt;
	inChannel *instance;

	// first parameter, player number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			aInt = session->lastPlayer;
		else
			aInt = atoi(param);
		if(!checkPnumber(aInt)){
			session->errMSG = "Bad player number.\n";
			return rError;            
		}
		session->lastPlayer = aInt;
		instance = &mixEngine->ins[aInt];
		instance->requested = instance->requested | change_stop;
		return rOK;
	}
	session->errMSG = "Missing parameter player number.\n";
	return rError;
}

unsigned char handle_pstat(ctl_session *session){
	char *tmp;
	char buf[4096]; /* send data buffer */
	int tx_length;
	unsigned int i;
	unsigned int mrev;
	float dur, btime;
	char *type;
	inChannel *instance;

	// player status
	tx_length = snprintf(buf, sizeof buf, "pNum\tstatus\tmeta-UID\tRev\ttype\tvol\tbal\tbus\tpos\tdur\tbuff\tnext\tseg\tfade\n");
	my_send(session, buf, tx_length, session->silent, 0);
	for(i=0;i<mixEngine->inCount;i++){
		instance = &mixEngine->ins[i];
		if(instance->status & (status_standby | status_loading)){
			btime = 0.0;						// bufferTime no longer impemented
			if(instance->UID){
				mrev = (unsigned int)GetMetaRev(instance->UID);
				dur = GetMetaFloat(instance->UID, "Duration", NULL);
				type = GetMetaData(instance->UID, "Type", 0);
			}else{
				mrev = 0;
				type = strdup("unknown");
				dur = 0.0;
			}
			tx_length = snprintf(buf, sizeof buf, "%u\t%u\t%08x\t%u\t%s\t%.3f\t%.2f\t%06x\t%.2f\t%.1f\t%.1f\t%d\t%.2f\t%.2f\n", 
				i, (unsigned int)instance->status, (unsigned int)instance->UID, 
				mrev, type, instance->vol, instance->bal,
				(instance->busses & 0x00ffffff), instance->pos, dur, btime, 
				instance->segNext - 1, instance->posSeg, instance->fadePos);
			free(type);
			my_send(session, buf, tx_length, session->silent, 0);
		}else{
			// empty
			tx_length = snprintf(buf, sizeof buf, "%u\t%d\n", (unsigned int)i, 0);
			my_send(session, buf, tx_length, session->silent, 0);
		}
	}

	return rNone;
}

unsigned char handle_meters(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;
	unsigned int i, c;
	unsigned int max;
	unsigned int chanWidth;
	inChannel *inPtr;
	vuData *meterPtr;

	// update the meter array values
	chanWidth = mixEngine->chanCount;
	max = chanWidth * mixEngine->busCount;
	tx_length = snprintf(buf, sizeof buf, "bus\tchan\tavr\tpeak\n");
	my_send(session, buf, tx_length, session->silent, 0);
	for(i=0; i<max; i++){
		tx_length = snprintf(buf, sizeof buf, "%u\t%u\t%.1f\t%.1f\n", 
				i / chanWidth, i % chanWidth,
				ftodb(mixEngine->mixbuses->VUmeters[i].avr), 
				ftodb(mixEngine->mixbuses->VUmeters[i].peak));
		my_send(session, buf, tx_length, session->silent, 0);
	}

	max = mixEngine->inCount;
	tx_length = snprintf(buf, sizeof buf, "\nin\tchan\tavr\tpeak\n");
	my_send(session, buf, tx_length, session->silent, 0);
	
	inPtr = &mixEngine->ins[i];
	for(i=0; i<max; i++){
		inPtr = &mixEngine->ins[i];
		for(c=0; c<chanWidth; c++){
			tx_length = snprintf(buf, sizeof buf, "%u\t%u\t%.1f\t%.1f\n", 
				i, c,
				ftodb(inPtr->VUmeters[c].avr), 
				ftodb(inPtr->VUmeters[c].peak));
			my_send(session, buf, tx_length, session->silent, 0);
		}
	}
	return rNone;
}

unsigned char handle_fade(ctl_session *session){
	char *param;
	uint32_t aInt;
	float aFloat;
	inChannel *instance;

	// first parameter, player number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			aInt = session->lastPlayer;
		else
			aInt = atoi(param);
		if(!checkPnumber(aInt)){
			session->errMSG = "Bad player number.\n";
			return rError;
		}
		// second parameter, time to start fade
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			aFloat = atof(param);
			session->lastPlayer = aInt;
			instance = &mixEngine->ins[aInt];
			if(instance->status & status_standby){
				instance->fadePos = aFloat;
				return rOK;
			}else{
				session->errMSG = "Specified player is empty.\n";
				return rError;
			}
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_vol(ctl_session *session){
	char *param;
	uint32_t aInt;
	float aFloat;
	inChannel *instance;

	// first parameter, input bus number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			aInt = session->lastPlayer;
		else
			aInt = atoi(param);		
		if(!checkPnumber(aInt)){
			session->errMSG = "Bad player number.\n";
			return rError;
		}
		// second parameter, volume
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			instance = &mixEngine->ins[aInt];
			if(*param == 'd'){
				float delta = atof(param+1);
				aFloat = delta * instance->vol;
				if(aFloat > 10.0)
					aFloat = 10.0;
				if((delta > 1.0) && (aFloat < 0.001)) // prvent 0 x numb = 0 problem
				   aFloat = 0.001;
			}else
				aFloat = atof(param);
			session->lastPlayer = aInt;
			if(instance->status & status_standby){
				instance->reqVol = aFloat;
				instance->requested = instance->requested | change_vol;
				return rOK;
			}else{
				session->errMSG = "Specified player is empty.\n";
				return rError;
			}
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_sender(ctl_session *session){
	pthread_t thisThread;
	uint32_t sender;
	int i;
	char *param;
	char *end;

	if(session->cs == 0){
		session->errMSG = "Can only set session sender id on a tcp connection.\n";
		return rError;
	}
		
	// first parameter, sender id to set session id to in hex format 
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		// hex number
		sender = strtoul(param, &end, 16);	
		thisThread = pthread_self();
		
		for(i=0; i<sessionListSize; i++){
			if(pthread_equal(sessionList[i].sessionThread, thisThread))
				break;
		}
		if(i < sessionListSize)
			sessionList[i].sender = sender;
		else{
			session->errMSG = "Invalid session?\n";
			return rError;
		}
		return rOK;
	}
	session->errMSG = "Missing parameter\n";
	return rError;
}

unsigned char handle_next(ctl_session *session){
	char *param;
	uint32_t aInt;
	int bInt;
	float aFloat;
	inChannel *instance;

	// first parameter, player number to set next player for
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			aInt = session->lastPlayer;
		else
			aInt = atoi(param);
		if(!checkPnumber(aInt)){
			session->errMSG = "Bad first player number.\n";
			return rError;            
		}
		session->lastPlayer = aInt;
		// second parameter, player number to start at segue time
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			if(*param == '$')
				bInt = session->lastPlayer;
			else
				bInt = atoi(param);			
			if(!checkPnumber(bInt)){
				if(bInt != -1){ // allow -1 to clear next player
					session->errMSG = "Bad second player number.\n";
					return rError;
				}
			}
			// third parameter, segue time
			param = strtok_r(NULL, " ", &session->save_pointer);
			if(param != NULL){
				aFloat = atof(param);
				if(bInt >= 0){
					instance = &mixEngine->ins[bInt];
					if(instance->status & status_standby == 0){
						session->errMSG = "The next player is empty.\n";
						return rError;
					}
				}
				bInt = bInt + 1;
				instance = &mixEngine->ins[aInt];
				if(instance->status & status_standby){
					instance->segNext = 0;
					instance->posSeg = aFloat;
					instance->segNext = bInt;
					return rOK;
				}else{
					session->errMSG = "Specified player is empty.\n";
					return rError;
				}					
			}
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_bus(ctl_session *session){
	char *param;
	char *end;
	uint32_t aInt;
	uint32_t aLong;
	inChannel *instance;
				
	// first parameter, input (player) number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			aInt = session->lastPlayer;
		else
			aInt = atoi(param);
		if(!checkPnumber(aInt)){
			session->errMSG = "Bad player number.\n";
			return rError;
		}
		// second parameter, output bus enable word (0 = disable, 1 = enable on bus corrisponding to bit number in word)
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			aLong = strtoul(param, &end, 16);
			session->lastPlayer = aInt;
			instance = &mixEngine->ins[aInt];
			instance->reqBusses = (instance->reqBusses & 0xff000000) | (aLong & 0x00ffffff);
			instance->requested = instance->requested | change_bus;
			return rOK;
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_showbus(ctl_session *session){
	char buf[16]; /* send data buffer */
	int tx_length;
	char *param;
	uint32_t aInt;
	inChannel *instance;
	
	// first parameter, input (player) number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			aInt = session->lastPlayer;
		else
			aInt = atoi(param);
		if(!checkPnumber(aInt)){
			session->errMSG = "Bad player number.\n";
			return rError;
		}
		session->lastPlayer = aInt;
		instance = &mixEngine->ins[aInt];
		tx_length = snprintf(buf, sizeof buf, "%08x\n", instance->busses);
		my_send(session, buf, tx_length, session->silent, 0);
		return rNone;
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_mutes(ctl_session *session){
	char *param;
	char *end;
	uint32_t aInt;
	uint32_t aLong;
	inChannel *instance;

	// first parameter, input (player) number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			aInt = session->lastPlayer;
		else
			aInt = atoi(param);
		if(!checkPnumber(aInt)){
			session->errMSG = "Bad player number.\n";
			return rError;
		}
		// second parameter, mute group enable byte (0 = disable, 1 = enable on bits of top byte)
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			aLong = strtoul(param, &end, 16);
			session->lastPlayer = aInt;
			instance = &mixEngine->ins[aInt];
			instance->reqBusses = (instance->reqBusses & 0x00ffffff) | (aLong & 0xff000000);
			instance->requested = instance->requested | change_mutes;
			return rOK;
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_mmbus(ctl_session *session){
	char *param;
	char *end;
	uint32_t bus;
	uint32_t aInt;
	inChannel *instance;
				
	// first parameter, input bus (player) number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			aInt = session->lastPlayer;
		else
			aInt = atoi(param);
		if(!checkPnumber(aInt)){
			session->errMSG = "Bad player number.\n";
			return rError;
		}
		// second parameter, mix-minus bus number +1 in the lower byte (zero for no bus), with
		// upper bit 31, 30, 29, and 24 set to enable cue feed over-ride of above on Talkback 
		// 3, 2, 1, and Cue active.
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			bus = atol(param);
			bus = (bus & 0xff00003f);		// limit range to 0-32 plus upper byte
			session->lastPlayer = aInt;
			instance = &mixEngine->ins[aInt];
			if(instance->status & status_standby){
				instance->reqFeedBus = bus;
				instance->requested = instance->requested | change_feedbus;
				return rOK;
			}else{
				session->errMSG = "Specified player is empty.\n";
				return rError;
			}
			return rOK;
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_mmvol(ctl_session *session){
	char *param;
	float aFloat;
	uint32_t aInt;
	inChannel *instance;

	// first parameter, input bus number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			aInt = session->lastPlayer;
		else
			aInt = atoi(param);
		if(!checkPnumber(aInt)){
			session->errMSG = "Bad player number.\n";
			return rError;
		}
		// second parameter, volume scalar
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			aFloat = atof(param);
			instance = &mixEngine->ins[aInt];
			session->lastPlayer = aInt;
			if(instance->status & status_standby){
				instance->reqFeedVol = aFloat;
				instance->requested = instance->requested | change_feedvol;
				return rOK;
			}else{
				session->errMSG = "Specified player is empty.\n";
				return rError;
			}
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_bal(ctl_session *session){
	char *param;
	float aFloat;
	uint32_t aInt;
	inChannel *instance;

	// first parameter, input bus number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			aInt = session->lastPlayer;
		else
			aInt = atoi(param);
		if(!checkPnumber(aInt)){
			session->errMSG = "Bad player number.\n";
			return rError;
		}
		// second parameter, balance (-1 = left only, +1 = right only, 0 = middle)  note: constant power panning, +3dB on Left/right only
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			aFloat = atof(param);
			if (aFloat > 1.0)
				aFloat = 1.0;
			if (aFloat < -1.0)
				aFloat = -1.0;
			instance = &mixEngine->ins[aInt];
			session->lastPlayer = aInt;
			if(instance->status & status_standby){
				instance->reqBal = aFloat;
				instance->requested = instance->requested | change_bal;
				return rOK;
			}else{
				session->errMSG = "Specified player is empty.\n";
				return rError;
			}
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_showmutes(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;
	uint32_t mute;
	
	mute = mixEngine->activeBus;
	if(mute & (1L << 24))
		tx_length = snprintf(buf, sizeof buf, "Cue +\n");
	else
		tx_length = snprintf(buf, sizeof buf, "Cue -\n");
	my_send(session, buf, tx_length, session->silent, 0);

	if(mute & (1L << 25))
		tx_length = snprintf(buf, sizeof buf, "MuteA +\n");
	else
		tx_length = snprintf(buf, sizeof buf, "MuteA -\n");
	my_send(session, buf, tx_length, session->silent, 0);

	if(mute & (1L << 26))
		tx_length = snprintf(buf, sizeof buf, "MuteB +\n");
	else
		tx_length = snprintf(buf, sizeof buf, "MuteB -\n");
	my_send(session, buf, tx_length, session->silent, 0);

	if(mute & (1L << 27))
		tx_length = snprintf(buf, sizeof buf, "MuteC +\n");
	else
		tx_length = snprintf(buf, sizeof buf, "MuteC -\n");
	my_send(session, buf, tx_length, session->silent, 0);

	if(mute & (1L << 29))
		tx_length = snprintf(buf, sizeof buf, "TB0 +\n");
	else
		tx_length = snprintf(buf, sizeof buf, "TB0 -\n");
	my_send(session, buf, tx_length, session->silent, 0);

	if(mute & (1L << 30))
		tx_length = snprintf(buf, sizeof buf, "TB1 +\n");
	else
		tx_length = snprintf(buf, sizeof buf, "TB1 -\n");
	my_send(session, buf, tx_length, session->silent, 0);

	if(mute & (1L << 31))
		tx_length = snprintf(buf, sizeof buf, "TB2 +\n");
	else
		tx_length = snprintf(buf, sizeof buf, "TB2 -\n");
	my_send(session, buf, tx_length, session->silent, 0);
	return rNone;
}

unsigned char handle_pos(ctl_session *session){
	char *param;
	uint32_t aInt;
	float aFloat;
	inChannel *instance;

	// first parameter, input bus number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			aInt = session->lastPlayer;
		else
			aInt = atoi(param);
		if(!checkPnumber(aInt)){
			session->errMSG = "Bad player number.\n";
			return rError;            
		}
		// second parameter, time
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			aFloat = atof(param);
			instance = &mixEngine->ins[aInt];
			session->lastPlayer = aInt;
			if(instance->status & status_standby){
				instance->reqPos = aFloat;
				instance->requested = instance->requested | change_pos;
				return rOK;
			}else{
				session->errMSG = "Specified player is empty.\n";
				return rError;
			}
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_unload(ctl_session *session){
	char *param;
	uint32_t aInt, c, cmax;
	inChannel *instance;
	jack_port_t **port;
	char *xfr, *name, *type, *req, *tmp;
	
	// first parameter, player number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			aInt = session->lastPlayer;
		else
			aInt = atoi(param);
		if(!checkPnumber(aInt)){
			session->errMSG = "Bad player number.\n";
			return rError;            
		}
		instance = &mixEngine->ins[aInt];
		if(instance->status & status_playing){
			session->errMSG = "Can't unload...item is playing.\n";
			return rError;
		}

		// handle possibvle sip player transfer on close
		if(instance->UID){
			type = GetMetaData(instance->UID, "Type", 0);
			if(!strcmp(type, "sip")){
				xfr = GetMetaData(0, "sip_transfer", 1);
				name = GetMetaData(instance->UID, "Name", 1);
				if(name){
					if(strlen(name)){
						// switch baresip to player's line number 
						// and initiate call transfer or hand-up
						int lineno = atoi(name);
						req = NULL;
						str_setstr(&req, "/line ");
						tmp = istr(lineno);
						str_appendstr(&req, tmp);
						free(tmp);
						str_appendchr(&req, '\n');
						if(xfr && strlen(xfr)){
							str_appendstr(&req, "/transfer ");
							str_appendstr(&req, xfr);
							str_appendchr(&req, '\n');
						}else{
							str_appendstr(&req, "/hangup\n");
						}
						pthread_mutex_lock(&sipMutex);
						send(sip_ctl_sock, req, strlen(req), 0);
						pthread_mutex_unlock(&sipMutex);
						free(req);
					}
					free(name);
				}
				if(xfr)
					free(xfr);
			}
			free(type);
		}
			 
				
		session->lastPlayer = aInt;
		if(instance->persist){
			instance->persist = persistOff;
			instance->status = status_delete;
		}
		port = instance->in_jPorts;
		cmax = mixEngine->chanCount;
		pthread_mutex_lock(&mixEngine->jackMutex);
		for(c=0; c<cmax; c++){
			jack_port_disconnect(mixEngine->client, *port);
			port++;
		}
		pthread_mutex_unlock(&mixEngine->jackMutex);
		return rOK;
/*		
		if(pLocks[aInt]->readLock(false)){
			instance = gLockedMem->pList[aInt];
			if(instance != NULL){
				if(instance->UID){
					pos = atof(GetMetaData(instance->UID, "Memory").c_str());
					if(pos > 0.0)
						// save play position
						dbSaveFilePos(instance->UID, instance->position);
					if(pos < 0)
						// clear position
						dbSaveFilePos(instance->UID, 0.0);
				}
				instance->status =  instance->status | status_delete;
				pLocks[aInt]->readUnlock();
				return rOK;
			}
			pLocks[aInt]->readUnlock();
*/			
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_load(ctl_session *session){
	char *param;
	int sInt;
	uint32_t aLong;
	inChannel *instance;
	char buf[4096]; /* send data buffer */
	int tx_length;

	// first parameter, player number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(*param == '$')
			sInt = session->lastPlayer;
		else
			sInt = atoi(param);
		// second parameter, url (file, input name, etc) is in save_pointer
		if(session->save_pointer){
			if(sInt < (signed)mixEngine->inCount){	
				if(LoadPlayer(&sInt, session->save_pointer, 0, 0)){
					instance = &mixEngine->ins[sInt];
					if(instance->status){
						session->lastPlayer = sInt;
						aLong = instance->UID;
						if(aLong){
							session->lastUID = aLong;
							session->lastPlayer = sInt;
							tx_length = snprintf(buf, sizeof buf, "UID=%08x\n", (unsigned int)aLong);
							my_send(session, buf, tx_length, session->silent, 0);
							return rNone;
						}
					}
				}else{
					session->errMSG = "failed to load player.\n";
					return rError;
				}
			}
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_notify(ctl_session *session){
	if(session->cs == 0){
		session->errMSG = "Can not be used on stdin/out connection.\n";
		return rError;
	}
	session->use_tcp = 1;
	return rOK;
}

unsigned char handle_vuon(ctl_session *session){
	if(session->cs == 0){
		session->errMSG = "Can not be used on stdin/out connection.\n";
		return rError;
	}
	session->notify_meters = 1;
	return rOK;
}

unsigned char handle_vuoff(ctl_session *session){
	if(session->cs == 0){
		session->errMSG = "Can not be used on stdin/out connection.\n";
		return rError;
	}
	session->notify_meters = 0;
	return rOK;
}

unsigned char handle_settings(ctl_session *session){
	char **keys;
	char **values;
	unsigned int count, i;
	int tx_length;
	char buf[4096]; /* send data buffer */

	if(count = GetMetaKeysAndValues(0, &keys, &values)){
		for(i=0; i<count; i++){
			tx_length = snprintf(buf, sizeof buf, "%s=%s\n", keys[i], values[i]);
			free(keys[i]);
			free(values[i]);
			my_send(session, buf, tx_length, session->silent, 0);
		}
		free(keys);
		free(values);
		return rNone;
	}
	session->errMSG = "AR system base meta-data (UID = 0) is missing!\n";
	return rError;
}

unsigned char handle_get(ctl_session *session){
	char *param, *val;
	char buf[4096]; /* send data buffer */
	int tx_length;

	// first parameter, property key
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		val = GetMetaData(0, param, 0);
		if(strlen(val)){
			tx_length = snprintf(buf, sizeof buf, "%s\n", val);
			free(val);
		}else{
			free(val);
			session->errMSG = "Empty value\n";
			return rError;
		}

		return rNone;
	}
	session->errMSG = "Missing parameter\n";
	return rError;
}

unsigned char handle_set(ctl_session *session){
	char *val, *key;
	
	// first parameter, property key
	key = strtok_r(NULL, " ", &session->save_pointer);
	if(key != NULL){
		// second parameter, value to set (in save_pointer)
		if(session->save_pointer != NULL){
			val = session->save_pointer;
			// value specified... set key to new value
			if(strcmp(key,"Version")){
				if(SetMetaData(0, key, val)){
					// send out notifications
					notifyData	data;
					data.reference = 0;
					data.senderID = getSenderID();
					data.value.iVal = 0;
					notifyMakeEntry(nType_mstat, &data, sizeof(data));
					return rOK; 
				}else{
					session->errMSG = "AR system base meta-data (UID = 0) is missing!\n";
					return rError;
				}
			}
		}
	}
	session->errMSG = "Bad or missing parameter\n";
	return rError;
}

unsigned char handle_saveset(ctl_session *session){
	FILE *fp;
	char *fPath;
	char **keys;
	char **values;
	unsigned int count, i;
	
	// save all current settings from pairs map
	fPath = GetMetaData(0, "file_prefs", 0);
	if(strlen(fPath) > 0){
		if((fp=fopen(fPath, "w")) != NULL){
			fprintf(fp, "; settings definitions\necho setting up last saved settings\n");
			if(count = GetMetaKeysAndValues(0, &keys, &values)){
				for(i=0; i<count; i++){
					if(strcmp(keys[i], "Version"))
						fprintf(fp, "set %s %s\n", keys[i], values[i]);
					free(keys[i]);
					free(values[i]);
				}
				free(keys);
				free(values);
				fclose(fp);
				return rOK;	
			}
		}
	}
	free(fPath);
	session->errMSG = "Missing or invalid path specified in file_prefs setting\n";
	return rError;
}

unsigned char handle_metalist(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;
	uidRecord *rec;

	tx_length = snprintf(buf, sizeof buf, "meta-UID\tRev\tusers\n");
	my_send(session, buf, tx_length, session->silent, 0);

	pthread_rwlock_rdlock(&dataLock);
	rec = (uidRecord *)&metaList;
	while(rec = (uidRecord *)getNextNode((LinkedListEntry *)rec)){
		tx_length = snprintf(buf, sizeof buf, "%08x\t%u\t%u\n", rec->UID, rec->rev, rec->refCnt);
		my_send(session, buf, tx_length, session->silent, 0);
	}
	pthread_rwlock_unlock(&dataLock);
	return rNone;
}

unsigned char handle_dumpmeta(ctl_session *session){
	char *param;
	char *end;
	int sInt;
	uint32_t aLong;
	char **keys;
	char **values;
	unsigned int count, i;
	char buf[4096]; /* send data buffer */
	int tx_length;

	// first parameter, meta data item UID in hex format
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			aLong = strtoul(param, &end, 16);
		}else{
			// decimal number (pNum)
			sInt = atoi(param);
			if(sInt < 0){
				aLong = session->lastUID;
			}else{
				aLong = (uint32_t)sInt;
				if(!getPlayerUID(&aLong)){
					session->lastUID = 0;
					session->errMSG = "bad player number.\n";
					return rError;
				}
			}
		}
		if(count = GetMetaKeysAndValues(aLong, &keys, &values)){
			tx_length = snprintf(buf, sizeof buf, "rev=%u\n", GetMetaRev(aLong));
			my_send(session, buf, tx_length, session->silent, 0);
			for(i=0; i<count; i++){
				tx_length = snprintf(buf, sizeof buf, "%s=%s\n", keys[i], values[i]);
				free(keys[i]);
				free(values[i]);
				my_send(session, buf, tx_length, session->silent, 0);
			}
			free(keys);
			free(values);
			return rNone;
		}
	}
	session->errMSG = "Missing or bad UID specified\n";
	return rError;
}

unsigned char handle_getmeta(ctl_session *session){
	char *param;
	char *end;
	char *val;
	int sInt;
	uint32_t aLong;
	char buf[4096]; /* send data buffer */
	int tx_length;

	// first parameter, meta data item UID in hex format
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			aLong = strtoul(param, &end, 16);
		}else{
			// decimal number (pNum)
			sInt = atoi(param);
			if(sInt < 0){
				aLong = session->lastUID;
			}else{
				aLong = (uint32_t)sInt;
				if(!getPlayerUID(&aLong)){
					session->lastUID = 0;
					session->lastPlayer = sInt;
					session->errMSG = "bad player number.\n";
					return rError;
				}
			}
		}	
		if(aLong != 0){
			// second parameter, key to get value for (in save_pointer)
			if(session->save_pointer != NULL){
				val = GetMetaData(aLong, session->save_pointer, 0);
				if(strlen(val)){
					tx_length = snprintf(buf, sizeof buf, "%s\n", val);
					free(val);
				}else{
					free(val);
					session->errMSG = "Empty value\n";
					return rError;
				}
				session->lastUID = aLong;
				return rNone;
			}
		}
	}
	session->errMSG = "Missing or bad parameter\n";
	return rError;
}

unsigned char handle_setmeta(ctl_session *session){
	char *param;
	char *end;
	int sInt;
	uint32_t aLong;
	char buf[4096]; /* send data buffer */
	int tx_length;

	// first parameter, meta data item UID in hex format or player number in decimal format
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			aLong = strtoul(param, &end, 16);
		}else{
			// decimal number (pNum)
			sInt = atoi(param);
			if(sInt < 0){
				aLong = session->lastUID;
			}else{
				aLong = (uint32_t)sInt;
				if(!getPlayerUID(&aLong)){
					session->lastPlayer = sInt;
					session->lastUID = 0;
					session->errMSG = "bad player number.\n";
					return rError;
				}
			}
		}       
		if(aLong != 0){
			// second parameter, key to set value for
			param = strtok_r(NULL, " ", &session->save_pointer);
			if(param != NULL){
				if(session->save_pointer != NULL){
					// third parameter, value to set (in save_pointer)
					if(SetMetaData(aLong, param, session->save_pointer)){
						// send out notifications
						notifyData	data;
						data.reference = htonl(aLong);
						data.senderID = getSenderID();
						data.value.iVal = 0;
						notifyMakeEntry(nType_mstat, &data, sizeof(data));
						return rOK; 
					}else{
						session->errMSG = "specified UID doesn't exist.\n";
						return rError;
					}  
				}
			}
		}
	}
	session->errMSG = "Missing or bad parameter\n";
	return rError;
}

unsigned char handle_delmeta(ctl_session *session){
	char *param;
	char *end;
	int sInt;
	uint32_t aLong;
	char buf[4096]; /* send data buffer */
    int tx_length;

	// first parameter, meta data item UID in hex format
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			aLong = strtoul(param, &end, 16);
		}else{
			// decimal number (pNum)
			sInt = atoi(param);
			if(sInt < 0){
				aLong = session->lastUID;
			}else{
				aLong = (uint32_t)sInt;
				if(!getPlayerUID(&aLong)){
					session->lastPlayer = sInt;
					session->lastUID = 0;
					session->errMSG = "bad player number.\n";
					return rError;
				}
			}
		}
		if(aLong != 0){
			// second parameter, key to delete (in save_pointer)
			if(session->save_pointer != NULL){
				if(DelMetaData(aLong, session->save_pointer)){
					// send out notifications
					notifyData	data;
					data.reference = htonl(aLong);
					data.senderID = getSenderID();
					data.value.iVal = 0;
					notifyMakeEntry(nType_mstat, &data, sizeof(data));
					session->lastUID = 0;
					return rOK; 
				}
			}
		}
	}
	session->errMSG = "Missing or bad parameter\n";
	return rError;
}

unsigned char handle_srcports(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;
	int size, i;
	const char **ports, *port;
	
	pthread_mutex_lock(&mixEngine->jackMutex);
	ports = jack_get_ports(mixEngine->client, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
	pthread_mutex_unlock(&mixEngine->jackMutex);
	if(ports){
		tx_length = snprintf(buf, sizeof buf, "Client:Port\n");
		my_send(session, buf, tx_length, session->silent, 0);
		i=0;
		while(port = ports[i]){
			tx_length = snprintf(buf, sizeof buf, "%s\n", port);
			my_send(session, buf, tx_length, session->silent, 0);
			i++;
		}
		jack_free(ports);
		return rNone;
	}
	session->errMSG = "Error retreaving current Jack client and port list.\n";
	return rError;
}

unsigned char handle_dstports(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;
	int size, i;
	const char **ports, *port;
	
	pthread_mutex_lock(&mixEngine->jackMutex);
	ports = jack_get_ports(mixEngine->client, NULL, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
	pthread_mutex_unlock(&mixEngine->jackMutex);
	if(ports){
		tx_length = snprintf(buf, sizeof buf, "Client:Port\n");
		my_send(session, buf, tx_length, session->silent, 0);
		i=0;
		while(port = ports[i]){
			tx_length = snprintf(buf, sizeof buf, "%s\n", port);
			my_send(session, buf, tx_length, session->silent, 0);
			i++;
		}
		free(ports);
		return rNone;
	}
	session->errMSG = "Error retreaving current Jack client and port list.\n";
	return rError;
}

unsigned char handle_rotatelog(ctl_session *session){	
	if(serverLogRotateLogFile())
		return rOK;
	session->errMSG = "There was a problem renaming the log file.\n";
	return rError;
}

unsigned char handle_savein(ctl_session *session){
	FILE *fp;
	char *fPath;
	inputRecord *rec;

	// save all line-input definitions from pairs map
	if(fPath = GetMetaData(0, "file_inputs", 0)){
		if(strlen(fPath) > 0){
			if((fp=fopen(fPath, "w")) != NULL){
				fprintf(fp, "; line-input definitions\necho setting up line input definitions\n");
				pthread_rwlock_rdlock(&inputLock);
				rec = (inputRecord *)&inputList;
				while(rec = (inputRecord *)getNextNode((LinkedListEntry *)rec)){
					fprintf(fp, "setin %s %08x %06x %s\n", rec->Name, rec->busses, rec->controls, rec->portList);
					if((rec->mmList) && strlen(rec->mmList))
						fprintf(fp, "setmm %s %s\n", rec->Name, rec->mmList);
				}
				pthread_rwlock_unlock(&inputLock);
				fclose(fp);
				return rOK;
			}
		}
		free(fPath);
	}
	session->errMSG = "Missing or invalid path specified in file_inputs setting.\n";
	return rError;
}

unsigned char handle_dumpin(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;
	inputRecord *rec;
	
	// dump all line-input definitions
	tx_length = snprintf(buf, sizeof buf, "Name\tBus\tcontrols\tPort List\n");
	my_send(session, buf, tx_length, session->silent, 0);

	pthread_rwlock_rdlock(&inputLock);
	rec = (inputRecord *)&inputList;
	while(rec = (inputRecord *)getNextNode((LinkedListEntry *)rec)){
		tx_length = snprintf(buf, sizeof buf,"%s\t%08x\t%06x\t%s\n", rec->Name, rec->busses, rec->controls, rec->portList);
		my_send(session, buf, tx_length, session->silent, 0);
	}
	pthread_rwlock_unlock(&inputLock);
	return rNone;
}

unsigned char handle_getin(ctl_session *session){
	char *param;
	char buf[4096]; /* send data buffer */
	int tx_length;
	inputRecord *rec = NULL;
		
	// first parameter, input Name
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		tx_length = snprintf(buf, sizeof buf, "Name\tBus\tControls\tPort List\n");
		my_send(session, buf, tx_length, session->silent, 0);

		pthread_rwlock_rdlock(&inputLock);
		if(rec = getRecordForInput((inputRecord *)&inputList, param)){
			tx_length = snprintf(buf, sizeof buf,"%s\t%08x\t%06x\t%s\n", rec->Name, rec->busses, rec->controls, rec->portList);
			my_send(session, buf, tx_length, session->silent, 0);
		}
		pthread_rwlock_unlock(&inputLock);
	}
	if(rec)
		return rNone;
	session->errMSG = "Missing or bad input name.\n";
	return rError;
}

unsigned char handle_setin(ctl_session *session){
	char *name, *param;
	char *end;
	uint32_t cbits;
	uint32_t bbits;
	inputRecord *rec;

	// first parameter, name to set input parameters for
	name = strtok_r(NULL, " ", &session->save_pointer);
	if(name != NULL){
		// second parameter, bus setting
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			bbits = strtoul(param, &end, 16);
			// third parameter, control bits
			param = strtok_r(NULL, " ", &session->save_pointer);
			if(param != NULL){
				cbits = strtoul(param, &end, 16);			
				// fourth parameter, device UID string to store
				if(session->save_pointer != NULL){
					pthread_rwlock_wrlock(&inputLock);
					rec = setValuesForInput((inputRecord *)&inputList, name, bbits, cbits, session->save_pointer);
					pthread_rwlock_unlock(&inputLock);
					if(rec)
						return rOK;
				}
			}
		}
	}
	session->errMSG = "Missing or bad parameter\n";
	return rError;
}

unsigned char handle_delin(ctl_session *session){
	char *param;
	inputRecord *rec= NULL;
		
	// first parameter, input Name
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		pthread_rwlock_wrlock(&inputLock);
		if(rec = getRecordForInput((inputRecord *)&inputList, param))
			releaseInputRecord((inputRecord *)&inputList, rec);
		pthread_rwlock_unlock(&inputLock);
	}
	if(rec)
		return rOK;
	session->errMSG = "Missing or bad input name.\n";
	return rError;
}

unsigned char handle_setout(ctl_session *session){
	char *param, *name;
	char *end;
	int count, i, firstFree, c, max;
	char buf[64];
	jack_port_t **port;
	
	uint32_t nameHash;
	uint32_t mg; // mute group
	uint32_t bus;
	unsigned char showUI;
	outChannel *instance;
	
	// first parameter, name string
	param = strtok_r(NULL, " ", &session->save_pointer);
	if((param != NULL) && strlen(param)){
		name = strdup(param);
		nameHash = ELFHash(0, name, strlen(name));
		// second parameter, mute gains (hex format)   
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			mg = strtoul(param, &end, 16);
			// third parameter, mix bus assignment
			param = strtok_r(NULL, " ", &session->save_pointer);
			if(param != NULL){
				bus = atoi(param);
				// 4th parameter, show in UI boolean
				param = strtok_r(NULL, " ", &session->save_pointer);
				if(param != NULL){
					showUI = atoi(param);
					// last parameter, jack port connection list
					if(session->save_pointer != NULL){
						firstFree = 0;
						instance = mixEngine->outs;
						pthread_rwlock_wrlock(&mixEngine->outGrpLock);
						for(i=0; i<mixEngine->outCount; i++){
							if(instance->name){
								if((instance->nameHash == nameHash) &&
										(!strcmp(name, instance->name))){
									// found existing record... update
									instance->muteLevels = mg;
									if(instance->bus != bus){
										instance->reqBus = bus;
										instance->requested = instance->requested | change_bus;
									}
									instance->showUI = showUI;
									updateOutputConnections(mixEngine, instance, 1, session->save_pointer, NULL);

									pthread_rwlock_unlock(&mixEngine->outGrpLock);
									free(name);
									return rOK;
								}
							}else{
								if(!firstFree){
									firstFree = i+1;
								}
							}
							instance++;
						}
						if(i == mixEngine->outCount){
							// not found... set new output group, with unity gain and no delay
							if(!firstFree){
								pthread_rwlock_unlock(&mixEngine->outGrpLock);
								session->errMSG = "Can't setup an new output group: All output groups are in use.\n";
								return rError;
							}else{
								firstFree--;
								instance = &mixEngine->outs[firstFree];
								instance->name = strdup(name);
								instance->nameHash = nameHash;
								instance->muteLevels = mg;
								instance->reqBus = bus;
								instance->reqVol = 1.0;
								instance->reqDelay = 0.0;
								instance->showUI = showUI;
								instance->requested = instance->requested | (change_vol | change_bus | change_delay);

								port = instance->jPorts;
								max = mixEngine->chanCount;
								/* set port name */
								pthread_mutex_lock(&mixEngine->jackMutex);
								for(c=0; c<max; c++){
									snprintf(buf, sizeof buf, "%s_ch%d", instance->name, c);
									jack_port_rename(mixEngine->client, *port, buf);
									port++;
								}
								pthread_mutex_unlock(&mixEngine->jackMutex);
								updateOutputConnections(mixEngine, instance, 1, session->save_pointer, NULL);
								
								pthread_rwlock_unlock(&mixEngine->outGrpLock);
								free(name);
								return rOK;
							}
						}
						pthread_rwlock_unlock(&mixEngine->outGrpLock);
					}
				}
			}
		}
		if(name)
			free(name);
	}
	session->errMSG = "Missing or bad parameter.\n";
	return rError;
}

unsigned char handle_saveout(ctl_session *session){
	FILE *fp;
	char *fPath;
	outChannel *instance;
	int i;

	// save all line-input definitions from pairs map
	fPath = GetMetaData(0, "file_outputs", 0);
	if(strlen(fPath) > 0){
		if((fp=fopen(fPath, "w")) != NULL){
			fprintf(fp, "; audio outputs definitions\necho setting up audio output devices\n");
			instance = mixEngine->outs;
			pthread_rwlock_rdlock(&mixEngine->outGrpLock);
			for(i=0; i<mixEngine->outCount; i++){
				if(instance->name){
					fprintf(fp, "setout %s %08x %d %d %s\n", instance->name, instance->muteLevels, instance->bus, instance->showUI, instance->portList);
					if(instance->vol != 1.0)
						fprintf(fp, "outvol %s %f\n", instance->name, instance->vol);
				}
				instance++;
			}
			pthread_rwlock_unlock(&mixEngine->outGrpLock);
			free(fPath);
			fclose(fp);
			return rOK;
		}
	}
	free(fPath);
	session->errMSG = "Missing or invalid path specified in file_outputs setting.\n";
	return rError;
}

unsigned char handle_dumpout(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;
	int i;
	outChannel *instance;
	
	// dump all output group definitions 
	tx_length = snprintf(buf, sizeof buf, "Name\tvolume\tMute\tBus\tShowUI\tPort List\n");
	my_send(session, buf, tx_length, session->silent, 0);
	instance = mixEngine->outs;
	pthread_rwlock_rdlock(&mixEngine->outGrpLock);
	for(i=0; i<mixEngine->outCount; i++){
		if(instance->name){
			tx_length = snprintf(buf, sizeof buf, "%s\t%.3f\t%08x\t%d\t%d\t%s\n", instance->name, instance->vol, instance->muteLevels, instance->bus, instance->showUI, instance->portList);
			my_send(session, buf, tx_length, session->silent, 0);
		}
		instance++;
	}
	pthread_rwlock_unlock(&mixEngine->outGrpLock);
	return rNone;
}

unsigned char handle_outvol(ctl_session *session){
	char *param, *name;
	outChannel *instance;
	uint32_t nameHash;
	float aFloat, delta;
	int i;
	
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param && strlen(param)){
		// first parameter, output name
		name = strdup(param);
		nameHash = ELFHash(0, name, strlen(name));
		instance = mixEngine->outs;
		pthread_rwlock_wrlock(&mixEngine->outGrpLock);
		for(i=0; i<mixEngine->outCount; i++){
			if(instance->name){
				if((instance->nameHash == nameHash) &&
										(!strcmp(name, instance->name))){
					// found the record... set volume
					if(*session->save_pointer == 'd'){
						delta = atof(session->save_pointer+1);
						aFloat = delta * instance->vol;
						if(aFloat > 1.0)
							aFloat = 1.0;
						if((delta > 1.0) && (aFloat < 0.001)) // prvent 0 x numb = 0 problem
						   aFloat = 0.001;
					}else
						aFloat = atof(session->save_pointer);
					instance->reqVol = aFloat;
					instance->requested = instance->requested | change_vol;
					
					pthread_rwlock_unlock(&mixEngine->outGrpLock);
					free(name);
					return rOK;
				}
			}
			instance++;
		}
		pthread_rwlock_unlock(&mixEngine->outGrpLock);
		free(name);
	}
	session->errMSG = "Missing or bad output group name.\n";
	return rError;
}

unsigned char handle_outbus(ctl_session *session){
	char *param, *name;
	outChannel *instance;
	uint32_t nameHash;
	uint32_t bus;
	int i;
	
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param && strlen(param)){
		// first parameter, output name
		name = strdup(param);
		nameHash = ELFHash(0, name, strlen(name));
		instance = mixEngine->outs;
		pthread_rwlock_wrlock(&mixEngine->outGrpLock);
		for(i=0; i<mixEngine->outCount; i++){
			if(instance->name){
				if((instance->nameHash == nameHash) &&
										(!strcmp(name, instance->name))){
					// found the record... set bus
					bus = atoi(session->save_pointer);
					instance->reqBus = bus;
					instance->requested = instance->requested | change_bus;
					
					pthread_rwlock_unlock(&mixEngine->outGrpLock);
					free(name);
					return rOK;
				}
			}
			instance++;
		}
		pthread_rwlock_unlock(&mixEngine->outGrpLock);
		free(name);
	}
	session->errMSG = "Missing or bad output group name.\n";
	return rError;
}

unsigned char handle_setdly(ctl_session *session){
	char *param, *name;
	outChannel *instance;
	uint32_t nameHash;
	float aFloat;
	int i;
	
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param && strlen(param)){
		// first parameter, output name
		name = strdup(param);
		nameHash = ELFHash(0, name, strlen(name));
		instance = mixEngine->outs;
		pthread_rwlock_wrlock(&mixEngine->outGrpLock);
		for(i=0; i<mixEngine->outCount; i++){
			if(instance->name){
				if((instance->nameHash == nameHash) &&
										(!strcmp(name, instance->name))){
					// found the record... set volume
					aFloat = atof(session->save_pointer);
					instance->reqDelay = aFloat;
					instance->requested = instance->requested | change_delay;
					
					pthread_rwlock_unlock(&mixEngine->outGrpLock);
					free(name);
					return rOK;
				}
			}
			instance++;
		}
		pthread_rwlock_unlock(&mixEngine->outGrpLock);
		free(name);
	}
	session->errMSG = "Missing or bad output group name.\n";
	return rError;
}

unsigned char handle_getdly(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;
	int i;
	outChannel *instance;
	
	// dump all output group definitions 
	tx_length = snprintf(buf, sizeof buf, "Dest ID\t\tName\t\tDelay\n");
	my_send(session, buf, tx_length, session->silent, 0);	
	instance = mixEngine->outs;
	pthread_rwlock_rdlock(&mixEngine->outGrpLock);
	for(i=0; i<mixEngine->outCount; i++){
		if(instance->name){
			tx_length = snprintf(buf, sizeof buf, "%s\t%s\t%.2f\n", instance->name, instance->name, instance->delay);
			my_send(session, buf, tx_length, session->silent, 0);
		}
		instance++;
	}
	pthread_rwlock_unlock(&mixEngine->outGrpLock);
	return rNone;
}

unsigned char handle_dump(void){
	int i;
	outChannel *instance;
	
	// set all output group delays to zero 
	instance = mixEngine->outs;
	pthread_rwlock_wrlock(&mixEngine->outGrpLock);
	for(i=0; i<mixEngine->outCount; i++){
		if(instance->name){
			instance->reqDelay = 0.0;
			instance->requested = instance->requested | change_delay;
		}
		instance++;
	}
	pthread_rwlock_unlock(&mixEngine->outGrpLock);
	return rOK;
}

unsigned char handle_delout(ctl_session *session){
	char *param, *name;
	outChannel *instance;
	uint32_t nameHash;
	char buf[64];
	unsigned int i, c, max;
	jack_port_t **port;
	
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param && strlen(param)){
		// first parameter, output name
		name = strdup(param);
		nameHash = ELFHash(0, name, strlen(name));
		instance = mixEngine->outs;
		pthread_rwlock_wrlock(&mixEngine->outGrpLock);
		for(i=0; i<mixEngine->outCount; i++){
			if(instance->name){
				if((instance->nameHash == nameHash) &&
										(!strcmp(name, instance->name))){
											
					// found the record... cleard it
					port = instance->jPorts;
					max = mixEngine->chanCount;
					/* unset port name */
					pthread_mutex_lock(&mixEngine->jackMutex);
					for(c=0; c<max; c++){
						snprintf(buf, sizeof buf, "Out%dch%d", i, c);
						jack_port_rename(mixEngine->client, *port, buf);
						port++;
					}
					pthread_mutex_unlock(&mixEngine->jackMutex);
					updateOutputConnections(mixEngine, instance, 1, NULL, NULL);
					
					pthread_rwlock_unlock(&mixEngine->outGrpLock);
					free(name);
					return rOK;
				}
			}
			instance++;
		}
		pthread_rwlock_unlock(&mixEngine->outGrpLock);
		free(name);
	}
	session->errMSG = "Missing or bad output group name.\n";
	return rError;
}

unsigned char handle_getout(ctl_session *session){
	char *param, *name;
	uint32_t nameHash;
	outChannel *instance;
	char buf[4096]; /* send data buffer */
	int tx_length;
	int i;

	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param && strlen(param)){
		// first parameter, output name
		name = strdup(param);
		nameHash = ELFHash(0, name, strlen(name));
		instance = mixEngine->outs;
		pthread_rwlock_rdlock(&mixEngine->outGrpLock);
		for(i=0; i<mixEngine->outCount; i++){
			if(instance->name){
				if((instance->nameHash == nameHash) &&
										(!strcmp(name, instance->name))){
					// found the record...
					tx_length = snprintf(buf, sizeof buf, "Name\tvolume\tMute\tBus\tPort List\n");
					my_send(session, buf, tx_length, session->silent, 0);
					tx_length = snprintf(buf, sizeof buf, "%s\t%.3f\t%08x\t%d\t%s\n", instance->name, instance->vol, instance->muteLevels, instance->bus, instance->portList);
					my_send(session, buf, tx_length, session->silent, 0);
					
					pthread_rwlock_unlock(&mixEngine->outGrpLock);
					free(name);
					return rOK;
				}
			}
			instance++;
		}
		pthread_rwlock_unlock(&mixEngine->outGrpLock);
		free(name);
	}
	session->errMSG = "Missing or bad output group name.\n";
	return rError;
}

unsigned char handle_jconlist(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;
	connRecord *rec;
	jack_port_t *jport;
	const char **conns, **ptr;
	const char *name;
	unsigned char flag;

	tx_length = snprintf(buf, sizeof buf, "connected\t(source)>(destination)\n");
	my_send(session, buf, tx_length, session->silent, 0);
	
	pthread_rwlock_rdlock(&connLock);
	rec = (connRecord *)&connList;
	while(rec = (connRecord *)getNextNode((LinkedListEntry *)rec)){
		flag = 0;
		pthread_mutex_lock(&mixEngine->jackMutex);
		if(jport = jack_port_by_name(mixEngine->client, rec->src)){	
			if(conns = jack_port_get_all_connections(mixEngine->client, jport)){
				pthread_mutex_unlock(&mixEngine->jackMutex);
				ptr = conns;
				while(name = *ptr){
					if(!strcmp(name, rec->dest)){
						flag = 1;
						break;
					}
					ptr++;
				}
				jack_free(conns);
			}else
				pthread_mutex_unlock(&mixEngine->jackMutex);
		}else
			pthread_mutex_unlock(&mixEngine->jackMutex);
		tx_length = snprintf(buf, sizeof buf, "%d\t%s>%s\n", 
					flag, rec->src, rec->dest);
		my_send(session, buf, tx_length, session->silent, 0);
	}
	pthread_rwlock_unlock(&connLock);
	return rNone;
}

unsigned char handle_savejcons(ctl_session *session){
	FILE *fp;
	char *fPath;
	char buf[4096]; /* send data buffer */
	connRecord *rec;
	jack_port_t *jport;
	const char **conns, **ptr;
	const char *name;
	unsigned char flag;	int i;

	// save all jack connection definition
	fPath = GetMetaData(0, "file_jackcons", 0);
	if(strlen(fPath) > 0){
		if((fp=fopen(fPath, "w")) != NULL){
			fprintf(fp, "; jack audio connection definitions\necho setting up jack audio connections\n");
			pthread_rwlock_rdlock(&connLock);
			rec = (connRecord *)&connList;
			while(rec = (connRecord *)getNextNode((LinkedListEntry *)rec)){
				fprintf(fp, "jackconn %s>%s\n", rec->src, rec->dest);
			}
			pthread_rwlock_unlock(&connLock);
			free(fPath);
			fclose(fp);
			return rOK;
		}
	}
	free(fPath);
	session->errMSG = "Missing or invalid path specified in file_jackcons setting.\n";
	return rError;
}

unsigned char handle_jackconn(ctl_session *session){
	char *src, *dest;
	int res;

	if(session->save_pointer && strlen(session->save_pointer)){
		if(src = str_NthField(session->save_pointer, ">", 0)){
			if(dest = str_NthField(session->save_pointer, ">", 1)){
				pthread_mutex_lock(&mixEngine->jackMutex);
				res = jack_connect(mixEngine->client, src, dest);
				pthread_mutex_unlock(&mixEngine->jackMutex);
				pthread_rwlock_wrlock(&connLock);
				setValuesForConn((connRecord *)&connList, src, dest);
				pthread_rwlock_unlock(&connLock);
				free(dest);
				free(src);
				if(!res || (res == EEXIST)){
					return rOK;	
				}else{
					session->errMSG = "Connection added to persistent list despite connection failure.\n";
					return rError;
				}
			}
			free(src);
		}
	}
	session->errMSG = "Missing or bad parameter.\n";
	return rError;
}

unsigned char handle_jackdisc(ctl_session *session){
	char *src, *dest;
	connRecord *rec;
	
	if(session->save_pointer && strlen(session->save_pointer)){
		if(src = str_NthField(session->save_pointer, ">", 0)){
			if(dest = str_NthField(session->save_pointer, ">", 1)){	
				pthread_rwlock_wrlock(&connLock);
				if(rec = findConnRecord((connRecord *)&connList, src, dest))
					releaseConnRecord((connRecord *)&connList, rec);
				pthread_rwlock_unlock(&connLock);
				pthread_mutex_lock(&mixEngine->jackMutex);
				if(!jack_disconnect(mixEngine->client, src, dest)){
					pthread_mutex_unlock(&mixEngine->jackMutex);
					free(dest);
					free(src);
					return rOK;	
				}else{
					pthread_mutex_unlock(&mixEngine->jackMutex);
					free(dest);
					free(src);
					session->errMSG = "Disconnect failed.\n";
					return rError;
				}
				free(dest);
			}
			free(src);
		}
	}
	session->errMSG = "Missing or bad parameter.\n";
	return rError;	
}

unsigned char handle_setmm(ctl_session *session){
	char *param, *end;
	inputRecord *rec = NULL;
	float vol;
	uint32_t bus;
		
	// first parameter, input Name
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		pthread_rwlock_wrlock(&inputLock);
		if(rec = getRecordForInput((inputRecord *)&inputList, param)){
			// second parameter, mix bus number +1 bit orded with mute busses pointer);
			if(param != NULL){
				bus = atol(param);
				bus = (bus & 0xff00001f);
				// third parameter, mix-minus volume scalar
				param = strtok_r(NULL, " ", &session->save_pointer);
				if(param != NULL){
					vol = atof(param);
					rec->mmVol = vol;
					rec->mmBus = bus;
					// fourth parameter, jack connection list
					if(rec->mmList)
						free(rec->mmList);
					if(session->save_pointer && strlen(session->save_pointer)){
						rec->mmList = strdup(session->save_pointer);
					}else{
						rec->mmList = NULL;
					}
				}
			}
		}
		pthread_rwlock_unlock(&inputLock);
	}
	if(rec)
		return rOK;
	session->errMSG = "Missing or bad input name.\n";
	return rError;
}

unsigned char handle_getmm(ctl_session *session){
	char *param;
	char buf[4096]; /* send data buffer */
	int tx_length;
	inputRecord *rec = NULL;
		
	// first parameter, input Name
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		pthread_rwlock_wrlock(&inputLock);
		if(rec = getRecordForInput((inputRecord *)&inputList, param)){
			// found the record...
			tx_length = snprintf(buf, sizeof buf, "Bus\tVolume\tConnection List\n");
			my_send(session, buf, tx_length, session->silent, 0);
			if(rec->mmList && strlen(rec->mmList))
				tx_length = snprintf(buf, sizeof buf, "%u\t%f\t%s\n", rec->mmBus, rec->mmVol, rec->mmList);
			else
				tx_length = snprintf(buf, sizeof buf, "%u\t%f\t\n", rec->mmBus, rec->mmVol);
			my_send(session, buf, tx_length, session->silent, 0);
		}
		pthread_rwlock_unlock(&inputLock);
	}
	if(rec)
		return rOK;
	session->errMSG = "Missing or bad input name.\n";
	return rError;
}

unsigned char handle_execute(ctl_session *session){
	uint32_t UID;

	if(session->save_pointer){
		ExecuteProcess(session->save_pointer, 0, 0);
		return rOK;
	}
	session->errMSG = "Shell command to execute is missing.\n";
	return rError;
}

unsigned char handle_task(ctl_session *session){
	char *param;
	char *passIn;
	char *name;
	int sInt;
	uint32_t timeout;

	if(session->save_pointer){
		// first parameter, task name
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			name = param;
			// second parameter, timeout in seconds or no timeout if zero
			param = strtok_r(NULL, " ", &session->save_pointer);
			sInt = atoi(param);
			if(sInt < 0)
				sInt = 0;
			timeout = (uint32_t)sInt;
			// third parameter (in save_pointer) is the command and arguments
			if(session->save_pointer){
				passIn = strdup(session->save_pointer); // will be freed after Task completes
				createTaskItem(name, ExecuteCommand, (void *)passIn, 0L, -1, timeout, 1);
				return rOK;   
			}
		}
	}
	session->errMSG = "Missing or bad parameter\n";
	return rError;
}

unsigned char handle_tasks(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;
	taskRecord *rec, *prev;

	// dump the task list
	tx_length = snprintf(buf, sizeof buf, "ID\tPID\tUID\tRuntime\tTimeout\tName\n");
	my_send(session, buf, tx_length, session->silent, 0);
	pthread_rwlock_rdlock(&taskLock);
	prev = (taskRecord *)&taskList;
	while(rec = (taskRecord *)getNextNode((LinkedListEntry *)prev)){
		tx_length = snprintf(buf, sizeof buf, "%u\t%u\t%08x\t%ld\t%d\t%s\n", 
				rec->taskID, (unsigned int)rec->taskID, (unsigned int)rec->UID, 
				time(NULL) - rec->started, rec->timeOut, rec->name);
		my_send(session, buf, tx_length, session->silent, 0);
		prev = rec;
	}
	pthread_rwlock_unlock(&taskLock);
	return rNone;
}

unsigned char handle_deltask(ctl_session *session){
	char *param;
	uint32_t aInt;
	taskRecord *rec;

	// first parameter, task number
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param){
		aInt = atoi(param);
		pthread_rwlock_wrlock(&taskLock);
		if(rec = (taskRecord *)findNode((LinkedListEntry *)&taskList, aInt, NULL, NULL)){
			if(rec->pid)
				// kill associated process, waitPID will clean up.
				kill(rec->pid, SIGKILL);
			else
				// set cancel flag to stop thread on next check of flag,
				// assuming the thread function check this flag periodically.
				rec->cancelThread = 1;
		}
		pthread_rwlock_unlock(&taskLock);
		if(rec)
			return rOK;
		else
			session->errMSG = "Invalid task ID.\n";
	}else{
		session->errMSG = "Missing task ID.\n";
	}
	return rError;
}

unsigned char handle_urlmeta(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length, i, count;
	uint32_t localUID;
	char **keys;
	char **values;  

	// first parameter, url is in save_pointer
	if(!session->silent && session->save_pointer){
		// create a meta data record to hold results
		localUID = createMetaRecord(session->save_pointer, NULL, 1);
		// fill the metadata record
		GetURLMetaData(localUID, session->save_pointer);
		if(count = GetMetaKeysAndValues(localUID, &keys, &values)){
			tx_length = snprintf(buf, sizeof buf, "rev=%u\n", GetMetaRev(localUID));
			my_send(session, buf, tx_length, session->silent, 0);
			for(i=0; i<count; i++){
				tx_length = snprintf(buf, sizeof buf, "%s=%s\n", keys[i], values[i]);
				my_send(session, buf, tx_length, session->silent, 0);
				free(keys[i]);
				free(values[i]);
			}
			free(keys);
			free(values);
			releaseMetaRecord(localUID);
			return rNone;
		}
		releaseMetaRecord(localUID);
	}
	session->errMSG = "Missing parameter\n";
	return rError;
}

unsigned char handle_dblist(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;

	if(!session->silent){
		tx_length = snprintf(buf, sizeof buf, "Name\tRev\n");
		my_send(session, buf, tx_length, session->silent, 0);
		DumpDBDriverList(session, buf, sizeof buf);
	}
	return rNone;
}

unsigned char handle_cue(ctl_session *session){
	int sInt;
	uint32_t aLong;
	inChannel *instance;
	char buf[4096]; // send data buffer
	int tx_length;

	// first parameter, url (file, input name, etc), is in save_pointer
	if(session->save_pointer){
		sInt = -1;  // load from next available player
		if(LoadPlayer(&sInt, session->save_pointer, 0, 0)){
			aLong = 0;
			instance = &mixEngine->ins[sInt];
			// loaded:  now put it in cue
			instance->reqBusses = instance->busses | 2L;
			instance->requested = instance->requested | change_bus;
									
			// and return the result info to the client
			aLong = instance->UID;
			if(aLong){
				session->lastUID = aLong;
				tx_length = snprintf(buf, sizeof buf, "UID=%08x Player=%d\n", (unsigned int)aLong, sInt);
				my_send(session, buf, tx_length, session->silent, 0);
				return rNone;
			}
		}else{
			session->errMSG = "failed to load player.\n";
			return rError;
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_playnow(ctl_session *session){
	// loads and starts playing the specified url or list item UID
	int sInt;
	uint32_t aLong;
	uint32_t newUID;
	char *key, *val, *url;
	inChannel *instance;
	char *end;
	
	aLong = 0;
	// first parameter, url (file, input name, etc), or playlist item UID is in save_pointer
	if(session->save_pointer){
		if(strlen(session->save_pointer) <= 4){
			// decimal number, possible -1 (for last UID)
			sInt = atoi(session->save_pointer);
			if((sInt != -1) || ((aLong = session->lastUID) == 0)){
				session->errMSG = "invalid last UID\n";
				return rError;	
			}
		}
		if(!aLong){
			// try as a hex number (UID)
			aLong = strtoul(session->save_pointer, &end, 16);
			if(*end)
				// the fist digit may have been hex, but there are other char's in there too... must be a URL
				aLong = 0;
		}
		if(aLong){
			// Hex value. Treat as UID
			session->lastUID = aLong;
	 	
			if(!getQueuePos(&aLong)){
				session->errMSG = "Specified UID is not in play list.\n";
				return rError;
			}
			sInt = (int)aLong;
			sInt = LoadItem(sInt, NULL);
			if(sInt < -3)
				// already in a player.  returned value is -4 - PlayerNumber
				sInt = -sInt - 4;
			if(sInt < 0){
				session->errMSG = "Item can not be loaded into a play right now.\n";
				return rError;
			}
			instance = &mixEngine->ins[sInt];
			session->lastUID = newUID;
			session->lastPlayer = sInt;
			instance->status = instance->status | status_deleteWhenDone;
			instance->requested = instance->requested | change_play;
			return rOK;
		}else{
			// Not a UID.  Handle as a URL
			sInt = -1;  // load from next available player
			newUID = 0;

			url = strtok_r(NULL, " ", &session->save_pointer);
			
			// pre-load metadata pairs, if any
			// example keys used to override defaults when loading a player: 
			// def_bus, Volume, FadeOut, fx_config
			while(key = strtok_r(NULL, " ", &session->save_pointer)){
				val = strtok_r(NULL, " ", &session->save_pointer);
				if(val){
					if(newUID == 0){
						newUID = createMetaRecord(url, NULL, 0);
						GetURLMetaData(newUID, url);
					}
					SetMetaData(newUID, key, val);
				}else
					break;
			}
			if(newUID = LoadPlayer(&sInt, url, newUID, 0)){
				if(sInt < 0){
					session->errMSG = "Item can not be loaded into a play right now.\n";
					return rError;
				}
				instance = &mixEngine->ins[sInt];
				session->lastUID = newUID;
				session->lastPlayer = sInt;
				instance->status = instance->status | status_deleteWhenDone;
				instance->requested = instance->requested | change_play;
				return rOK;

			}else{
				session->errMSG = "Failed to load player.  Possible bad URL?\n";
				return rError;
			}
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_logmeta(ctl_session *session){
	char *param;
	char *end;
	int sInt;
	uint32_t aLong;

	// first (only) parameter, meta data item UID in hex format
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			aLong = strtoul(param, &end, 16);
		}else{
			// decimal number (pNum, or -1 for last uid)
			if(*param == '$')
				sInt = session->lastPlayer;
			else
				sInt = atoi(param);
			if(sInt < 0){
				aLong = session->lastUID;
			}else{
				aLong = (uint32_t)sInt;
				if(!getPlayerUID(&aLong)){
					session->lastUID = 0;
					session->errMSG = "bad player number.\n";
					return rError;
				}
			}
		}		
		if(updateLogMeta(aLong)){
			session->lastUID = aLong;
			return rOK;
		}
		session->errMSG = "Specified UID not in the log\n";
		return rError;
	}
	session->errMSG = "Missing or bad UID specified\n";
	return rError;
}

unsigned char handle_stat(ctl_session *session){
	char buf[4096]; /* send data buffer */
	char *tmp;
	int tx_length;
	
	// play list status
	if(plRunning)
		tx_length = snprintf(buf, sizeof buf, "ListRev=%u %s\n", (unsigned int)plRev, "Running");
	else
		tx_length = snprintf(buf, sizeof buf, "ListRev=%u %s\n", (unsigned int)plRev, "Stopped");
	my_send(session, buf, tx_length, session->silent, 0);
	
	// last log entry time
	tx_length = snprintf(buf, sizeof buf, "LogTime=%lld\n", (long long)logChangeTime);
	my_send(session, buf, tx_length, session->silent, 0);
	
	// automation status
	pthread_rwlock_rdlock(&queueLock);
	if(autoState == auto_unatt){
		tx_length = snprintf(buf, sizeof buf, "auto=on %s\n", fillStr);
	}else{
		if(autoState == auto_live){
			if((GetMetaInt(0, "auto_live_flags", NULL) & live_fill) && fillStr)
				tx_length = snprintf(buf, sizeof buf, "auto=live %s\n", fillStr);
			else
				tx_length = snprintf(buf, sizeof buf, "auto=live\n");
		}else{
			tx_length = snprintf(buf, sizeof buf, "auto=off\n");
		}
	}
	pthread_rwlock_unlock(&queueLock);
	my_send(session, buf, tx_length, session->silent, 0);
	
	// sip telephone interface status
	if(sipStatus == 2)
		tx_length = snprintf(buf, sizeof buf, "sipPhone=registered\n");
	else if(sipStatus == 1)
		tx_length = snprintf(buf, sizeof buf, "sipPhone=unregistered\n");
	else
		tx_length = snprintf(buf, sizeof buf, "sipPhone=off\n");
	my_send(session, buf, tx_length, session->silent, 0);

	return rNone;
}

unsigned char handle_list(ctl_session *session){
	queueRecord *rec;
	unsigned int i;
	float totalTime;
	char buf[4096]; /* send data buffer */
	int tx_length;
	float segInT;
	float segOutT;
	float fadeT;
	float curPos;
	char *tmp;
	char *Name;
	char *dur;
	char *type;
	uint32_t status;
	
	// dump the entire play list
	i = 0;
	tx_length = snprintf(buf, sizeof buf, "index\tstatus\tpNum\tmeta-UID\tRev\ttype\tdur\tsegin\tsegout\ttotal\tname\n");
	my_send(session, buf, tx_length, session->silent, 0);
	totalTime = 0.0;
	
	pthread_rwlock_rdlock(&queueLock);
	rec = (queueRecord *)&queueList;
	while(rec = (queueRecord *)getNextNode((LinkedListEntry *)rec)){
		segInT = 0.0;
		segOutT = 0.0;
		status = getQueueRecStatus(rec, NULL);
		Name = GetMetaData(rec->UID, "Name", 0);
		dur =  GetMetaData(rec->UID, "Duration", 0);
		type = GetMetaData(rec->UID, "Type", 0);
		segOutT = GetMetaFloat(rec->UID, "SegOut", NULL);
		if(segOutT == 0.0){
			segOutT = atof(dur);
		}
		
		fadeT = GetMetaFloat(rec->UID, "FadeOut", NULL);
		if((fadeT > 0.0) && (fadeT < segOutT))
			segOutT = fadeT;
		
		curPos = 0.0;
		if(rec->player){
			inChannel *instance;
			if(checkPnumber(rec->player-1)){
				instance = &mixEngine->ins[rec->player-1];
				curPos = instance->pos;
				// if loaded into a player, use the more accurite actual position segue-out/fade
				segOutT = instance->posSeg;
				if(instance->fadePos)
					segOutT = instance->fadePos;
			}
		}
		if(segOutT > curPos)
			segOutT = segOutT - curPos;
		if((status & status_playing) != 0)
			totalTime = segOutT;
		else
			totalTime = totalTime + segOutT;
		segInT = GetMetaFloat(rec->UID, "SegIn", NULL);
		if((status & status_hasPlayed) == 0){
			if(i > 0)
				totalTime = totalTime - segInT;
		}

		tx_length = snprintf(buf, sizeof buf, "%u\t%u\t%d\t%08x\t%u\t%s\t%s\t%.1f\t%.1f\t%.1f\t%s\n", 
					i, status, rec->player-1, (unsigned int)rec->UID, 
					GetMetaRev(rec->UID), type, dur, segInT, segOutT, totalTime, Name);
		my_send(session, buf, tx_length, session->silent, 0);
		
		free(Name);
		free(dur);
		free(type);
		i++;
	}
	pthread_rwlock_unlock(&queueLock);

	return rNone;
}

unsigned char handle_add(ctl_session *session){
	char *param;
	char *end;
	char buf[4096]; /* send data buffer */
	uint32_t aLong, idx;
	int aInt;
	int bInt;
	int tx_length;

	aLong = 0;
	// first parameter, position (-1 for end of list) or UID
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 4){
			// hex number (UID)
			aLong = strtoul(param, &end, 16);
			pthread_rwlock_rdlock(&queueLock);
			if(!findNode((LinkedListEntry *)&queueList, aLong, &idx, NULL)){
				pthread_rwlock_unlock(&queueLock);
				session->errMSG = "Insertion point UID is not in play list.\n";
				return rError;
			} 
			pthread_rwlock_unlock(&queueLock);
			aInt = idx - 1;
		}else{
			// decimal number (position)
			aInt = atoi(param);
			if(aInt == -2){
				// add at the position of the next item to be play
				aInt = queueGetNextSegPos(NULL);
			}
		}
		// second parameter, URL or preloaded player number to add
		if(session->save_pointer != NULL){
			if(isdigit(*session->save_pointer)){
				// second param: player number
				bInt = atoi(session->save_pointer);
				if(!checkPnumber(bInt)){
					session->errMSG = "Bad player number.\n";
					return rError;
				}
				AddPlayer(aInt, bInt);
				return rOK;

			}else{
				// Second Param: URL
				if(aLong = AddItem(aInt, session->save_pointer, "", 0)){
					session->lastUID = aLong;
					tx_length = snprintf(buf, sizeof buf, "UID=%08x\n", (unsigned int)aLong);
					my_send(session, buf, tx_length, session->silent, 0);
					return rNone;
				}
			}
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_delete(ctl_session *session){
	char *param;
	char *end;
	uint32_t aLong;
	int sInt;
	queueRecord *rec;

	// first parameter, position or UID
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 4){
			// hex number (UID)
			aLong = strtoul(param, &end, 16);
			if(!releaseQueueEntry(aLong)){
				session->errMSG = "Specified UID is not in queue list.\n";
				return rError;
			}
			session->lastUID = aLong;
			return rOK;
		}
		// decimal number (list index)
		sInt = atoi(param);
		if(sInt >= 0){
			pthread_rwlock_wrlock(&queueLock);
			if(rec = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, sInt+1))
				releaseQueueRecord((queueRecord	*)&queueList, rec, 0);
			pthread_rwlock_unlock(&queueLock);
			return rOK;
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_move(ctl_session *session){
	char *param;
	char *end;
	int aInt;
	int bInt;
	uint32_t aLong;
	uint32_t bLong;

	aLong = 0;
	bLong = 0;

	// first parameter, item/position from
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 4){
			// hex number (UID)
			aLong = strtoul(param, &end, 16);
		}else{
			// decimal number (index)
			aInt = atoi(param);
		}

		// second parameter, new position (-1 for end of list)
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			if(strlen(param) > 4){
				// hex number (UID)
				bLong = strtoul(param, &end, 16);
			}else{
				// decimal number (index)
				bInt = atoi(param);
			}
			
			pthread_rwlock_wrlock(&queueLock);
			if(aLong){
				if(!findNode((LinkedListEntry *)&queueList, aLong, &aInt, NULL)){
					pthread_rwlock_unlock(&queueLock);
					session->errMSG = "Move from UID or index not in play list.\n";
					return rError;				
				}
				aInt--;
			}
			if(bLong){
				if(!findNode((LinkedListEntry *)&queueList, bLong, &bInt, NULL)){
					pthread_rwlock_unlock(&queueLock);
					session->errMSG = "Move to UID not in play list.\n";
					return rError;
				}
				bInt--;
			}
			MoveItem(aInt, bInt, 1);
			pthread_rwlock_unlock(&queueLock);
			
			notifyData	data;
			data.reference = 0;
			data.senderID = getSenderID();
			data.value.iVal = 0;
			notifyMakeEntry(nType_status, &data, sizeof(data));
			return rOK;
		}
	}
	session->errMSG = "Bad or missing parameter.\n";
	return rError;
}

unsigned char handle_uadd(ctl_session *session){
	queueRecord *instance;
	char *param;
	char *end, *tmp;
	char buf[4096]; /* send data buffer */
	uint32_t aLong;
	int aInt;
	int tx_length;
	
	// first parameter, position (-1 for end of list) or UID
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 4){
			// hex number (UID)
			aLong = strtoul(param, &end, 16);
			if(!getQueuePos(&aLong)){
				session->errMSG = "Insertion point UID is not in play list.\n";
				return rError;
			}
			aInt = (int)aLong;
		}else{
			// decimal number (position)
			aInt = atoi(param);
			if(aInt == -2){
				// add at the position of the current next item to play
				aInt = queueGetNextSegPos(NULL);
			}
		}
		// second parameter, URL
		if(session->save_pointer != NULL){
			// search to see if it's already in the list
			aLong = 0;
			instance = (queueRecord *)&queueList;
			pthread_rwlock_rdlock(&queueLock);
			while(instance = (queueRecord *)getNextNode((LinkedListEntry *)instance)){
				if(instance->UID){
					tmp = GetMetaData(instance->UID, "URL", 0);
					if(!strcmp(tmp, session->save_pointer)){
						aLong = instance->UID;
						free(tmp);
						break;
					}
					free(tmp);
				}
			}
			pthread_rwlock_unlock(&queueLock);

			if(aLong == 0)			
				// not already in the list... add it
				aLong = AddItem(aInt, session->save_pointer, "", 0);
			if(aLong){
				session->lastUID = aLong;
				tx_length = snprintf(buf, sizeof buf, "UID=%08x\n", (unsigned int)aLong);
				my_send(session, buf, tx_length, session->silent, 0);
				return rNone;
			}
		}
	}
	session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_split(ctl_session *session){
	char *param;
	char *end;
	char buf[4096]; /* send data buffer */
    uint32_t aLong;
	int tx_length;

	// first parameter, uid of list item to split
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			aLong = strtoul(param, &end, 16);
		}else{
			// assume we want the last UID used in this session
			aLong = session->lastUID;
		}
		// second parameter, URL for split child
		if(session->save_pointer != NULL){
			// Second Param: URL
			aLong = SplitItem(aLong, session->save_pointer, 1);
			if(aLong){
				session->lastUID = aLong;
				tx_length = snprintf(buf, sizeof buf, "UID=%08x\n", (unsigned int)aLong);
				my_send(session, buf, tx_length, session->silent, 0);
				return rNone;
			}
		}
	}
	session->errMSG = "Missing or bad parameter.\n";
	return rError;
}

unsigned char handle_expand(ctl_session *session){
	char *param;
	char *end;
	char *tmp;
	uint32_t aLong, pos;
	queueRecord *instance;

	// first parameter, uid of list item (playlist) to expand
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			aLong = strtoul(param, &end, 16);
		}else{
			// assume we want the last UID used in this session
			aLong = session->lastUID;
		}
		
		tmp = GetMetaData(aLong, "Type", 0);
		if(strcmp(tmp, "filepl") && strcmp(tmp, "playlist")){
			free(tmp);
			session->errMSG = "Item is not a playlist.\n";
			return rError;
		}
		free(tmp);

		pos = aLong;
		if(!getQueuePos(&pos)){
			session->errMSG = "item is not in play list.\n";
			return rError;
		}
		
		session->lastUID = aLong;
		plTaskRunner(aLong);
		return rOK;
	}else
		session->errMSG = "Missing parameter.\n";
	return rError;
}

unsigned char handle_getuid(ctl_session *session){
	char buf[16]; /* send data buffer */
    int tx_length;
	char *param;
	uint32_t uid;
	
	// search for the specified item in the metadata list
	uid = 0;
	if(session->save_pointer != NULL){		
		// first parameter: metadata property string
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			// second parameter: value string to match
			if(session->save_pointer != NULL){
				if(uid = FindUidForKeyAndValue(param, session->save_pointer, 0)){
					tx_length = snprintf(buf, sizeof buf, "%08x\n", uid);
					my_send(session, buf, tx_length, session->silent, 0);
					session->lastUID = uid;
					return rOK; 
				}else{
					session->lastUID = 0;
					session->errMSG = "UID record with key of given value not found.\n";
					return rError;
				}
			}
		}
	}
	session->lastUID = 0;
	session->errMSG = "Missing or bad parameter\n";
	return rError;
}

unsigned char handle_inuid(ctl_session *session){
	char buf[16]; /* send data buffer */
	int tx_length;
	char *url;
	uint32_t uid;
	
	// search for the specified input:///<name> url in the metadata list
	uid = 0;
	if(session->save_pointer != NULL){		
		// first parameter: input name string
		url = uriEncode(session->save_pointer);
		str_insertstr(&url, "input:///", 0);
		if(uid = FindUidForKeyAndValue("URL", url, 0)){
			tx_length = snprintf(buf, sizeof buf, "%08x\n", uid);
			my_send(session, buf, tx_length, session->silent, 0);
			session->lastUID = uid;
			return rOK; 
		}else{
			session->lastUID = 0;
			session->errMSG = "Specified input not found.\n";
			return rError;
		}
	}
	session->lastUID = 0;
	session->errMSG = "Missing or bad parameter\n";
	return rError;
}

unsigned char handle_waitseg(ctl_session *session){
	char *param;
	char *end;
	uint32_t aLong;
	uint32_t pos;
	int next;
	int sInt;
	
	// first parameter, metadata item UID in hex format of the list item beyond 
	// which any item that the list starts playing will break the wait loop
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			aLong = strtoul(param, &end, 16);
		}else{
			// decimal number (pNum)
			sInt = atoi(param);
			if(sInt < 0){
				aLong = session->lastUID;
			}else{
				aLong = (uint32_t)sInt;
				if(!getPlayerUID(&aLong)){
					session->errMSG = "Bad player number.\n";
					return rError;
				}
			}
		}
	
		pthread_mutex_lock(&lastsegMutex);
		while(run){
			pthread_cond_wait(&lastsegSemaphore, &lastsegMutex );
			next = queueGetNextSegPos(NULL);
			pos = aLong;
			if(!getQueuePos(&pos) || ((unsigned)next > pos)){
				// break out of wait loop
				break;
			}
		}
		pthread_mutex_unlock(&lastsegMutex);
	}else{
		session->errMSG = "Missing parameter.\n";
		return rError;
	}
	return rOK;
}

unsigned char handle_segnow(ctl_session *session){
	int thisPlayer;
	inChannel *instance;

	queueGetNextSegPos(&thisPlayer);
	if(checkPnumber(thisPlayer)){
		instance = &mixEngine->ins[thisPlayer];	
		if(instance->status & status_standby){
			instance->fadePos = instance->pos;
			return rOK;
		}
	}
	session->errMSG = "Nothing cued next.\n";
	return rError;
}

unsigned char handle_segall(ctl_session *session){
	inChannel *instance;
	int i;

	// search all players, fade items that are found to be playing and managed
	for(i=0;i<mixEngine->inCount;i++){
		instance = &mixEngine->ins[i];
		if((instance->status & status_playing) && ((instance->status & status_cueing) == 0)){
			// currently playing and not in cue... fade it!
			instance->fadePos = instance->pos;
		}
	}	
	return rOK;
}

unsigned char handle_fadeprior(ctl_session *session){
	int i, count;
	inChannel *instance;
	queueRecord *rec;
	uint32_t status;
	
	count = queueCount();
	if(count > mixEngine->inCount)
		count = mixEngine->inCount;
	i = count;
	pthread_rwlock_rdlock(&queueLock);
	while(--i >= 0){
		if(rec = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, i+1)){
			if(getQueueRecStatus(rec, NULL) & status_hasPlayed){
				// this is the most recent playing list item
				// don't fade this one, but keep going back and fade earlier items in players
				while(--i >= 0){
					if(rec = (queueRecord *)getNthNode((LinkedListEntry *)&queueList, i+1)){
						status = getQueueRecStatus(rec, &instance);		
						if(instance && instance->managed && ((status & status_playing) != 0) && ((status & status_cueing) == 0)){
							// currently playing and not in cue... fade it!
							instance->segNext = 0;
							instance->fadePos = instance->pos;
						}
					}
				}
				break;
			}
		}else
			break;
	}
	pthread_rwlock_unlock(&queueLock);
	return rOK;
}

unsigned char handle_modbuspoll(ctl_session *session){
	// modbuspoll period IP-address UnitID InputAddress config_file
	
	struct modbusPollRec *rec;
	char *param, *addr, *conf;
	char *end;
	char *tmp;
	int period;
	unsigned char unitID;
	unsigned short inputID;
	unsigned long aInt;
	taskRecord *task;
	
	// first parameter, poll period
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		period = atoi(param);
		if(period > 0){
			// second parameter, ip-address of unit
			addr = strtok_r(NULL, " ", &session->save_pointer);
			if(addr != NULL){
				// third parameter, unitID
				param = strtok_r(NULL, " ", &session->save_pointer);
				if(param != NULL){
					aInt = strtoul(param, &end, 16);
					if(aInt <= 0xFF){
						unitID = aInt;
						// fourth parameter, inputID
						param = strtok_r(NULL, " ", &session->save_pointer);
						if(param != NULL){
							aInt = strtoul(param, &end, 16);
							if(aInt <= 0xFFFF){
								inputID = aInt;
								// fifth parameter, associated config file name.
								conf = strtok_r(NULL, " ", &session->save_pointer);
								if(conf != NULL){
									rec = (struct modbusPollRec*)malloc(sizeof(struct modbusPollRec));
									rec->period = period;
									rec->unitID = unitID;
									rec->inputID = inputID;
									strncpy(rec->addr, addr, sizeof(rec->addr));
									strncpy(rec->conf, conf, sizeof(rec->conf));

									tmp = strdup("modbus poll:");
									str_appendstr(&tmp, conf);
									createTaskItem(tmp, modbusPoll, rec, 0, 0, 0, 0); // no timeout
									free(tmp);

									return rOK;
								}
							}
						}
					}
				}
			}
		}
	}

	session->errMSG = "Missing or bad parameter\n";
	return rError;
}

unsigned char handle_coilset(ctl_session *session, unsigned char val){
	char *param, *addr;
	char *end;
	char *tmp;
	unsigned long aInt;
	unsigned short *sptr;
	struct modbusData packet, reply;
	int sock;
	char *dest;
	size_t size, limit;
	int rx_length;
	struct sockaddr_in  adrRec;
	struct timeval tv;
	
	// first parameter, ip-address of unit
	addr = strtok_r(NULL, " ", &session->save_pointer);
	if(addr != NULL){
		if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) >= 0){
			tv.tv_sec = 1;  
			tv.tv_usec = 0;  
			setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tv, sizeof(struct timeval));
			setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (struct timeval *)&tv, sizeof(struct timeval));
			bzero(&adrRec, sizeof(adrRec));
			adrRec.sin_family = AF_INET;
			adrRec.sin_port = htons(502);
			
			if(inet_pton(AF_INET, addr, &adrRec.sin_addr.s_addr) > 0){
				// second parameter, UnitID
				param = strtok_r(NULL, " ", &session->save_pointer);
				if(param != NULL){
					bzero(&packet, sizeof(packet));
					aInt = strtoul(param, &end, 16);
					if(aInt < 256){
						packet.unit = aInt;
						// third parameter, AddressID
						param = strtok_r(NULL, " ", &session->save_pointer);
						if(param != NULL){
							aInt = strtoul(param, &end, 16);
							if(aInt <= 0xFFFF){
								sptr = (unsigned short *)&packet.data[0];
								*sptr = htons(aInt);
								
								// fill in other packet values
								packet.function = 5;
								if(val) 
									aInt = 0xff00;
								else
									aInt = 0x0000;
								sptr = (unsigned short *)&packet.data[2];
								*sptr = htons(aInt);
								packet.length = 6;

								if(connect(sock, (struct sockaddr *)&adrRec, sizeof(adrRec)) == 0){
									write(sock, &packet, packet.length + 6);
									// get reply
									dest = (char *)(&reply);
									size = 0;
									bzero(&reply, sizeof(reply));
									limit = 6; // start by reading the header only
									while((rx_length = read(sock, dest+size, limit-size)) > 0){
										size = size + rx_length;
										if(size == 6){
											if((reply.pading != 0) || (reply.length != packet.length)){
												tmp = strdup("[session] handle_coilset-Modbus Send: Bad reply length from ");
												str_appendstr(&tmp, addr);
												serverLogMakeEntry(tmp);
												free(tmp);
												break;
											}
											limit = reply.length + 6; // now read to the end of the packet only
										}
										if(size == limit){
											// we have a whole packet
											if(memcmp(&packet, &reply, size)){
												tmp = strdup("[session] handle_coilset-Modbus Send: Reply mismatch from ");
												str_appendstr(&tmp, addr);
												serverLogMakeEntry(tmp);
												free(tmp);
											}
											break;
										}
									}
										
									if(rx_length < 0){
										tmp = strdup("[session] handle_coilset-Modbus Send: No reply from ");
										str_appendstr(&tmp, addr);
										serverLogMakeEntry(tmp);
										free(tmp);
									}
									close(sock);
									return rOK;
								}else{
									tmp = strdup("[session] handle_coilset-Modbus Send: Connection failed to ");
									str_appendstr(&tmp, addr);
									serverLogMakeEntry(tmp);
									free(tmp);
									close(sock);
									return rOK;
								}
							}
						}
					}
				}
			}
			close(sock);
		}
	}
	session->errMSG = "Missing or bad parameter\n";
	return rError;
}

unsigned char handle_rstat(ctl_session *session){
	char buf[4096]; /* send data buffer */
	int tx_length;
	unsigned char empty;
	unsigned int i;
	uint32_t UID;
	int status;
	float pos, vol;
	time_t when;
	char *name;
	
	// recorders status
	tx_length = snprintf(buf, sizeof buf, "encoder\t\tstatus\ttime\tlimit\tpersist\tgain\tname\n");
	my_send(session, buf, tx_length, session->silent, 0);
	i = 0;
	while(UID = FindUidForKeyAndValue("Type", "encoder", i)){
		name = GetMetaData(UID, "Name", 0);
		if(!strlen(name))
			str_setstr(&name, "<unnamed>");
		status = GetMetaInt(UID, "Status", NULL);
		pos = GetMetaFloat(UID, "Position", NULL);
		if((status & rec_running) && (when = GetMetaInt(UID, "TimeStamp", NULL)))
			pos = pos + (time(NULL) - when);
		vol = GetMetaFloat(UID, "Volume", &empty);
		if(empty)
			vol = 1.0;
		tx_length = snprintf(buf, sizeof buf, "%08x\t%d\t%.1f\t%u\t%u\t%.3f\t%s\n", UID, status, pos, (unsigned int)GetMetaInt(UID, "Limit", NULL), 
								(unsigned int)GetMetaInt(UID, "Persistent", NULL), GetMetaFloat(UID, "Volume", NULL), name);
		free(name);
		my_send(session, buf, tx_length, session->silent, 0);
		i++;
	}
	return rNone;
}

unsigned char handle_startrec(ctl_session *session){
	char *param;
	char *tmp, *end;
	int sInt;
	int status;
	uint32_t uid = 0;

	// first (only) parameter, meta data item UID in hex format
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			uid = strtoul(param, &end, 16);
		}else{
			// decimal number (-1 for last uid)
			sInt = atoi(param);
			if(sInt < 0){
				uid = session->lastUID;
			}
		}
		if(uid){
			tmp = GetMetaData(uid, "Type", 0);
			if(!strcmp(tmp, "encoder")){
				free(tmp);
				status = GetMetaInt(uid, "Status", NULL);
				if(status & rec_locked){
					session->errMSG = "Encoder is locked\n";
					return rError;
				}
				if(queueControlOutPacket(mixEngine, cPeer_recorder | cType_start, uid, 0, NULL)){
					session->lastUID = uid;
					return rOK; 
				}
			}else
				free(tmp);
		}
	}
	session->errMSG = "Missing or bad parameter\n";
	return rError;
}

unsigned char handle_stoprec(ctl_session *session){
	char *param;
	char *tmp, *end;
	int sInt;
	int status;
	uint32_t uid = 0;

	// first (only) parameter, meta data item UID in hex format
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			uid = strtoul(param, &end, 16);
		}else{
			// decimal number (-1 for last uid)
			sInt = atoi(param);
			if(sInt < 0){
				uid = session->lastUID;
			}
		}
		if(uid){
			tmp = GetMetaData(uid, "Type", 0);
			if(!strcmp(tmp, "encoder")){
				free(tmp);
				status = GetMetaInt(uid, "Status", NULL);
				if(status & rec_locked){
					session->errMSG = "Encoder is locked\n";
					return rError;
				}
				if(queueControlOutPacket(mixEngine, cPeer_recorder | cType_stop, uid, 0, NULL)){
					session->lastUID = uid;
					return rOK; 
				}
			}else
				free(tmp);
		}
	}
	session->errMSG = "Missing or bad parameter\n";
	return rError;	
}
unsigned char handle_recgain(ctl_session *session){
	char *param;
	char *tmp, *end;
	int sInt;
	int status;
	valuetype val;
	uint32_t uid = 0;

	// first parameter, meta data item UID in hex format
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			uid = strtoul(param, &end, 16);
		}else{
			// decimal number (-1 for last uid)
			sInt = atoi(param);
			if(sInt < 0){
				uid = session->lastUID;
			}
		}
		// second parameter, volume
		param = strtok_r(NULL, " ", &session->save_pointer);
		if(param != NULL){
			val.fVal = atof(param);
			val.iVal = htonl(val.iVal);
			if(uid){
				tmp = GetMetaData(uid, "Type", 0);
				if(!strcmp(tmp, "encoder")){
					free(tmp);
					status = GetMetaInt(uid, "Status", NULL);
					if(status & rec_locked){
						session->errMSG = "Encoder is locked\n";
						return rError;
					}
					if(queueControlOutPacket(mixEngine, cPeer_recorder | cType_vol, uid, 4, (char *)&(val.iVal))){
						session->lastUID = uid;
						// send out notifications
						notifyData	data;
						data.senderID = 0;
						data.reference = htonl(uid);
						data.value.iVal = val.iVal;
						notifyMakeEntry(nType_rgain, &data, sizeof(data));
						return rOK; 
					}
				}else
					free(tmp);
			}
		}
	}
	session->errMSG = "Missing or bad parameter\n";
	return rError;
}

unsigned char handle_lockrec(ctl_session *session){
	char *param;
	char *tmp, *end;
	int sInt;
	uint32_t uid = 0;

	// first (only) parameter, meta data item UID in hex format
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			uid = strtoul(param, &end, 16);
		}else{
			// decimal number (-1 for last uid)
			sInt = atoi(param);
			if(sInt < 0){
				uid = session->lastUID;
			}
		}
		if(uid){
			tmp = GetMetaData(uid, "Type", 0);
			if(!strcmp(tmp, "encoder")){
				free(tmp);
				if(queueControlOutPacket(mixEngine, cPeer_recorder | cType_lock, uid, 0, NULL)){
					session->lastUID = uid;
					return rOK; 
				}
			}else
				free(tmp);
		}
	}
	session->errMSG = "Missing or bad parameter\n";
	return rError;	
}

unsigned char handle_unlockrec(ctl_session *session){
	char *param;
	char *tmp, *end;
	int sInt;
	uint32_t uid = 0;

	// first (only) parameter, meta data item UID in hex format
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			uid = strtoul(param, &end, 16);
		}else{
			// decimal number (-1 for last uid)
			sInt = atoi(param);
			if(sInt < 0){
				uid = session->lastUID;
			}
		}
		if(uid){
			tmp = GetMetaData(uid, "Type", 0);
			if(!strcmp(tmp, "encoder")){
				free(tmp);
				if(queueControlOutPacket(mixEngine, cPeer_recorder | cType_unlock, uid, 0, NULL)){
					session->lastUID = uid;
					return rOK; 
				}
			}else
				free(tmp);
		}
	}
	session->errMSG = "Missing or bad parameter\n";
	return rError;	
}

unsigned char handle_closerec(ctl_session *session){
	char *param;
	char *tmp, *end;
	int sInt;
	int status;
	uint32_t uid = 0;

	// first (only) parameter, meta data item UID in hex format
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			uid = strtoul(param, &end, 16);
		}else{
			// decimal number (-1 for last uid)
			sInt = atoi(param);
			if(sInt < 0)
				uid = session->lastUID;
		}
		if(uid){
			tmp = GetMetaData(uid, "Type", 0);
			if(!strcmp(tmp, "encoder")){
				free(tmp);
				status = GetMetaInt(uid, "Status", NULL);
				if(status & rec_locked){
					session->errMSG = "Encoder is locked\n";
					return rError;
				}
				if(status){
					if(queueControlOutPacket(mixEngine, cPeer_recorder | cType_end, uid, 0, NULL)){
						session->lastUID = uid;
						return rOK; 
					}
				}else{
					// not initialized... just release the metadata record
					releaseMetaRecord(uid);
					session->lastUID = uid;
					notifyData data;
					data.senderID = 0;
					data.reference = htonl(0);
					data.value.iVal = htonl(0);
					notifyMakeEntry(nType_rstat, &data, sizeof(data));
					return rOK;
				}
			}else
				free(tmp);
		}
	}
	session->errMSG = "Missing or bad parameter\n";
	return rError;	
}

unsigned char handle_rtemplates(ctl_session *session){
	char *tmp;
	char *file;
	int tx_length;
	struct stat path_stat;
	glob_t globbuf;
	
	// get partial path to recorder templates directory
	tmp = GetMetaData(0, "file_rec_template_dir", 0);
	if(strlen(tmp)){
		if(strrchr(tmp, directoryToken) != (tmp + strlen(tmp) -1))
			// no trailing slash... add it
			str_appendchr(&tmp, directoryToken);
	}else
		str_setstr(&tmp, ".audiorack/templates/");
	// see if path points to a valid directory
	if(!stat(tmp, &path_stat) && S_ISDIR(path_stat.st_mode)){
		// add glob wild card for .rec files
		str_appendstr(&tmp, "*.rec");
		globbuf.gl_offs = 0;
		globbuf.gl_pathc = 0;
		if(!glob(tmp, 0, NULL, &globbuf)){
			if(globbuf.gl_pathc){
				unsigned int i;
				for(i=0; i<globbuf.gl_pathc; i++){
					file = strrchr(globbuf.gl_pathv[i], directoryToken);
					if(file){
						file++; // move past dir token
						my_send(session, file, strlen(file), session->silent, 0);
						my_send(session, "\n", 1, session->silent, 0);
					}
				}
				globfree(&globbuf);
			}
			free(tmp);
			return rOK;
		}
	}
	free(tmp);
	session->errMSG = "Error: rec_template_dir invalid, or empty of .rec files.\n";
	return rError;
}

unsigned char handle_newrec(ctl_session *session){
	char buf[32]; /* send data buffer */
	int tx_length;
	uint32_t newUID;
	struct tm tm;
	time_t ut;
	char *tmp;
	
	if(newUID = createMetaRecord(NULL, NULL, 0)){
		time(&ut);
		localtime_r(&ut, &tm);
		strftime(buf, sizeof(buf), "Rec%Y-%m-%d-%H%M%S", &tm);
		SetMetaData(newUID, "Name", buf);
		SetMetaData(newUID, "Type", "encoder");
		SetMetaData(newUID, "Status", "0");
		SetMetaData(newUID, "Pipeline", "");
		
		// get partial path to recorder templates directory
		tmp = GetMetaData(0, "file_rec_template_dir", 0);
		if(strlen(tmp)){
			if(strrchr(tmp, directoryToken) != (tmp + strlen(tmp) - 1))
				// no trailing slash... add it
				str_appendchr(&tmp, directoryToken);
		}else
			str_setstr(&tmp, ".audiorack/templates/");
			
		if(session->save_pointer && strlen(session->save_pointer)){
			// if a second parameter is provided, use name or path to specify the recorder config file 
			if(strcmp(session->save_pointer, "none")){
				if(strchr(session->save_pointer, directoryToken)){
					// full or partial path specified... load the template file as passed
					str_setstr(&tmp, session->save_pointer);
				}else{
					// just a name, make path relative to the templates directory
					str_appendstr(&tmp, session->save_pointer);
				}
			}else{
				// unlesss it's "none", then don't load any config file
				free(tmp);
				tmp = NULL;
			}
		}else{
			// use default recorder template: 
			str_appendstr(&tmp, "default.rec");
		}
		
		session->lastUID = newUID;  // need to set this for config load below

		if(tmp){
			if(!loadConfiguration(session, tmp)){
				// failed to open file
				free(tmp);
				releaseMetaRecord(newUID);
				session->errMSG = "Failed to read specified recorder template\n";
				return rError;
			}
			free(tmp);
		}
		
		tx_length = snprintf(buf, sizeof buf, "UID=%08x\n", (unsigned int)newUID);
		my_send(session, buf, tx_length, session->silent, 0);
		notifyData data;
		data.senderID = 0;
		data.reference = htonl(0);
		data.value.iVal = htonl(0);
		notifyMakeEntry(nType_rstat, &data, sizeof(data));
		return rOK;
	}
	session->errMSG = "Error creating a new encoder UID record.\n";
	return rError;
}

unsigned char handle_initrec(ctl_session *session){
	char *param;
	char *end;
	char *tmp;
	char *name;
	char *pipe;
	int status;
	uint32_t uid;
	
	struct execRec{
		char *argv[16];	// arRecorder takes no more than 14 args + argv[0] run path + NULL at end
		pid_t child;
	} recPtr;
	char *wdir, *rdir;
	char *save_pointer;
	int i, fd, chanCnt;
	char pidStr[256];
	long val;
	float gain;

	// first parameter, encoder UID in hex format
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		uid = 0;
		if(strlen(param) > 2){
			// hex number (UID)
			uid = strtoul(param, &end, 16);
		}else if(atoi(param) == -1){
			// we want the last UID used in this session
			uid = session->lastUID;
		}
		if(uid){
			tmp = GetMetaData(uid, "Type", 0);
			if(!strcmp(tmp, "encoder")){
				free(tmp);
				status = GetMetaInt(uid, "Status", NULL);
				if(status & rec_locked){
					session->errMSG = "Encoder is locked\n";
					return rError;
				}
				rdir = GetMetaData(0, "def_record_dir", 0);
				if(!strlen(rdir)){
					// get working diretory if def_record_dir is empty
					struct passwd pwrec;
					struct passwd* pwPtr;
					char pwbuf[1024];
					free(rdir);
					if(strlen(wdir_path) == 0){
						// Get our effective user's home directory
						if(getpwuid_r(geteuid(), &pwrec, pwbuf, sizeof(pwbuf), &pwPtr) != 0)
							rdir = strdup(pwPtr->pw_dir);
						else
							rdir = strdup("~");
					}else
						rdir = strdup(wdir_path);
				}
				if(strrchr(rdir, directoryToken) != (rdir + strlen(tmp) - 1))
					// no trailing slash... add it
					str_appendchr(&rdir, directoryToken);
				
				// populate argument strings
				tmp = GetMetaData(0, "file_bin_dir", 0);
				if(strlen(tmp)){
					if(strrchr(tmp, directoryToken) != (tmp + strlen(tmp) - 1))
						// no trailing slash... include it
						str_appendstr(&tmp, "/arRecorder4");
					else
						// already has training slash
						str_appendstr(&tmp, "arRecorder4");
					recPtr.argv[0] = tmp;
				}else{
					free(tmp);
					recPtr.argv[0] = strdup("/opt/audiorack/bin/arRecorder4");
				}
				i = 1;
				/* optional settings */
				if(GetMetaInt(uid, "Persistent", NULL)){
					recPtr.argv[i] = strdup("-p");
					i++;
				}
				if(val=GetMetaInt(uid, "Limit", NULL)){
					recPtr.argv[i] = strdup("-l");
					i++;
					recPtr.argv[i] = ustr(val);
					i++;
				}
				if(val=GetMetaInt(uid, "Start", NULL)){
					recPtr.argv[i] = strdup("-s");
					i++;
					recPtr.argv[i] = ustr(val);
					i++;
				}
				tmp=GetMetaData(uid, "MakePL", 0);
				if(strlen(tmp)){
					// handle special rec_dir macro, which doesn't corrispond to a meta value
					str_ReplaceAll(&tmp, "[rec_dir]", rdir);
					// next we convert all string macros into their values
					// by example: MakePL = "[rec_dir][Name].fpl" would create the PL file /the/default/recording/dir/name-of-encoder.fpl
					resolveStringMacros(&tmp, uid);
					recPtr.argv[i] = strdup("-a");
					i++;
					recPtr.argv[i] = tmp;
					i++;
				}else
					free(tmp);
					
				/* required settings */
				if(val=GetMetaInt(uid, "TagBus", NULL)){
					recPtr.argv[i] = strdup("-b");
					i++;
					recPtr.argv[i] = ustr(val);
					i++;
					
					name=GetMetaData(uid, "Name", 0);
					if(strlen(name)){
						recPtr.argv[i] = name;
						i++;
						recPtr.argv[i] = strdup(mixEngine->ourJackName);
						i++;
						recPtr.argv[i] = ustr(uid);
						i++;
						tmp=GetMetaData(uid, "Ports", 0);
						str_ReplaceAll(&tmp, "[ourJackName]", mixEngine->ourJackName);
						if(strlen(tmp)){
							recPtr.argv[i] = tmp;
							i++;
							pipe=GetMetaData(uid, "Pipeline", 0);
							if(strlen(pipe)){
								// handle special rec_dir macro, which doesn't corrispond to a meta value
								str_ReplaceAll(&pipe, "[rec_dir]", rdir);
								chanCnt=1;
								while(*tmp){
									if(*tmp == '&')
										chanCnt++;
									tmp++;
								}
								char *val = istr(chanCnt);
								str_ReplaceAll(&pipe, "[channels]", val);
								free(val);
								// next we conver all pipeline string macros into their values
								resolveStringMacros(&pipe, uid);
								
								recPtr.argv[i] = pipe;
								i++;
								recPtr.argv[i] = NULL;
								// note desired volume (gain) before running
								// this meta value will be cleared to 1.0, and we will need 
								// to restore it.
								gain = GetMetaFloat(uid, "Volume", NULL);
								
								// execute it;
								recPtr.child = fork();
								if(recPtr.child == 0){
									// if we are the forked child
									// set working dir if specified or user home directory
									struct passwd pwrec;
									struct passwd* pwPtr;
									char pwbuf[1024];
									if(strlen(wdir_path) == 0){
										// Get our effective user's home directory
										if(getpwuid_r(geteuid(), &pwrec, pwbuf, sizeof(pwbuf), &pwPtr) != 0)
											wdir = pwPtr->pw_dir;
										else
											wdir = "";
									}else
										wdir = wdir_path;
									if(strlen(wdir) > 0)
										chdir(wdir);
								
									// redirect the standard descriptors to /dev/null
									fd = open("/dev/null", O_RDONLY);
									if(fd != STDIN_FILENO){
										dup2(fd, STDIN_FILENO);
										close(fd);
									}
									fd = open("/dev/null", O_WRONLY);
									if(fd != STDOUT_FILENO) {
										dup2(fd, STDOUT_FILENO);
										close(fd);
									}
									fd = open("/dev/null", O_WRONLY);
									if(fd != STDERR_FILENO) {
										dup2(fd, STDERR_FILENO);
										close(fd);
									}
									// close all other file descriptors
									for(fd=(getdtablesize()-1); fd >= 0; --fd){
										if((fd != STDERR_FILENO) && (fd != STDIN_FILENO) && (fd != STDOUT_FILENO))
											close(fd); // close all descriptors we are not interested in
									}
									// unblock all signals and set to default handlers
									sigset_t sset;
									sigemptyset(&sset);
									pthread_sigmask(SIG_SETMASK, &sset, NULL);
									// obtain a new process group 
									setsid();
						 
									// and run...
									execvp(recPtr.argv[0], recPtr.argv);
									// we should never get here
									exit(0);

								}
								if(recPtr.child > 0){
									/* parent continues here */
									i = 0;
									while(recPtr.argv[i]){
										free(recPtr.argv[i]);
										i++;
									}
									free(rdir);
									SetMetaData(uid, "Status", "1");	// set status to loading
									sleep(2);
									if(waitpid(recPtr.child, NULL, WNOHANG)){
										/* failed to run or ran and quit due to an error */
										SetMetaData(uid, "Status", "0");	// set status back to empty
										session->errMSG = "Failed to run arRecorder or ran and quit due to a setting error\n";
										if(GetMetaInt(uid, "Persistent", NULL))
											releaseMetaRecord(uid);
										return rError;
									}else{
										/* still running after 2 seconds */
										valuetype val;
										/* restore recorder gain, if Volume setting was non-zero prior to running */
										if(val.fVal = gain){
											val.iVal = htonl(val.iVal);
											queueControlOutPacket(mixEngine, cPeer_recorder | cType_vol, uid, 4, (char *)&(val.iVal));
										}
										/* all done */
										return rOK;
									}
								}
								
							}else
								free(pipe);
						}else
							free(tmp);
						
					}else
						free(name);	
				}
				/* failed */
				free(rdir);
				while(i--)
					free(recPtr.argv[i]);
			}else
				free(tmp);
		}
	}
	session->errMSG = "Missing or bad parameter.\n";
	return rError;
}

unsigned char handle_jsonpost(ctl_session *session){
	uint32_t i;
	int count;
	char *param;
	char *end;
	uint32_t aLong;
	
	// first parameter, encoder UID in hex format, or zero for all, or -1 for lastUid
	param = strtok_r(NULL, " ", &session->save_pointer);
	if(param != NULL){
		if(strlen(param) > 2){
			// hex number (UID)
			aLong = strtoul(param, &end, 16);
		}else{
			// decimal intereger number (zero or -1)
			if((aLong = atoi(param)) < 0)
				// use the last UID used in this session
				aLong = session->lastUID;
			else 
				// send to all RSP encoders
				aLong = 0;
		}
		// second parameter, json object in non-printable (no LF/CR) text format to queue into rsp streamers
		count = 0;
		if(session->save_pointer){
			if(aLong){
				char *tmp = GetMetaData(aLong, "Type", 0);
				if(!strcmp(tmp, "encoder")){
					free(tmp);
					if(queueControlOutPacket(mixEngine, cPeer_recorder | cType_tags, aLong, strlen(session->save_pointer), session->save_pointer))
						return rOK;
					else{
						session->errMSG = "jSON string is >2048 bytes.\n";
						return rError;		
					}
				}	
				free(tmp);
				session->errMSG = "Specified UID is not an encoder.\n";
				return rError;	
			}else{
				if(queueControlOutPacket(mixEngine, cPeer_allrec | cType_tags, 0, strlen(session->save_pointer), session->save_pointer))
					return rOK;
				else{
					session->errMSG = "jSON string is >2048 bytes.\n";
					return rError;		
				}
			}
		}
	}
	session->errMSG = "Missing or bad parameter.\n";
	return rError;	
}

