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
#include <string.h>
#include <sys/mman.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/app/gstappsink.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>
#include <pthread.h>
#include <arpa/inet.h>

// cType (control packet) real-time flagged packets are handled directly
// in the jack process function.  Others are queued for handing in a 
// non-realtime thread.

#define cPeer_MASK		0x70
#define cPeer_none		0x00
#define cPeer_player	0x10
#define cPeer_recorder	0x20
#define cPeer_bus		0x30

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
#define cType_posack	12	// arServer acknowlage of pos change back to a player - data is empty

#define	queueSizeBytes 	64 * 1024

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

/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
	char **argv;
	unsigned int chCount;
	unsigned int sampleRate;
	uint32_t ctlID;
	GstElement *src;  		/* source element */
	GstElement *asink;  	/* audio sink element */
	char *lastTags;
	jack_client_t *client;
	const char *ourJackName;
	jack_port_t **audioOut_jPorts; // pointer to chCount array of jack ports
	jack_port_t *midiIn_jPort;	// midi in control 
	jack_port_t *midiOut_jPort;	// midi out status/time
	jack_ringbuffer_t *ringbuffer;
	jack_ringbuffer_t *ctlqueue;
	jack_default_audio_sample_t **jbufs; // pointer to chCount array of jack ports data buffers
	gboolean endFlag;
	gboolean playing;      /* Are we in the PLAYING state? */
	gboolean playReq;
	gboolean pauseReq;
	gboolean terminate;    /* Should we terminate execution? */
	gboolean seek_enabled; /* Is seeking enabled for this media? */
	gboolean seek_done;    /* Have we performed the seek already? */
	gint64 duration;       /* How long does this media last, in nanoseconds */
	double curPos;			/* position update in seconds */
	double reqPos;			/* position request in seconds */
	gboolean posReq;		/* new position requested */
	gboolean posUpdate;		/* cur position send requested */
	pthread_mutex_t changedMutex;
	pthread_cond_t changedSemaphore;
	pthread_t changedThread;
	unsigned char connected;	/* 1 for audio, 2 for audio and control */
} CustomData;

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

void jack_shutdown_callback(void *arg){
	CustomData *data = (CustomData *)arg;
	/* Jack server went away... shutdown. */
	data->terminate = TRUE;
}

