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

#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "tasks.h"
#include "utilities.h" 
#include "session.h"
#include "data.h"

void taskAddSelf(taskRecord *rec){
	// note time started
	rec->started = time(NULL);
	// hook into task list
	pthread_rwlock_wrlock(&taskLock);
	rec->next = taskList;
	taskList = rec;
	pthread_rwlock_unlock(&taskLock);
}

void taskCleanUp(void *pass){
	taskRecord *rec = (taskRecord *)pass;
	rec->finished = 1;
	if(rec->UID){
		releaseMetaRecord(rec->UID);
		releaseQueueEntry(rec->UID);
	}
	pthread_rwlock_wrlock(&taskLock);
	// the function below will also free task name and userData, if set.
	releaseTaskRecord((taskRecord *)&taskList, rec);
	pthread_rwlock_unlock(&taskLock);
}

void *taskCallProcInThread(void *inRefCon){
	taskRecord *rec = (taskRecord *)inRefCon;
	uint32_t passUID;
	queueRecord *qnode;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);	// assume task is not cancalable
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);	// the task itself can change this if is can be canceled

	taskAddSelf(rec);

	pthread_cleanup_push((void (*)(void *))taskCleanUp, (void *)rec);

	// set running flag if found in playlist
	pthread_rwlock_wrlock(&queueLock);
	if(qnode = (queueRecord *)findNode((LinkedListEntry *)&queueList, rec->UID, NULL, NULL))
		qnode->status = qnode->status | status_running;
	pthread_rwlock_unlock(&queueLock);

	// call the requester function/procedure
	(*rec->Proc)(rec);

	// clear running flag, and remove, if found in playlist
	pthread_rwlock_wrlock(&queueLock);
	if(qnode = (queueRecord *)findNode((LinkedListEntry *)&queueList, rec->UID, NULL, NULL))
		qnode->status = qnode->status & ~status_running;
	pthread_rwlock_unlock(&queueLock);

	pthread_cleanup_pop(1);
	return NULL;
}

void initTaskList(void){
	taskList = NULL;
	pthread_rwlock_init(&taskLock, NULL);
}

void freeTaskList(void){
	/* this could crash if called on running tasks.  Only call at shutdown */
	taskRecord *rec;

	while(rec = taskList)
		releaseTaskRecord((taskRecord *)&taskList, rec);
	pthread_rwlock_destroy(&dataLock);
}

void createTaskItem(char *theName, void (*theProc)(void *), void *usrDataPtr, uint32_t theUID, int16_t thePlayer, uint32_t theLifeTime, unsigned char allowDelete){

	taskRecord *rec;
	uidRecord *uid_rec;
	/* name is copied and release by the task */
	/* If userData pptr is NOT NULL, it will be freed when task finishes */
	if(rec = (taskRecord *)calloc(1, sizeof(taskRecord))){
		rec->refCnt = 1;
		rec->finished = 0;
		rec->pid = 0;
		rec->name = strdup(theName);
		rec->userData = usrDataPtr;
		rec->Proc = theProc;
		rec->player = thePlayer;
		rec->UID = theUID;
		if(rec->UID){
			retainMetaRecord(rec->UID);
			rec->delUID = allowDelete;
		}else{
			rec->delUID = 0;
		}
		rec->timeOut = theLifeTime;
		rec->cancelThread = 0;

		retainListRecord((LinkedListEntry *)rec);
		pthread_create(&rec->thread, NULL, &taskCallProcInThread, rec);
		rec->taskID = (uint32_t)rec->thread;
		pthread_detach(rec->thread);
		releaseTaskRecord((taskRecord *)&taskList, rec);
	}
}

//********************** Tasks routines here ************************//

void executeCleanUp(void *pass){
	free(pass);
}

void ExecuteCommand(void *refIn){
	char *command, *line, *start, *frag;
	ctl_session session;
	taskRecord *parent = (taskRecord *)refIn;

	session.lastPlayer = parent->player;
	session.lastUID = parent->UID;
	session.lastAID = 0;
	session.cs = 0;
	session.silent = 1;

	command =(char*)(parent->userData);
	pthread_cleanup_push((void (*)(void *))executeCleanUp, (void *)command);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);	// task is non-cancalable
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

	start = command;
	// replace any '\r' chars with '\n'
	// (deal with realbasic stupidity until I can get the UI re-writen in objective-c)
	session.save_pointer = command;
	while(session.save_pointer = strchr(session.save_pointer, '\r'))
		*session.save_pointer = '\n';
	while(line = strtok_r(start, "\n", &frag)){
		// line delimitor found in the string
		start = NULL;
		processCommand(&session, line, NULL);
		// check for delete cancelation
	}
	pthread_cleanup_pop(1);
}

