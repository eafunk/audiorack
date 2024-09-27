/*
  Copyright (C) 2019-2023 Ethan Funk
  
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

#include "arserver.h"
#include "database.h"
#include "mix_engine.h"
#include "session.h"
#include "data.h"
#include "tasks.h"
#include "automate.h"

#include <stdio.h>
#include <jack/jack.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <mcheck.h>


/* program wide globals */
unsigned int maxSessions = 25;
const char *versionStr="4.1.1";
const char *versionCR="2004-2024  Ethan Funk";
mixEngineRecPtr mixEngine;
unsigned char run;
unsigned char quit;
unsigned char restart;
unsigned int lastp;
char *startup_path;
char *wdir_path;
jack_options_t options;

/* main only globals */
short tcpPort;
unsigned int chCnt;
unsigned int inCnt;
unsigned int outCnt;
unsigned int busCnt;
char *lock_path;
char *ourJackName;
char *jackServer;
pid_t lastChild;

void TERMhandler(int sig)
{
	signal(sig, SIG_IGN);
	quit = 1;
}

void TERMhandlerLauncher(int sig)
{
	if(lastChild)
		kill(lastChild, SIGTERM);
	exit(0);
}

void INThandler(int sig)
{
	int *i;
	
	// Use SIGINT to cause a crash resulting in a OS generated crash report
	// This is for debuging of a deadlock

	i = NULL;
	*i = 0;
}

void HUPhandler(int sig)
{
	// called to force log file to close and re-load prefs

	signal(sig, SIG_IGN);
	
	ctl_session session;
	session.cs = STDERR_FILENO;
	session.silent = 1;
	session.lastPlayer = -1;
	session.lastAID = 0;
	session.lastUID = 0;
	loadConfiguration(&session, startup_path);
	
	serverLogRotateLogFile();

	signal(sig, HUPhandler);
}

void loadPreConfig(void)
{
	char *defPath;
    FILE *fp;
    char *arg, *param, *result, line[4096];
 
	if(strlen(startup_path) == 0){
		// first try local user home directory location for settings
		struct passwd *pwrec;
		if(pwrec = getpwuid(geteuid())){
			str_setstr(&startup_path, pwrec->pw_dir);
			str_appendchr(&startup_path, '/');
		}
		str_appendstr(&startup_path, UserSettingsDirectory);
		str_appendstr(&startup_path, "ars_startup.conf"); 
		if((fp = fopen(startup_path, "r")) == NULL){
			// next try default config file
			str_setstr(&startup_path, AppSupportDirectory);
			str_appendstr(&startup_path, "user_startup.conf"); 
			if((fp = fopen(startup_path, "r")) == NULL){
				serverLogMakeEntry("[main] loadPreConfig-: tried standard startup.conf file paths: Could not open file for reading");
				/* set back to default if no config file found */
				if(pwrec = getpwuid(geteuid())){
					str_setstr(&startup_path, pwrec->pw_dir);
					str_appendchr(&startup_path, '/');
				}
				str_appendstr(&startup_path, UserSettingsDirectory);
				str_appendstr(&startup_path, "ars_startup.conf"); 
				return;
			}
		}
	}else{
		if((fp = fopen(startup_path, "r")) == NULL){
			snprintf(line, sizeof line, "[main] loadPreConfig-%s:Could not open file for reading", startup_path);
			serverLogMakeEntry(line);
			return;
		}
	}
	result = fgets(line, sizeof line, fp);
	while(result != NULL){
		if(line[0] == '-'){ // a preload configuartion switch ('-' first char in line)
			strtok_r(line, "\n\r", &param);	// strip LF/CR
			arg = strtok_r(line, " ", &param);
			if(strcmp(arg, "-x") == 0) {
				// no starting of deafult jackd server
				options |= JackNoStartServer;
			}else
			if (strcmp(arg, "-p") == 0) {
				 // tcp listening port being specified
				 tcpPort = atoi(param);
			}else
			if (strcmp(arg, "-i") == 0) {
				// input count specified
				inCnt = atoi(param);
			}else
			if (strcmp(arg, "-o") == 0) {
				// output count specified
				outCnt = atoi(param);
			}else
			if (strcmp(arg, "-b") == 0) {
				// mix bus count specified
				busCnt = atoi(param);
			}else
			if (strcmp(arg, "-w") == 0) {
				// channel width specified
				chCnt = atoi(param);
			}else if (strcmp(arg, "-j") == 0) {
				// requested JACK name for arserver
				str_setstr(&ourJackName, param);
			}else if(strcmp(arg, "-s") == 0) {
				// requested JACK server to connect to
				str_setstr(&jackServer, param);
			}else if(strcmp(arg, "-l") == 0) {
				// set maxilum number of concurrent tcp listening command connections 
				maxSessions = atoi(param);
			}
		}
		result = fgets(line, sizeof line, fp);
	}
	fclose(fp);
}

