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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <string.h>
#include <libgen.h>
#include <sys/mman.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/app/gstappsrc.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>

// cType (control packet) real-time flagged packets are handled directly
// in the jack process function.  Others are queued for handing in a 
// non-realtime thread.

#define cPeer_MASK		0x70
#define cPeer_none		0x00
#define cPeer_player	0x10
#define cPeer_recorder	0x20
#define cPeer_bus		0x30
#define cPeer_allrec	0x40

#define cType_MASK		0x0f
#define cType_tags	 	0	// tags - data is counted json string of track tags
#define cType_pos 		1 	// position - data is a float (seconds)
#define cType_start 	2	// start - data is empty	
#define cType_stop	 	3	// stop - data is empty
#define cType_end	 	4	// reached media end - data is empty.  Closes recorder
#define cType_anc	 	5	// anounce (recorder) - data is json collection of recorder settings.
#define cType_vu	 	6	// vU meters (recorder) - data is array of vuNData type (see below)
#define cType_err	 	7	// error message - data is uint32 error code
#define cType_reid	 	8	// change UID (recorder) - data is a new uint32 UID
#define cType_vol	 	9	// change volume - data is a float
#define cType_lock	 	10	// set recorder to locked - data is empty
#define cType_unlock	11	// set recorder to unlocked - data is empty

#define	queueSizeBytes 	64 * 1024

// rec status bits: b0-12 status, b16-31 are error code, if any 
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

typedef union { 
	float_t			fVal;
	int32_t			iVal;
	int8_t			cVal[4];
} valuetype;

typedef struct __attribute__((packed)){	
						/* Structure of contol packet: used to control/communicate with recorders and players
						 * NOTE: packets passed as midi data via jackaudio midi API, even though the data format
						 * is not midi.  Jack is fine with this, but don't try to connect the arserver control midi
						 * ports to other applications that are expecting actual midi data. */
	uint8_t				type;
	uint32_t			peer;		// network byte order - player input number [0,N] or recorder UID.
	uint16_t			dataSize;	// network byte order - size, in bytes, of the data, if any.
	int8_t				data[1];	// network byte order or text.  Text need NOT to be null terminated
									// due to the size (length) specified above.
}controlPacket;

typedef struct __attribute__((packed)){		/* data structures for each channel inside of vuInstance */
	// values are saclar magnitude
	uint8_t	peak;		// value is 255 times the sqrt of scalar magnitude (VU metere like scaling)
	uint8_t	avr;		// value is 255 times the sqrt of scalar magnitude (VU metere like scaling)
}vuNData; 

typedef struct{
	// values are scalar magnitude
	float	peak;		
	float	avr;
} vuData;

#define cbQsize	256		// must be a power of 2
#define cbMASK	cbQsize-1
typedef struct{
	jack_port_id_t portID[cbQsize];
	volatile unsigned int rdIdx;
	volatile unsigned int wrIdx;
	pthread_spinlock_t spinlock;
} callbackQueue;

typedef struct {
		uint32_t ID;
		uint32_t fingerprint;
        char *name;
        char *artist;
        char *album;
        char *source;
		char *comment;
		char *owner;
        uint32_t albumID;
        uint32_t artistID;
		uint32_t ownerID;
}ProgramLogRecord;

/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
	unsigned int chCount;
	unsigned int sampleRate;
	long limit;
	time_t start;
	float vol;
	uint32_t UID;
	uint32_t status;
	GstState state;
	GstElement *pl;  		/* gst pipeline */
	GstElement *asrc;  		/* app source element */
	FILE *ascPlayList;
	const char *aplPath;
	int fpFilePos;
	const char *pipeline;
	callbackQueue cbQueue;
	jack_client_t *client;
	const char *ourJackName;
	const char *portList;
	const char *ctlClient;
	int vuSampleRem;
	unsigned int vuPeriod;
	vuData *VUmeters; 			// pointer to chCount array of vuData
	jack_port_t **audioIn_jPorts; // pointer to chCount array of jack ports
	jack_port_t *midiIn_jPort;	// midi in control 
	jack_port_t *midiOut_jPort;	// midi out status/time
	jack_ringbuffer_t *ringbuffer;
	jack_ringbuffer_t *ctlsendqueue;
	jack_ringbuffer_t *ctlrecvqueue;
	jack_default_audio_sample_t **jbufs; // pointer to chCount array of jack ports data buffers
	gboolean settingsChanged;
	gboolean flushAudio;	/* truse to trigger flushing back audio from the ring buffer */
	gboolean closeReq;
	gboolean closeWaiting;	/* flag set indicating that a shutdown request has been made and we are waiting for the pipeline to finish */
	gboolean eos;			/* flag to send End-Of-Stream control message */
	gboolean terminate;		/* Should we terminate execution? */
	gboolean persist;		/* should we keep running when jack ports are disconnected? */
	unsigned char tagBus;	/* bit number [1..8] of tag play bus that must be set to pass tags down pipeline */
	GstClockTime curPos;			/* position update in gst clock time units */
	pthread_mutex_t ctlMutex;
	pthread_cond_t ctlSemaphore;
	pthread_mutex_t pushMutex;
	pthread_cond_t pushSemaphore;
	pthread_t pushThread;
	pthread_t ctlThread;
	unsigned char connected;
} CustomData;

CustomData data;
	
void INThandler(int sig){	/* CONTROL-C toggles record/pause */
	if(data.status & (rec_start | rec_running)){
		data.status = data.status & ~(rec_start | rec_running);
	}else{
		if(data.start)
			data.status = data.status | rec_wait;
		else{
			if(data.state == GST_STATE_PLAYING)
				data.status = data.status | (rec_ready | rec_running);
			else
				data.status = data.status | rec_start;
		}
	}
	data.settingsChanged = TRUE;
	pthread_cond_broadcast(&data.ctlSemaphore);
}

void QUIThandler(int sig){	/* CONTROL-\ triggers end-of-recording clean-up/shutdown */
	data.closeReq = 1;
}

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

unsigned char ftovu(float linMag){
	// convert linMag to 1.7 fixed point format
	float scale;
	
	// linMag is assumed to be magnitude squared!
	scale = 255.0;
	linMag = sqrtf(linMag);			// un-squares the value passed in
	linMag = scale * sqrtf(linMag);	// square root again to make the vu scale close to an analog meter
	if(linMag > 255.0)
		return 255;
	else
		return (unsigned char)linMag;
}

unsigned int str_CountFields(char *string, const char *token){
	unsigned int result;
	int tsize;
	
	result = 0;
	if(tsize = strlen(token)){
		while(string = strstr(string, token)){
			result++;
			string = string + tsize;
		}
	}
	return result;
}

