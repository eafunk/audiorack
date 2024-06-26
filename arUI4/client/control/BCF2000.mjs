var midiout;
var midiin;
const pCount = 8;
var bank = 0;
var vPotMode = -1;
var volIgn = new Array();
var lastMidi;

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

function selectVpotMode(mode){
	let i;
	let msg = new Array(3);
	
	// check for no change
	if(vPotMode == mode)
		return;
	if(mode > 3)
		return;
	
	vPotMode = mode;
	// clear current vPot displays
	for(i = 0; i < pCount; i++){
		msg[0] = 0xb0;
		msg[1] = 0x10 + i;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
	}
	for(i = 0; i < 4; i++){
		// light and clear the corrisponding mode LED
		msg[0] = 0xb0;
		msg[1] = 0x0c;
		msg[2] = 11;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0 ;
		msg[1] = 0x2c;
		if(i == mode)
			msg[2] = 0x40 + (7 - i);
		else
			msg[2] = 0x00 + (7 - i);
		if(midiout)
			midiout.send(msg);
	}
}

function initialize(inport, outport){
	if(inport && outport){
		midiout = outport;
		midiin = inport;
		midiin.onmidimessage = onMIDIRecv;
	}
	playersUpdate();
	showAutoStat();
	selectVpotMode(0);
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
	if((data[0] & 0xf0) == 0xb0){
		if(lastMidi && (data.length == 3) && (lastMidi[1] == 0x0f) && (data[1] == 0x2f)){
			// switch change message
			zone = lastMidi[2] & 0x0f;
			sw = data[2] & 0x0f;
			if((data[2] & 0xf0) == 0x40)
				iVal = 1;
			else
				iVal = 0;
			if((zone >= 0) && (zone < pCount)){
				// channel switches
				ch = zone;
				if((sw == 2) && (iVal > 0)){
					// start/stop button
					if(studioStateCache.ins[(ch + (bank * pCount))].status & 0x04){
						// play -> stop
						playerAction("stop", false, ch + (bank * pCount), 1);
					}else{
						// stopped -> play
						playerAction("play", false, ch + (bank * pCount), 1);
					}
				}else if((sw == 3) && (iVal > 0)){
					//cue button
					let studio = studioName;
					let p = studioStateCache.ins[(ch + (bank * pCount))];
					if(p && studio.length){
						let bus = parseInt(p.bus, 16);
						bus = bus & 0x00ffffff;
						if(bus & 2)
							bus = bus & ~2;
						else
							bus = bus | 2;
						let hexStr =  ("00000000" + bus.toString(16)).substr(-8);
						fetchContent("studio/"+studio+"?cmd=bus "+(ch + (bank * pCount))+" "+hexStr+"&rt=1");
					}
				}else if(sw == 7){
					//vPot button
					if(vPotMode == 0){
						// zero position
						let studio = studioName;
						if(studio.length)
							fetchContent("studio/"+studio+"?cmd=pos "+(ch + (bank * pCount))+" 0.0&rt=1");
					}else if(vPotMode == 1){
						// center pan
						let studio = studioName;
						if(studio.length)
							fetchContent("studio/"+studio+"?cmd=bal "+(ch + (bank * pCount))+" 0.0&rt=1");
					}else if(vPotMode == 2){
						// unload player
						playerAction("unload", false, ch + (bank * pCount), 1);
					}
				}
			}else if(zone == 10){
				if((sw == 1) && (iVal > 0)){
					// Bank < Button
					if((iVal = bank) > 0){
						iVal--;
						bank = iVal;
						playersUpdate();
					}
				}else if((sw == 3) && (iVal > 0)){
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
				}
			}else if(zone == 11){
				if((sw == 7) && (iVal > 0)){
					// Position vPot mode select
					selectVpotMode(0); 
				}else if((sw == 6) && (iVal > 0)){
					// Pan vPot mode select
					selectVpotMode(1);
					playersUpdate();
				}else if((sw == 5) && (iVal > 0)){
					// VU/Unload vPot mode select
					selectVpotMode(2);
				}
			}else if(zone == 14){
				if((sw == 1) && (iVal > 0)){
					// automation on/off
					let studio = studioName;
					if(studio.length){
						if(studioStateCache.autoStat > 0)
							fetchContent("studio/"+studio+"?cmd=autooff&rt=1");
						else
							fetchContent("studio/"+studio+"?cmd=autoon&rt=1");
					}
				}else if((sw == 2) && (iVal > 0)){
					let studio = studioName;
					if(studio.length){
						if(studioStateCache.autoStat == 1)
							fetchContent("studio/"+studio+"?cmd=autoon&rt=1");
						else
							fetchContent("studio/"+studio+"?cmd=autolive&rt=1");
					}
				}else if((sw == 3) && (iVal > 0)){
					// playlist run/stop
					let studio = studioName;
					if(studio.length){
						if(studioStateCache.runStat)
							fetchContent("studio/"+studio+"?cmd=halt&rt=1");
						else
							fetchContent("studio/"+studio+"?cmd=run&rt=1");
					}
				}else if((sw == 4) && (iVal > 0)){
					// seg now button
					let studio = studioName;
					if(studio.length)
						fetchContent("studio/"+studio+"?cmd=segnow&rt=1");
				}
			}
		}else if((data.length == 3) && ((data[1] & 0xf0) == 0x40)){
			// vPot message
			zone = data[1] & 0x0f;
			iVal = data[2] & 0x7f;
			if(iVal < 64)
				iVal = -iVal;
			else
				iVal = iVal - 64;
			if(iVal && (zone < 8)){
				if(vPotMode == 0){
					//position control
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
				}else if(vPotMode == 1){
					//pan control
					zone = (zone + (bank * pCount));
					let bal = parseFloat(studioStateCache.ins[zone + (bank * pCount)].bal);
					bal = bal + (iVal / 64.0);
					if(bal > 1.0)
						bal = 1.0;
					if(bal < -1.0)
						bal = -1.0;
					let studio = studioName;
					if(studio.length){
						fetchContent("studio/"+studio+"?cmd=bal "+(zone + (bank * pCount))+" "+bal+"&rt=1");
						studioStateCache.ins[zone + (bank * pCount)].bal = bal;
					}
				}
			}
		}else if(lastMidi && (data.length == 3) && ((lastMidi[1] & 0xf0) == 0x00)  && ((data[1] & 0xf0) == 0x20)){
			// fader change message val.
			zone = lastMidi[1] & 0x0f;
			if(zone < 8){
				// fader
				iVal = lastMidi[2] & 0x7f;
				iVal = iVal << 7;
				iVal = iVal + (data[2]  & 0x7f);
				if(iVal > 0){
					fVal = (1.5 / 16383.0) * iVal;
					fVal = Math.pow(fVal, 4);
					if(fVal <= 0.00001)
						fVal = 0.0;
					if(fVal > 5.1)
						fVal = 5.1;
				}else{
					fVal = 0.0;
				}
				let studio = studioName;
				if(studio.length){
					volIgn[zone] = 8; // ignore vol callbacks for 8 ticks
					fetchContent("studio/"+studio+"?cmd=vol "+(zone + (bank * pCount))+" "+fVal+"&rt=1");
				}
			}
		}
		if(lastMidi)
			lastMidi = undefined;
		else if((data.length == 3) && (data[1] & 0xf0) == 0)
			lastMidi = data;
	}
}

