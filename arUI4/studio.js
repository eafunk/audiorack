/*
 Copyright (c) 2021 Ethan Funk
 
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

"use strict"; // prevents accidental global var creation when a variable is assigned but not previously declared.

const DefLimit = 50;

const _= require('lodash');
var fs = require('fs');
var path = require('path');
const genericPool = require("generic-pool");
var linereader = require('line-reader');
var sse = require('./sse.js');
const net = require('net');

var studiolist = false;
var poollist = {};

/****** generic tcp connection pool functions  ******/
class conFactory {
	constructor(host, port){
		this.host = host;
		this.port = port;
	}
	create(){
		// returns a Promise that resolves to a a new connection instance (client)
		// or rejects to an Error if it is unable to create a resource.
		return new Promise(function(resolve, reject){
				let client = net.createConnection(this.port, this.host, function(){
					client.setKeepAlive(true);
					client.setEncoding('utf8'); // arServer uses UTF-8 encoding for everything
					// wait for server prompt
					prepForResponse(client, 1000);
					getResponse(client).then(function(result){
							// we ignore the result
							resolve(client);
						}).catch(function(err){
							reject(err);
						});
					});
				
				client.on('error', function(err){
					if(client.respromise)	// response promise is waiting
						client.reject(err);
					// delay 2 seconds to prevent rapid connection retry
					let delay = setTimeout(() => {
						reject(err);
					}, 2000);
				});
				
				client.response = "";
				client.on('data', function(data){
					if(client.respromise){
						client.response += data;	
						let idx = client.response.indexOf("\nars>");
						if(idx > -1){
							if(client.resTimer){
								clearTimeout(client.resTimer);
							}
							let result = client.response.substr(0, idx+1);	// leave the leading \n in the result
							client.response = client.response.substr(idx+5);
							delete client.respromise;
							client.resolve(result);
						}
					}
				});
		}.bind(this));
	}
	
	destroy(client){
		// returns a promise that resolves once it has destroyed the resource.
		return new Promise(function(resolve, reject){
			client.destroy();
			resolve();
		});
	}
	
	validate(client){
		// return a promise that resolves to a boolean where true indicates the resource is still valid.
		return new Promise(function(resolve, reject){
			if(prepForResponse(client, 1000)){
				client.write('\n');
				getResponse(client).then(function(result){
						if(result){
							resolve(true);
						}else{
							resolve(false);
						}
					}).catch(function(err){
						resolve(false);
					});
			}else{
				resolve(false);
			}
		});
	}
}

function execShellCommand(cmd) {
	const exec = require("child_process").exec;
	return new Promise((resolve, reject) => {
		exec(cmd, (error, stdout, stderr) => {
			if(error){
				reject(error); 
			}else{
				resolve(stdout);
			}
		});
	});
}