int jack_process(jack_nframes_t nframes, void *arg){
	CustomData *data = (CustomData *)arg;
	jack_port_t **port;
	unsigned int i;
	jack_default_audio_sample_t *dest, *src;
	jack_ringbuffer_data_t rbData[2];
	jack_ringbuffer_data_t *rbdPtr;
	size_t sampCnt, sampsRead, cnt;
	jack_midi_event_t in_event;
	jack_nframes_t event_count;
	controlPacket *packet;
	controlPacket header;
	void* midi_buffer;
			
	/* handle received control packets */		
	midi_buffer = jack_port_get_buffer(data->midiIn_jPort, nframes);
	event_count = jack_midi_get_event_count(midi_buffer);
	char change_flag = FALSE;
	for(i=0; i<event_count; i++){
		jack_midi_event_get(&in_event, midi_buffer, i);
		if(in_event.size > 6){
			uint32_t peer = htonl(data->ctlID);
			packet = (controlPacket *)in_event.buffer;
			if(((packet->type & cPeer_MASK) == cPeer_player) && (packet->peer == peer)){
				char type = packet->type & cType_MASK;
				cnt = htons(packet->dataSize);
				if(type == cType_posack){
					/* handle realtime pos. change ack packet */
					data->posUpdate = FALSE;
				}
				if(type == cType_start){
					/* handle realtime control play packet */
					data->playReq = TRUE;
					change_flag = TRUE;			
				}
				if(type == cType_stop){
					/* handle realtime control pause packet */
					data->pauseReq = TRUE;
					change_flag = TRUE;		
					data->endFlag = FALSE;
				}
				if((type == cType_pos) && (cnt == 4)){
					/* handle realtime control pos change packet */
					double syncTime;
					valuetype *val;
					val = (valuetype *)&packet->data;
					val->iVal = ntohl(val->iVal);
					syncTime = val->fVal;
					syncTime = syncTime - (double)(in_event.time + 1) / (double)data->sampleRate;
					/* syncTime is requested time in seconds adjusted for arrival time offset */
					if(!data->posReq){
						data->reqPos = syncTime;
						data->posReq = TRUE;
						change_flag = TRUE;
						data->endFlag = FALSE;
					}
				}
			}
		}
	}

	/* send any required control messages, if control port is connected */
	if(data->connected > 1){
		midi_buffer = jack_port_get_buffer(data->midiOut_jPort, nframes);
		jack_midi_clear_buffer(midi_buffer);
		if(data->endFlag){
			/* send end of media message */	
			if(packet = (controlPacket *)jack_midi_event_reserve(midi_buffer, 0, 7)){
				packet->type = cType_end | cPeer_player;	
				packet->peer = htonl(data->ctlID);
				packet->dataSize = 0;
				/* NOTE: we keep sending this message until we receive a 
				 * pause/stop back from arServer, then we clear the endFlag */
			}
		}
		if(data->posUpdate && data->seek_enabled){
			/* send pos control for current position (at start of this process cycle) time */
			valuetype *val;
			if(packet = (controlPacket *)jack_midi_event_reserve(midi_buffer, 0, 11)){
				packet->type = cType_pos | cPeer_player;	
				packet->peer = htonl(data->ctlID);
				packet->dataSize = htons(4);
				val = (valuetype *)&packet->data;
				val->fVal = data->curPos;
				val->iVal = htonl(val->iVal); 			
			}
		}

		// Check tags (control queue) for non-realtime queued packets
		// to be sent, upto one per process cycle
		cnt = jack_ringbuffer_peek(data->ctlqueue, (char*)&header, 7);
		if(cnt == 7){
			cnt = 7 + ntohs(header.dataSize);
			if(jack_ringbuffer_read_space(data->ctlqueue) >= cnt){
				if(packet = (controlPacket *)jack_midi_event_reserve(midi_buffer, 0, cnt)){
					jack_ringbuffer_read(data->ctlqueue, (char*)packet, cnt);
				}
			}
		}
	}
	
	sampsRead = 0;
	/* handle filling audio output ports with ringbuffer data */
	port = data->audioOut_jPorts;
	for(i=0; i<data->chCount; i++){
		/* get JACK port sample buffer */
		data->jbufs[i] = jack_port_get_buffer(port[i], nframes);
	}
	jack_ringbuffer_get_read_vector(data->ringbuffer, rbData);
	
	/* pain-in-the-ass buffer de-interleaving across a possibly split (ring) buffer */
	sampCnt = (rbData[0].len + rbData[1].len) / sizeof(jack_default_audio_sample_t);
	rbdPtr = &rbData[0];
	while(nframes && sampCnt){
		for(i=0; i<data->chCount; i++){
			src = (jack_default_audio_sample_t*)rbdPtr->buf;
			dest = data->jbufs[i];
			*dest = *src;
			rbdPtr->buf = rbdPtr->buf + sizeof(jack_default_audio_sample_t);
			data->jbufs[i] = data->jbufs[i] + 1;
			sampCnt--;
			sampsRead++;
			rbdPtr->len = rbdPtr->len - sizeof(jack_default_audio_sample_t);
			if(!rbdPtr->len)
				rbdPtr = &rbData[1];
		}
		nframes--;	
	}
	jack_ringbuffer_read_advance(data->ringbuffer, sampsRead * sizeof(jack_default_audio_sample_t)); 
	if(nframes){
		/* zero-fill remainder of frames */
		for(i=0; i<data->chCount; i++){
			dest = data->jbufs[i];
			memset(dest, 0, sizeof(jack_default_audio_sample_t) * nframes);
		}
	}
	
	/* notify midi in status change execution thread of requests */	
	if(change_flag)
		pthread_cond_broadcast(&data->changedSemaphore);
	return 0;      
}

