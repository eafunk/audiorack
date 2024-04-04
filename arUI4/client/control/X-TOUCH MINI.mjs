var midiout;
var midiin;
const pCount = 8;
var bank = 0;
var blink = 0;
var cue = new Array();

function constrainPlayerNumber(i){
	// remaps player index to pCount number in current bank
	// or return -1 if out of range
	i = i - (bank * pCount);
	if(i < pCount){
		if(i >= 0)
			return i;
	}
	return -1;
}

function initialize(inport, outport){
	if(inport && outport){
		midiout = outport;
		midiin = inport;
		midiin.onmidimessage = onMIDIRecv;
	}
	playersUpdate();
	showAutoStat();
}

function uninitialize(){
	if(midiin){
		midiin.onmidimessage = null;
		midiin = null;
	}
	if(midiout)
		midiout = null;
}

function onMIDIRecv(message){
	let zone;
	let sw;
	let iVal;
	let ch;
	let fVal;
	let data = message.data;
	if((data.length == 18) && (data[0] === 0xf0) && (data[5] === 0x01)){
		// connection query
		let r = new Array();
		data[5] = 0x02;
		r[0] = 0x7F & (data[13] + (data[14] ^ 0xA) - data[16]);
		r[1] = 0x7F & ((data[15] >> 4) ^ (data[13] + data[16]));
		r[2] = 0x7F & (data[16] - (data[15] << 2) ^ (data[13] | data[14]));
		r[3] = 0x7F & (data[14] - data[15] + (0xF0  ^(data[16] << 4)));
		data[13] = r[0];
		data[14] = r[1];
		data[15] = r[2];
		data[16] = r[3];
		midiout.send(data);
	}else if(data[0] == 0x90){
		// switch change message
		sw = data[1];
		if(data[2] == 0x7f)
			iVal = 1;
		else
			iVal = 0;
		zone = sw & 0x07;
		ch = -1;
		switch(sw){
			case 0x59:
				ch = 0;
				break;
			case 0x5a:
				ch = 1;
				break;
			case 0x28:
				ch = 2;
				break;
			case 0x29:
				ch = 3;
				break;
			case 0x2A:
				ch = 4;
				break;
			case 0x2B:
				ch = 5;
				break;
			case 0x2C:
				ch = 6;
				break;
			case 0x2D:
				ch = 7;
				break;
		}
		if((ch > -1) && (iVal > 0)){
			// start/stop button
			if(studioStateCache.ins[(ch + (bank * pCount))].status & 0x04){
				// play -> stop
				playerAction("stop", false, ch + (bank * pCount), 1);
			}else{
				// stopped -> play
				playerAction("play", false, ch + (bank * pCount), 1);
			}
		}else if(((sw & 0xf8) == 0x20) && (iVal > 0)){
			//cue button
			let studio = studioName;
			let p = studioStateCache.ins[(zone + (bank * pCount))];
			if(p && studio.length){
				let bus = parseInt(p.bus, 16);
				bus = bus & 0x00ffffff;
				if(bus & 2)
					bus = bus & ~2;
				else
					bus = bus | 2;
				let hexStr =  ("00000000" + bus.toString(16)).substr(-8);
				fetchContent("studio/"+studio+"?cmd=bus "+(zone + (bank * pCount))+" "+hexStr+"&rt=1");
			}
		}else if((sw == 0x54) && (iVal > 0)){
			// Bank < Button
			if((iVal = bank) > 0){
				iVal--;
				bank = iVal;
				playersUpdate();
			}
		}else if((sw == 0x55) && (iVal > 0)){
			// Bank > Button
			let banks;
			let vis = 8;
			let settings = studioStateCache.meta[0];
			if(settings && settings.client_players_visible)
				 vis = settings.client_players_visible;
			banks = Math.floor(((vis - 1) / pCount) + 1);
			if(banks < 1)
				banks = 1;
			if((iVal = bank) < (banks - 1)){
				iVal++;
				bank = iVal;
				playersUpdate();
			}
		}else if((sw == 0x57) && (iVal > 0)){
			// Auto-on
			let studio = studioName;
			if(studio.length)
				fetchContent("studio/"+studio+"?cmd=autoon&rt=1");
		}else if((sw == 0x58) && (iVal > 0)){
			// Auto-live
			let studio = studioName;
			if(studio.length)
				fetchContent("studio/"+studio+"?cmd=autolive&rt=1");
		}else if((sw == 0x5B) && (iVal > 0)){
			// auto-off
			let studio = studioName;
			if(studio.length)
				fetchContent("studio/"+studio+"?cmd=autooff&rt=1");
		}else if((sw == 0x5D) && (iVal > 0)){
			// Queue stop
			let studio = studioName;
			if(studio.length)
				fetchContent("studio/"+studio+"?cmd=halt&rt=1");
		}else if((sw == 0x5E) && (iVal > 0)){
			// Queue run
			let studio = studioName;
			if(studio.length)
				fetchContent("studio/"+studio+"?cmd=run&rt=1");
		}else if((sw == 0x5F) && (iVal > 0)){
			// seg now
			let studio = studioName;
			if(studio.length)
				fetchContent("studio/"+studio+"?cmd=segnow&rt=1");
		}
	}else if((data.length == 3) && (data[0] == 0xb0) && ((data[1] & 0xf8) == 0x10)){
		// vPot message
		zone = data[1] & 0x07;
		iVal = data[2] & 0x7f;
		if(iVal & 0x40)
			iVal = 64 - iVal;
		if(cue[zone]){
			// position control
			let dur = parseFloat(studioStateCache.ins[zone + (bank * pCount)].dur);
			let pos = studioStateCache.ins[zone + (bank * pCount)].pos;
			if(pos)
				pos = parseFloat(pos);
			else
				pos = 0.0;
			if(dur){
				if(iVal < 0)
					iVal = (iVal * -iVal) / 10.0;
				else
					iVal = (iVal * iVal) / 10.0;
				iVal = iVal + pos;
				if(iVal < 0.0)
					iVal = 0.0;
				if(iVal > dur)
					iVal = dur;
				let studio = studioName;
				if(studio.length){
					fetchContent("studio/"+studio+"?cmd=pos "+(zone + (bank * pCount))+" "+iVal+"&rt=1");
					studioStateCache.ins[zone + (bank * pCount)].pos = iVal;
				}
			}
		}else{
			// volume control
			zone = (zone + (bank * pCount));
			iVal =  Math.pow(1.0593, iVal);
			if(iVal > 10.0)
				iVal = 10.0;
			if(iVal < 0.0)
				iVal = 0.0;
			let studio = studioName;
			if(studio.length)
				fetchContent("studio/"+studio+"?cmd=vol "+zone+" d"+iVal+"&rt=1");
		}
	}else if((data.length == 3) && (data[0] == 0xE8) && (data[1] == 0x00)){
		// monitor fader message
		iVal = data[2] & 0x7f;
		if(iVal > 0){
			iVal = (1.33 / 127.0) * iVal;
			iVal = Math.pow(iVal, 4.3478);
		}else{
			iVal = 0.0;
		}
		let studio = studioName;
		if(studio.length)
			fetchContent("studio/"+studio+"?cmd=outvol Monitor "+iVal+"&rt=1");
	}
}