function handleNotifyPacket(name, type, data){
	if(type == 0x08){
		// nType_vu: vu levels packet
		// send vuData as a raw hex string to be parsed by the clients: for high data rate eff.
		sse.postSSEvent("vu_"+name, data.toString('hex'));
	}else{
		// all other types turned into json strings
		let ref = data.readUInt32BE(4);
		let msg = "";
		let val = 0;
		switch(type){
			case 0x01:	// nType_vol: player or output volume change, ref=0xC0000000 + index for output or 0x00 + index for input/player
				val = data.readFloatBE(8);
				if((BigInt(ref) & 0xFF000000n) == 0xC0000000n){
					// output volume
					ref = ref & 0x00FFFFFF;
					msg = {type: "outvol", num: ref, val: val};
					sse.postSSEvent(name, JSON.stringify(msg));
				}else{
					// input volume
					ref = ref & 0x00FFFFFF;
					msg = {type: "invol", num: ref, val: val};
					sse.postSSEvent(name, JSON.stringify(msg));
				}
				break;
			case 0x02:	// nType_bal: player balance change
				val = data.readFloatBE(8);
				msg = {type: "inbal", num: ref, val: val};
				sse.postSSEvent(name, JSON.stringify(msg));
				break;
			case 0x03:	// nType_bus: player or output bus change
				val = data.readUInt32BE(8);
				if((BigInt(ref) & 0xFF000000n) == 0xC0000000n){
					// output bus
					ref = ref & 0x00FFFFFF;
					msg = {type: "outbus", num: ref, val: val};
					sse.postSSEvent(name, JSON.stringify(msg));
				}else{
					// input bus
					ref = ref & 0x00FFFFFF;
					msg = {type: "inbus", num: ref, val: val.toString(16)};
					sse.postSSEvent(name, JSON.stringify(msg));
				}
				break;
			case 0x04:	// nType_pstat: player status change
				val = data.readUInt32BE(8);
				msg = {type: "instat", num: ref, val: val};
				sse.postSSEvent(name, JSON.stringify(msg));
				break;
			case 0x05:	// nType_status: over-all status change - use stat to get info
				msg = {type: "status"};
				sse.postSSEvent(name, JSON.stringify(msg));
				break;
			case 0x06:	// nType_mstat: meta data record change - use dumpmeta to syncronize
				msg = {type: "metachg", uid: ref};
				sse.postSSEvent(name, JSON.stringify(msg));
				break;
			case 0x07:	// nType_rstat: recorder status change - use rstat to get info
				msg = {type: "rstat"};
				sse.postSSEvent(name, JSON.stringify(msg));
				break;
			case 0x09:	// nType_rgain: recorder gain changed, ref=UID
				val = data.readFloatBE(8);
				msg = {type: "recgain", uid: ref, val: val};
				sse.postSSEvent(name, JSON.stringify(msg));
				break;
			case 0x0a:	// nType_pos: player position change
				val = data.readFloatBE(8);
				msg = {type: "inpos", num: ref, val: val};
				sse.postSSEvent(name, JSON.stringify(msg));
				break;
			case 0x0b:	// nType_del: item deleted, ref=UID
				msg = {type: "metadel", uid: ref};
				sse.postSSEvent(name, JSON.stringify(msg));
				break;
			case 0x0c:	// nType_dly: output delay setting changed
				val = data.readFloatBE(8);
				if((BigInt(ref) & 0xFF000000n) == 0xC0000000n){
					ref = ref & 0x00FFFFFF;
					msg = {type: "outdly", num: ref, val: val};
					sse.postSSEvent(name, JSON.stringify(msg));
				}
				break;
			case 0x10:	// nType_load: Processor load, cVal[0] = % realtime JACK load, 0.8 format
				val = data.readUInt8(8);
				msg = {type: "cpu", val: val};
				sse.postSSEvent(name, JSON.stringify(msg));
				break;
			default:
				console.log("received bad notify packet type "+type.toString(16)+" from "+name);
		}
	}
}

function handleNotifyDataRX(name, data){
	// add new data to notify buffer
	if(poollist[name].nfybuf){
		poollist[name].nfybuf = Buffer.concat([poollist[name].nfybuf, data]);
	}else{
		poollist[name].nfybuf = data;
	}
	while(1){
		if(!poollist[name].nfylen){
			// look for start of new packet (null byte)
			let idx = poollist[name].nfybuf.indexOf(0);
			if(idx == -1){
				// no start delimiter found yet, toss buffer data
				poollist[name].nfybuf = null;
				break;
			}else{
				// found start of new packet
				if(idx+4 < poollist[name].nfybuf.length){
					// enought data to get length
					poollist[name].nfytype = poollist[name].nfybuf.readUInt8(idx+1);
					poollist[name].nfylen = poollist[name].nfybuf.readUInt16BE(idx+2);
					// re-reference the buffer to the start of the packet data after the null delimiter
					poollist[name].nfybuf = poollist[name].nfybuf.subarray(idx+4); // save data, just past 4 byte header
				}else{
					// but not enough data to get the length
					break;
				}
			}
		}
		let len = poollist[name].nfylen
		if(len){
			// do we have a full data packet?
			if(poollist[name].nfybuf.length >= len){
				let packetData = poollist[name].nfybuf.subarray(0, poollist[name].nfylen);
				// store remaining data
				poollist[name].nfybuf = poollist[name].nfybuf.subarray(poollist[name].nfylen);
				// handle the packet
				handleNotifyPacket(name, poollist[name].nfytype, packetData);
				// zero nfylen to start looking for next packet
				poollist[name].nfylen = 0;
			}else{
				// we don't have a full packet yet
				break;
			}
		}
	}
}