int main(int argc, const char *argv[])
{    
	const char *err;
	pid_t child;
	int statset[2], wdset[2];
	pthread_mutexattr_t attr;
	int i, stat, fd;
	char command[1024];
	unsigned char nofork, keepalive, keepstderr, started;
	int trueVal = 1;
	
	options = 0;        
	nofork = 0;
	keepalive = 0;
	keepstderr = 0;
	
	i = 1;
	while((argc - i) > 0){
		if(strcmp(argv[i], "-n") == 0){
			// do not daemonize.
			nofork = 1;
		}
		if(strcmp(argv[i], "-k") == 0){
			// set keep alive flag
			keepalive = 1;
		}
		if(strcmp(argv[i], "-e") == 0){
			// don't close STDERR
			keepstderr = 1;
		}
			
		if(!strcmp(argv[i], "?") || !strcmp(argv[i], "help") || !strcmp(argv[i], "-h")){
			fprintf(stdout,"Usage-- command line only options:\n");
			fprintf(stdout,"\t-c [startup configuration file path]\n");
			fprintf(stdout,"\t-e Keep STDERR open.  Otherwise STDERR is shutdown after the server starts normally.\n");
			fprintf(stdout,"\t-n Run server in local (non-daemon) mode\n");
			fprintf(stdout,"\t-k Keep server alive - relaunch on crash (ignored with -n option)\n");
			fprintf(stdout,"Options taken on command line, or read line by line from the startup configuration file:\n");
			fprintf(stdout,"\t-p [client connection tcp port number]\n");
			fprintf(stdout,"\t-i [audio mixing matrix source input count]\n");
			fprintf(stdout,"\t-b [audio mixing matrix bus/output count]\n");
			fprintf(stdout,"\t-o [output group count]\n");
			fprintf(stdout,"\t-w [channel width i.e. 2 = stereo]\n");
			fprintf(stdout,"\t-r [/runlock/file/directory/path/] (file named ars{portNumber}.pid, and contains pid)\n");
			fprintf(stdout,"\t-j [requested JACK name for us (arServer)]\n");
			fprintf(stdout,"\t-s [name of JACK server to connect to]\n");
			fprintf(stdout,"\t-x Prevent starting of default jackd audio server if jackd isn't already running\n");
			fprintf(stdout,"\t-l [Maximum number of concurrent tcp listening command connections]\n"); 
			fprintf(stdout,"\t<none> uses defaults:\n");
			fprintf(stdout,"\t\tcontrol tcp port (9550)\n");
			fprintf(stdout,"\t\tJack name ars<control port number>\n");
			fprintf(stdout,"\t\t10 stereo input sources, 4 stereo buses, 6 output groups, all 2 chan. wide.\n");
			fprintf(stdout,"\t\tdefault configuartion file locations.\n");
			exit(0);
		}
		i = i + 1;
	}
	if(nofork == 0){
		// prepare to deamonize!
		if(socketpair(AF_UNIX, SOCK_STREAM, 0, statset) < 0) {
			fprintf(stderr, "error %d on status socketpair\n", errno);
			exit(1);
		}
		
		// first fork runs our launcher
		child = fork();
		if(child < 0){
			fprintf(stderr, "launcher fork error %d\n", errno);
			exit(1);
		}
		if(child > 0){
			// original process continues here
			close(statset[0]);
			// echo child twice removed start-up status
			while((i = read(statset[1], command, sizeof(command))) > 0){
				write(STDOUT_FILENO, command, i);
			}
			// all done... clean up and exit
			close(statset[1]);
			exit(0);
		}
		// launcher continues here...
		setsid(); // obtain a new process group 
		// close all descriptors, except for socket to the parent
		for(i=(getdtablesize()-1); i >= 0; --i){
			if(i != statset[0])
				close(i); 
		}
		if((fd = open("/dev/null", O_RDWR)) < 0)
			exit(1);
		// Redirect standard error to /dev/null
		if(fd != STDERR_FILENO){
			if(dup2(fd, STDERR_FILENO) != STDERR_FILENO){
				exit(1);
			}
		}
		// Redirect standard out to /dev/null
		if(fd != STDOUT_FILENO){
			if(dup2(fd, STDOUT_FILENO) != STDOUT_FILENO){
				exit(1);
			}
		}
		// Redirect standard in to /dev/null
		if(fd != STDIN_FILENO){
			if(dup2(fd, STDIN_FILENO) != STDIN_FILENO){
				exit(1);
			}
		}
		if(fd > STDERR_FILENO)
			close(fd);
		
		started = 0;
		do{
			lastChild = 0;
			signal(SIGTERM, TERMhandlerLauncher);
			// second fork runs arserver propper
			if(socketpair(AF_UNIX, SOCK_STREAM, 0, wdset) < 0){
				exit(1);
			}
			child = fork();
			if(child < 0){
				exit(1);
			}
			if(child > 0){
				lastChild = child;
				fd_set read_fds, exc_fds;
				struct timeval tv;
				int h = 0;
				
				i = 0;
				int n;
				do{
					/* Wait up to one second (1 second). */
					tv.tv_sec = 1;
					tv.tv_usec = 0;
					
					FD_ZERO(&read_fds);
					FD_ZERO(&exc_fds);
					FD_SET(wdset[1], &read_fds);
					FD_SET(wdset[1], &exc_fds);
					if(wdset[1] > h)
						h = wdset[1];
					if(select(h+1, &read_fds, NULL, &exc_fds, &tv) > 0){
						n = read(wdset[1], command, sizeof(command));
						if((n > 0) && memchr(command, '*', n))
							// the child indicated to the launcher that it has started.
							started = 1;
						else if((n > 0) && memchr(command, '!', n))
							// the child indicated to the launcher to shutdown.
							exit(0);
						else if((n > 0) && memchr(command, '#', n))
							// the child indicated to the launcher to restart... silence detected, etc
							i = 60;
						else if(n < 0)
							// socket error...
							i = 60;
						else
							i = 0;
					}else
						// select timed out...
						++i;
					if(i > 59){
						// receive timeout 1 minute or other error: Kill process
						if(++i > 60)
							// second try: Kill process
							kill(child, 9);
						else
							//  first try: send a SIGINT to force a crash w/crash report
							kill(child, 2);
						sleep(4);
					}
				}while(waitpid(child, &stat, WNOHANG) == 0);
				close(wdset[1]);
			}
			// if keepalive is true, the launcher will fork a new child process
			// if keepalive is false, it exits after running arserver once
		}while((child > 0) && started && (keepalive == 1));
		if(child){
			// launcher (parent) is finished
			exit(0);
		}
		
		// child continues here
		if(nofork == 0){
			// redirect stderr to status socket, or to /dev/null it status socket is not valid
			if(statset[0] != STDERR_FILENO){
				if(dup2(statset[0], STDERR_FILENO) != STDERR_FILENO){
					if((fd = open("/dev/null", O_RDWR)) < 0){
						exit(1);
					}
					if(fd != STDERR_FILENO){
						if(dup2(fd, STDERR_FILENO) != STDERR_FILENO)
							exit(1);
						close(fd);
					}
					
				}
				close(statset[0]);
			}
			// redirect stdout befor final exec
			if(wdset[0] != STDOUT_FILENO){
				if(dup2(wdset[0], STDOUT_FILENO) != STDOUT_FILENO)
					exit(1);
				close(wdset[0]);
			}
		}
		// and run again only with the -n flag...
		const char *cmd[argc+2];
		// copy original command flags
		for(i=0; i<argc; i++)
			cmd[i] = argv[i];
		// add -n flag and null terminate
		cmd[i] = "-n";
		cmd[i+1] = (char*)0;
		
		// and run...
		execv(argv[0], (char* const*)cmd);
		// If we return from the above call, something is wrong.
		snprintf(command, sizeof command, "execv() failed to launch final arserver instance\n");
		write(STDERR_FILENO, command, strlen(command));
		goto fail;
	}
	
	// NOTE: STDOUT is used to send status data (watchdog timer) to launcher
	// STDERR is used to send startup status to the launchers parent, then it is closed and redirected to /dev/null

	// We are now a deamon! 
	signal(SIGPIPE, SIG_IGN);	//	<-- Blocking SIGPIPE
	signal(SIGINT, INThandler);
	signal(SIGTERM, TERMhandler);
	signal(SIGHUP, HUPhandler);
		  		
	/* defaults settings...can be overridden by CLI or config file */
	tcpPort = 9550;
	chCnt = 2;
	inCnt = 10;
	outCnt = 6;
	busCnt = 4;
	
	lock_path = NULL;
	wdir_path = NULL;
	startup_path = NULL;
	ourJackName = NULL;
	jackServer = NULL;	// Default is NULL, not an empty string
	str_setstr(&lock_path, DefLockfileDirectory);
	str_setstr(&wdir_path, "");
	str_setstr(&startup_path, "");
	str_setstr(&ourJackName, "");

	i = 1;
	while((argc - i) > 1){
		if (strcmp(argv[i], "-c") == 0) {
			// config file path being specified
			i = i + 1;
			str_setstr(&startup_path, argv[i]);
		}
		i = i + 1;
	}
		
	// load configuration file
	loadPreConfig();   

	// settings directly for command-line arguments
	i = 1;
	while((argc - i) > 1){
		// there is a CLI specified parameter
		if(strcmp(argv[i], "-x") == 0){
			// no starting of deafult jackd server
			options |= JackNoStartServer;
			i = i + 1;
		}else if(strcmp(argv[i], "-p") == 0) {
			// tcp listening port being specified
			i = i + 1;
			tcpPort = atoi(argv[i]);
			i = i + 1;
		}else if(strcmp(argv[i], "-w") == 0) {
			// channel width count specified
			i = i + 1;
			chCnt = atoi(argv[i]);
			i = i + 1;
		}else if(strcmp(argv[i], "-b") == 0) {
			// mix bus count specified
			i = i + 1;
			busCnt = atoi(argv[i]);
			i = i + 1;
		}else if(strcmp(argv[i], "-i") == 0) {
			// input count specified
			i = i + 1;
			inCnt = atoi(argv[i]);
			i = i + 1;
		}else if(strcmp(argv[i], "-o") == 0) {
			// output count specified
			i = i + 1;
			outCnt = atoi(argv[i]);
			i = i + 1;
		}else if(strcmp(argv[i], "-r") == 0) {
			//run-lock file directory path being specified
			i = i + 1;
			str_setstr(&lock_path, argv[i]);
			i = i + 1;
		}else if(strcmp(argv[i], "-j") == 0) {
			//requested Jack name for us being specified
			i = i + 1;
			str_setstr(&ourJackName, argv[i]);
			i = i + 1;
		}else if(strcmp(argv[i], "-s") == 0) {
			//JACK server name to connect to specified
			i = i + 1;
			str_setstr(&jackServer, argv[i]);
			i = i + 1;
		}else if(strcmp(argv[i], "-l") == 0) {
				// set maxilum number of concurrent tcp listening command connections 
			i = i + 1;
			maxSessions = atoi(argv[i]);
			i = i + 1;
		}else
			i = i + 1;
	}
	if(geteuid() == 0){
		// for root user, create lock file so only one server can run at a time on a given port
		// This isn't really needed since binding to the TCP listening port will fail
		// when subsequent processes try to run.

		// set created file permissions: r/w/x user, group, enyone (for the lock file)
		umask(000); 
		
		// create the lock file with the pid as decimal text as it's contents
		snprintf(command, sizeof command, "%sars%d.pid", lock_path, tcpPort);
		i = open(command, O_RDWR|O_CREAT, 0666);
		if(i < 0){
			// failed to create lock file
			snprintf(command, sizeof command, "failed to create lock file\n");
			write(STDERR_FILENO, command, strlen(command)); 	
			goto fail;
		}
		if(lockf(i, F_TLOCK,0)<0){
			// failed to lock lock file
			snprintf(command, sizeof command, "Audio Rack Server (ars) is already running on port %d.\n", tcpPort);
			write(STDERR_FILENO, command, strlen(command));
			goto fail;
		}
		
		// back to r/w/x user, r/x group, everyone
		umask(022); 
		
		// write pid to lock file
		snprintf(command, sizeof command, "%u\n",getpid());
		write(i, command, strlen(command)); 	
		
		snprintf(command, sizeof command, "pid=%u\n", getpid());
		write(STDERR_FILENO, command, strlen(command));
	}
	
	// try droping priveleges to the "arserver" user and group.
	// this only works if we are run as root user, otherwise, it keeps the same user/group as the parent.
	// any instance of arserver forked from this launcher will inherit the launchers user/group.
	struct passwd *pwrec;
	if(pwrec = getpwnam("arserver")){
		if(!setgid(pwrec->pw_gid)){
			setuid(pwrec->pw_uid);
		}
	}

	// set working dir if specified, or user home directory
	if(strlen(wdir_path) == 0){
		// Get our effective user's home directory
		struct passwd *pwrec;
		if(pwrec = getpwuid(geteuid()))
			str_setstr(&wdir_path, pwrec->pw_dir);
	}
	if(strlen(wdir_path) > 0){
		if((chdir(wdir_path)) < 0) {
			snprintf(command, sizeof command, "Failed to set working directory to %s.\n", wdir_path);
			write(STDERR_FILENO, command, strlen(command)); 	
		}
	}else{
		snprintf(command, sizeof command, "Failed to set working directory to %s.\n", wdir_path);
		write(STDERR_FILENO, command, strlen(command)); 	
	}
	
	/* set up settings and metadata list */
	initDataLists();
	
	// display version
	snprintf(command, sizeof command, "AudioRack Server, version %s\n", versionStr);
	write(STDERR_FILENO, command, strlen(command));
	snprintf(command, sizeof command, "Copyright (C) %s\n\n", versionCR);
	write(STDERR_FILENO, command, strlen(command));

	snprintf(command, sizeof command, "AudioRack Server comes with ABSOLUTELY NO WARRANTY; for details\n");
	write(STDERR_FILENO, command, strlen(command));
	snprintf(command, sizeof command, "type `info'.  This is free software, and you are welcome\n");
	write(STDERR_FILENO, command, strlen(command));
	snprintf(command, sizeof command, "to redistribute it under certain conditions; See the\n");
	write(STDERR_FILENO, command, strlen(command));
	snprintf(command, sizeof command, "GNU General Public License included with this program for details.\n\n");
	write(STDERR_FILENO, command, strlen(command));
	snprintf(command, sizeof command, "==================================================================\n");
	write(STDERR_FILENO, command, strlen(command));

	// create base metadata record (UID = 0) for storing ae server settings
	createSettingsRecord(versionStr);
	
	// set up task list
	initTaskList();
	
	// start TCP control session connection listener
	if(err = initSessions(maxSessions, &tcpPort)){
		write(STDERR_FILENO, err, strlen(err));
		write(STDERR_FILENO, "\n", 1);
		goto fail;
	}
	snprintf(command, sizeof command, "listening on port %d, max connections = %d\n", tcpPort, maxSessions);
	write(STDERR_FILENO, command, strlen(command));
	
	if(strlen(ourJackName) == 0){
		snprintf(command, sizeof command, "ars%d", tcpPort);
		str_setstr(&ourJackName, command);
	}
	
	quit = 0;
	run = 1;
	restart = 0;
	lastp = 0;

	// start audio engine
	if(err = initMixer(&mixEngine, chCnt, inCnt, outCnt, busCnt, 
								jackServer, ourJackName, options)){
		write(STDERR_FILENO, err, strlen(err));
		write(STDERR_FILENO, "\n", 1);
		goto fail;
	}else{
		err = "Audio engine started.\n";
		write(STDERR_FILENO, err, strlen(err));
	}
	
	initDispatcherThreads();
	
	/* this initializes gstream for media metadata/tag extraction */
	if(!mediaInit())
		serverLogMakeEntry("[main] mediaInit-:Failed to initialize gstreamer library. This is trouble!");

	initAutomator();
	
	// load configuration file
	snprintf(command, sizeof command, "Loading start-up configuration: %s\n", startup_path);
	write(STDERR_FILENO, command, strlen(command));
	ctl_session session;
	session.cs = 0;
	session.silent = 1;
	session.lastPlayer = -1;
	session.lastAID = 0;
	session.lastUID = 0;
	log_busses = 0x0c;	// default log busses: main and alt only.
	loadConfiguration(&session, startup_path);
	if(db_preflight())
		serverLogMakeEntry("[main] db_preflight-:Failed to initialize database libraries. This is trouble!");
	
/* No IAX support yet 
	if(iaxp_initialize()){
		ServerLoger->MakeEntry("IAX telephone client failed to initialized");
		snprintf(command, sizeof command, "IAX telephone client failed to initialized\n");
		write(STDERR_FILENO, command, strlen(command));
	}else{
		ServerLoger->MakeEntry("IAX telephone client has started");
		snprintf(command, sizeof command, "IAX telephone client has started\n");
		write(STDERR_FILENO, command, strlen(command));
	}
*/
	snprintf(command, sizeof command, "[main] -:Server has started on port %d", tcpPort);
	serverLogMakeEntry(command);

	snprintf(command, sizeof command, "Server has started\n");
	write(STDERR_FILENO, command, strlen(command));
	if(!keepstderr){
		shutdown(STDERR_FILENO, SHUT_RDWR); // parent gets a read EOF if other than /dev/null
		close(STDERR_FILENO);

		// Redirect standard error to /dev/null
		if((fd = open("/dev/null", O_RDWR)) < 0)
			goto fail;
		if(fd != STDERR_FILENO){
			dup2(fd, STDERR_FILENO);
			close(fd);
		}	
	}else{
		/* If we are keeping stderr open, then assume debug mode: enable memory tracing too */
		mtrace();
	}
	
	// notify launcher that we have started
	write(STDOUT_FILENO, "*", 1);
	
 	// run queueManager task loop until run is false.  This function also
 	// calls the watchdog timer reset each time through it's loop, which 
 	// sends a 'W' out STDOUT. The spauing process that started us watches 
 	// for this, and will kill and relaunch us if it doesn't get a 'W' 
 	// often enough.
	QueManagerTask(&quit);
	
	run = 0;					// tell all other threads to finish
	
	shutdownSessions();
	shutdownDispatcherThreads();
	mediaShutdown();
	freeTaskList();
	freeDataLists();
	shutdownMixer(mixEngine);  
	db_shutdown();
	  
/*	iaxp_shutdown(); */

	if(!restart){
		// signal to shutdown laucher, so it doesn't restart us
		write(STDOUT_FILENO, "!", 1);
		sleep(2);
	}	
fail:
	// signal parents we are exiting
	shutdown(STDOUT_FILENO, SHUT_RDWR);
	shutdown(STDERR_FILENO, SHUT_RDWR);
	exit(0);
}