void ExecuteProcess(char *command, uint32_t UID, uint32_t timeOut){
	struct execRec{
		char **argv;
		char *str;
		pid_t child;
	} *recPtr;
	char *wdir;
	char *save_pointer;
	int i, fd;
	char pidStr[256];

	if(command && strlen(command)){
		// allocate record for execution var's
		recPtr = (struct execRec *)calloc(1, sizeof(struct execRec));

		// make copy of c string
		recPtr->str = strdup(command);
		// make array for holding arguments
		i = str_CountFields(command, " ");

		recPtr->argv = (char **)malloc(sizeof(char*) * (i+2));
		//parse string for spaces, replace with nulls, and fill argv array with pointers to each parsed segment
		i = 0;
		recPtr->argv[i] = strtok_r(recPtr->str, " ", &save_pointer);
		while(recPtr->argv[i]){
			i++;
			recPtr->argv[i] = strtok_r(NULL, " ", &save_pointer);
		}

		// execute it;
		recPtr->child = fork();
		if(recPtr->child == 0){
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
			execvp(recPtr->argv[0], recPtr->argv);
			free(recPtr->str);
			free(recPtr->argv);
			free(recPtr);
			exit(0);

		}
		if(recPtr->child > 0){
 			snprintf(pidStr, sizeof(pidStr), "%s:%i", recPtr->argv[0], recPtr->child);
			createTaskItem(pidStr, WaitPID, (void *)recPtr, UID, -1, timeOut, 1);
			return;
		}
		free(recPtr->argv);
		free(recPtr->str);
		free(recPtr);
	}
}

void WaitPID(void *refIn){
	struct execRec{
		char **argv;
		char *str;
		pid_t child;
	} *recPtr;
	taskRecord *parent = (taskRecord *)refIn;

	recPtr = (struct execRec*)parent->userData;
	parent->pid = recPtr->child;

	// wait for child process to complete
	while(waitpid(recPtr->child, NULL, WNOHANG) == 0)
		sleep(5);
	// all done - clean up and return
	free(recPtr->argv);
	free(recPtr->str);
	// recPtr itself will be freed by the cleanup function
}

void loadConfigFromTask(void *refIn){
	//  How to call this function:
	//	createTaskItem(filePath, loadConfigFromTask, NULL, uid, pNum, 0L, false); // no timeout

	ctl_session session;
	taskRecord *parent = (taskRecord *)refIn;

	session.cs = 0;
	session.silent = 1;
	session.lastAID = 0;
	session.lastPlayer = parent->player;
	session.lastUID = parent->UID;
	if(strlen(parent->name) > 0)
		loadConfiguration(&session, parent->name);
}

void modbusTrigger(unsigned char state, char *conf){
	char *trig_path;

	trig_path = GetMetaData(0, "file_trigger_dir", 0);
	if(strlen(trig_path) > 0){
		if(trig_path[strlen(trig_path)-1] != '/'){
			// add trailing slash
			str_appendchr(&trig_path, '/');
		}
		str_appendstr(&trig_path, conf);
		if(state){
			str_appendstr(&trig_path, ".on");
			createTaskItem(trig_path, loadConfigFromTask, NULL, 0, 0, 0, 0); // no timeout
		}else{
			str_appendstr(&trig_path, ".off");
			createTaskItem(trig_path, loadConfigFromTask, NULL, 0, 0, 0, 0); // no timeout
		}
	}
	free(trig_path);
}