char *str_NthField(const char *string, const char *token, unsigned int field){
	unsigned int tsize;
	char *result, *tmp;
	
	/* if result is not empty, string has been copied and will need to be freed */
	result = NULL;	
	if(tsize = strlen(token)){
		while(field > 0){
			if(string = strstr(string, token)){
				field--;
				string = string + tsize;
			}else
				return result;
		}
		result = strdup(string);
		if(tmp = strstr(result, token))
			*tmp = 0;
	}
	return result;	
}

void str_setstr(char **string, const char *cStr){
	/* if *string is NULL, this will allocate memory for it as well */
	unsigned int size;
	
	if(cStr == NULL)
		return;
	if(*string != NULL)
		free(*string);
	*string = strdup(cStr);
}

void str_appendstr(char **string, const char *cStr){
	/* if *string is NULL, this will allocate memory for it as well */
	unsigned int size;
	
	if(cStr == NULL)
		return;
	size = strlen(cStr) + 1;
	if(*string == NULL)
		*string = calloc(1, size);
	size = strlen(*string) + size;
	*string = realloc(*string, size);
	strcat(*string, cStr);
}

void str_insertstr(char **string, const char *cStr, unsigned int pos){
	unsigned int sizeA, sizeB;
	
	if(cStr == NULL)
		return;
	if(*string == NULL)
		return;
	sizeA = strlen(*string) + 1;
	sizeB = strlen(cStr);
	if(pos > sizeA)
		return;
	*string = realloc(*string, sizeA + sizeB);
	memmove(*string + sizeB + pos, *string + pos, sizeA-pos);
	memcpy(*string + pos, cStr, sizeB);
}

char *ustr(unsigned long num){
	/* result will need to be freed */
	char buf[32];
	snprintf(buf, sizeof buf, "%lu", num);
	return strdup(buf);
}

char *fstr(double num, int dp){
	/* result will need to be freed */
	char buf[32];
	snprintf(buf, sizeof buf, "%.*f", dp, num);
	return strdup(buf);
}

int CreateFPLHeader(FILE *fpl, char *Name, int *fpFilePos){
	char *tmp = NULL;
	char *fp;
	int durFilePos;
	
	if(fpl == NULL)
		return -1;
	fputs("Type\tfilepl\n", fpl);
	fputs("Revision\t1.0\n", fpl);
	durFilePos = ftell(fpl);
	fputs("Duration\t0\n-------------\n", fpl);
	str_setstr(&tmp, "Name\t");
	str_appendstr(&tmp, Name);
	str_appendstr(&tmp,"\n");
	fputs(tmp, fpl);	
	if(fpFilePos)
		*fpFilePos = ftell(fpl);
	fputs("Fingerprint\t0\n-------------\n", fpl);
	fputs("\n", fpl);
	fflush(fpl);
	free(tmp);
	return durFilePos;
}

void CloseFPL(FILE *fpl, double duration, int durFilePos){	
	char *tmp = NULL;
	char *dur;
	
	if(fpl == NULL)
		return;
	if(durFilePos <= 0)
		return;
	if(fseek(fpl, durFilePos, SEEK_SET) == 0){
		str_setstr(&tmp, "Duration\t");
		str_appendstr(&tmp, (dur = fstr(duration, 1)));
		free(dur);
		str_appendstr(&tmp, "\n");
		fputs(tmp, fpl);	
	}
	free(tmp);
	fclose(fpl);
}

void AddFPLEntryFromProgramLogStruct(FILE *fpl, double Offset, ProgramLogRecord *Rec, int *fpFilePos){	
	char *fpstr, *tmp;
	char *line = NULL;

	if(fpl == NULL)
		return;
	if(Rec == NULL)
		return;
	
	if(fpFilePos && *fpFilePos && Rec->fingerprint){
		size_t curLoc = ftell(fpl);
		if(fseek(fpl, *fpFilePos, SEEK_SET) == 0){
			str_setstr(&line, "Fingerprint\t");
			str_appendstr(&line, (fpstr = ustr(Rec->fingerprint)));
			free(fpstr);
			str_appendstr(&line, "\n");
			fputs(line, fpl);	
			fflush(fpl);
			fseek(fpl, curLoc, SEEK_SET);
		}
		*fpFilePos = 0;
	}
	
	// Use the data in the ProgramLogStruct
	// set up strings
	if(Rec->name && strlen(Rec->name)){
		str_setstr(&line, "Name\t");
		str_appendstr(&line, Rec->name);
		str_appendstr(&line, "\n");
		fputs(line, fpl);
	}
	if(Rec->artist && strlen(Rec->artist)){
		str_setstr(&line, "Artist\t");
		str_appendstr(&line, Rec->artist);
		str_appendstr(&line, "\n");
		fputs(line, fpl);
	}
	if(Rec->album && strlen(Rec->album)){
		str_setstr(&line, "Album\t");
		str_appendstr(&line, Rec->album);
		str_appendstr(&line, "\n");
		fputs(line, fpl);
	}
	str_setstr(&line, "URL\t");
	str_appendstr(&line, Rec->source);
	str_appendstr(&line, "\n");
	fputs(line, fpl);
	
	// set up Integers
	str_setstr(&line, "ID\t");
	str_appendstr(&line, (tmp = ustr(Rec->ID)));
	free(tmp);
	str_appendstr(&line, "\n");
	fputs(line, fpl);
	
	str_setstr(&line, "ArtistID\t");
	str_appendstr(&line, (tmp = ustr(Rec->artistID)));
	free(tmp);
	str_appendstr(&line, "\n");
	fputs(line, fpl);
	
	str_setstr(&line, "AlbumID\t");
	str_appendstr(&line, (tmp = ustr(Rec->albumID)));
	free(tmp);
	str_appendstr(&line, "\n");
	fputs(line, fpl);
	
	// set up offset
	str_setstr(&line, "Offset\t");
	str_appendstr(&line, (tmp = fstr(Offset, 1)));
	str_appendstr(&line, "\n\n");
	fputs(line, fpl);
	fflush(fpl);
	free(line);
}