/* The appsink has received a buffer */
GstFlowReturn pull_samples(GstAppSink *sink, gpointer* data){
	CustomData *appData = (CustomData*)data;
	GstSample *sample;
	GstBuffer *buf;
	GstMapInfo info;
	size_t bytes;
	
	/* Retrieve the buffer */
	sample = gst_app_sink_pull_sample(GST_APP_SINK(appData->asink));
	if(sample){
		buf = gst_sample_get_buffer(sample);
		if(buf){
			if(gst_buffer_map(buf, &info, GST_MAP_READ)){
				/* write the data into the real-time-thread-safe ring buffer 
				 * for the JACK realtime thread to read and send */
				bytes = jack_ringbuffer_write(appData->ringbuffer, info.data, info.size);
				gst_buffer_unmap(buf, &info);
				gst_sample_unref(sample);
				return GST_FLOW_OK;
			}
			gst_buffer_unmap(buf, &info);
		}
		gst_sample_unref(sample);
	}
	return GST_FLOW_ERROR;
}

gchar *gvalToString(GValue *val){
	gchar *str;
	if(G_VALUE_HOLDS_STRING(val))
		str = g_value_dup_string(val);
	else
		str = gst_value_serialize(val);
}

static void appendTagData(const GstTagList *tags, const gchar *tag, gpointer user_data) {
	cJSON *obj = (cJSON *)user_data;
	cJSON *ar;
	GValue val = { 0, };
	gchar *str;
	const char *prop;
	double num;

	gst_tag_list_copy_value(&val, tags, tag);
	prop = gst_tag_get_nick(tag);

	str = NULL;
	if(!strcmp(prop, GST_TAG_EXTENDED_COMMENT)){
		// special AR specific data
		str = gvalToString(&val);
		if(strstr(str, "AR=") == str){
			if(ar = cJSON_Parse(str+3))
				cJSON_AddItemToObject(obj, "AR", ar);
		}else{
			g_free(str);
			str = NULL;
		}
	}else if(!strcmp(prop, GST_TAG_TITLE)){
		str = gvalToString(&val);
		cJSON_AddStringToObject(obj, "Name", str);
	}else if(!strcmp(prop, GST_TAG_ARTIST)){
		str = gvalToString(&val);
		cJSON_AddStringToObject(obj, "Artist", str);
	}else if(!strcmp(prop, GST_TAG_ALBUM)){
		str = gvalToString(&val);
		cJSON_AddStringToObject(obj, "Album", str);
	}else if(!strcmp(prop, GST_TAG_TRACK_NUMBER)){
		str = gvalToString(&val);
		num = g_value_get_uint(&val);
		cJSON_AddNumberToObject(obj, "Track", num);
	}else if(!strcmp(prop, GST_TAG_ALBUM_ARTIST)){
		str = gvalToString(&val);
		cJSON_AddStringToObject(obj, "AlbumArtist", str);
	}else if(!strcmp(prop, GST_TAG_ISRC)){
		str = gvalToString(&val);
		cJSON_AddStringToObject(obj, "ISRC", str);
	}
	if(str)
		g_free(str);	

	g_value_unset(&val);
}

char *tagsToControlPacketData(const GstTagList *tags) {
	cJSON *obj;
	char *tagStr;
	
	obj = cJSON_CreateObject();
	gst_tag_list_foreach(tags, appendTagData, obj);
	tagStr = cJSON_PrintUnformatted(obj);
	cJSON_Delete(obj);	
	return tagStr;
}

