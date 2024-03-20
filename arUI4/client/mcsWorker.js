/* SStudio MIDI Control Surface shared worker thread */

var studioName;
var studioStateCache = {control:false, midiList:[], settings:{}, ins:[], autoStat:0, runStat:0};
var midiAccess;
var cred;
var bc = new BroadcastChannel("arUIBroadcastChannel");
bc.onmessage = bcRcvMsg;

async function bcRcvMsg(event){
	let msg = event.data;
	if(msg.type == "studioChange"){
		if(studioName == msg.value)
			return;	// no change
		studioChangeCallback(msg.value);
	}
	else if(msg.type == "sse"){
		if(msg.sseType.indexOf("vu_") == 0)
			return;
		else if(msg.sseType === "msg")
			return;
		else
			studioHandleNotice(msg.value);
	}
	else if(msg.type == "authChange"){
		cred = msg.value;
// exclude: cred = flase/null or cred.permission = library, traffic, production
//		await sseSetup();
	}
}

self.onconnect = function(e){
	let port = e.ports[0];
	// set msg rx handler
	port.onmessage = function (e) {
		console.log("to studioWorker:", e.data);
		port.postMessage("pong");
	};
	port.postMessage("Hello");
}

/*
//  periodic check of sse connection for reconnect
setInterval(function() {
	if(!es || (es.readyState !== EventSource.OPEN)){
		if(cred)
			sseSetup();
	}
}, sseReconFreqMilliSec);
*/

async function playerAction(cmd, evt, pNum, rt){
	// evt is the event from the button press, or if
	// evt is false, pNum is the player number to stop
	let player;
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
		player = evt.target.parentNode.parentElement.parentElement.getAttribute("data-pnum");
	}else
		player = pNum;
	if(studioName && studioName.length){
		if(rt)
			fetchContent("studio/"+studioName+"?cmd="+cmd+" "+player+"&rt=1");
		else
			fetchContent("studio/"+studioName+"?cmd="+cmd+" "+player);
	}
}

async function fetchContent(url, options){
	let response;
	try{
		response = await fetch(url, options);
	}catch(err){
		return err;
	}
	if(response.ok)
		return response;
	else{
		let msg = await response.text();
		if(msg && msg.length){
			let obj = {statusText: msg, status: response.status, ok: response.ok};
			return obj;
		}
	}
	return false;
}

async function studioChangeCallback(value){
	studioName = value;
	studioStateCache = {...studioStateCache, ...{settings: {}, ins: [], autoStat: 0, runStat: 0}};
/*	await getServerInfo(value);
	await syncStudioMetalist(value);
	await syncStudioStat(value);
	await syncPlayers(value);
	await refreshOutGroups();
*/
	updateControlSurface(); 
}

function studioHandleNotice(data){
	if(!studioStateCache.control)
		return;
	let val = 0;
	let ref = 0;
	switch(data.type){
		case "invol":			// player volume change, ref=input index, val=scalar volume
			val = data.val;	// number
			ref = data.num;
			studioStateCache.control.setPVol(val, ref);
			break;
		case "inbal":			// player balance change, ref=input index, val=scalar balance, zero for center
			val = data.val;	// number
			ref = data.num;
			studioStateCache.control.setPBal(val, ref);
			break;
		case "inbus":			// input bus assignment change, ref=input index, val=hex string bus assignment bits
			val = parseInt(data.val, 16);	// hex string
			ref = data.num;
			studioStateCache.control.setPBus(val, ref);
			break;
		case "instat":			// input status change, ref=input index, val=status number
			val = data.val;	// number
			ref = data.num;
			studioStateCache.control.setPStat(val, ref);
//!!		syncPlayers(studioName);
			break;
		case "status":			// over-all status change, no ref, no val.  Use "stat" command to get status
			// no ref or value: Change in ListRev, LogTime, automation status trigger this notice. Does not include sip registoration.
//!!			syncStudioStat(studioName);
			break;
		case "metachg":		// metadata content change, ref=UID number, no val. Use "dumpmeta" command to get new content
			if(ref == 0){
//!!			ref = data.uid;
//!!			updateMetaItem(studioName, ref);
			}
			break;
		case "inpos":		// input position change, ref=input index, val=position in seconds
			val = data.val;	// number
			ref = data.num;
			studioStateCache.control.setPPos(val, ref);
			break;
		default:
			// ignore unknown type;
			return;
	}
}

function setStorageMidiControl(name){
	localStorage.setItem("midiControl", name);
}

function getStorageMidiControl(){
	return localStorage.getItem("midiControl");
}

async function updateControlSurface(){
	if(!midiAccess){
		if(navigator.requestMIDIAccess){
			midiAccess = await navigator.requestMIDIAccess({sysex: false});
			if(midiAccess)
				midiAccess.onstatechange = updateControlSurface; // call this function when midi devices change
		}
	}
	if(midiAccess){
		let resp = await fetchContent("control");
		if(resp && resp.ok){
			let list = [];
			let csmodules = await resp.json();
			for(let i = 0; i < csmodules.length; i++)
				csmodules[i] = csmodules[i].substring(0, csmodules[i].lastIndexOf('.')); // remove file extention
			let inputs = midiAccess.inputs.values();
			// inputs is an Iterator
			for(let input = inputs.next(); input && !input.done; input = inputs.next()){
				input = input.value;
				let outputs = midiAccess.outputs.values();
				for(let output = outputs.next(); output && !output.done; output = outputs.next()){
					output = output.value;
					if((output.name === input.name) && (output.manufacturer === input.manufacturer)){
						// found matching output.  See if we have a js module for this device
//						let devname = input.manufacturer + "_" + input.name;
						let devname = input.name;	// pipewire doesn't capture the manufacturer, just use name for matching
						for(let i = 0; i < csmodules.length; i++){
							if(devname.startsWith(csmodules[i])){
								let entry = {name: devname, module: csmodules[i]+".mjs", input: input, output: output};
								list.push(entry);
								break;
							}
						}
					}
				}
			}
			studioStateCache.midiList = list;
			// updates the control surface selection menu list
			// when the locListCache variable changes
			bc.postMessage({type:"mcsList", value:list});
			let savedname = getStorageMidiControl();
			let i = list.length;
			if(savedname){
				for(i = 0; i < list.length; i++){
					if(list[i].name == savedname){
						selectControlSurface(list[i]);
						break;
					}
				}
			}
			if(i == list.length)
				selectControlSurface(undefined);
		}else{
			bc.postMessage({type:"mcsList", value:[]});
		}
	}else{
		console.log("mcsWorker no browser support for midi");
		bc.postMessage({type:"mcsNoMIDID"});
	}
}

async function selectControlSurface(entry){
	// load module
	if(entry){
		let Module = await import('/control/'+entry.module);
		if(entry.name != studioStateCache.midiName){
			if(Module){
				setStorageMidiControl(entry.name);
				studioStateCache.midiName = entry.name;
				studioStateCache.control = Module;
				Module.init(entry.input, entry.output);
			}
			bc.postMessage({type:"mcsChange", value:entry.name});
		}else
			Module.init();	// already selected, just refresh controls to new studio
	}else{
		if(studioStateCache.midiName !== false){
			studioStateCache.midiName = false;
			studioStateCache.control = false;
			if(entry === false){ // undefined does not changed saved, incase interface comes back
				setStorageMidiControl(false);
			}
		}
		bc.postMessage({type:"mcsChange", value:false});
	}
}