void checkPortReconnect(CustomData *data, jack_port_id_t port_id){
	jack_port_t **port;
	jack_port_t *cport;
	const char *name;
	char *chanList = NULL;
	char *portName = NULL;
	int c, i;
	
	if(cport = jack_port_by_id(data->client, port_id)){
		name = jack_port_name(cport);

		/* check audio port list */
		port = data->audioIn_jPorts;
		for(c=0; c<data->chCount; c++){
			if(chanList = str_NthField(data->portList, "&", c)){
				i = 0;
				while(portName = str_NthField(chanList, "+", i)){
					if(!strcmp(portName, name)){
						/* registered port name matches a port in the connect list */
						if(!jack_port_connected_to(*port, name)){
							/* and we are not currently connected to it: reconnect now */
							if(jack_connect(data->client, name, jack_port_name(*port)))
								g_printerr("\nERROR: failed to re-connect to JACK port  %s.\n", name);
							free(portName);
							free(chanList);
							return;
						}
					}
					free(portName);
					i++;
				}
				free(chanList);
			}
			port++;
		}

		/* check midi/control ports */
		char pname[256];
		snprintf(pname, sizeof pname, "%s:ctlIn", data->ctlClient);
		if(!strcmp(pname, name)){
			/* registered port name matches a the controlIn port */
			if(!jack_port_connected_to(data->midiOut_jPort, name)){
				/* and we are not currently connected to it: reconnect now */
				if(jack_connect(data->client, jack_port_name(data->midiOut_jPort), name))
					g_printerr("\nERROR: failed to re-connect to JACK port  %s.\n", name);
				return;
			}
		}				
		snprintf(pname, sizeof pname, "%s:ctlOut", data->ctlClient);
		if(!strcmp(pname, name)){
			/* registered port name matches a the controlIn port */
			if(!jack_port_connected_to(data->midiIn_jPort, name)){
				/* and we are not currently connected to it: reconnect now */
				if(jack_connect(data->client, name, jack_port_name(data->midiIn_jPort)))
					g_printerr("\nERROR: failed to re-connect to JACK port  %s.\n", name);
				return;
			}
		}
	}
} 

static void jack_reg_callback(jack_port_id_t port_id, int isReging, void *arg){
	CustomData *data = (CustomData *)arg;
	
	if(isReging){
		/* this is a port registration notice, not an un-register notice 
		 * scan though our port connection list to see if it is a port we
		 * were previously connected to and should try to connect to again */
		 putCBQitem(&data->cbQueue, port_id);
	}
}

static void jack_rename_callback(jack_port_id_t port_id, const char *old_name, const char *new_name, void *arg){
	CustomData *data = (CustomData *)arg;
	
	/* this is a port rename notice. scan though our port connection 
	 * list to see if it is a port we were previously connected to and 
	 * should try to connect to again */
	putCBQitem(&data->cbQueue, port_id);
}

void jack_shutdown_callback(void *arg){
	CustomData *data = (CustomData *)arg;
	/* Jack server went away... shutdown. */
	data->closeReq = TRUE;
	data->client = NULL;
	pthread_cond_broadcast(&data->pushSemaphore);
}

static int jack_process(jack_nframes_t nframes, void *arg){
	CustomData *data = (CustomData *)arg;
	jack_port_t **port;
	unsigned int i, s; 
	float SampSqrd, pk, avr, vol;
	vuData *vu;
	jack_default_audio_sample_t *samp;
	jack_default_audio_sample_t *dest, *src;
	jack_ringbuffer_data_t rbData[2];
	jack_ringbuffer_data_t *rbdPtr;
	size_t sampCnt, sampWrite, cnt;
	jack_midi_event_t in_event;
	jack_nframes_t event_count;
	controlPacket *packet;
	controlPacket header;
	valuetype *val;
	void *midi_bufferIn, *midi_bufferOut;
	
	midi_bufferOut = jack_port_get_buffer(data->midiOut_jPort, nframes);
	jack_midi_clear_buffer(midi_bufferOut);
	if(data->eos){
		/* send end of media message */	
		if(packet = (controlPacket *)jack_midi_event_reserve(midi_bufferOut, 0, 7)){
			packet->type = cType_end | cPeer_recorder;	
			packet->peer = htonl(data->UID);
			packet->dataSize = 0;
		}
		data->eos = FALSE;
	}

	// Check tags (send queue) for non-realtime queued packets
	// to be sent, upto one per process cycle: settings, VU, etc.
	cnt = jack_ringbuffer_peek(data->ctlsendqueue, (char*)&header, 7);
	if(cnt == 7){
		cnt = 7 + ntohs(header.dataSize);
		if(jack_ringbuffer_read_space(data->ctlsendqueue) >= cnt){
			if(packet = (controlPacket *)jack_midi_event_reserve(midi_bufferOut, 0, cnt)){
				jack_ringbuffer_read(data->ctlsendqueue, (char*)packet, cnt);
			}
		}
	}
			
	/* handle received packets */		
	midi_bufferIn = jack_port_get_buffer(data->midiIn_jPort, nframes);
	event_count = jack_midi_get_event_count(midi_bufferIn);
	char change_flag = FALSE;
	uint32_t peer = htonl(data->UID);
	for(i=0; i<event_count; i++){
		jack_midi_event_get(&in_event, midi_bufferIn, i);
		if(in_event.size > 6){
			packet = (controlPacket *)in_event.buffer;
			cnt = ntohs(packet->dataSize);
			if(((packet->type & cPeer_MASK) == cPeer_recorder) && (packet->peer == peer)){
				uint8_t type = packet->type & cType_MASK;
				if(type == cType_start){
					/* handle realtime control play packet */
					if((data->status & (rec_start | rec_running)) == 0){
						if(data->start)
							data->status = data->status | rec_wait;
						else{
							if(data->state == GST_STATE_PLAYING)
								data->status = data->status | (rec_ready | rec_running);
							else
								data->status = data->status | rec_start;
						}
						change_flag = TRUE;
					}
				}else if(type == cType_stop){
					/* handle realtime control pause packet */
					if((data->status & (rec_start | rec_running | rec_wait))){
						if(data->start)
							data->status = data->status & ~rec_wait;
						else
							data->status = data->status & ~(rec_start | rec_running);
						change_flag = TRUE;
					}
				}else if(type == cType_end){
					/* handle realtime control end packet */
					data->closeReq = TRUE;
					change_flag = FALSE;
				}else if(type == cType_lock){
					/* handle realtime control end packet */
					if(!(data->status & rec_locked)){
						data->status = data->status | rec_locked;
						change_flag = TRUE;
					}
				}else if(type == cType_unlock){
					/* handle realtime control end packet */
					if(data->status & rec_locked){
						data->status = data->status & ~rec_locked;
						change_flag = TRUE;
					}
				}else if(type == cType_vol){
					/* handle realtime record volume packet */
					if(cnt == 4){
						val = (valuetype *)&packet->data;
						val->iVal = ntohl(val->iVal);
						data->vol = val->fVal;
						change_flag = TRUE;
					}
				}else if(type == cType_reid){
					if(cnt == 4){
						val = (valuetype *)&packet->data;
						data->UID = ntohl(val->iVal);
						change_flag = TRUE;
						peer = htonl(data->UID);
					}
				}
			}else if(((packet->type & cType_MASK) == cType_tags)){ 
				if(((packet->type & cPeer_MASK) == cPeer_allrec) 
						|| (((packet->type & cPeer_MASK) == cPeer_bus) && (ntohl(packet->peer) & (1 << (data->tagBus - 1))))
						|| (((packet->type & cPeer_MASK) == cPeer_recorder) && (ntohl(packet->peer) == peer))){
					/* enqueue packet for non-realtime tag handling */
					if(((cnt+7) <= 2048) && (jack_ringbuffer_write_space(data->ctlrecvqueue) >= (cnt+7))){
						jack_ringbuffer_write(data->ctlrecvqueue, (char *)packet, cnt+7);
						change_flag = TRUE;
					}
				}
			}
		}
	} 

	/* check for start time trigger */
	if((data->status & rec_wait) && (data->start >= time(NULL))){
		data->status = data->status & ~rec_wait;	
		if(data->state == GST_STATE_PLAYING)
			data->status = data->status | rec_running;	
		else
			data->status = data->status | rec_start;	
		change_flag = TRUE;
		data->start = 0;	
	}
	
	sampWrite = 0;
	/* handle scaling and vu metering of audio in port(s) data */
	port = data->audioIn_jPorts;
	vol = data->vol;
	for(i=0; i<data->chCount; i++){
		/* get JACK port sample buffer */
		samp = data->jbufs[i] = jack_port_get_buffer(port[i], nframes);
		
		// volume  scaling
		pk = 0.0;
		avr = 0.0;
		for(s = 0; s < nframes; s++){
			*samp = *samp * vol;
			// VU meter sample calculations - all VU levels are squared (power)
			SampSqrd = (*samp) * (*samp);
			avr = avr + SampSqrd;
			if(SampSqrd > pk)
				pk = SampSqrd;
			samp++;
		}
		
		/* VU Block calculations */
		vu = &(data->VUmeters[i]);
		// VU avarage over 10,000 samples - aprox 10 Hz @ sample rate = 96,000
		avr = ( 1 - (0.0001 * nframes)) * vu->avr + 0.0001 * avr;
		if(avr > 100.0)
			avr = 100.0; 
		vu->avr = avr;

		// VU peak fall time constatnt is 50,000 samples - aprox 2 Hz @ sample rate = 96,000
		vu->peak = vu->peak * ( 1 - (0.00002 * nframes));
		if(pk > 100.0)
			pk = 100.0;
		if(pk > vu->peak)
			vu->peak = pk;	
	}
	
	/* send uv control port data every 100mS */
	if(data->vuPeriod < nframes)
		data->vuPeriod = nframes;
	data->vuSampleRem = data->vuSampleRem - nframes;
	if(data->vuSampleRem <= 0){
		data->vuSampleRem = data->vuSampleRem + data->vuPeriod;
		cnt = (sizeof(controlPacket) - 1) + (data->chCount * sizeof(vuNData));
		if(packet = (controlPacket *)jack_midi_event_reserve(midi_bufferOut, nframes-1, cnt)){
			vuNData *values;
			packet->type = cType_vu | cPeer_recorder;	
			packet->peer = htonl(data->UID);
			packet->dataSize = htons(cnt - (sizeof(controlPacket) - 1));
			values = (vuNData *)&packet->data;
			for(i=0; i<data->chCount; i++){
				vu = &(data->VUmeters[i]);
				values->avr = ftovu(vu->avr), 
				values->peak = ftovu(vu->peak);
				values++;
			}
		}
	}
	
	if(data->closeReq)
		pthread_cond_broadcast(&data->pushSemaphore);
	else if((data->status & rec_running) || ((data->status & (rec_start | rec_wait)) == rec_start)){
		jack_ringbuffer_get_write_vector(data->ringbuffer, rbData);
		/* pain-in-the-ass ring buffer interleaving across a possibly split (ring) buffer */
		sampCnt = (rbData[0].len + rbData[1].len) / sizeof(jack_default_audio_sample_t);
		rbdPtr = &rbData[0];
		while(nframes && sampCnt){
			for(i=0; i<data->chCount; i++){
				dest = (jack_default_audio_sample_t*)rbdPtr->buf;
				src = data->jbufs[i];
				*dest = *src;
				rbdPtr->buf = rbdPtr->buf + sizeof(jack_default_audio_sample_t);
				data->jbufs[i] = data->jbufs[i] + 1;
				sampCnt--;
				sampWrite++;
				rbdPtr->len = rbdPtr->len - sizeof(jack_default_audio_sample_t);
				if(!rbdPtr->len)
					rbdPtr = &rbData[1];
			}
			nframes--;	
		}
		jack_ringbuffer_write_advance(data->ringbuffer, sampWrite * sizeof(jack_default_audio_sample_t)); 
		pthread_cond_broadcast(&data->pushSemaphore);
		if(nframes)
			// flag audio dropout error
			data->status = data->status | rec_err_keepup;
	}
	/* notify midi in status change execution thread of requests */	
	if(change_flag){
		data->settingsChanged = TRUE;
		pthread_cond_broadcast(&data->ctlSemaphore);
	}
	return 0;      
}

