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

#define mix_buffer_durration	16 /* minimum length (seconds) of mix buffers */
#define	ctlQueueSizeBytes 	64 * 1024  /* control packet queue size in bytes */

#include "mix_engine.h"
#include "arserver.h"
#include "dispatch.h"
#include <math.h>
#include <pthread.h>
#include <sys/wait.h>

void clearCBQ(callbackQueue *Q){
	pthread_spin_lock(&Q->spinlock);
	Q->rdIdx = 0;
	Q->wrIdx = 0;
	pthread_spin_unlock(&Q->spinlock);
}

jack_port_id_t *getCBQitem(callbackQueue *Q){
	jack_port_id_t *result;
	
	pthread_spin_lock(&Q->spinlock);
	/* check for nothing to read state */
	if(Q->rdIdx == Q->wrIdx){
		pthread_spin_unlock(&Q->spinlock);
		return NULL;
	}
	result = &Q->portID[Q->rdIdx & cbMASK];
	Q->rdIdx++;
	pthread_spin_unlock(&Q->spinlock);
	return result;
}

unsigned char putCBQitem(callbackQueue *Q, jack_port_id_t ID){
	pthread_spin_lock(&Q->spinlock);
	/* check for space to write */
	if((Q->wrIdx - Q->rdIdx) >= cbQsize){
		pthread_spin_unlock(&Q->spinlock);
		return 0;
	}
	Q->portID[Q->wrIdx & cbMASK] = ID;
	Q->wrIdx++;
	pthread_spin_unlock(&Q->spinlock);
	return 1;
}

unsigned char checkPnumber(int pNum)
{
	if(pNum < 0)
		return 0;
	if(pNum >= mixEngine->inCount)
		return 0;
	return 1;
}

/* 3 JACK callback for the condition stated in the function name */
void jack_shutdown_callback(void *arg){
	mixEngineRecPtr mixEngineRef = (mixEngineRecPtr)arg;
	
	/* this is a jackd shutdown notice.  We indicate this to the callback
	 * handler via a callback-queue entry with a port ID set to -1, which 
	 * is 0xffffffff in hex, and invalid port ID. */
	putCBQitem(&mixEngineRef->cbQueue, (unsigned)(-1));
	pthread_cond_broadcast(&mixEngineRef->cbQueueSemaphore);
	serverLogMakeEntry("jack-audio- jackd audio server has gone away: We are shutting down too.");
}
 
static void jack_reg_callback(jack_port_id_t port_id, int isReging, void *arg){
	mixEngineRecPtr mixEngineRef = (mixEngineRecPtr)arg;
	
	if(isReging){
		/* this is a port registration notice, not an un-register notice 
		 * scan though our port connection list to see if it is a port we
		 * were previously connected to and should try to connect to again */
		 putCBQitem(&mixEngineRef->cbQueue, port_id);
		 pthread_cond_broadcast(&mixEngineRef->cbQueueSemaphore);
	}
}

static void jack_rename_callback(jack_port_id_t port_id, const char *old_name, const char *new_name, void *arg){	
	mixEngineRecPtr mixEngineRef = (mixEngineRecPtr)arg;
	
	/* this is a port rename notice. scan though our port connection 
	 * list to see if it is a port we were previously connected to and 
	 * should try to connect to again */
	putCBQitem(&mixEngineRef->cbQueue, port_id);
	pthread_cond_broadcast(&mixEngineRef->cbQueueSemaphore);
}

void setInChanToDefault(inChannel *chrec){
	chrec->vol = def_vol;		// default scalar gain
	chrec->busses = def_busses;	// default bus settings
	chrec->bal = def_bal;		// default balance value
	chrec->posack = 0;
	chrec->segNext = 0;
	chrec->posSeg = 0.0;
	chrec->fadePos = 0.0;
	chrec->fadeTime = 0;
	chrec->attached = 0;
	chrec->nextAplEvent = 0.0;
	chrec->persist = persistOff;
	chrec->sourceType = sourceTypeLive;
}

void setAllUnloadedToDefault(mixEngineRecPtr mixRef){
	inChannel *chrec = mixRef->ins;
	unsigned int i, incnt;
	incnt = mixRef->inCount;
	for(i=0; i<incnt; i++){
		if(chrec->status == status_empty)
			setInChanToDefault(chrec);
		chrec++;
	}	
}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread, once for each audio cycle.
 */