function tick(){
	if(midiout){
		let msg = new Array(3);
		let i;
		let base;
		// This is a great place to send a "ping" to control surface, since this code gets executed every half second.
		msg[0] = 0x90;
		msg[1] = 0x00;
		msg[2] = 0x00;
		midiout.send(msg);
		base = bank * pCount;
		for(i = 0; i < pCount; i++){
			if(volIgn[i]){
				volIgn[i] = volIgn[i] - 1;
			}
			if(vPotMode == 0){
				if(studioStateCache && studioStateCache.ins && studioStateCache.ins[base+i]){
					let pos = studioStateCache.ins[base+i].pos;
					showPPos(pos, i + base);
				}
			}
		}
	}
}

function showAutoStat(){
	let msg = new Array(3);
	if(studioStateCache.autoStat == 2){ 
		// auto on
		msg[0] = 0xb0;
		msg[1] = 0x0c;
		msg[2] = 14;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x2c;
		msg[2] = 0x41;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x0c;
		msg[2] = 14;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x2c;
		msg[2] = 0x02;
		if(midiout)
			midiout.send(msg);
	}else if(studioStateCache.autoStat == 1){
		// auto live
		msg[0] = 0xb0;
		msg[1] = 0x0c;
		msg[2] = 14;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x2c;
		msg[2] = 0x42;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x0c;
		msg[2] = 14;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x2c;
		msg[2] = 0x41;
		if(midiout)
			midiout.send(msg);
	}else{
		//auto off
		msg[0] = 0xb0;
		msg[1] = 0x0c;
		msg[2] = 14;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x2c;
		msg[2] = 0x01;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x0c;
		msg[2] = 14;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x2c;
		msg[2] = 0x02;
		if(midiout)
			midiout.send(msg);
	}
	if(studioStateCache.runStat){
		msg[0] = 0xb0;
		msg[1] = 0x0c;
		msg[2] = 14;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x2c;
		msg[2] = 0x43;
		if(midiout)
			midiout.send(msg);
	}else{
		msg[0] = 0xb0;
		msg[1] = 0x0c;
		msg[2] = 14;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x2c;
		msg[2] = 0x03;
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
	if((i > -1) && (vPotMode == 0)){
		val = parseFloat(val);
		msg[0] = 0xb0;
		msg[1] = 0x10 + i;
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
		msg[0] = 0xb0;
		msg[1] = 0x0c;
		msg[2] = i & 0x07;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x2c;
		if(val & 0x4)
			// is playing
			msg[2] = 0x42;
		else
			// is stopped
			msg[2] = 0x02;
		if(midiout)
			midiout.send(msg);
		if(val == 0){
			// unloaded
			// zero vpot display
			msg[0] = 0xb0;
			msg[1] = 0x10 + i;
			msg[2] = 0x20;
			if(midiout)
				midiout.send(msg);
			// set fader to zero too.
			showPVol(0.0, ref, true);
		}
	}
}

function showPBus(val, ref){
	let msg = new Array(3);
	ref = parseInt(ref);
	val = parseInt(val);
	let i = constrainPlayerNumber(ref);
	if(i > -1){
		// Cue
		msg[0] = 0xb0;
		msg[1] = 0x0c;
		msg[2] = i & 0x07;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x2c;
		if(val & 2)
			msg[2] = 0x43;
		else
			msg[2] = 0x03;
		if(midiout)
			midiout.send(msg);
	}
}

function showPBal(val, ref){
	if(vPotMode != 1)
		return;
		
	let msg = new Array(3);
	ref = parseInt(ref);
	val = parseFloat(val);
	let i = constrainPlayerNumber(ref);
	if(i > -1){
		val = Math.round(val * 5) + 0x16;
		if(val < 0x11) 
			val = 0x11;
		if(val > 0x1b) 
			val = 0x1b;
		msg[0] = 0xb0;
		msg[1] = 0x10 + i;
		msg[2] = val;
		if(midiout)
			midiout.send(msg);
	}
}

function showPVol(val, ref, force){
	let msg = new Array(3);
	ref = parseInt(ref);
	if(val === undefined)
		return;
	val = parseFloat(val);
	let i = constrainPlayerNumber(ref);
	if(i > -1){
		if(!force && volIgn[i]){
			return;	// ignore updates on this fader
		}
		val = Math.pow(val, 0.25);
		if(val <= 0.0){
			val = 0;
		}else{
			val = val * (16383.0 / 1.5);
			val = Math.round(val);
			if(val > 16383)
				val = 16383;
			if(val < 0.0)
				val = 0.0;
		}
		if(val < 900)
			val = 900;
		msg[0] = 0xb0;
		msg[1] = 0x00 + (i & 0x0f);
		msg[2] = 0x7f & (val >> 7);
		if(midiout)
			midiout.send(msg);
		msg[0] = 0xb0;
		msg[1] = 0x20 + (i & 0x0f);
		msg[2] = 0x7f & val;
		if(midiout)
			midiout.send(msg);
	}
}

function showPVU(val, ref){
	if(vPotMode != 2)
		return;
	// val = 0 to 255
	let msg = new Array(3);
	if(val === undefined)
		return;
	ref = parseInt(ref);
	val = parseInt(val);
	let i = constrainPlayerNumber(ref);
	if(i > -1){
		val = Math.round(0x14 * (val/255)) - 2;
		if(val > 0x0b)
			val = 0x0b;
		if(val < 0)
			val = 0;
		msg[0] = 0xb0;
		msg[1] = 0x10 + i;
		msg[2] = 0x20 + val;
		if(midiout)
			midiout.send(msg);
	}
}

function playersUpdate(){
	let base = bank * pCount;
	for(let i=0; i<pCount; i++){
		volIgn[i] = 0; // clear fader callback ignore
		// show status
		let stat = studioStateCache.ins[base+i].status;
		showPStat(stat, base+i);
		// show bus 
		let bus = parseInt(studioStateCache.ins[base+i].bus, 16);
		showPBus(bus, base+i);
		// show fader
		if(parseInt(stat)){
			let vol = studioStateCache.ins[base+i].vol;
			showPVol(vol, base+i, true);
		}else{
			showPVol(0.0, base+i, true);
		}
		let bal = studioStateCache.ins[base+i].bal;
		showPBal(bal, base+i);
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
	showPVU as setPVU,
};