function createConnectionPool(name, host, port, maxval, minval){
	console.log("new connection pool to studio: "+name);
	// create connection pool (and notify connection)
	if(!port || !host)
		return;
	let factory = new conFactory(host, port);
	if(!maxval)
		maxval = 5;
	if(!minval)
		maxval = 2;
	let popts = {
		max: maxval, // maximum size of the pool
		min: minval, // minimum size of the pool
		testOnBorrow: true,		// validate before giving connection
		acquireTimeoutMillis: 5000,		// 5 second acquire timeout
		evictionRunIntervalMillis: 60000, // 60 second interval for running the eviction checker
		softIdleTimeoutMillis: 300000	// 5 minute timeout for inactive connections
	};
	poollist[name] = genericPool.createPool(factory, popts);
	poollist[name].nfylen = 0;	// notify packet current length, zero = waiting for new packet.
	let nfSocket = net.createConnection(port, host, function(){
			nfSocket.setKeepAlive(true);
			nfSocket.write('notify\n');
			nfSocket.write('vuon\n');
			console.log("connected to notice socket for studio: "+name);
		});
	nfSocket.on('error', function(err){});

	nfSocket.on('close', function(err){
			// delayed connection retry in 10 seconds
			let delay = setTimeout(() => {
				nfSocket.connect(port, host, {'force new connection': true }, function(){
					nfSocket.setKeepAlive(true);
					nfSocket.write('notify\n');
					nfSocket.write('vuon\n');
					console.log("reconnected to notice socket for studio: "+name);
				});
			}, 10000);
		});
				
	nfSocket.on('data', function(data){
			handleNotifyDataRX(name, data);
		});
	poollist[name].nfSocket = nfSocket;
}

function removeConnectionPool(name){
	console.log("closing connection pool to studio: "+name);
	// if it exists, nicely close all pool connections (and the notify connection)
	let pool = poollist[name];
	if(pool){
		pool.drain().then(function(){
			pool.clear();
		});
		pool.nfSocket.removeAllListeners('close');
		pool.nfSocket.destroy();
		// then remove from the list
		delete poollist[name];
	}
}

async function runLocalStudioServer(name, run){
	console.log("starting studio server '"+name+"', with command: "+run);
	let text;
	try{
		let cmdresult = await execShellCommand("/opt/audiorack/bin/"+run);
		const lines = cmdresult.split('\n');
		let last = lines.length-2;
		if(last < 0)
			last = 0;
		text = "studio server '"+name+"':\n"+cmdresult;
	}catch(error){
		text = "studio server run error '"+name+"':\n"+error;
		return {status:false, msg: text};
	}
	return {status:true, msg: text};
}

function reconfigureStudios(curSettings, newSettings){
	// handle removed or changed studios
	let keys = Object.keys(curSettings);
	let vals = Object.values(curSettings);
	for(let i=0; i < keys.length; i++){
		let key = keys[i];
		let newer = newSettings[key];
		if(newer){
			// studio exists in new settings too: check for changes
			let older = vals[i];
			if((newer.host != older.host) || (newer.port != older.port)){
				// connection has changed... reconfigure connection pool
				removeConnectionPool(key);
				createConnectionPool(key, newer.host, newer.port);
			}
		}else{
			// studio has been removed in the new settings.  Remove connection pool
			removeConnectionPool(key);
		}
	}
	// handle new studios
	keys = Object.keys(newSettings);
	vals = Object.values(newSettings);
	for(let i=0; i < keys.length; i++){
		let key = keys[i];
		if(curSettings[key] === undefined){
			// this is a new studio
			let val = vals[i];
			if(val.run && val.run.length && val.startup)
				runLocalStudioServer(key, val.run).then((running) => {
					if(running.status){
						createConnectionPool(key, val.host, val.port, val.maxpool, val.minpool);
					}
					console.log(running.msg);
				});
			else
				createConnectionPool(key, val.host, val.port, val.maxpool, val.minpool);
		}
	}
}

function prepForResponse(connection, timeoutmS){
	if(connection.respromise){
		// connection is already waiting for a response
		return false;
	}
	connection.respromise = new Promise(function(resolve, reject){
		connection.resolve = resolve;
		connection.reject = reject;
		connection.resTimer = setTimeout(() => {
				reject("response timeout");
			}, timeoutmS);
	});
	return true;
}