int process(jack_nframes_t nframes, void *arg){
	mixEngineRecPtr mixEngineRef = (mixEngineRecPtr)arg;
	
	unsigned int i, b, c, s, max, icount, ccount, bcount, busbits;
	jack_default_audio_sample_t *in, *out, *samp, *dest;
	jack_port_t **in_port;
	jack_port_t **out_port;
	float leftVol, rightVol, vol;
	float SampSqrd, pk, avr, sum;
	double frameTime, syncTime;
	int delay;
	unsigned int tmpStatus;
	size_t sizeA, sizeB, size;
	inChannel *inchrec;
	outChannel *outchrec;
	vuData *vu;
	unsigned int activeBus;
	jack_midi_event_t in_event;
	jack_nframes_t event_count;
	unsigned char* midi_data;
	void* midi_buffer;
	controlPacket *packet;
	controlPacket header;
	valuetype *val;
	unsigned int groupGain, least;
	unsigned char wakeChanged;
	unsigned char isConnected;
	float curSegLevel;	
	unsigned char handled = 0;
	
	wakeChanged = 0;
	activeBus = 0;
	ccount = mixEngineRef->chanCount;
	bcount = mixEngineRef->busCount;
	frameTime = nframes / (double)mixEngineRef->mixerSampleRate;
	  
	/* handle received control packets */		
	midi_buffer = jack_port_get_buffer(mixEngineRef->ctlInPort, nframes);
	event_count = jack_midi_get_event_count(midi_buffer);
	for(c=0; c<event_count; c++){
		jack_midi_event_get(&in_event, midi_buffer, c);
		if(in_event.size > 6){
			packet = (controlPacket *)in_event.buffer;
			size = 7 + ntohs(packet->dataSize);
			handled = 0;
			if((packet->type & cPeer_MASK) == cPeer_player){
				i = ntohl(packet->peer);
				if(checkPnumber(i)){
					inchrec = &mixEngineRef->ins[i];
					if(inchrec->status){
						char type = packet->type & 0x0f;
						if(type == cType_end){
							/* handle end of media message */
							inchrec->status = inchrec->status | status_finished;	
							// force a segue when segNext is set
							if(inchrec->segNext)
								inchrec->posSeg = 0.1;				
							inchrec->requested = inchrec->requested | change_stop;
							handled = 1;
						}else if((type == cType_pos) && (size == 11)){
							val = (valuetype *)&packet->data;
							val->iVal = ntohl(val->iVal);
							syncTime = val->fVal;
							inchrec->posack = 1;	/* set flag to send pos ack control packet */
							if(inchrec->status & status_playing)
								/* adjust for midi arrive time within sample frame - NOTE: nFrames of time will be added soon */
								syncTime = syncTime - (double)in_event.time / (double)mixEngineRef->mixerSampleRate;
							if(inchrec->sourceType != sourceTypeCanRepos){
								inchrec->sourceType = sourceTypeCanRepos;
								inchrec->changed = inchrec->changed | change_type;
							}
							if(fabs(syncTime - inchrec->pos) > 0.1){
								/* more than a minor adjustment */
								inchrec->pos = syncTime;
								inchrec->changed = inchrec->changed | change_pos;
							}
							inchrec->pos = syncTime;
							handled = 1;
						}else if((type == cType_vol) && (size == 11)){
							val = (valuetype *)&packet->data;
							val->iVal = ntohl(val->iVal);
							inchrec->requested = inchrec->requested | change_vol;
							inchrec->reqVol = val->fVal;
							handled = 1;	
						}
					}
				}
			}
			if(!handled){
				// non-realtime packet... queue it for handling by another thread
				if(jack_ringbuffer_write_space(mixEngineRef->ctlInQueue) >= size){
					// enque packet in midi/control queue ring buffer
					jack_ringbuffer_write(mixEngineRef->ctlInQueue, (char *)packet, size);	
					wakeChanged = 1;
				}				
			}
		}
	}
	if(wakeChanged){
		pthread_cond_broadcast(&mixEngineRef->ctlInQueueSemaphore);
		wakeChanged = 0;
	}
	
	/* TODO:
	 * control: Talkback
	 */
	 	
	/* Mix each mixer input to it's assigned mixbus ring-buffers */
	midi_buffer = jack_port_get_buffer(mixEngineRef->ctlOutPort, nframes);
	jack_midi_clear_buffer(midi_buffer);
	
	inchrec = mixEngineRef->ins;
	icount = mixEngineRef->inCount;
	for(i=0; i<icount; i++){
		/* input number i */
		in_port = inchrec->in_jPorts;
		out_port = inchrec->mm_jPorts;
		
		if(inchrec->posack){
			/* send pos ack control packet */
			if(packet = (controlPacket *)jack_midi_event_reserve(midi_buffer, 0, 7)){
				packet->type = cType_posack | cPeer_player;	
				packet->peer = htonl(i);
				packet->dataSize = 0;
			}	
		}

		/* handle change requests */
		tmpStatus = inchrec->status;
		if(inchrec->requested & change_vol){
			inchrec->vol = inchrec->reqVol;
			inchrec->changed = inchrec->changed | change_vol;
			inchrec->requested = inchrec->requested & ~change_vol;
		}
		if(inchrec->requested & change_bal){
			inchrec->bal = inchrec->reqBal;
			inchrec->changed = inchrec->changed | change_bal;
			inchrec->requested = inchrec->requested & ~change_bal;
		}
		if(inchrec->requested & change_bus){
			if(((inchrec->busses & 2L) == 0) && (inchrec->reqBusses & 2L))
				inchrec->status = inchrec->status | status_cueing;
			if((inchrec->busses & 2L) && ((inchrec->reqBusses & 2L) == 0)){
				inchrec->status = inchrec->status & ~status_cueing;
				// if playing, stop
				if(inchrec->status & status_playing)
					inchrec->requested = inchrec->requested | change_stop;
			}
			// keep top 8 bits of busses, these are mute group flags
			inchrec->busses = (inchrec->busses & 0xff000000) | (inchrec->reqBusses & 0x00ffffff);
			inchrec->changed = inchrec->changed | change_bus;
			inchrec->requested = inchrec->requested & ~change_bus;
		}
		if(inchrec->requested & change_pos){
			if(inchrec->status & status_standby){
				inchrec->status = inchrec->status & ~status_finished;
				if(inchrec->sourceType == sourceTypeCanRepos){
					if(packet = (controlPacket *)jack_midi_event_reserve(midi_buffer, 0, 11)){
						packet->type = cType_pos | cPeer_player;	
						packet->peer = htonl(i);
						packet->dataSize = htons(4);
						val = (valuetype*)&packet->data;
						val->fVal = inchrec->reqPos;
						val->iVal = htonl(val->iVal); 
					}		
				}
			}
			inchrec->requested = inchrec->requested & ~change_pos;
		}
		if(inchrec->requested & change_play){
			if(inchrec->status & status_standby){
				if((inchrec->status & status_playing) == 0){
					if(packet = (controlPacket *)jack_midi_event_reserve(midi_buffer, 0, 7)){
						packet->type = cType_start | cPeer_player;	
						packet->peer = htonl(i);
						packet->dataSize = 0;
					}	
					inchrec->changed = inchrec->changed | change_play;
					inchrec->status = inchrec->status | status_playing;
					if((inchrec->busses & 2L) == 0){ 
						// not in cue
						inchrec->status = inchrec->status | status_hasPlayed;
					}
					if(inchrec->sourceType != sourceTypeCanRepos){
						inchrec->pos = 0.0;
						inchrec->changed = inchrec->changed | change_pos;
					}
				}
				inchrec->requested = inchrec->requested & ~change_play;
			}	
		}
		if(inchrec->requested & change_stop){
			if(inchrec->status & status_standby){
				if(inchrec->status & status_playing){
					if(packet = (controlPacket *)jack_midi_event_reserve(midi_buffer, 0, 7)){
						packet->type = cType_stop | cPeer_player;	
						packet->peer = htonl(i);
						packet->dataSize = 0;
					}	
					inchrec->status = inchrec->status & ~status_playing;
					inchrec->changed = inchrec->changed | change_stop;
					if((inchrec->status & status_cueing) == 0){ 
						if(inchrec->status & status_playing){
							// change status
							if(inchrec->sourceType == sourceTypeLive)
								inchrec->status = inchrec->status & ~(status_logged | status_hasPlayed);
						}
					}
				}
				inchrec->requested = inchrec->requested & ~change_stop;
			}
		}
		if(inchrec->requested & change_tb){
			if(inchrec->status & status_standby){
				/* !!! TODO  */
				inchrec->changed = inchrec->changed | change_tb;
			}
			inchrec->requested = inchrec->requested & ~change_tb;
		}
		if(inchrec->requested & change_untb){
			if(inchrec->status & status_standby){
				/* !!! TODO  */
				inchrec->changed = inchrec->changed | change_untb;
			}
			inchrec->requested = inchrec->requested & ~change_untb;
		}
		
		/* handle volume and fading */
		if(inchrec->fadeTime && (inchrec->fadeTime < time(NULL))){
			inchrec->fadePos = inchrec->pos;
			inchrec->fadeTime = 0;
		}
		
		vol = inchrec->vol;

		if(inchrec->fadePos && (inchrec->pos >= inchrec->fadePos)){
			inchrec->levelSeg = 0.0;
			inchrec->posSeg = inchrec->fadePos;  // segout on fade too
			vol = vol - (frameTime * 0.333333);		// 3 sec. fade from unity gain			
			if(vol < 0.0)
				vol = 0.0;
			inchrec->changed = inchrec->changed | change_vol;
			if(vol < 0.0001){						// faded to below -80 dB
				inchrec->status = inchrec->status | status_finished;
				vol = 0.0;
				inchrec->fadePos = 0.0;
				inchrec->fadeTime = 0;
				
				inchrec->requested = inchrec->requested | change_stop;
			}
			inchrec->vol = vol;
		}
		
		if(inchrec->sourceType == sourceTypeLive){
			if((inchrec->status & status_render) == 0)
				/* channel is muted */
				vol = 0.0;
		}else{
			if((inchrec->status & status_playing) == 0)
				/* channel is muted */
				vol = 0.0;
		}
		if(vol){
			leftVol = vol * sqrt(1.0 - inchrec->bal);
			rightVol = vol * sqrt(1.0 + inchrec->bal);
		}else{
			leftVol = rightVol = 0.0;
		}

		if((inchrec->status & status_loading) && (inchrec->attached)){
			/* check for failed player process load */
			if(waitpid(inchrec->attached, NULL, WNOHANG)){
				inchrec->status = status_empty;
				inchrec->changed = inchrec->changed | change_unloaded;
			}
		}
				
		busbits = inchrec->busses;
		if(busbits & 2){
			/* when in cue, disable mixing into all other buses */
			busbits = 2;
			if((inchrec->sourceType == sourceTypeLive) && (inchrec->status & status_standby) && !(inchrec->status & status_loading))	
				// live sources activeate cue mute bus even when not playing
				activeBus = activeBus | 2;
		}
		
		if(inchrec->status & status_playing)
			activeBus = activeBus | busbits;
			
		curSegLevel = 0;
		isConnected = 0;
		for(c=0; c<ccount; c++){
			/* noted any connection to an inputs port in the status */
			if(jack_port_connected(*in_port))
				isConnected = 1;
				
			/* channel c of input number i */
			in = samp = jack_port_get_buffer(*in_port, nframes);
			// volume and balance scaling
			pk = 0.0;
			avr = 0.0;
			for(s = 0; s < nframes; s++){
				if(c & 0x1)
					*samp = *samp * rightVol;
				else
					*samp = *samp * leftVol;
					
				// VU meter sample calculations - all VU levels are squared (power)
				SampSqrd = (*samp) * (*samp);
				avr = avr + SampSqrd;
				if(SampSqrd > pk)
					pk = SampSqrd;
					
				samp++;
			}
			
			/* VU Block calculations */
			vu = &(inchrec->VUmeters[c]);
			// VU avarage over 10,000 samples - aprox 10 Hz @ sample rate = 96,000
			avr = ( 1 - (0.0001 * nframes)) * vu->avr + 0.0001 * avr;
			if(avr > 100.0) 
				avr = 100.0;
			vu->avr = avr;
			/* for level based segue, use the largest avr VU channel value */
			if(avr > curSegLevel)
				curSegLevel = avr;

			// VU peak fall time constatnt is 50,000 samples - aprox 2 Hz @ sample rate = 96,000
			vu->peak = vu->peak * ( 1 - (0.00002 * nframes));
			if(pk > 100.0)
				pk = 100.0;
			if(pk > vu->peak)
				vu->peak = pk;
			
			/* Mix samples into mix ringbuffers at current write point */
			for(b=0; b<bcount; b++){
				/* bus b, channel c arranged as a linear array of alternating 
				 * channels grouped by bus, index = 2*b+c */
				 
				if(i == 0)
					/* zero all the channel mix buffers for the first mixer input */
					mixbuffer_sum(mixEngineRef->mixbuses, nframes, 
														NULL, c, b, 1);
				
				/* if the corrisponding mix bus bit is set for the
				 * input channel, add input samples into the mix buffer */ 
				if(b == 0)
					// mix-minus only for bus 0
					out = jack_port_get_buffer(*out_port, nframes);
				if((1 << b) & busbits){
					mixbuffer_sum(mixEngineRef->mixbuses, nframes, 
														in, c, b, 0);
					if(b == 0){							
						/* invert and copy pre-mixed samples to the corrisponding 
						 * mixminus/premix outputs */
						samp = in;
						for(s = 0; s < nframes; s++){
							*out = -(*samp);
							samp++;
							out++;
						}
						out_port++;	
					}		
				}else if(b == 0){
					/* clear mixminus/premix outputs */	
					memset(out, 0, 	nframes * sizeof(jack_default_audio_sample_t));	
					out_port++;
				}
				if(i == (icount-1)){
					/* readback of summed bus VU meters goes here */
				}
			}
			in_port++;
		}			

		if(isConnected){
			if((inchrec->status & status_standby) == 0){
				inchrec->status = inchrec->status & (~status_loading);
				inchrec->status = inchrec->status | status_standby;
				inchrec->pos = 0.0;
				inchrec->posSeg = 0.0;
				inchrec->changed = inchrec->changed | (change_pos | change_loaded);
				if(inchrec->persist == persistDisConn)
					inchrec->persist = persistConnected;
			}
		}else{
			if(tmpStatus && !(tmpStatus & status_loading)){
				if(inchrec->persist == persistConnected){
					inchrec->persist = persistDisConn;
					inchrec->status = status_loading;
				}else{
					if(tmpStatus != status_remove)
						inchrec->changed = inchrec->changed | change_unloaded;
					inchrec->status = status_empty;
					setInChanToDefault(inchrec); 
				}
			}
		}
		
		/* advance curent time position counter & check for segue */
		if(inchrec->status & status_playing){
			inchrec->pos = inchrec->pos + frameTime;
			
			/* check for time segue when playing and not in Cue, and if segTime & segNext are valid */
			if(inchrec->posSeg){
				if((inchrec->segNext) && (inchrec->segNext <= icount)){
					if((busbits & 2) == 0){
						if(inchrec->pos > inchrec->posSeg){
							if((inchrec->levelSeg == 0.0) || (curSegLevel < inchrec->levelSeg)){
								inChannel *next = &mixEngineRef->ins[inchrec->segNext-1];
								next->requested = next->requested | change_play;
								inchrec->posSeg = 0.0;
								inchrec->segNext = 0;
								inchrec->levelSeg = 0.0;
							}
						}
					}
				}
			}		
			/* check for past next APL event when playing and not in Cue */
			if(inchrec->nextAplEvent){
				if(((busbits & 2) == 0) && (inchrec->pos > inchrec->nextAplEvent)){
					inchrec->changed = inchrec->changed | change_aplEvent;
				}
			}
		}
		
		if(inchrec->status != tmpStatus){
			inchrec->changed = inchrec->changed | change_stat;
		}
		/* check if we changed the associated changed flags */
		if(inchrec->changed)
			wakeChanged = 1;
				
		inchrec++;	
	}

	/* distribute mix buffers to assigned output groups 
	 * and dedicated mix-bus outputs */
	icount = mixEngineRef->outCount;
	outchrec = mixEngineRef->outs;
	for(i=0; i<icount; i++){
		/* output number i */
		out_port = outchrec->jPorts;
		
		/* handle change requests */
		if(outchrec->requested & change_vol){
			outchrec->vol = outchrec->reqVol;
			outchrec->changed = outchrec->changed | change_vol;
		}
		if(outchrec->requested & change_delay){
			outchrec->delay = outchrec->reqDelay;
			outchrec->changed = outchrec->changed | change_delay;
		}
		
		if(outchrec->requested & change_bus){
			outchrec->bus = outchrec->reqBus;
			outchrec->changed = outchrec->changed | change_bus;
		}
		
		delay = outchrec->delay * mixEngineRef->mixerSampleRate;
		if((b = outchrec->bus) >= bcount)
			b = 0;

			// set output device volume to lowest active mute group gain * current device volume
			least = 0xff;
			if(activeBus & (1L << 1)){
				// cue mute enabled
				least = outchrec->muteLevels & 0xff; // truncate to lower byte
			}
			if(activeBus & (1L << 24)){
				// mute group A enabled
				groupGain = (outchrec->muteLevels >> 8); //second byte
				groupGain = (groupGain & 0xff); // truncate to lower byte
				if(groupGain < least)
					least = groupGain;
			}
			if(activeBus & (1L << 25)){
				// mute group B enabled
				groupGain = (outchrec->muteLevels >> 16); //third byte
				groupGain = (groupGain & 0xff); // truncate to lower byte
				if(groupGain < least)
					least = groupGain;
			}
			if(activeBus & (1L << 26)){
				// mute group B enabled
				groupGain = (outchrec->muteLevels >> 24); //fourth byte
				groupGain = (groupGain & 0xff); // truncate to lower byte
				if(groupGain < least)
					least = groupGain;
			}
			vol = ((float)least / 255.0); // make a float
			vol = powf(vol, 3) * outchrec->vol;

		/* note: ccount still set from input processing loop */
		for(c=0; c<ccount; c++){
			/* channel c of output number i */
			samp = out = jack_port_get_buffer(*out_port, nframes);

			/* get samples from assigned mixbus ring buffer */
			mixbuffer_read(mixEngineRef->mixbuses, nframes, 
											delay, out, c, b);
											
			if(vol < 1.0){	
				/* scale the sample for the output group volume */
				for(s = 0; s < nframes; s++){
					*samp = *samp * vol;					
					samp++;
				}		
			}									
			out_port++;
		}
		/* after all requests have been handled */
		outchrec->requested = 0;

		/* check if we changed the associated changed flags */
		if(outchrec->changed)
			wakeChanged = 1;
			
		outchrec++;	
	}
	
	/* and copy mix buffers to accociated mix outputs */
	bcount = mixEngineRef->busCount;
	out_port = mixEngineRef->mixbuses->busout_jPorts;
	for(b=0; b<bcount; b++){
		for(c=0; c<ccount; c++){
			/* channel c of mix output number b */
			out = samp = jack_port_get_buffer(*out_port, nframes);
			/* get samples from assigned mixbus ring buffer */
			mixbuffer_read(mixEngineRef->mixbuses, nframes, 
											0, out, c, b);
			pk = 0.0;
			avr = 0.0;
			for(s = 0; s < nframes; s++){
				// VU meter sample calculations - all VU levels are squared (power)
				SampSqrd = (*samp) * (*samp);
				avr = avr + SampSqrd;
				if(SampSqrd > pk)
					pk = SampSqrd;
					
				samp++;
			}				

			/* VU Block calculations */
			i = (ccount * b) + c;
			vu = &(mixEngineRef->mixbuses->VUmeters[i]);
			// VU avarage over 10,000 samples - aprox 10 Hz @ sample rate = 96,000
			vu->avr = ( 1 - (0.0001 * nframes)) * vu->avr + 0.0001 * avr;
			if(vu->avr > 100.0) 
				vu->avr = 100.0;

			// VU peak fall time constatnt is 50,000 samples - aprox 2 Hz @ sample rate = 96,000
			vu->peak = vu->peak * ( 1 - (0.00002 * nframes));
			if(pk > vu->peak)
				vu->peak = pk;
			if(vu->peak > 100.0) 
				vu->peak = 100.0;
				
			out_port++;
		}
	}
	// update the mix-engine record of the active mute buses
	if(activeBus != mixEngineRef->activeBus){
		mixEngineRef->activeBus = activeBus;
		wakeChanged = 1;
	}

	/* advance mixbus buffer write marker */
	mixbuffer_advance(mixEngineRef->mixbuses, nframes);
			
	/* handle sending queued out-going control packets
	 *  upto one per  process cycle */
	size = jack_ringbuffer_peek(mixEngineRef->ctlOutQueue, (char*)&header, 7);
	if(size == 7){
		size = 7 + ntohs(header.dataSize);
		if(jack_ringbuffer_read_space(mixEngineRef->ctlOutQueue) >= size){
			if(packet = (controlPacket *)jack_midi_event_reserve(midi_buffer, nframes-1, size)){
				jack_ringbuffer_read(mixEngineRef->ctlOutQueue, (char*)packet, size);
			}
		}
	}

	/* One or more Changed flags have been set... signal the thread that cares */
	if(wakeChanged)
		pthread_cond_broadcast(&mixEngineRef->changedSemaphore);

	return 0;      
}