/* queue appsrc samples from ringbuffer */
void* pushRingbufferSamples(void *refCon){
	CustomData *data = (CustomData*)refCon;
	GstBuffer *buffer;
	void *mem;
	GstState state, pending;
	guint size;
	GstClockTime timestamp, dur;

	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
	do{
		// if we should be "playing" and the pipeline is stopped, get it going
		if((data->status & (rec_start | rec_running)) && (data->state != GST_STATE_PLAYING)){
			gst_element_set_state(data->pl, GST_STATE_PLAYING);
		} 

		if(data->closeReq){
			/* once the main thread notice that the pipline has
			 * completed, it will trigger a full program shutdown. */
			gst_element_send_event(data->asrc, gst_event_new_eos());
			data->closeReq = FALSE;
			data->status = data->status & ~(rec_start | rec_running);
			data->closeWaiting = TRUE;
		}
			 
		if(data->flushAudio){
			if(size = jack_ringbuffer_read_space(data->ringbuffer))
				jack_ringbuffer_read_advance(data->ringbuffer, size);
			data->flushAudio = FALSE;
		}
		
		if(size = jack_ringbuffer_read_space(data->ringbuffer)){
			if(mem = malloc(size)){
				if(buffer = gst_buffer_new_wrapped(mem, size)){
					jack_ringbuffer_peek(data->ringbuffer, mem, size);
					timestamp = data->curPos;
					dur = gst_util_uint64_scale_int(size, GST_SECOND, data->sampleRate * data->chCount * sizeof(jack_default_audio_sample_t));
					GST_BUFFER_TIMESTAMP(buffer) = timestamp;
					GST_BUFFER_DURATION(buffer) = dur;
					if(gst_app_src_push_buffer(GST_APP_SRC(data->asrc), buffer) == GST_FLOW_OK){
						jack_ringbuffer_read_advance(data->ringbuffer, size);
						data->curPos = timestamp + dur; 
						if(data->limit && (gst_util_uint64_scale_int(data->curPos, 1, GST_SECOND) >= data->limit)){
							// reached recording time limit
							data->status = data->status | rec_done;
							gst_element_send_event(data->pl, gst_event_new_eos());
							data->closeReq = TRUE;
						}
					}else{
						// failed to hand off buffer to gstreamer pipeline
						gst_buffer_unref(buffer);
					}
				}else
					free(mem);
			}
		}
		pthread_mutex_lock(&data->pushMutex);
		pthread_cond_wait(&data->pushSemaphore, &data->pushMutex);
		pthread_mutex_unlock(&data->pushMutex);
	}while(!data->terminate);
	
	return NULL;
}