function tick(){
	if(midiout){
		let msg = new Array(3);
		let i;
		let base = bank * pCount;
		blink++;
		if(blink > 1)
			blink = 0;
		for(let i = 0; i < pCount; i++){
			if(cue[i]){
				// chan in cue mode.. vPot shows position, on light blinks
				msg[0] = 0x90;
				if(i<2)
					msg[1] = i + 0x59;
				else 
					msg[1] = i + 0x26;
				if(blink)
					msg[2] = 0x7f;
				else
					msg[2] = 0x00;
				
				midiout.send(msg);
				// update position display
				if(studioStateCache && studioStateCache.ins && studioStateCache.ins[base+i]){
					let pos = studioStateCache.ins[base+i].pos;
					showPPos(pos, base+i);
				}
			}/*else{
				// update status display
				if(studioStateCache && studioStateCache.ins && studioStateCache.ins[base+i]){
					let stat = studioStateCache.ins[base+i].status;
					showPStat(stat, base+i);
				}
			}*/
		}
	}
}

function showAutoStat(){
	let msg = new Array(3);
	if(studioStateCache.autoStat == 2){ 
		// auto on
		msg[0] = 0x90;
		msg[1] = 0x57;
		msg[2] = 0x7f;
		if(midiout)
			midiout.send(msg);
		msg[1] = 0x58;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		msg[1] = 0x5b;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
	}else if(studioStateCache.autoStat == 1){
		// auto live
		msg[0] = 0x90;
		msg[1] = 0x57;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		msg[1] = 0x58;
		msg[2] = 0x7f;
		if(midiout)
			midiout.send(msg);
		msg[1] = 0x5b;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
	}else{
		//auto off
		msg[0] = 0x90;
		msg[1] = 0x57;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0x90;
		msg[1] = 0x58;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0x90;
		msg[1] = 0x5b;
		msg[2] = 0x7f;
		if(midiout)
			midiout.send(msg);
	}
	if(studioStateCache.runStat){
		msg[0] = 0x90;
		msg[1] = 0x5d;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		msg[1] = 0x5e;
		msg[2] = 0x7f;
		if(midiout)
			midiout.send(msg);
		msg[1] = 0x5f;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
	}else{
		msg[0] = 0x90;
		msg[1] = 0x5d;
		msg[2] = 0x7f;
		if(midiout)
			midiout.send(msg);
		msg[1] = 0x5e;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		msg[1] = 0x5f;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
	}
}

