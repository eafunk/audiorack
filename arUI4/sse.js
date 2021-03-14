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

var sseClients = {};
var sseID = BigInt(0);

module.exports = {
	clients: sseClients,
	postSSEvent: function(event, data){
		sseID = sseID + 1n;
		for(const sid in sseClients){
			let client = sseClients[sid];
			let res = client.response;
			if(res){
				if(event){
					// specific event name
					if(client.registered.indexOf(event) > -1){
						res.write("retry: 10000");
						res.write("id: "+sseID+"\n");
						res.write("event: "+event+"\n");
						res.write("data: "+data+"\n\n");
					}
				}else{
					// general message 
					res.write("retry: 10000");
					res.write("event: message\n");
					res.write("id: "+sseID.toString()+"\n");
					res.write("data: "+data+"\n\n");
				}
			}
		}
	},
	startSessionClearing: function (sessionStore, interval){
		setInterval(function (){
			for(const sid in sseClients){
				this.store.get(sid, function(error, session){
					if(!session){
						if(sseClients[sid].res)
							sseClients[sid].res.end();
						delete sseClients[sid];
					}
				});
			}
		}.bind({store: sessionStore}), interval);
	}
}