void handle_message(CustomData *data, GstMessage *msg) {
	GError *err;
	gchar *debug_info;
	GstState old_state, new_state, pending_state;
	GstTagList *tags = NULL;

	switch (GST_MESSAGE_TYPE (msg)) {
		case GST_MESSAGE_ERROR:
			gst_message_parse_error (msg, &err, &debug_info);
			g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
			g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
			g_clear_error (&err);
			g_free (debug_info);
			data->terminate = TRUE;
			break;
		case GST_MESSAGE_EOS:
			g_print ("End-Of-Stream reached.\n");
			data->endFlag = TRUE;
			break;
		case GST_MESSAGE_DURATION:
			/* The duration has changed, mark the current one as invalid */
			data->duration = GST_CLOCK_TIME_NONE;
			break;
		case GST_MESSAGE_STATE_CHANGED:
			gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
			if(GST_MESSAGE_SRC(msg) == GST_OBJECT (data->src)){
				g_print ("Pipeline state changed from %s to %s:\n",
				gst_element_state_get_name(old_state), gst_element_state_get_name (new_state));
				
				/* Remember whether we are in the PLAYING state or not */
				data->playing = (new_state == GST_STATE_PLAYING);
				
				if(!data->connected && ((new_state == GST_STATE_PAUSED) || (new_state == GST_STATE_PLAYING))){
					/* Check if seeking is possible */
					GstQuery *query;
					gint64 start, end;
					gint64 current = -1;
					query = gst_query_new_seeking (GST_FORMAT_TIME);
					if(gst_element_query(data->src, query)){
						gst_query_parse_seeking(query, NULL, &data->seek_enabled, &start, &end);
						if(data->seek_enabled){
							/* send seekable status (via position) as quickly as possible if 
							 * we are seekable.  This lets arServer know that the player's 
							 * media type as soon as we know. */ 
							if(gst_element_query_position(data->src, GST_FORMAT_TIME, &current)){
								if(!data->posUpdate){
									data->curPos = current / (double)GST_SECOND;
									data->posUpdate = TRUE;
								}	
							}
							g_print ("Seeking is ENABLED from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT "\n",
								GST_TIME_ARGS (start), GST_TIME_ARGS (end));
						}else{
							g_print ("Seeking is DISABLED for this stream.\n");
						}
					}else{
						g_printerr ("Seeking query failed.");
					}
					gst_query_unref(query);				
					
					/* connect JACK midi out port.  NOTE: MIDI ports connected first, so we are 
					 * assured of control comms prior to audio port connection. arServer marks 
					 * a player status at standby after the audio ports have been connected, so 
					 * we want to make sure that the standby status flag is set only after both 
					 * control and audio ports have been connected, thus, audio connected after 
					 * MIDI control ports are connected. */
					unsigned char ctl_con = 0; 
					char pname[256];
					snprintf(pname, sizeof pname, "%s:ctlIn", data->argv[3]);
					if(!jack_connect(data->client, jack_port_name(data->midiOut_jPort), pname)){
						/* connect JACK midi in port */
						snprintf(pname, sizeof pname, "%s:ctlOut", data->argv[3]);
						if(!jack_connect(data->client, pname, jack_port_name(data->midiIn_jPort))){
							/* and set flag to start the pipeline running/paused */
							ctl_con = 1;
						}else
							g_printerr("\nERROR: failed to connect to midi out port %s... no control: starting up playing.\n", pname);
					}else	
						g_printerr("\nERROR: failed to connect to midi in port %s... no control: starting up playing.\n", pname);
					
					/* Connect JACK ports to specified client ports */
					char *chanList = NULL;
					char *portName = NULL;
					int c, i;
					jack_port_t **port;

					port = data->audioOut_jPorts;
					for(c=0; c<data->chCount; c++){
						if(chanList = str_NthField(data->argv[5], "&", c)){
							i = 0;
							while(portName = str_NthField(chanList, "+", i)){
								if(strlen(portName)){
									if(jack_connect(data->client, jack_port_name(*port), portName)){
										g_printerr("\nERROR: failed to connect to JACK port  %s.\n", portName);
										free(portName);
										free(chanList);
										data->terminate = TRUE;
									}
								}
								free(portName);
								i++;
							}
							free(chanList);
						}
						port++;
					}

					/* and start the pipeline playing if we are NOT connected to arServer*/
					if(!ctl_con){
						gst_element_set_state(data->src, GST_STATE_PLAYING);
						data->connected = 1;
					}else
						data->connected = 2;
				}
			}
			break;
			
		case GST_MESSAGE_TAG:
			gst_message_parse_tag(msg, &tags);
			char *jstr = tagsToControlPacketData(tags);
			if(jstr){
				if(!data->lastTags || strcmp(jstr, data->lastTags)){
					if(data->lastTags)
						free(data->lastTags);
					data->lastTags = jstr;
					if(data->playing) {
						// only pass track tags that come in while we are playing
						size_t len;
						len = strlen(jstr);
						if((len <= 2048) && (jack_ringbuffer_write_space(data->ctlqueue) >= (len + 7))){
							// enque packet in midi/control queue ring buffer
							controlPacket header;
							header.type = cPeer_player | cType_tags;
							header.peer = htonl(data->ctlID);
							header.dataSize = htons(len);
							jack_ringbuffer_write(data->ctlqueue, (char*)&header, 7);
							jack_ringbuffer_write(data->ctlqueue, jstr, len);	
						}
					}
				}
				if(data->lastTags != jstr)
					free(jstr);
			}
			break;
			
		default:
			/* We should not reach here */
			g_printerr("Unexpected message received.\n");
			break;
	}
	gst_message_unref (msg);
}