/**
 * Set up the mixer and associated JACK ports
 * based on the give in, out and bus counts.
 * Must be called AFTER we have connected to a JACK server
 */
char *initMixer(mixEngineRecPtr *mixEngineRef, unsigned int width, 
						unsigned int inputs, unsigned int outputs, 
						unsigned int buses, const char *server, 
						const char* reqName, jack_options_t options){
														
	unsigned int i, c, max, size;
	char pname[32];
	jack_port_t **port;
	mixEngineRecPtr mixRef;
	char *err;
	jack_status_t status;
	
	def_vol = 1.0;		// default to unity gain
	def_busses = 0x5;	// default to monitor and main bus
	def_bal = 0.0;		// default to center
	
	/* create holdering structore for all mixer related stuff */
	size = sizeof(mixEngineRec);
	*mixEngineRef = mixRef = calloc(1, size);
	if(mixRef == NULL)
		return "failed to allocate mix engine record";
	mlock(*mixEngineRef, size);

	/* set related mixer reference variables */
	mixRef->chanCount = width;
	mixRef->inCount = inputs;
	mixRef->outCount = outputs;
	mixRef->busCount = buses;
	
	pthread_mutex_init(&mixRef->ctlOutQueueMutex, NULL);  
	pthread_mutex_init(&mixRef->changedMutex, NULL);  
	pthread_cond_init(&mixRef->changedSemaphore, NULL);
	pthread_rwlock_init(&mixRef->outGrpLock, NULL);

	pthread_mutex_init(&mixRef->cbQueueMutex, NULL);  
	pthread_cond_init(&mixRef->cbQueueSemaphore, NULL);
	pthread_mutex_init(&mixRef->ctlInQueueMutex, NULL);  
	pthread_cond_init(&mixRef->ctlInQueueSemaphore, NULL);
	
	/* set up connection to jack audio server */
	if(server)
		options = options | JackServerName;

	mixRef->client = jack_client_open(reqName, options, &status, server);
	if(mixRef->client == NULL) {
		if(status & JackServerFailed)
			return "Unable to connect to JACK server";
		else
			return "jack_client_open() failed";
	}
	mixRef->ourJackName = jack_get_client_name(mixRef->client);
	mixRef->mixerSampleRate = jack_get_sample_rate(mixRef->client);

	/* allocate mix buffers (to support output delays) */
	size = mixRef->mixerSampleRate * mix_buffer_durration; 	// size of buffer in samples
	mixRef->mixbuses = mixbuffer_create(size, width, buses, mixRef->client);
	if(mixRef->mixbuses == NULL)
		return "failed to allocate mix bus buffers";

	/* create peer control ports and queues */
	mixRef->ctlInPort = jack_port_register(mixRef->client, "ctlIn", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	if(mixRef->ctlInPort == NULL) 
		return "no more JACK ports available for peer control in port";
	mixRef->ctlOutPort = jack_port_register(mixRef->client, "ctlOut", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	if(mixRef->ctlOutPort == NULL) 
		return "no more JACK ports available for peer control out port";
	if((mixRef->ctlInQueue = jack_ringbuffer_create(ctlQueueSizeBytes)) == NULL)
		return "control queue (receive) ring buffer allocation failed.";
	mlock(mixRef->ctlInQueue, ctlQueueSizeBytes);
	if((mixRef->ctlOutQueue = jack_ringbuffer_create(ctlQueueSizeBytes)) == NULL)
		return "control queue (send) ring buffer allocation failed.";
	mlock(mixRef->ctlOutQueue, ctlQueueSizeBytes);

	pthread_spin_init(&mixRef->cbQueue.spinlock, PTHREAD_PROCESS_PRIVATE);
	clearCBQ(&mixRef->cbQueue);
	
	/* create JACK mixer input channels */
	size = sizeof(inChannel) * inputs;
	if(mixRef->ins = (inChannel*)calloc(inputs, sizeof(inChannel))){
		mlock(mixRef->ins, size);
		inChannel *chrec = mixRef->ins;
		for(i=0; i<inputs; i++){
			chrec->status = status_empty; 
			setInChanToDefault(chrec);
			/* Create Jack ports for inputs */
			if(chrec->in_jPorts = (jack_port_t**)calloc(width, 
											sizeof(jack_port_t *))){
				mlock(chrec->in_jPorts, sizeof(jack_port_t *) * width);
				port = chrec->in_jPorts;
				for(c=0; c<width; c++){
					snprintf(pname, sizeof pname, "In%dch%d", i, c );
					*port = jack_port_register(mixRef->client, pname,
							JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
					if(*port == NULL) 
						return "no more JACK ports available for inputs";
					port++;
				}
			}else
				return "memory allocation for input ports failed";						
			
			/* Create Jack ports for monitor mix-minus taps */
			max = width;
			if(chrec->mm_jPorts = (jack_port_t**)calloc(max, 
											sizeof(jack_port_t *))){
				mlock(chrec->mm_jPorts, sizeof(jack_port_t *) * max);				
				port = chrec->mm_jPorts;
				for(c=0; c<max; c++){
					snprintf(pname, sizeof pname, "mmIn%dch%d", i, c);
					*port = jack_port_register(mixRef->client, pname,
						JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
					if(*port == NULL) 
						return "no more JACK ports available for premix taps";
					port++;
				}
			}else
				return "memory allocation for premix tap ports failed";	
			
			if(chrec->VUmeters = (vuData*)calloc(width, sizeof(vuData)))
				mlock(chrec->VUmeters, sizeof(vuData) * width);
			else
				return "memory allocation for input VU meter array failed";	
						
			chrec++;
		}
	}else
		return "memory allocation for input records failed";

	/* create JACK mixer output groups */	
	size = outputs * sizeof(outChannel);
	if(mixRef->outs = (outChannel*)calloc(outputs, sizeof(outChannel))){
		mlock(mixRef->outs, size);
		outChannel *chrec = mixRef->outs;
		for(i=0; i<outputs; i++){
			chrec->vol = 1.0;
			chrec->muteLevels = -1; // default no mute/ducking on mute groups
			chrec->bus = 0x1 << i; // default: associate out number with bus number
			/* Create Jack ports for output groups */
			if(chrec->jPorts = (jack_port_t**)calloc(width, 
											sizeof(jack_port_t *))){
				mlock(chrec->jPorts, sizeof(jack_port_t *) * width);
				port = chrec->jPorts;
				for(c=0; c<width; c++){
					snprintf(pname, sizeof pname, "Out%dch%d", i, c);
					*port = jack_port_register(mixRef->client, pname,
						JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
					if(*port == NULL)
						return "no more JACK ports available for output groups";
					port++;
				}
			}else
				return "memory allocation for output group ports failed";	
			chrec++;
		}
	}else
		return "memory allocation for output group records failed";

	jack_set_process_callback(mixRef->client, process, mixRef);
	jack_on_shutdown(mixRef->client, jack_shutdown_callback, mixRef);
	
	jack_set_port_registration_callback(mixRef->client, jack_reg_callback, mixRef);
	jack_set_port_rename_callback(mixRef->client, jack_rename_callback,mixRef);
		
	if(jack_activate(mixRef->client)) 
		return "cannot activate client";
							
	/* no errors */
	return NULL;
}

/**
 * Shutdown/Free the mixer and associated JACK ports
 * Called to clean up for app shutdown only after initMixer  
 * Assumes JACK server connection has already been de-activated 
 */
void shutdownMixer(mixEngineRecPtr mixEngineRef){
	unsigned int i, c, max;
	jack_port_t **port;
	
	if(mixEngineRef == NULL)
		return;
		
	jack_deactivate(mixEngineRef->client);
	/* free mix buss ring buffers */
	if(mixEngineRef->mixbuses)
		mixbuffer_free(mixEngineRef->mixbuses);
	/* free mixer inputs and associated pre-mix outputs */
	if(mixEngineRef->ins){
		inChannel *chrec = mixEngineRef->ins;
		for(i=0; i<mixEngineRef->inCount; i++){
			if(port = chrec->in_jPorts){
				munlock(chrec->in_jPorts, sizeof(jack_port_t *) * mixEngineRef->chanCount * mixEngineRef->busCount);
				free(chrec->in_jPorts);	
			}
			if(port = chrec->mm_jPorts){
				munlock(chrec->mm_jPorts, sizeof(jack_port_t *) * mixEngineRef->chanCount);
				free(chrec->mm_jPorts);	
			}
				
			if(chrec->VUmeters){
				munlock(chrec->VUmeters, sizeof(vuData) * mixEngineRef->chanCount);	
				free(chrec->VUmeters);
			}
				
			chrec++;	
		}
		munlock(mixEngineRef->ins, sizeof(inChannel) * mixEngineRef->inCount);	
		free(mixEngineRef->ins);
	}	
	
	/* free mixer output groups */
	if(mixEngineRef->outs){
		outChannel *chrec = mixEngineRef->outs;
		for(i=0; i<mixEngineRef->outCount; i++){
			if(chrec->name)
				free(chrec->name);
			if(chrec->portList)
				free(chrec->portList);
			if(port = chrec->jPorts){
				munlock(chrec->jPorts, sizeof(jack_port_t *) * mixEngineRef->chanCount);
				free(chrec->jPorts);
			}
			chrec++;	
		}
		munlock(mixEngineRef->outs, sizeof(outChannel) * mixEngineRef->outCount);	
		free(mixEngineRef->outs);		
	}
	pthread_cond_destroy(&mixEngineRef->changedSemaphore);
	pthread_mutex_destroy(&mixEngineRef->changedMutex);
	pthread_rwlock_destroy(&mixEngineRef->outGrpLock);
	
	/* free control ports and queues */
	jack_port_unregister(mixEngineRef->client, mixEngineRef->ctlInPort);
	jack_port_unregister(mixEngineRef->client, mixEngineRef->ctlOutPort);
	if(mixEngineRef->ctlInQueue){
		munlock(mixEngineRef->ctlInQueue, ctlQueueSizeBytes);
		jack_ringbuffer_free(mixEngineRef->ctlInQueue);
	}
	if(mixEngineRef->ctlOutQueue){
		munlock(mixEngineRef->ctlOutQueue, ctlQueueSizeBytes);
		jack_ringbuffer_free(mixEngineRef->ctlOutQueue);
	}	
	pthread_mutex_destroy(&mixEngineRef->ctlOutQueueMutex);
	pthread_cond_destroy(&mixEngineRef->ctlInQueueSemaphore);
	pthread_mutex_destroy(&mixEngineRef->ctlInQueueMutex);  
	
	pthread_mutex_destroy(&mixEngineRef->cbQueueMutex);  
	pthread_cond_destroy(&mixEngineRef->cbQueueSemaphore);

	pthread_spin_destroy(&mixEngineRef->cbQueue.spinlock);
	jack_client_close(mixEngineRef->client);

	/* free mixer container structure */
	munlock(mixEngineRef, sizeof(mixEngineRec));
	free(mixEngineRef);
}

void updateOutputConnections(mixEngineRecPtr mixEngineRef, outChannel *rec, unsigned char updatePorts, const char *portList){
	/* NOTE:  the outGrpLock lock is assumed to already be read locked, or
	 * if updatePorts is true, write locked. If updatePorts is false, the
	 * existing record list will be used to reconnect ports, if needed       */

	jack_port_t **cPort;
	int c, cmax, i;
	char *curPortList;
	char *oldChList, *newChList, *portName;
	const char *newPortList;
	
	/* convert null strings to empty strings */
	newPortList = portList;
	if(!newPortList)
		newPortList = "";
	curPortList = rec->portList;
	if(!curPortList)
		curPortList = "";
		
	if(updatePorts){
		/* remove current connections not in the new list */
		cPort = rec->jPorts;
		cmax = mixEngineRef->chanCount;
		for(c=0; c<cmax; c++){
			oldChList = str_NthField(curPortList, "&", c);
			newChList = str_NthField(newPortList, "&", c);
			i = 0;
			while(oldChList && (portName = str_NthField(oldChList, "+", i))){
				if(!newChList || (!strstr(newChList, portName))){
					// not in new list: disconnect the port
					if(strlen(portName))
						jack_disconnect(mixEngineRef->client, jack_port_name(*cPort), portName);
				}
				i++;
				free(portName);
			}
			if(newChList)
				free(newChList);
			if(oldChList)
				free(oldChList);
			cPort++;
		}			
		/* set new list to current; new list (portList) can be NULL */
		if(rec->portList)
			free(rec->portList);
		if(portList){
			rec->portList = strdup(portList);
			
		}else{
			// list is null... we are clearing this record
			rec->portList = NULL;
			if(rec->name)
				free(rec->name);
			rec->name = NULL;
		}
	}
			
	/* make connection in the current list if not already established */
	if(rec->portList){
		cPort = rec->jPorts;
		cmax = mixEngineRef->chanCount;
		for(c=0; c<cmax; c++){	
			if(newChList = str_NthField(rec->portList, "&", c)){
				i = 0;
				while(portName = str_NthField(newChList, "+", i)){
					if(!jack_port_connected_to(*cPort, portName))
						jack_connect(mixEngineRef->client, jack_port_name(*cPort), portName);
					free(portName);
					i++;
				}
				free(newChList);
			}
			cPort++;
		}
	}
}