function getResponse(connection){
	return new Promise(function(resolve, reject){
		connection.respromise.then(function(result){
				resolve(result);
			}).catch(function(err){
				reject(err);
			});
	});
};

async function sendCommand(connection, command){
	let result = false;
	if(Array.isArray(command)){
		// array of command strings, to be sent on a single connection/session.
		for(let i=0; i < command.length; i++){
			if(command[i] == "notify")	// disallow the issuing of the notify command on a pooled cmd/response connection
				continue;
			if(command[i] == "vuon")	// disallow the issuing of the vuon command on a pooled cmd/response connection
				continue;
			if(prepForResponse(connection, 5000)){
				connection.write(command[i]+'\n');
				result = await getResponse(connection);
			}
		}
	}else{
		if(command == "notify")	// disallow the issuing of the notify command on a pooled cmd/response connection
			return false;
		if(command == "vuon")	// disallow the issuing of the vuon command on a pooled cmd/response connection
			return false;
		if(prepForResponse(connection, 5000)){
			connection.write(command+'\n');
			result = await getResponse(connection);
		}
	}
	return result;
}

function commandResponse(request, response, params, dirs){
	let st_name = dirs[2];	// length already verified
	// check for permission
	if((request.session.permission != "admin") && (request.session.permission != "manager") && (request.session.permission != "studio") && (request.session.permission != "programming")){
		response.status(401);
		response.end();
		return;
	}
	// check for valid studio name
	let st_pool = poollist[st_name];
	if(st_pool){
		if(params.rt){
			// real-time command (example "vol"), response ignored.  Use the nfSocket (notify socket)
			st_pool.nfSocket.write(params.cmd+'\n');
			response.status(201);
			response.end();
		}else{
			let resourcePromise = st_pool.acquire();
			resourcePromise.then(function(client){
				sendCommand(client, params.cmd).then((result) => {
					if(result){
						if(!params.raw)
							result = result.replace(/\n/g, "<br>");	// convert \n to \r\n format
						response.status(201);
						response.send(result);
						response.end();
					}else{
						response.status(500);
						response.end("studio connection session failure.");
					}
					st_pool.release(client);
				});
			}).catch(function(err){
				response.status(500);
				response.end("failed to get studio connection from pool");
			});
		}
	}else{
		response.status(400);
		response.end("Invalid studio name");
		return;
	}
}

module.exports = {
	configure: function (config) {
		let studios = config['studios'];
		if(_.isEqual(studios, studiolist) == false){
			console.log('Studio Configuration changed.');
			reconfigureStudios(studiolist, studios);
			studiolist = _.cloneDeep(studios);
		}
		console.log('Studio Configuration done.');
		return true;
	},
	
	handleRequest: function (request, response) {
		let params = undefined;
		if(request.method == 'GET')
			params = request.query;
		else if(request.method == 'POST')
			params = request.body;
		else{
			response.status(400);
			response.end();
			return;
		}
		let dirs = request.path.split('/');
		if(dirs[2] && dirs[2].length){
			commandResponse(request, response, params, dirs);	// /studioName?cmd=value1[&raw=1][&rt=1]
																				// raw, if set and true, will not convert \n to <br> for result viewing in a browser.
																				// cmd can be a list, all executed in sequence on a single connection/session.
																				// only the last response is retunred.
																				// If rt, this is a realtime (no delay) request.  No response is returned, and only a single
																				// command (line) can be issued.
		}else{
			response.status(400);
			response.end("Missing studio name");
		}
	},
	
	runStudio: function (request, response) { // /run/studioName
		// check for permission: admin only
		if(request.session.permission != "admin"){
			response.status(401);
			response.end();
			return;
		}
		// find named studio
		let dirs = request.path.split('/');
		if(dirs[3] && dirs[3].length){
			let rec = studiolist[dirs[3]];
			if(rec){
				runLocalStudioServer(dirs[3], rec.run).then((running) => {
					if(running.status){
						createConnectionPool(dirs[3], rec.host, rec.port, rec.maxpool, rec.minpool);
						response.status(201);
					}else
						response.status(500);
					response.send(running.msg);
					response.end();
				});
			}else{
				response.status(400);
				response.end("Invalid studio name");
			}
		}else{
			response.status(400);
			response.end("Missing studio name");
		}
	}
};