void* changeRequestWatcher(void *refCon){
	CustomData *data = (CustomData*)refCon;

    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    do{
		if(data->playReq){
			gst_element_set_state(data->src, GST_STATE_PLAYING);
			data->playReq = FALSE;
		}else if(data->pauseReq){
			gst_element_set_state(data->src, GST_STATE_PAUSED);
			data->pauseReq = FALSE;
		}
		if(data->posReq){
			if(data->reqPos < 0.0)
				data->reqPos = 0.0;
			gst_element_seek_simple(data->src, GST_FORMAT_TIME,
						GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
						data->reqPos * (double)GST_SECOND);
			data->posReq = FALSE;
		}
		
		pthread_mutex_lock(&data->changedMutex);
		pthread_cond_wait(&data->changedSemaphore, &data->changedMutex);
		pthread_mutex_unlock(&data->changedMutex);		
	}while(!data->terminate);
	
	return NULL;
}

void mainloop(char *argv[], char isPipeline){
	GError *err;
	GstStateChangeReturn ret;
	GstElement *vsink;
	CustomData data;
	GstMessage *msg = NULL;
	GstBus *bus;
	GstAudioInfo info;
	GstCaps *audio_caps;
	jack_options_t options = JackNullOption;
	jack_status_t status;
	const char *server = NULL;
	jack_port_t **port;
	unsigned int disconnected;
	unsigned int i;
	size_t rbsize;

	mlock(&data, sizeof(CustomData));
	data.argv = argv;
	data.lastTags = NULL;
	data.playing = FALSE;
	data.terminate = FALSE;
	data.seek_enabled = FALSE;
	data.duration = GST_CLOCK_TIME_NONE;
	data.chCount = 0;
	data.connected = 0;
	data.audioOut_jPorts = NULL;
	data.midiIn_jPort = NULL;
	data.midiOut_jPort = NULL;
	data.ringbuffer = NULL;
	data.ctlqueue = NULL;
	data.jbufs = NULL;
	data.posUpdate = FALSE;
	data.posReq = FALSE;
	data.endFlag = FALSE;
	data.playReq = FALSE;
	data.pauseReq = FALSE;
	data.changedThread = 0;
	
	/* initialise the gstreamer system */
	gst_init(NULL, NULL);
	
	if(isPipeline){
		/* create a pipeline using gst_parse_launch, which
		 * construts a pipeline from a text description. */
		data.src = gst_parse_launch(argv[2], &err);
        if(err){
			g_printerr("\nERROR: failed to create specified gstreamer pipline\n");
			goto finish;
		}
		data.asink = gst_bin_get_by_name(GST_BIN(data.src), "audiosink");
        if(data.asink){
			g_printerr("\nERROR: specified pipeline filed to include an appsink element name d audiosink.\n");
			goto finish;
		}
	}else{
		/* create a pipleing using the playbin element.
		 * NOTE: playbin is a hybrid element AND pipeline */
		data.asink = gst_element_factory_make ("appsink", "app_sink");
		data.src = gst_element_factory_make("playbin", "source");
		vsink = gst_element_factory_make("fakesink", "vsink");
		if(!data.src || !data.asink || !vsink){
			g_printerr("\nERROR: failed to create gstreamer pipeline elements\n");
			goto finish;
		}
	}

	data.chCount = str_CountFields(argv[5], "&") + 1;

	/* setup JACK and get pads properties */
	data.ctlID = atoi(argv[4]);
	char ourName[256];
	snprintf(ourName, sizeof ourName, "arPlayer-%d", data.ctlID);
	
	data.client = jack_client_open(ourName, options, &status, server);
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

	if((data.ctlqueue = jack_ringbuffer_create(queueSizeBytes)) == NULL){
		g_printerr("\nERROR: control queue (tag send) ring buffer allocation failed.\n");
		goto finish;
	}
	mlock(data.ctlqueue, queueSizeBytes);
		
	/* create jack ports (audio and midi) */
	char pname[256];
	data.jbufs = (jack_default_audio_sample_t**)calloc(data.chCount, 
							sizeof(jack_default_audio_sample_t *));
	if(data.audioOut_jPorts = (jack_port_t**)calloc(data.chCount, 
											sizeof(jack_port_t *))){

		port = data.audioOut_jPorts;
		for(i=0; i<data.chCount; i++){
			snprintf(pname, sizeof pname, "Output%d", i);
			*port = jack_port_register(data.client, pname,
						JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
			if(*port == NULL)
				break;
			port++;
		}
	}
	if(!data.audioOut_jPorts || !data.jbufs || (i < data.chCount)){
		g_printerr("\nERROR: JACK audio output ports allocation failed.\n");
		goto finish;
	}
	mlock(data.audioOut_jPorts, data.chCount * sizeof(jack_port_t *));
	mlock(data.jbufs, data.chCount * sizeof(jack_default_audio_sample_t *));
	/* register midi in and out ports for attached player control */
	data.midiOut_jPort = jack_port_register(data.client, "ctlOut", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	data.midiIn_jPort = jack_port_register(data.client, "ctlIn", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);
	if(!data.midiOut_jPort || !data.midiIn_jPort){
		g_printerr("\nERROR: JACK midi ports allocation failed.\n");
		goto finish;
	}
				
	/* Configure appsink to match JACK pad properties */
	gst_audio_info_set_format(&info, GST_AUDIO_FORMAT_F32, data.sampleRate, data.chCount, NULL);
	audio_caps = gst_audio_info_to_caps(&info);
	g_object_set(data.asink, "caps", audio_caps, NULL);
	gst_caps_unref(audio_caps);
  
	/* set appsink new sample data callback function */
	GstAppSinkCallbacks callbackRec;
	callbackRec.eos = NULL;
    callbackRec.new_preroll = NULL;
    callbackRec.new_sample = (GstFlowReturn (*)(GstAppSink *, void *))pull_samples;
    gst_app_sink_set_callbacks((GstAppSink *)data.asink, &callbackRec, &data, NULL);
    
	if(!isPipeline){
		/* set play bin properties if using a url, not a custome pipeline */
		g_object_set(G_OBJECT(data.src), "uri", argv[2], NULL);
		g_object_set(G_OBJECT(data.src), "audio-sink", data.asink, NULL);
		g_object_set(G_OBJECT(data.src), "video-sink", vsink, NULL);
	}
	
	/* create midi in command handling thread, etc */
	pthread_mutex_init(&data.changedMutex, NULL);  
	pthread_cond_init(&data.changedSemaphore, NULL);
	pthread_create(&data.changedThread, NULL, &changeRequestWatcher, &data);

	/* get the message bus from the pipline */
	bus = gst_pipeline_get_bus(GST_PIPELINE(data.src));
	
	/* set JACK callbacks */
	jack_set_process_callback(data.client, jack_process, &data);
	jack_on_shutdown(data.client, jack_shutdown_callback, &data);

	/* start JACK IO running */
	jack_activate(data.client);
	
	/* set pipeline to paused/stand-by */
	gst_element_set_state(data.src, GST_STATE_PAUSED);	
				
	/* Listen to the bus */
	do{
		msg = gst_bus_timed_pop_filtered(bus, 1000 * GST_MSECOND,
				GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | 
				GST_MESSAGE_EOS | GST_MESSAGE_DURATION | GST_MESSAGE_TAG);

		/* Parse message */
		if(msg != NULL){
			handle_message(&data, msg);
		}else{
			/* No message: this means the timeout expired */
			gint64 current = -1;

			/* Query the current position of the stream */
			if(!gst_element_query_position(data.src, GST_FORMAT_TIME, &current)){
				g_printerr("Could not query current position.\n");
			}else{
				if(!data.posUpdate){
					data.curPos = current / (double)GST_SECOND;
					data.posUpdate = TRUE;
				}	
			}
			if(data.playing){
				/* If we didn't know it yet, query the stream duration */
				if(!GST_CLOCK_TIME_IS_VALID(data.duration)){
					if(!gst_element_query_duration(data.src, GST_FORMAT_TIME, &data.duration)){
						g_printerr("Could not query current duration.\n");
					}
				}

				/* Print current position and total duration */
				g_print("Position %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "\r",
						GST_TIME_ARGS(current), GST_TIME_ARGS(data.duration));
			}
			/* check if our input ports are still connected */
			disconnected = 0;
			port = data.audioOut_jPorts;
			for(i=0; i<data.chCount; i++){
				/* noted disconnection of an input ports */
				if(data.connected && !jack_port_connected(port[i]))
					disconnected++;
			}
			/* if all audio ports are disconnected, set terminate flag to shutdown this program */
			if(disconnected >= data.chCount)
				data.terminate = 1;
		}
	}while(!data.terminate);
  
finish:
	/* free and shutdown */
	data.terminate = TRUE;		// just incase we got here with it false.
	if(data.lastTags)
		free(data.lastTags);
	jack_deactivate(data.client);
	jack_client_close(data.client);
	if(data.changedThread){
		pthread_cond_broadcast(&data.changedSemaphore);
		pthread_join(data.changedThread, NULL);
	}
	gst_element_set_state(data.src, GST_STATE_NULL);
	gst_object_unref(data.src);
	gst_object_unref(bus);
	if(data.audioOut_jPorts){
		munlock(data.audioOut_jPorts, data.chCount * sizeof(jack_port_t *));
		free(data.audioOut_jPorts);
	}
	if(data.ringbuffer){
		munlock(data.ringbuffer, rbsize);
		jack_ringbuffer_free(data.ringbuffer);
	}
	if(data.ctlqueue){
		munlock(data.ctlqueue, queueSizeBytes);
		jack_ringbuffer_free(data.ctlqueue);
	}
	if(data.jbufs){
		munlock(data.jbufs, data.chCount * sizeof(jack_default_audio_sample_t *));
		free(data.jbufs);
	}

	pthread_mutex_destroy(&data.changedMutex);
	pthread_cond_destroy(&data.changedSemaphore);
	munlock(&data, sizeof(CustomData));
	gst_deinit();
}

int main(int argc, char *argv[]){
	unsigned int chCount;
	if(argc != 6){
		fprintf(stderr, "Usage: (optional) [required]\n");
		fprintf(stderr, "%s [(-u playURL) or (-p gstreamer-pipline)] [Jack client name] [ctlIDNumber] [jack port connection list]\n\n", argv[0]);
		fprintf(stderr, "where client name is the Jack name for an arServer instance to which we will connect our control ports,\n");
		fprintf(stderr, "and ctlIDNumber is our player number which we will will tag control messages we send, and watch for\n");
		fprintf(stderr, "taged messages that match our id that we receive.\n");
		fprintf(stderr, "\nThe jack port list format is, by example:\n");
		fprintf(stderr, "\tclient:port1+client:port2&client:port3+client:port4\n");
		fprintf(stderr, "\n... which connects the our first channel to client sink port1 AND port2, then connects our second channel\n");
		fprintf(stderr, "to client port3 AND port4. Multiple ports per channel are optional, specified with the + delimiter. \n");
		fprintf(stderr, "The & delimiter increments the list to our next channel connection. As such, our channel count is determined\n");
		fprintf(stderr, "by the number of & delimiters in the list.\n");
		fprintf(stderr, "/nIf the -p option is used, the gstreamer pipline must end with a sink element named audiosink: \" ! appsink name=audiosink\"\n");
		fprintf(stderr, "NOTE: The gstreamer pipline must be a single parameter. Quoting or escaping of spaces may be required when run from a shell.\n");

		return 0;
	}else{
		if(!strcmp(argv[1], "-u")){
			mainloop(argv, 0);
			return 0;		
		}else if(!strcmp(argv[1], "-p")){
			mainloop(argv, 1);
			return 0;				
		}else{
			fprintf(stderr, "Error:  first parameter must specify -u or -p, followed by a corrisponding URL or gstreamer pipline description.\n");
			return -1;			
		}
	}		
}
