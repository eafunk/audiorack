/*
 Copyright (c) 2024 Ethan Funk
 
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

const WebSocket = require('ws');
var dgram = require('dgram');
var sipProxInterfaces = {
	UDP: false,
	WS: [],
	WSS: []
};

var branch = 1;

function heartbeat() {
	this.isAlive = true;
}

function getNextBranchID(){
	let branchID = ";branch=z9hG4bK" + ("000000000"+branch.toString(16)).slice(-9);
	branch++;
	return branchID;
}

function parseSipURI(textURI, transport){
	// example-- sip:bob@203.0.113.22:5060;transport=udp
	let result = {transport: transport};
	let addr;
	let trn = transport;
	if(textURI.search("sip:") > -1){
		textURI = textURI.replace("sip:", "");
		result.isSip = true;
	}
	let parts = textURI.split("@");
	if(parts.length > 1)
		result.user = parts.shift();
	parts = parts[0].split(";");
	for(let i=1; i<parts.length; i++){
		let sub = parts[i].split("=");
		if(sub[0] == "transport"){
			result.transport = sub[1].toUpperCase();
			break;
		}
	}
	if(!result.transport)
		result.transport = "UDP";
	addr = parts[0];
	parts = addr.split("]");
	if(parts.length > 1){
		result.addr = parts[0]+"]";
		parts = parts[1].split(":");
		result.port = parts[1];
	}else{
		parts = parts[0].split(":");
		result.port = parts[1];
		addr = parts[0];
	}
	if(result.transport == "UDP"){
		if(!result.port || !parseInt(result.port))
			result.port = "5060";
	}
	result.address = addr;
	return result;
}

function routePacket(dataLines, dest, insertVia){
	if(dest.transport == "UDP"){
		let socket = sipProxInterfaces.UDP;
		if(socket){
			if(insertVia){
				let line = "Via: SIP/2.0/UDP ";
				line += socket.Identity+":"+socket.address().port+getNextBranchID();
				dataLines.splice(insertVia, 0, line);
			}
			let packet = dataLines.join("\r\n");
			socket.send(packet, 0, packet.length, dest.port, dest.address);
		}
	}else{
		let list = false;
		let socket = false;
		let svr;
		if(dest.transport == "WS")
			list = sipProxInterfaces.WS;
		else if(dest.transport == "WSS")
			list = sipProxInterfaces.WSS;
		if(list && list.length){
			for(let n = 0; n < list.length; n++){
				svr = list[n];
				let sockets = Array.from(svr.clients);
				for(let i = 0; i < sockets.length; i++){
					if(sockets[i].wsID == dest.address){
						socket = sockets[i];
						break;
					}
				}
			}
			if(socket){
				if(insertVia){
					let line = "Via: SIP/2.0/" + dest.transport + " ";
					line += svr.Identity+getNextBranchID();
					dataLines.splice(insertVia, 0, line);
				}
				let packet = dataLines.join("\r\n");
				socket.send(packet);
			}
		}
	}
}

function processWSsip(msg, socket, svr){
	msg = msg.toString();
	let dest = false;
	let viaLine = false;
	let lines = msg.split("\n");
	let viacnt = 0
	for(let n = 0; n < lines.length; n++){
		let line = lines[n].replace("\r", ""); // strip \r from line ends
		lines[n] = line; // store back without \n\r
		if(n == 0){
			// first via is us, forward to next via
			// example first via is not us -- ACK sip:bob@203.0.113.22:5060;transport=udp SIP/2.0
			// example first via is not us -- INVITE sip:bob@somedimain.com SIP/2.0 --> assume UDP on port 5060
			// example-- SIP/2.0 200 OK --> first via must be us, send to second via.  Otherwise ignore 
			let command = line.split(" ");
			if(command.length == 3){
				dest = parseSipURI(command[1]);
				if(!dest.isSip)
					dest = false;
			}
		}

		if(line.startsWith("Via: ")){
			// example-- Via: SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKhjhs8ass877
			let fields = line.split(" ");
			if(!viacnt){
				let sub = fields[1].split("/");
				let transport = sub[2];
				sub = fields[2].split(";");
				let id = sub[0];
				sub = sub[1].split("=");
				let branch = sub[1];
				if(id == svr.Identity){ // IPv6 address must have [] around the address string
					// This via is us: strip this via off and forward to next via.
					lines.splice(n, 1);
					if(n >= lines.length)
						return; // no additional line!
					line = lines[n].replace("\r", ""); // strip \r from line end
					if(line.startsWith("Via: ")){
						fields = line.split(" ");
						sub = fields[1].split("/");
						transport = sub[2];
						sub = fields[2].split(";");
						dest = parseSipURI(sub[0], transport);
					}else
						return;	// next line should be a via, and it is not!
					line = lines[n].replace("\r", ""); // strip \r from line ends
					lines[n] = line; // store back without \n\r
					continue;
				}else{
					// Otherwise, note via as ws socket this message came from, add our via address, and forward to dest address
					viaLine = n;
					if(socket.wsID != id)
						socket.wsID = id;
				}
			}
			viacnt++;
		}
		if(line.startsWith("Max-Forwards: ")){
			let fields = line.split(" ");
			let val = Number.parseInt(fields[1]);
			val--;
			if(val > 0){
				// update hop count remaining
				fields[1] = val.toString();
				lines[n] = fields.join(" ");
			}else{
				console.log("proxy ws packet to many hops");
				return;
			}
		}
	}
	if(dest)
		// send processed packet
		routePacket(lines, dest, viaLine);
}

function processUDPsip(msg, rinfo){
	msg = msg.toString();
	let dest = false;
	let dest = false;
	let viaLine = false;
	let lines = msg.split("\n");
	let viacnt = 0
	for(let n = 0; n < lines.length; n++){
		let line = lines[n].replace("\r", ""); // strip \r from line ends
		lines[n] = line; // store back without \n\r
		if(n == 0){
			// first via is us, forward to next via
			// example first via is not us -- ACK sip:bob@203.0.113.22:5060;transport=udp SIP/2.0
			// example first via is not us -- INVITE sip:bob@somedimain.com SIP/2.0 --> assume UDP on port 5060
			// example-- SIP/2.0 200 OK --> first via must be us, send to second via.  Otherwise ignore 
			let command = line.split(" ");
			if(command.length == 3){
				dest = parseSipURI(command[1]);
				if(!dest.isSip)
					dest = false;
			}
		}
		if(line.startsWith("Via: ")){
			// example-- Via: SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKhjhs8ass877
			let fields = line.split(" ");
			if(!viacnt){
				let sub = fields[1].split("/");
				let transport = sub[2];
				sub = fields[2].split(";");
				let id = sub[0];
				sub = sub[1].split("=");
				let branch = sub[1];
				let svrAdr = sipProxInterfaces.UDP.address();
				if(id == (sipProxInterfaces.UDP.Identity+":"+svrAdr.port)){ // IPv6 address must have [] around the address string
					// This via is us: strip this via off and forward to next via.
					lines.splice(n, 1);
					if(n >= lines.length)
						return; // no additional line!
					line = lines[n].replace("\r", ""); // strip \r from line end
					if(line.startsWith("Via: ")){
						fields = line.split(" ");
						sub = fields[1].split("/");
						transport = sub[2];
						sub = fields[2].split(";");
						dest = parseSipURI(sub[0], transport);
					}else
						return;	// next line should be a via, and it is not!
					line = lines[n].replace("\r", ""); // strip \r from line ends
					lines[n] = line; // store back without \n\r
					continue;
				}else{
					// Otherwise, add our via address, and forward to dest address
					if(!dest)
						dest = {transport: transport.toUpperCase(),  address: id};
					viaLine = n;
				}
			}
			viacnt++;
		}
		if(line.startsWith("Max-Forwards: ")){
			let fields = line.split(" ");
			let val = Number.parseInt(fields[1]);
			val--;
			if(val > 0){
				// update hop count remaining
				fields[1] = val.toString();
				lines[n] = fields.join(" ");
			}else{
				console.log("proxy ws packet to many hops");
				return;
			}
		}
	}
	if(dest)
		// send processed packet
		routePacket(lines, dest, viaLine);
}

function wsSetCallbacks(wssvr){
	wssvr.conTimer = setInterval((wssvr) => {	// handle connection timeout
			wssvr.clients.forEach(function each(socket) {
				if(socket.isAlive === false){
					socket.terminate();
					return ;
				}
				socket.isAlive = false;
				socket.ping();
			});
	}, 30000, wssvr);
	wssvr.on('connection', socket => wsHandleConnection(socket, wssvr));
	wssvr.on('close', conTimer => wsCloseProxy(conTimer, wssvr));
}

function wsHandleConnection(socket, wssvr){
	socket.isAlive = true;
	socket.on('message', data => processWSsip(data, socket, wssvr));
	socket.on('pong', heartbeat);
}

function wsCloseProxy(conTimer, proxy){
	removeProxy(proxy); 
	clearInterval(conTimer);
}

function removeProxy(proxy){
	let idx = sipProxInterfaces.WS.indexOf(proxy);
	if(idx > -1)
		sipProxInterfaces.WS.splice(idx, 1);
	idx = sipProxInterfaces.WSS.indexOf(proxy);
	if(idx > -1)
		sipProxInterfaces.WSS.splice(idx, 1);
	if(sipProxInterfaces.UDP == proxy)
		sipProxInterfaces.UDP = false;
}

function addProxy(proxy, kind){
	if(kind == "UDP"){
		// only one UDP socket
		sipProxInterfaces.UDP = proxy;
	}else if(kind == "HTTP"){
		// Can have multiple Websocket interfaces
		if(proxy._server.key)
			sipProxInterfaces.WSS.push(proxy);
		else
			sipProxInterfaces.WS.push(proxy);
	}
}

module.exports = {
	initializeProxyWS: function initializeProxyWS(httpServer, proxy, identityAddr) {
		if(proxy){
			removeProxy(proxy);
			proxy.close();	// close existing ws server
		}
		if(httpServer && identityAddr && identityAddr.length){
			proxy = new WebSocket.Server({ server: httpServer });
			proxy.Identity = identityAddr;
			wsSetCallbacks(proxy);
			addProxy(proxy, "HTTP");
			console.log("SIP proxy websocket added to http server instance.");
			return proxy;
		}
		return false;
	},
	
	initializeProxyUDP: function initializeProxyUDP(bindInfo, udpSocket, identityAddr) {
		if(udpSocket)
				udpSocket.close();	// close existing udp socket
		if(identityAddr && identityAddr.length){
			try{
				let sock = dgram.createSocket({ type: 'udp4', reuseAddr: true });
				sock.on('message', (msg, rinfo) => processUDPsip(msg, rinfo));
				sock.bind(bindInfo);
				sock.Identity = identityAddr;
				addProxy(sock, "UDP");
				return sock;
			}catch(e){
				console.log(e);
			}
		}
		return false;
	}
};
