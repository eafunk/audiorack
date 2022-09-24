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

#ifndef __AUTOMATE_H__
#define __AUTOMATE_H__

#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "arserver.h"
extern time_t live_event;
extern time_t silent_event;
extern char silent_tryseg;
extern float silent_thresh;
extern uint32_t silent_bus;
extern uint32_t silent_timeout;

extern uint32_t plRev;
extern uint32_t autoState;
extern unsigned char plRunning;
extern char *fillStr;		// thread protect by queueLock mutex

unsigned char initAutomator(void);
void shutdownAutomator(void);
uint32_t AddItem(int pos, char *URLstr, char *adder, uint32_t adderUID);
void AddPlayer(int pos, int pNum);
uint32_t SplitItem(uint32_t parent, char *URLstr, unsigned char last);
int LoadItem(int pos, queueRecord *qrec);
void MoveItem(int sourcePos, int destPos, unsigned char clearTimes);
void setSegTimes(inChannel *thisp, inChannel *nextp, int nextNum);
void QueManagerTask(unsigned char *stop);
void wakeQueManager(void);

#endif //__AUTOMATE_H__
