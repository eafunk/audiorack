/* Server Side Events shared worker thread */

const sseReconFreqMilliSec = 10000;	// 10 seconds

var es;
var studioName;
var stReqID;
var lastClientNumber = 0;
var cred;
var bc = new BroadcastChannel("arUIBroadcastChannel");
bc.onmessage = bcRcvMsg;

async function bcRcvMsg(event){
	let msg = event.data;
	if(msg.type == "studioRequest"){
		if((studioName == msg.value) && (stReqID == msg.who))
			return;	// no change
		stReqID = msg.who; // used to keep just one midi control in use
		if(studioName = msg.value){
			if(studioName && studioName.length){
				await sseEventTypeUnreg(studioName);
				await sseEventTypeUnreg("vu_"+studioName);
			}
			studioName = msg.value;
			if(studioName && studioName.length){
				await sseEventTypeReg(studioName);
				await sseEventTypeReg("vu_"+studioName);
			}
		}
		bc.postMessage({type:"studioChange", value:studioName, who:msg.who});
	}
	if(msg.type == "authChange"){
		cred = msg.value;
		await sseSetup();
	}
}

onconnect = function(e){
	let port = e.ports[0];
	// set msg rx handler
	lastClientNumber++;
	port.onmessage = getMessage;
	port.postMessage(lastClientNumber);
}

function getMessage(e){
	console.log(e.data); // this should never be called since we do not get messages
}

function sseListener(event) { // broadcast sse events
	bc.postMessage({type:"sse", sseType:event.type, value:event.data});
}

//  periodic check of sse connection for reconnect
setInterval(function() {
	if(!es || (es.readyState !== EventSource.OPEN)){
		if(cred)
			sseSetup();
	}
}, sseReconFreqMilliSec);

async function sseSetup(){
	// login status changed callback or reconnecting
	if(es){
		es.close();
		es = false;
	}
	if(cred){
		// register for sse stream
		es = new EventSource('/ssestream'); // event source creation
		es.onopen = async function(e) {
			es.addEventListener('msg', sseListener); // listen for 'msg' general messages at a minimum
			await addListenersForAllSubscriptions();
		};
		es.onerror = function(e) {
			es.close();
			es = false;
		};
	}
}

/* unregister to receive a event type */ 
async function sseEventTypeUnreg(type){
	let response = await fetch("/sserem/"+type);
	if(response.status == 200){
		// sucess...
		es.removeEventListener(type, sseListener);
	}
}

/* register receiving of an event type */
async function sseEventTypeReg(type){
	let response = await fetch("/sseadd/"+type);
	if(response.status == 200){
		// sucess...
		await addListenersForAllSubscriptions();
	}
}

/* add a handler for all evens the server has us listed to get 
		for this login session */ 
async function addListenersForAllSubscriptions(){
	let response = await fetch("/sseget");
	if((response.status >= 200) && (response.status < 400)){
		// sucess...
		let data = await response.json();
		for(const entry of data){
			es.removeEventListener(entry, sseListener); // to prevent duplicate calls if already registered
			es.addEventListener(entry, sseListener);
		}
	}
}