static gboolean gst_send_tag_event(GstElement *src, GstTagList *tags){
	GstEvent *event;

	if(gst_tag_list_is_empty(tags)){
		gst_tag_list_unref (tags);
		return FALSE;
	}
	event = gst_event_new_tag(tags);
    GST_EVENT_TIMESTAMP(event) = 0;
	return gst_pad_push_event(GST_BASE_SRC_PAD(src), event);
}

char *settingsToControlPacketData(CustomData *data){
	cJSON *obj;
	char *str;
	
	obj = cJSON_CreateObject();
	cJSON_AddStringToObject(obj, "Name", data->ourJackName);
	cJSON_AddStringToObject(obj, "Type", "encoder");
	cJSON_AddStringToObject(obj, "Pipeline", data->pipeline);
	cJSON_AddStringToObject(obj, "Ports", data->portList);
	if(data->aplPath)
		cJSON_AddStringToObject(obj, "MakePL", data->aplPath);
	cJSON_AddNumberToObject(obj, "Limit", data->limit);
	if(data->persist)
		cJSON_AddTrueToObject(obj, "Persistent");
	else
		cJSON_AddFalseToObject(obj, "Persistent");
	if(data->status & rec_locked)
		cJSON_AddTrueToObject(obj, "Locked");
	else
		cJSON_AddFalseToObject(obj, "Locked");
	cJSON_AddNumberToObject(obj, "TagBus", data->tagBus); // bus bit +1
	cJSON_AddNumberToObject(obj, "Start", data->start);
	cJSON_AddNumberToObject(obj, "Position", (double)gst_util_uint64_scale_int(data->curPos, 10, GST_SECOND) * 0.1);
	cJSON_AddNumberToObject(obj, "Volume", data->vol);
	
	cJSON_AddNumberToObject(obj, "Status", data->status);
	str = cJSON_PrintUnformatted(obj);
	cJSON_Delete(obj);	
	return str;
}

void* handleCtlQueues(void *refCon){
	CustomData *data = (CustomData*)refCon;
	GstTagList *tags;
	struct timeval tv;
	struct timespec ts;
	int cause, cnt;
	char *jstr;
	size_t len;
	controlPacket *packet;
	cJSON *obj, *ar, *item;
	controlPacket header;
	ProgramLogRecord logRec;


    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    do{
		/* check for writing settings to outqueue */
		if(data->settingsChanged){

			jstr = settingsToControlPacketData(data);
			len = strlen(jstr);
			if((len <= 2048) && (jack_ringbuffer_write_space(data->ctlsendqueue) >= (len + 7))){
				// enque packet in midi/control queue ring buffer
				controlPacket header;
				header.type = cPeer_recorder | cType_anc;
				header.peer = htonl(data->UID);
				header.dataSize = htons(len);
				jack_ringbuffer_write(data->ctlsendqueue, (char*)&header, 7);
				jack_ringbuffer_write(data->ctlsendqueue, jstr, len);	
			}
			free(jstr);
			data->settingsChanged = FALSE;
		}
		
		/* check inqueue for tags */
		len = jack_ringbuffer_peek(data->ctlrecvqueue, (char*)&header, 7);
		if(len == 7){
			len = ntohs(header.dataSize);
			if(jack_ringbuffer_read_space(data->ctlrecvqueue) >= (len+7)){
				if(packet = (controlPacket *)calloc(1, len+8)){
					jack_ringbuffer_read(data->ctlrecvqueue, (char*)packet, len+7);
					packet->data[len] = 0;
					if(obj = cJSON_Parse(packet->data)){
						if(item = obj->child){ 
							tags = gst_tag_list_new_empty();
							gst_tag_list_set_scope(tags, GST_TAG_SCOPE_GLOBAL);
							memset(&logRec, 0, sizeof(ProgramLogRecord));
							do{
								if(item->child && item->string){
									if(jstr = cJSON_PrintUnformatted(item)){
										str_insertstr(&jstr, "=", 0);
										str_insertstr(&jstr, item->string, 0);
										gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, "extended-comment", jstr, NULL);
										free(jstr);
									}
								}else if(!strcasecmp(item->string, "Name")){
									if(item->valuestring && (strlen(item->valuestring))){
										gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_TITLE, item->valuestring, NULL);
										logRec.name = item->valuestring;
									}
								}else if(!strcasecmp(item->string, "Artist")){
									if(item->valuestring && (strlen(item->valuestring))){
										gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_ARTIST, item->valuestring, NULL);
										logRec.artist = item->valuestring;
									}
								}else if(!strcasecmp(item->string, "Album")){
									if(item->valuestring && (strlen(item->valuestring))){
										gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_ALBUM, item->valuestring, NULL);
										logRec.album = item->valuestring;
									}
								}else if(!strcasecmp(item->string, "Track")){
									if(jstr = ustr(item->valueint)){
										gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_TRACK_NUMBER, jstr, NULL);
										free(jstr);
									}
								}else if(!strcasecmp(item->string, "ISRC")){
									if(item->valuestring && (strlen(item->valuestring))){
										gst_tag_list_add(tags, GST_TAG_MERGE_REPLACE, GST_TAG_ISRC, item->valuestring, NULL);
									}
								}
							}while(item = item->next);
							gst_send_tag_event(data->asrc, tags);
							
							if(data->ascPlayList){
								if(ar = cJSON_GetObjectItem(obj, "AR")){
									if((item = cJSON_GetObjectItem(ar, "ID")) && (item->valueint))
										logRec.ID = item->valueint;
									if((item = cJSON_GetObjectItem(ar, "FP")) && (item->valueint))
										logRec.fingerprint = item->valueint;
									if((item = cJSON_GetObjectItem(ar, "OwnerID")) && (item->valueint))
										logRec.ownerID = item->valueint;
									if((item = cJSON_GetObjectItem(ar, "ArtistID")) && (item->valueint))
										logRec.artistID = item->valueint;
									if((item = cJSON_GetObjectItem(ar, "AlbumID")) && (item->valueint))
										logRec.albumID = item->valueint;
									if((item = cJSON_GetObjectItem(ar, "Owner")) && (item->valuestring))
										logRec.owner = item->valuestring;
									if((item = cJSON_GetObjectItem(ar, "Source")) && (item->valuestring))
										logRec.source = item->valuestring;
									if((item = cJSON_GetObjectItem(ar, "Comment")) && (item->valuestring))
										logRec.comment = item->valuestring;
								}
								AddFPLEntryFromProgramLogStruct(data->ascPlayList, 
													(double)gst_util_uint64_scale_int(data->curPos, 10, GST_SECOND) * 0.1,
													&logRec, &data->fpFilePos);
							}
						}
						cJSON_Delete(obj);
					}
					free(packet);
				}
			}
		}

		/* wait for signal or timeout in 10 seconds */
		gettimeofday(&tv, NULL);
		ts.tv_sec = time(NULL) + 10;
		ts.tv_nsec = 0;
		pthread_mutex_lock(&data->ctlMutex);
		cause = pthread_cond_timedwait(&data->ctlSemaphore, &data->ctlMutex, &ts);
		pthread_mutex_unlock(&data->ctlMutex);	
		if(cause == ETIMEDOUT)
			data->settingsChanged = TRUE;	// timed out: force settings send
	}while(!data->terminate);
	
    return NULL;
}

