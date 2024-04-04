var midiout;
var midiin;
const pCount = 8;
var bank = 0;
var vPotMode = -1;
var volIgn = new Array();

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
		msg[1] = 0x30 + i;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
	}
	for(i = 0; i < 4; i++){
		// light and clear the corrisponding mode LED
		msg[0] = 0x90;
		msg[1] = 0x28 + i;
		if(i == mode)
			msg[2] = 0x7f;
		else
			msg[2] = 0x00;
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
	}else if(data.length == 3){
		if(data[0] == 0x90){
			// switch change message
			sw = data[1];
			if(data[2] == 0x7f)
				iVal = 1;
			else
				iVal = 0;
			zone = sw & 0x07;
			if(((sw & 0xf8) == 0x00) && (iVal > 0)){
				// start/stop button
				if(studioStateCache.ins[(zone + (bank * pCount))].status & 0x04){
					// play -> stop
					playerAction("stop", false, zone + (bank * pCount), 1);
				}else{
					// stopped -> play
					playerAction("play", false, zone + (bank * pCount), 1);
				}
			}else if(((sw & 0xf8) == 0x08) && (iVal > 0)){
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
			}if(((sw & 0xf8) == 0x18) && (iVal > 0)){
				//main button
				let studio = studioName;
				let p = studioStateCache.ins[(zone + (bank * pCount))];
				if(p && studio.length){
					let bus = parseInt(p.bus, 16);
					bus = bus & 0x00ffffff;
					if(bus & 4)
						bus = bus & ~4;
					else
						bus = bus | 4;
					let hexStr =  ("00000000" + bus.toString(16)).substr(-8);
					fetchContent("studio/"+studio+"?cmd=bus "+(zone + (bank * pCount))+" "+hexStr+"&rt=1");
				}
			}else if(((sw & 0xf8) == 0x10) && (iVal > 0)){
				//alt button
				let studio = studioName;
				let p = studioStateCache.ins[(zone + (bank * pCount))];
				if(p && studio.length){
					let bus = parseInt(p.bus, 16);
					bus = bus & 0x00ffffff;
					if(bus & 8)
						bus = bus & ~8;
					else
						bus = bus | 8;
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
			}else if(((sw & 0xf8) == 0x20) && (iVal > 0)){
				//vPot button
				if(vPotMode == 0){
					// zero position
					let studio = studioName;
					if(studio.length)
						fetchContent("studio/"+studio+"?cmd=pos "+(zone + (bank * pCount))+" 0.0&rt=1");
				}else if(vPotMode == 1){
					// center pan
					let studio = studioName;
					if(studio.length)
						fetchContent("studio/"+studio+"?cmd=bal "+(zone + (bank * pCount))+" 0.0&rt=1");
				}else if(vPotMode == 2){
					// unload player
					playerAction("unload", false, zone + (bank * pCount), 1);
				}
			}else if((sw == 0x5b) && (iVal > 0)){
				// Auto-on
				let studio = studioName;
				if(studio.length)
					fetchContent("studio/"+studio+"?cmd=autoon&rt=1");
			}else if((sw == 0x56) && (iVal > 0)){
				// Auto-live
				let studio = studioName;
				if(studio.length)
					fetchContent("studio/"+studio+"?cmd=autolive&rt=1");
			}else if((sw == 0x5d) && (iVal > 0)){
				// auto-off
				let studio = studioName;
				if(studio.length)
					fetchContent("studio/"+studio+"?cmd=autooff&rt=1");
			}else if((sw == 0x5c) && (iVal > 0)){
				// Queue run
				let studio = studioName;
				if(studio.length)
					fetchContent("studio/"+studio+"?cmd=run&rt=1");
			}else if((sw == 0x5f) && (iVal > 0)){
				// Queue stop
				let studio = studioName;
				if(studio.length)
					fetchContent("studio/"+studio+"?cmd=halt&rt=1");
			}else if((sw == 0x5e) && (iVal > 0)){
				// seg now
				let studio = studioName;
				if(studio.length)
					fetchContent("studio/"+studio+"?cmd=segnow&rt=1");
			}else if((sw == 0x28) && (iVal > 0))
				// Position vPot mode select
				selectVpotMode(0); 
			else if((sw == 0x29) && (iVal > 0)){
				// Pan vPot mode select
				selectVpotMode(1);
				playersUpdate();
			}else if((sw == 0x2a) && (iVal > 0))
				// VU/Unload vPot mode select
				selectVpotMode(2);
			else if((sw == 0x2e) && (iVal > 0)){
				// Monitor down 0.5 dB
				let studio = studioName;
				if(studio.length)
					fetchContent("studio/"+studio+"?cmd=outvol Monitor d0.89125&rt=1");
			}else if((sw == 0x2f) && (iVal > 0)){
				// Monitor up 0.5dB
				let studio = studioName;
				if(studio.length)
					fetchContent("studio/"+studio+"?cmd=outvol Monitor d1.12202&rt=1");
			}else if((sw == 0x30) && (iVal > 0)){
				// Phones down 0.5dB
				let studio = studioName;
				if(studio.length)
					fetchContent("studio/"+studio+"?cmd=outvol Phones d0.89125&rt=1");
			}else if((sw == 0x31) && (iVal > 0)){
				// Phones up 0.5 dB
				let studio = studioName;
				if(studio.length)
					fetchContent("studio/"+studio+"?cmd=outvol Phones d1.12202&rt=1");
			}
		}else if((data[0] == 0xb0) && ((data[1] & 0xf8) == 0x10)){
			// vPot message
			zone = data[1] & 0x07;
			iVal = data[2] & 0x7f;
			if(iVal & 0x40)
				iVal = 64 - iVal;
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
		}else if((data[0] & 0xf0) == 0xe0){
			// fader change message val.
			zone = data[0] & 0x0f;
			if(zone < 8){
				// echo value back to fader
				if(midiout)
					midiout.send(data);
				iVal = data[2] & 0x7f;
				iVal = iVal << 7;
				iVal = iVal + (data[1] & 0x7f);
				if(iVal > 0){
					fVal = (1.28 / 16383.0) * iVal;
					fVal = Math.pow(fVal, 8);
					if(fVal <= 0.00001)
						fVal = 0.0;
					if(fVal > 6.8)
						fVal = 6.8;
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
		msg[0] = 0x90;
		msg[1] = 0x5b;
		msg[2] = 0x7f;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0x90;
		msg[1] = 0x56;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0x90;
		msg[1] = 0x5d;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
	}else if(studioStateCache.autoStat == 1){
		// auto live
		msg[0] = 0x90;
		msg[1] = 0x5b;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0x90;
		msg[1] = 0x56;
		msg[2] = 0x7f;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0x90;
		msg[1] = 0x5d;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
	}else{
		//auto off
		msg[0] = 0x90;
		msg[1] = 0x5b;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0x90;
		msg[1] = 0x56;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0x90;
		msg[1] = 0x5d;
		msg[2] = 0x7f;
		if(midiout)
			midiout.send(msg);
	}
	if(studioStateCache.runStat){
		msg[0] = 0x90;
		msg[1] = 0x5c;
		msg[2] = 0x7f;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0x90;
		msg[1] = 0x5f;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
	}else{
				msg[0] = 0x90;
		msg[1] = 0x5c;
		msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		msg[0] = 0x90;
		msg[1] = 0x5f;
		msg[2] = 0x7f;
		if(midiout)
			midiout.send(msg);
	}
}

function showPPos(val, ref){
	if(vPotMode != 0)
		return;
		
	let msg = new Array(3);
	let timeSec = 0.0;
	let durSec = 0.0;
	ref = parseInt(ref);
	val = parseFloat(val);
	let i = constrainPlayerNumber(ref);
	if(i > -1){
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
		msg[1] = i & 0x07;
		if(val & 0x4)
			// is playing
			msg[2] = 0x7f;
		else
			// is stopped
			msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		if(val == 0){
			// unloaded
			// zero vpot display
			msg[0] = 0xb0;
			msg[1] = 0x30 + i;
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
		msg[0] = 0x90;
		msg[1] = (i & 0x07) + 0x08;
		if(val & 2)
			msg[2] = 0x7f;
		else
			msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		// alt
		msg[0] = 0x90;
		msg[1] = (i & 0x07) + 0x10;
		if(val & 8)
			msg[2] = 0x7f;
		else
			msg[2] = 0x00;
		if(midiout)
			midiout.send(msg);
		// main
		msg[0] = 0x90;
		msg[1] = (i & 0x07) + 0x18;
		if(val & 4)
			msg[2] = 0x7f;
		else
			msg[2] = 0x00;
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
		msg[1] = 0x30 + i;
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
		val = Math.pow(val, 0.125);
		if(val <= 0.0){
			val = 0;
		}else{
			val = val * (16383.0 / 1.28);
			val = Math.round(val);
			if(val > 16383)
				val = 16383;
			if(val < 0.0)
				val = 0.0;
		}
		msg[0] = 0xe0 + (i & 0x0f);
		msg[1] = 0x7f & val;
		msg[2] = 0x7f & (val >> 7);
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
		msg[1] = 0x30 + i;
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