char modbusQuery(int *sock, char *addr, unsigned char unitID, unsigned short inputID){
	unsigned short *sptr;
	struct modbusData packet, reply;
	char *dest;
	size_t size, limit;
	int rx_length;
	struct sockaddr_in  adrRec;
	struct timeval tv;

	// format request packet
	bzero(&packet, sizeof(packet));
	packet.unit = unitID;
	packet.function = 2;						// Read Discrete Input modbus function code
	sptr = (unsigned short *)&packet.data[0];
	*sptr = htons(inputID);						// input address
	sptr = (unsigned short *)&packet.data[2];
	*sptr = htons(1);							// one bit only requested
	packet.length = 6;

	if(*sock < 0){
		// open new connection if one doesn't already exist
		if((*sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) >= 0){
			tv.tv_sec = 5;
			tv.tv_usec = 0;
			setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tv, sizeof(struct timeval));
			setsockopt(*sock, SOL_SOCKET, SO_SNDTIMEO, (struct timeval *)&tv, sizeof(struct timeval));
			bzero(&adrRec, sizeof(adrRec));
			adrRec.sin_family = AF_INET;
			adrRec.sin_port = htons(502);
			if(inet_pton(AF_INET, addr, &adrRec.sin_addr.s_addr) <= 0){
				// bad address failure
				close(*sock);
				*sock = -1;
				return -1;
			}
			if(connect(*sock, (struct sockaddr *)&adrRec, sizeof(adrRec)) != 0){
				// connection failure
				close(*sock);
				*sock = -1;
				return -1;
			}
		}
	}

	// send request
	if((rx_length = write(*sock, &packet, packet.length + 6)) < (packet.length + 6)){
		// connection failure
		close(*sock);
		*sock = -1;
		return -1;
	}

	// get reply
	dest = (char *)(&reply);
	size = 0;
	bzero(&reply, sizeof(reply));
	limit = 6; // start by reading the header only
	while((rx_length = read(*sock, dest+size, limit-size)) > 0){
		size = size + rx_length;
		if(size == 6){
			if(reply.pading != 0){
				// reply is too big!
				close(*sock);
				*sock = -1;
				return -5;
			}
			limit = reply.length + 6; // now read to the end of the packet only
		}
		if(size == limit){
			// we have a whole packet
			if((reply.length == 4) && (reply.function == 2) && (reply.unit == unitID) && (reply.data[0] == 1)){
				// valid reply
				return (reply.data[1] & 0x01);
			}else{
				// invalid reply
				return -3;
			}
		}
	}

	// lost connection
	close(*sock);
	*sock = -1;
	if(rx_length)	// negative number indicates socket error
		return -6;
	return -2;	// zero indicated connection closed or RX timeout
}

void modbusPollCleanUp(void *pass){
	struct modbusPollRec *rec;

	rec = (struct modbusPollRec *)pass;
	if(rec->sock >=0)
		close(rec->sock);
	free(rec);
}

void modbusPoll(void  *refIn){
	taskRecord *parent;
	struct modbusPollRec *rec;
	char state, result;
	struct timespec timeout;
	char *tmp;

	parent = (taskRecord *)refIn;
	rec = (struct modbusPollRec *)parent->userData;

    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_cleanup_push((void (*)(void *))modbusPollCleanUp, (void *)rec);

	timeout.tv_nsec = 0;
	timeout.tv_sec = rec->period;

	state = -1;
	rec->sock = -1;

    while(1){
		result = modbusQuery(&rec->sock, rec->addr, rec->unitID, rec->inputID);
		switch(result){
			case -1:
				tmp = strdup("[session] modbusPoll-Modbus Query: connection failed to ");
				str_appendstr(&tmp, rec->addr);
				serverLogMakeEntry(tmp);
				free(tmp);
				break;
			case -2:
				tmp = strdup("[session] modbusPoll-Modbus Query: losted connection from ");
				str_appendstr(&tmp, rec->addr);
				serverLogMakeEntry(tmp);
				free(tmp);
				break;
			case -3:
				tmp = strdup("[session] modbusPoll-Modbus Query: Invalid reply  from ");
				str_appendstr(&tmp, rec->addr);
				serverLogMakeEntry(tmp);
				free(tmp);
				break;
			case -4:
			case -5:
				tmp = strdup("[session] modbusPoll-Modbus Query: Wrong size reply from ");
				str_appendstr(&tmp, rec->addr);
				serverLogMakeEntry(tmp);
				free(tmp);
				break;
			case -6:
				tmp = strdup("[session] modbusPoll-Modbus Query: Socket error from ");
				str_appendstr(&tmp, rec->addr);
				serverLogMakeEntry(tmp);
				free(tmp);
				break;

			default:
				if((state >= 0) && (result != state)){
					modbusTrigger(result, rec->conf);
				}
				state = result;
		}
		nanosleep(&timeout, NULL);
    }
	pthread_cleanup_pop(1);
}