void handle_message(CustomData *data, GstMessage *msg) {
	GError *err;
	gchar *debug_info;
	GstState old_state, new_state, pending_state;
	GstTagList *tags = NULL;

	switch (GST_MESSAGE_TYPE (msg)) {
		case GST_MESSAGE_ERROR:
			gst_message_parse_error(msg, &err, &debug_info);
			g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
			g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
			g_clear_error(&err);
			g_free(debug_info);
			data->status = data->status | rec_err_con_fail;
			break;
		case GST_MESSAGE_EOS: 
			g_print ("End-Of-Stream from %s\n", GST_OBJECT_NAME (msg->src));
			if(!data->closeWaiting)
				data->status = data->status | rec_err_con_fail;
			gst_element_set_state(data->pl, GST_STATE_READY);
			break;
		case GST_MESSAGE_STATE_CHANGED:
			gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
			if(GST_MESSAGE_SRC(msg) == GST_OBJECT (data->pl)){
				g_print("Pipeline state changed from %s to %s:\n",
								gst_element_state_get_name(old_state), 
								gst_element_state_get_name(new_state));
				
				data->state = new_state;
								
				if(new_state == GST_STATE_READY){
					data->status = data->status | rec_ready;
					data->settingsChanged = TRUE;
					pthread_cond_broadcast(&data->ctlSemaphore);
				}
				if((new_state == GST_STATE_PLAYING) && !data->closeWaiting){
					data->status = data->status | (rec_ready | rec_running);
					if(data->status & rec_conn)
						data->status = data->status & ~(rec_err_con_fail | rec_conn);
					data->status = data->status & ~rec_start;
					data->settingsChanged = TRUE;
					pthread_cond_broadcast(&data->ctlSemaphore);
g_print("status set to running & ready\n");
				}					
				if((new_state == GST_STATE_READY) && (old_state != GST_STATE_NULL)){
					if(!data->closeWaiting && (data->status & (rec_start | rec_running))){
						// try to start up again
						g_print("trying to re-initialized pipeline...\n");
						data->flushAudio = TRUE;
						gst_element_set_state(data->pl, GST_STATE_NULL);
						data->status = rec_conn | rec_start;
						sleep(1);
						data->settingsChanged = TRUE;
						pthread_cond_broadcast(&data->ctlSemaphore);
					}
				} 
			}
			break;
					
		default:
			/* We should not reach here */
			g_printerr ("Unexpected message received.\n");
			break;
	}
	gst_message_unref(msg);
}