function showPPos(val, ref){
	let msg = new Array(3);
	let timeSec = 0.0;
	let durSec = 0.0;
	ref = parseInt(ref);
	val = parseFloat(val);
	let i = constrainPlayerNumber(ref);
	if((i > -1) && cue[i]){
		val = parseFloat(val);
		msg[0] = 0xb0;
		msg[1] = 0x30 + i;
		let dur = parseFloat(studioStateCache.ins[ref].dur);
		if(dur){
			dur = parseFloat(dur);
			if(val){
				if(dur > val)
					val = Math.round(0x0c * (val / dur));
				else
					val = 0x0b;
			}
		}else
			val = 0;
		msg[2] = 0x20 + val;
		if(midiout)
			midiout.send(msg);
	}
}

function showPStat(val, ref){
	let msg = new Array(3);
	ref = parseInt(ref);
	val = parseInt(val);
	let i = constrainPlayerNumber(ref);
	if(i > -1){
		msg[0] = 0x90;
		if(i<2)
			msg[1] = i + 0x59;
		else 
			msg[1] = i + 0x26;
		if(val & 0x4)
			// is playing
			msg[2] = 0x7f;
		else
			msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		if(!val){
			// clear Vpot
			msg[0] = 0xb0;
			msg[1] = 0x30 + i;
			msg[2] = 0x20;
			if(midiout)
				midiout.send(msg);
		}
	}
}

function showPBus(val, ref){
	ref = parseInt(ref);
	val = parseInt(val);
	let i = constrainPlayerNumber(ref);
	if(i > -1){
		// Cue
		if(val & 2)
			cue[i] = 1;
		else
			cue[i] = 0;
		if(cue[i]){
			// change vPot LEDs to position
			let pos = studioStateCache.ins[ref].pos;
			showPPos(pos, ref);
		}else{
			// change vPot LEDs to volume
			let vol = studioStateCache.ins[ref].vol;
			showPVol(vol, ref);
		}
	}
}

function showPBal(val, ref){
	// NOT USED ON THIS CONTROL SURFACE
}

function showPVol(val, ref){
console.log("showPVol:", val, ref);
	let msg = new Array(3);
	ref = parseInt(ref);
	if(val === undefined)
		return;
	val = parseFloat(val);
	let i = constrainPlayerNumber(ref);
	if((i > -1) && !cue[i]){
		val = Math.pow(val, 0.25);
		if(val <= 0.0){
			val = 0;
		}else{
			val = val * (12.0 / 1.33);
			if(val > 12.0) 
				val = 12.0;
			if(val < 0.0) 
				val = 0.0;
		}
		msg[0] = 0xb0;
		msg[1] = 0x30 + i;
		msg[2] = 0x20 + val;
		if(midiout)
			midiout.send(msg);
	}
}

function showPVU(val, ref){

}

function playersUpdate(){
	let base = bank * pCount;
	for(let i=0; i<pCount; i++){
		// show status
		let stat = studioStateCache.ins[base+i].status;
		showPStat(stat, base+i);
		// show bus -- this will subsequently show vol or pos 
		// and set cue array as well
		let bus = parseInt(studioStateCache.ins[base+i].bus, 16);
		showPBus(bus, base+i);
	}
}

export {
	initialize as init,
	uninitialize as uninit,
	tick as tick,
	showAutoStat as setAutoStat,
	showPPos as setPPos,
	showPStat as setPStat,
	showPBus as setPBus,
	showPBal as setPBal,
	showPVol as setPVol,
	showPVU as setPVU
};