void mainloop(int next_arg, char *argv[], int apl_arg, unsigned char persist, long limit, time_t start, unsigned char tagBus){
	GError *err = NULL;
	GstStateChangeReturn ret;
	GstMessage *msg = NULL;
	GstBus *bus = NULL;
	GstAudioInfo info; 
	GstCaps *audio_caps;
	jack_options_t options = JackUseExactName;
	jack_status_t status;
	const char *server = NULL;
	jack_port_t **port;
	jack_port_id_t *IDptr;
	unsigned int i;
	size_t rbsize;
	int durFilePos;
	GstState state, pending;
	unsigned int disconnected;

	mlock(&data, sizeof(CustomData));
	data.state = 0;
	data.status = 0;
	data.settingsChanged = FALSE;
	data.portList = argv[next_arg+3];
	data.ctlClient = argv[next_arg+1];
	data.pipeline = argv[next_arg+4];
	data.aplPath = NULL;
	data.ascPlayList = NULL;
	data.vol = 1.0;
	data.VUmeters = NULL;
	data.terminate = FALSE;	
	data.eos = FALSE;
	data.fpFilePos = 0;
	data.chCount = 0;
	data.connected = 0;
	data.vuPeriod = 0;
	data.vuSampleRem = 0;
	data.audioIn_jPorts = NULL;
	data.midiIn_jPort = NULL;
	data.midiOut_jPort = NULL;
	data.ringbuffer = NULL;
	data.ctlsendqueue = NULL;
	data.ctlrecvqueue = NULL;
	data.jbufs = NULL;
	data.flushAudio = FALSE;
	data.closeReq = FALSE;
	data.closeWaiting = FALSE;
	data.pushThread = 0;
	data.ctlThread = 0;
	data.curPos = 0;
	data.persist = persist;
	data.limit = limit;
	data.start = start;
	data.tagBus = tagBus;
	pthread_spin_init(&data.cbQueue.spinlock, PTHREAD_PROCESS_PRIVATE);
	clearCBQ(&data.cbQueue);
	
	/* initialise the gstreamer system */
	gst_init(NULL, NULL);
	
	/* create a pipeline using gst_parse_launch, which
	 * construts a pipeline from a text description. */
	data.pl = gst_parse_launch(argv[next_arg+4], &err);
	if(err){
		g_printerr("\nERROR: failed to create specified gstreamer pipline.");
		goto finish;
	}
	data.asrc = gst_bin_get_by_name(GST_BIN(data.pl), "audiosrc");
	if(!data.asrc){
		g_printerr("\nERROR: specified pipeline filed to include an appsrc element named audiosrc.\n");
		goto finish;
	}
	data.chCount = str_CountFields(argv[next_arg+3], "&") + 1;
	g_object_set(G_OBJECT(data.asrc), "stream-type", 0,
            "is-live", TRUE, "format", GST_FORMAT_TIME, NULL);
            
	/* setup JACK and get pads properties */
	data.client = jack_client_open(argv[next_arg], options, &status, server);
	if(data.client == NULL) {
		if(status & JackServerFailed)
			g_printerr("\nERROR: Unable to connect to JACK server\n");
		else
			g_printerr("\nERROR: jack_client_open() failed\n");
		goto finish;
	}
	data.ourJackName = jack_get_client_name(data.client);
	data.sampleRate = jack_get_sample_rate(data.client);
	/* size (in bytes) of a 10 second ring buffer */
	rbsize = sizeof(jack_default_audio_sample_t) * data.chCount *
		data.sampleRate * 10.0;	
	if((data.ringbuffer = jack_ringbuffer_create(rbsize)) == NULL){
		g_printerr("\nERROR: 10 second ring buffer allocation failed.\n");
		goto finish;
	}
	mlock(data.ringbuffer, rbsize);

	if((data.ctlsendqueue = jack_ringbuffer_create(queueSizeBytes)) == NULL){
		g_printerr("\nERROR: control queue send (settings, VU, etc.) ring buffer allocation failed.\n");
		goto finish;
	}
	mlock(data.ctlsendqueue, queueSizeBytes);
	
	if((data.ctlrecvqueue = jack_ringbuffer_create(queueSizeBytes)) == NULL){
		g_printerr("\nERROR: control queue receive (tags) ring buffer allocation failed.\n");
		goto finish;
	}
	mlock(data.ctlrecvqueue, queueSizeBytes);	
		
	/* create jack ports (audio and midi) */
	data.UID = atoi(argv[next_arg+2]);
	char pname[256];
	data.vuPeriod = data.sampleRate / 10;	// sample count for 100 mS 
	data.vuSampleRem = data.vuPeriod;
	data.VUmeters = (vuData *)calloc(data.chCount, sizeof(vuData));
	data.jbufs = (jack_default_audio_sample_t**)calloc(data.chCount, 
							sizeof(jack_default_audio_sample_t *));
	if(data.audioIn_jPorts = (jack_port_t**)calloc(data.chCount, 
											sizeof(jack_port_t *))){

		port = data.audioIn_jPorts; 
		for(i=0; i<data.chCount; i++){
			snprintf(pname, sizeof pname, "Input%d", i);
			*port = jack_port_register(data.client, pname,
						JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
			if(*port == NULL)
				break;
			port++;
		}
	}
	if(!data.audioIn_jPorts || !data.jbufs || (i < data.chCount)){
		g_printerr("\nERROR: JACK audio input ports allocation failed.\n");
		goto finish;
	}
	mlock(data.audioIn_jPorts, data.chCount * sizeof(jack_port_t *));
	mlock(data.jbufs, data.chCount * sizeof(jack_default_audio_sample_t *));
	/* register midi in and out ports for attached recorder control */
	data.midiOut_jPort = jack_port_register(data.client, "ctlOut", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	data.midiIn_jPort = jack_port_register(data.client, "ctlIn", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	if(!data.midiOut_jPort || !data.midiIn_jPort){
		g_printerr("\nERROR: JACK midi ports allocation failed.\n");
		goto finish;
	}

	/* Configure appsource to match JACK pad properties */
	gst_audio_info_set_format(&info, GST_AUDIO_FORMAT_F32, data.sampleRate, data.chCount, NULL);
	audio_caps = gst_audio_info_to_caps(&info);
	gst_app_src_set_caps(GST_APP_SRC(data.asrc), audio_caps);
	gst_caps_unref(audio_caps);
	
	/* create push thread to take realtime data from ringbuffer and push it into the gstreamer appsrc element */
	pthread_mutex_init(&data.pushMutex, NULL);  
	pthread_cond_init(&data.pushSemaphore, NULL);
	pthread_create(&data.pushThread, NULL, &pushRingbufferSamples, &data);
	
	/* create control port queue monitor thread */
	pthread_mutex_init(&data.ctlMutex, NULL);  
	pthread_cond_init(&data.ctlSemaphore, NULL);
	pthread_create(&data.ctlThread, NULL, &handleCtlQueues, &data);
	
	if(apl_arg){
		if(data.ascPlayList = fopen(argv[apl_arg], "w+")){

			char *tmp, *loc, *cpy = strdup(argv[apl_arg]);
			tmp = basename(cpy);
			if(loc = strrchr(tmp, '.'))
				*loc = 0;
			durFilePos = CreateFPLHeader(data.ascPlayList, tmp, &data.fpFilePos);
			data.aplPath = argv[apl_arg];
			free(cpy);
		}
	}
	
	/* get the message bus from the pipline */
	bus = gst_pipeline_get_bus(GST_PIPELINE(data.pl));
	
	/* set JACK callbacks */
	jack_set_process_callback(data.client, jack_process, &data);
	jack_on_shutdown(data.client, jack_shutdown_callback, &data);

	if(data.persist){
		jack_set_port_registration_callback(data.client, jack_reg_callback, &data);
		jack_set_port_rename_callback(data.client, jack_rename_callback, &data);
	}
	
	/* start JACK IO running */
	jack_activate(data.client);
	
	/* Connect JACK ports to specified client ports */
	char *chanList = NULL;
	char *portName = NULL;
	int c;
	port = data.audioIn_jPorts;
	for(c=0; c<data.chCount; c++){
		if(chanList = str_NthField(argv[next_arg+3], "&", c)){
			i = 0;
			while(portName = str_NthField(chanList, "+", i)){
				if(strlen(portName)){
					if(jack_connect(data.client, portName, jack_port_name(*port))){
						g_printerr("\nERROR: failed to connect to JACK port  %s.\n", portName);
						if(!data.persist){
							free(portName);
							free(chanList);
							// failed jack connection... quit
							goto finish;
						}
					}
				}
				free(portName);
				i++;
			}
			free(chanList);
		}
		port++;
	}
		
	gst_element_set_state(data.pl, GST_STATE_PLAYING);
	data.connected = 1;
	
	/* connect midi out port */
	snprintf(pname, sizeof pname, "%s:ctlIn", argv[next_arg+1]);
	if(!jack_connect(data.client, jack_port_name(data.midiOut_jPort), pname)){
		/* connect midi in port */
		snprintf(pname, sizeof pname, "%s:ctlOut", argv[next_arg+1]);
		if(jack_connect(data.client, pname, jack_port_name(data.midiIn_jPort)))
			g_printerr("\nERROR: failed to connect to %s:ctlOut midi port... no control: start/stop,end using signals (ctl-c, ctl-\\).\n", pname);
	}else{
		g_printerr("\nERROR: failed to connect to %s:ctlIn midi ports... no control: start/stop,end using signals (ctl-c, ctl-\\).\n", pname);
	}

	signal(SIGINT, INThandler);		// CTL-c
	signal(SIGQUIT, QUIThandler);	// ctl-\

	data.settingsChanged = TRUE;	// trigger initial setting announcement
	pthread_cond_broadcast(&data.ctlSemaphore);

	/* Listen to the bus */
	do{
		msg = gst_bus_timed_pop_filtered(bus, 1000 * GST_MSECOND,
				GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | 
				GST_MESSAGE_EOS);

		/* Parse message */
		if(msg != NULL){
			handle_message(&data, msg);
		}else{
			/* No message: this means the timeout expired.
			 * Check for port reconnrcts (if any) */
			while(IDptr = getCBQitem(&data.cbQueue))
				checkPortReconnect(&data, *IDptr);
				
			/* ...and update/print status & record time. */
			if(data.status & rec_running)
				g_print("Recording %.1f\r", (double)gst_util_uint64_scale_int(data.curPos, 10, GST_SECOND) * 0.1);
			else
				g_print("Stopped %.1f\r", (double)gst_util_uint64_scale_int(data.curPos, 10, GST_SECOND) * 0.1);
				
			if((data.closeWaiting) && (data.state != GST_STATE_PLAYING)){
				g_print("Shutting down.\n");
				data.eos = TRUE;
				sleep(1);	// wait for End-of-media message to be sent	
				data.terminate = TRUE;
			}
			/* if not persistent and all jack audio ports are disconnected, 
			* set close request flag to finish the gstreamer piepline, and 
			* eventually trigger a full program shutdown. */
			if((!data.persist) && (!data.closeWaiting)){
				/* check if our input ports are still connected */
				disconnected = 0;
				port = data.audioIn_jPorts;
				for(i=0; i<data.chCount; i++){
					/* noted disconnection of an input ports */
					if(data.connected && !jack_port_connected(port[i]))
						disconnected++;
				}
				if(disconnected >= data.chCount)
					data.closeReq = TRUE;
			}
		}
		
	}while(!data.terminate);
  
finish:
	/* free and shutdown */
	data.terminate = 1;
	pthread_spin_destroy(&data.cbQueue.spinlock);
	if(data.pushThread){
		pthread_cond_broadcast(&data.pushSemaphore);
		pthread_join(data.pushThread, NULL);
		pthread_cond_destroy(&data.pushSemaphore);
		pthread_mutex_destroy(&data.pushMutex);
	}
	if(data.ctlThread){
		pthread_cond_broadcast(&data.ctlSemaphore);
		pthread_join(data.ctlThread, NULL);
		pthread_cond_destroy(&data.ctlSemaphore);
		pthread_mutex_destroy(&data.ctlMutex);
	}
	if(data.client){
		jack_deactivate(data.client);
		jack_client_close(data.client);
	}
	if(data.audioIn_jPorts){
		munlock(data.audioIn_jPorts, data.chCount * sizeof(jack_port_t *));
		free(data.audioIn_jPorts);
	}
	if(data.ringbuffer){
		munlock(data.ringbuffer, rbsize);
		jack_ringbuffer_free(data.ringbuffer);
	}
	if(data.ctlsendqueue){
		munlock(data.ctlsendqueue, queueSizeBytes);
		jack_ringbuffer_free(data.ctlsendqueue);
	}
	if(data.ctlrecvqueue){
		munlock(data.ctlrecvqueue, queueSizeBytes);
		jack_ringbuffer_free(data.ctlrecvqueue);
	}
	if(data.jbufs){
		munlock(data.jbufs, data.chCount * sizeof(jack_default_audio_sample_t *));
		free(data.jbufs);
	}
	if(data.VUmeters){
		munlock(data.VUmeters, data.chCount * sizeof(vuData));
		free(data.VUmeters);
	}
	if(data.pl){
		gst_element_set_state(data.pl, GST_STATE_NULL);
		// wait upto 5 seconds for state change
		gst_element_get_state(data.pl, NULL, NULL, 5 * GST_MSECOND);
		if(bus)
			gst_object_unref(bus);
	}
	if(data.ascPlayList)
		CloseFPL(data.ascPlayList, (double)gst_util_uint64_scale_int(data.curPos, 10, GST_SECOND) * 0.1, durFilePos);
	munlock(&data, sizeof(CustomData));
	gst_deinit();
}

int main(int argc, char *argv[]){
	int i, aplarg;
	unsigned char bit;
	long limit;
	time_t start;
	unsigned char pers;

	
	if(argc >= 6){
		pers = 0;
		i = 1;
		aplarg = 0;
		limit = 0;
		start = 0;
		bit = 0;
		while(*argv[i] == '-'){
			if(!strcmp(argv[i], "-p")){
				// persistent when jack connections lost
				pers = 1;
				i++;
			}else
			if(!strcmp(argv[i], "-a")){
				i++;
				aplarg = i;
				i++;
			}else
			if(!strcmp(argv[i], "-l")){
				i++;
				limit = atol(argv[i]);
				i++;
			}else
			if(!strcmp(argv[i], "-b")){
				i++;
				bit = atol(argv[i]);
				i++;
			}else
			if(!strcmp(argv[i], "-s")){
				i++;
				start = atoll(argv[i]);
				i++;
			}else
				break;
		}
		if((argc - i) == 5){
			mainloop(i, argv, aplarg, pers, limit, start, bit);
			return 0;
		}
	}
	fprintf(stderr, "Usage: (optional, in front of required) [required - in order]\n"); 
	fprintf(stderr, "%s (-p) (-a file-path) (-s unix-start-time) (-l time-limit-seconds) (-b tags-bus-bit-number) [our unique Jack name] [control client name] [ctlUID] [jack port connection list] [gstreamer-pipline]\n\n", argv[0]);
	fprintf(stderr, "-p optionaly enables jack connection persistance, causing arRecorder to keep running when jack connections are lost\n");
	fprintf(stderr, "   attempting to reconnect to the ports specified in the jack port list if/when they become avalable again.\n");
	fprintf(stderr, "-a optionally specifies a file path to log track tag data that is received from audiorack control port in audiorack .apl format.\n");
	fprintf(stderr, "   Note that the -b option (see below) must also be set for any tracks to actually be logged.\n");
	fprintf(stderr, "-s optionaly enables recorder auto-starting at/after the specified unix-time value.\n");
	fprintf(stderr, "-l optionaly enables recorder auto-stoping at/after the specified record duration is reached.\n");
	fprintf(stderr, "-b optionaly enables the passing of song tag data, received from the control port, to the gstreamer pipeline\n");
	fprintf(stderr, "when the received tag play bus bits have the specified bit number [1..8] set.\n");
	fprintf(stderr, "Control client name is the Jack name for an arServer instance to which we will connect our control ports,\n");
	fprintf(stderr, "and ctlUID is our recorder unique ID with which we will tag control messages we send, and watch for taged messages\n");
	fprintf(stderr, "that match our id that we receive.\n");
	fprintf(stderr, "\nThe jack port list format is, by example:\n");
	fprintf(stderr, "\tclient:port1+client:port2&client:port3+client:port4\n");
	fprintf(stderr, "\n... which connects the our first channel to client source port1 AND port2, then connects our second channel\n");
	fprintf(stderr, "to client port3 AND port4. Multiple ports per channel are optional, specified with the + delimiter. \n");
	fprintf(stderr, "The & delimiter increments the list to our next channel connection. As such, our channel count is determined\n");
	fprintf(stderr, "by the number of & delimiters in the list.\n");
	fprintf(stderr, "\nThe gstreamer pipline must start with a source element named audiosrc: \"  appsrc name=audiosrc ! \"\n");
	fprintf(stderr, "NOTE: The gstreamer pipline must be a single parameter. Quoting or escaping of spaces may be required when run from a shell.\n");

	return 0;
}
