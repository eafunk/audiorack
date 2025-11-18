// node.js version 14 or later required
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

var crypto = require('crypto');
const os = require('os');
var express = require('express');
var session = require('express-session');
var sessionStore = {store: false}; // will be set later
var fileStore = require('session-file-store')(session);
var bodyParser = require('body-parser');
var http = require('http');
var https = require('https');
var fs = require('fs');
var path = require('path');
const _ = require('lodash');
const multer = require('multer');
var slug = require('slug');
const extract = require('extract-zip');
var sse = require('./sse.js');
var lib = require('./library');
var studio = require('./studio');
var sipproxy = require('./sipWsProxy');

var httpServer = false;
var httpsServer = false;
var wss;	// secure websocket server instance for sip/websocket proxy
var ws;	// nonSSL websocket server instance for sip/websocket proxy
var udp;	// UDP socket for sip/websocket proxy
var config = false;
var tmpDirIntervalObj = false;
var storage = false;
var sessionUse = false;
var httpConf = false;

const DefLimit = 0;

/********** general trap for uncought exceptions, adding a backtrace to console output for debugging help **********/
process.on('uncaughtException', async (error) => {
	console.error(`Caught exception: ${error}\n` + `Exception origin: ${error.stack}`);
	await sleep(2000);
	process.exit(1);
});


/********** Search For Object Functions **********/
// NOT USED
function fineObjectWithValue(key, value, anArray){
	for(let i=0; i < anArray.length; i++){
		let obj = anArray[i];
		if(obj[key] === value)
			return obj;
	}
}

/********** REST request for settings object or subobject array **********/

function restObjectRequest(request, response, object, keysonly, parentkey){
	// if parentkey is false, empty, etc., all keys in the source object will be rendered as a list 
	// (array) of objects with "id" set to the key, and "value" set to the key's value, or if 
	// the key is associated with another object, the list entry object will additionally inherit 
	// all key/value pairs from the key's object instead of a single "value." 
	
	// If parentkey is set, an array with a single result object is returned with its "id" set the
	// the specified key, and with the result object inheriting all key/value pairs from the object.
	
	// If keysonly is true, we do not send any values, to support low authurity user queries.
	
	let params = {};
	if(request.method == 'GET')
		params = request.query;
	else if(request.method == 'POST')
		params = request.body;
		
	// handle range request
	let offset = 0;
	let cnt = DefLimit;
	if(params.range){
		let parts = params.range;
		offset = parts[0];
		cnt = parts[1]-offset;
	}
	
	let result = [];
	let total = 0;
	if(parentkey){
		if(keysonly)
			result.push({id: parentkey});
		else{
			if(typeof object !== 'object')
				result.push({id: parentkey, value: object});
			else
				result.push({id: parentkey, ...object});
		}
	}else{
		let keys = Object.keys(object);
		for(let i = 0; i < keys.length; i++){
			let key = keys[i];
			let val = object[key];
			if(keysonly){
				result.push({id: key});
			}else{
				if(typeof val !== 'object')
					result.push({id: key, value:val});
				else{
					result.push({id: key, ...val});
				}
			}
		}
	}

	// size result for requested range
	total = result.length;
	if(cnt){
		if(cnt + offset > total)
			cnt = total - offset;
		result = result.slice(offset, offset + cnt);
	}
	// handle items range response
	let last = result.length-1;
	if(last < 0)
		last = 0;
	let header = "items "+offset+"-"+last+"/"+total;
	response.setHeader('Content-Range', header);
	response.status(200);
	response.json(result);
	response.end();
}

/********** Authentication Functions **********/

function generatePwdHash(password){
	let salt = crypto.randomBytes(64).toString('base64');
	
	// SHA512 the salt + password string
	let hash = crypto.createHmac('sha512', salt);
	hash.update(password);
	
	return {salt: salt, hash: hash.digest('base64')};
}

function checkPwdHash(salt, clearpw, hashedpw){
	let hash = crypto.createHmac('sha512', salt);
	hash.update(clearpw);
	let hashstr = String(hash.digest('base64'));
	return hashedpw == hashstr;
}

/********** Configuration Functions **********/
/* config file json structure:
{
	"http":{
		"http_port": 4000,	// delete this propety to disable http server
		"https_certfile": "/path/to/ssl/certfile",	// optional: required for enabling https server
		"https_keyfile": "/path/to/ssl/keyfile",	// optional: required for enabling https server
		"https_port": 8888,	// optional: required for enabling https server
		"ses_secret": "",	// session storage secret for client encryption.  Will be set randomy and saved if missing.
		"sipproxy_websocket_id": "some ip address", // client facing ip address for http and https servers above, if used for sip websocket proxy. Blank to disable.
		"sipproxy_udp_port": 5050 // UDP interface port number for the sip/webRTC gateway, 0 to let OS choose.
		"sipproxy_udp_id": "some ip address", // client facing ip address for the UDP interface used for a sip websocket proxy. Blank to disable.
	},
	"files": {
		"prefixes": ["prefixpattern1", "prefixpattern2", "prefixpattern3"],	// optional to override the default prefix patterns for the OS
		"tmpMediaDir": "/some/tmp/dir/",
		"tmpMediaAgeLimitHrs": 12.0,
		"mediaDir": "/default/media/dir/",
		"mediaDir-other": "/some/other/place/",   // alternate media directories with the name given after the dash, in this case, "other"
		"supportDir": "/path/to/audiorack/support/dir"  // optional to override the default /opt/audiorack/support location.
	},
	"users": {
		"admin": {
			"salt": "abc123", 
			"password": "configure", 
			"permission": "admin" 		// air, manager, admin, traffic
		},
		"anotheruser": {
				user settings...
		}
	],
	"library": {
		"type": "mysql",
		"host": "localhost",
		"user": "username",
		"port": port-number,			// this is optional
		"password": "password",
		"database": "databasename",
		"prefix": "ar_",
		"conLimit": 6;
	},
	"studios": {
		"StudioA": {	// no spaces for easy URL passing
			"host": "localhost",
			"port": 9550,
			"dbloc": 1,
			"run": "/opt/audiorack/bin/arServer4 -k"
		},
		"StudioB": {	// no spaces for easy URL passing
				additional studio settings
		}
	}
}
*/

function listControlDirFilesFunc(request, response){
	if(request.session.loggedin == true){
		fs.readdir(__dirname+"/client/control", function(err, files){
			if(err){
				console.log("Error getting control surface directory listing: ", err);
				response.status(500);
				response.end();
			}else{
				response.status(200);
				response.json(files);
				response.end();
			}
		});
	}else{
		response.status(403);
		response.end();
	}
}

function listInvTemplateDirFilesFunc(request, response){
	if(request.session.loggedin == true){
		fs.readdir(__dirname+"/client/inv-template", function(err, files){
			if(err){
				console.log("Error getting template directory listing: ", err);
				response.status(500);
				response.end();
			}else{
				response.status(200);
				response.json(files);
				response.end();
			}
		});
	}else{
		response.status(403);
		response.end();
	}
}

function listTmpDirFilesFunc(request, response){
	if(request.session.loggedin == true){
		let files = config['files'];
		if(files){
			let tmpDir = files['tmpMediaDir'];
			if(tmpDir && tmpDir.length){
				let tail ="";
				let tailIdx = request.path.search("/tmplist/");
				if(tailIdx > -1){
					tail = request.path.substring(tailIdx + 9);
					tail = decodeURIComponent(tail);
				}
				tmpDir = tmpDir + tail;
				fs.readdir(tmpDir, function(err, files){
					if(err){
						console.log("Error getting temporary media directory listing from " + tmpDir);
						response.status(500);
						response.end();
					}else{
						for(let  i=0; i<files.length; i++){
							let file = files[i];
							let thisFile = path.join(tmpDir, file);
							let finfo = fs.statSync(thisFile);
							let obj = {}; 
							obj.isDir = finfo.isDirectory();
							obj.id = file;	// file name, key is id for react-admin frontend easy integration
							if(obj.isDir)
								obj.size = "";
							else
								obj.size = Math.round(10 * finfo.size / 1024) / 10;
							obj.created = finfo.ctime;
							obj.modified = finfo.mtime;
							files[i] = obj;
						}
						response.status(200);
						response.send(files);
						response.end();
					}
				});
			}else{
				console.log("Error: tmpMediaDir not set. Can't get directory listing.");
				response.status(500);
				response.end();
			}
		}else{
			console.log("Error: tmpMediaDir not set. Can't get directory listing.");
			response.status(500);
			response.end();
		}
	}else{
		response.status(403);
		response.end();
	}
}

function clearTmpDirAgedFilesFunc(){
	let files = config['files'];
	if(files){
		let tmpDir = files['tmpMediaDir'];
		let tmpAge = files['tmpMediaAgeLimitHrs'];
		if(tmpAge && tmpDir && tmpDir.length){
			fs.readdir(tmpDir, function(err, files){
				if(err){
					console.log("Error getting temporary media directory listing from " + tmpDir);
				}else{
					files.forEach(function(file){
						let thisFile = path.join(tmpDir, file);
						fs.stat(thisFile, function(err, stats){
							if(stats){
								let diffHrs = (Date.now() - stats.ctime.getTime()) / (1000 * 60 * 60); // gives hour difference 
								if(diffHrs > tmpAge){
									if(stats.isFile()){
										// delete this file, it's too old
										try{
											fs.unlink(thisFile, function (err){
												if(err)
													console.log("aged tempMediaDir file remove failed: " + file + "err="+ err);
												else
													console.log("aged tempMediaDir file removed: " + file);
											});
										}catch(e){
											console.log('Temp. file deletion error:');
											console.log(e);
										}
									}else if(stats.isDirectory()){
										// recursivly remove directory and contents regardless of contents age
										try{
											fs.rmdir(thisFile, {recursive: true}, function (err){
												if(err)
													console.log("aged tempMediaDir directory remove failed: " + file + "err="+ err);
												else
													console.log("aged tempMediaDir directory removed: " + file);
											});
										}catch(e){
											console.log('Temp. directory deletion error:');
											console.log(e);
										}
									}
								}
							}
						});
					});
				}
			})
		}
	}
}

function cleanFileNameForUpload(file){
	// we want to keep '.', so be break the path into pieces, clean each piece and reassemble
	let dots = file.split(".");
	for(let i=0; i<dots.length; i++)
		dots[i] = slug(dots[i]);
	return dots.join(".");
}

function applySettingsToApp(conf){
	if(_.isEqual(conf.http, httpConf))
		return;
	console.log('HTTP Configuration changed.');
	httpConf = _.cloneDeep(conf.http);
	if(conf.http.https_port && conf.http.https_keyfile && conf.http.https_certfile){
		fs.readFile(conf.http.https_keyfile, 'utf8', function(err, data){ 
			if(err){
				console.log("failed to read https key file: "+conf.http.https_keyfile);
			}else{
				let privateKey = data;
				fs.readFile(conf.http.https_certfile, 'utf8', function(err, data){ 
					if(err){
						console.log("failed to read https key file: "+conf.http.https_keyfile);
					}else{
						let certificate = data;
						let credentials = {key: privateKey, cert: certificate, requestCert: false, rejectUnauthorized: false};
						if(httpsServer){
							console.log("stopping existing https server");
							httpsServer.close(() => {
								httpsServer = https.createServer(credentials, app);
								if(conf.http.https_bind)
									httpsServer.listen(conf.http.https_port, conf.http.https_bind);
								else
									httpsServer.listen(conf.http.https_port);
								console.log("https server listening on port "+conf.http.https_port);
							});
						}else{
							httpsServer = https.createServer(credentials, app);
							if(conf.http.https_bind)
								httpsServer.listen(conf.http.https_port, conf.http.https_bind);
							else
								httpsServer.listen(conf.http.https_port);
							console.log("https server listening on port "+conf.http.https_port);
						}
						wss = sipproxy.initializeProxyWS(httpsServer, wss, conf.http.sipproxy_websocket_id);
					}
				});
			}
		});
	}else{
		sipproxy.initializeProxyWS(false, wss);
		console.log("no parameters for https server");
	}
	
	if(conf.http.http_port){
		if(httpServer){
			console.log("stopping existing http server");
			httpServer.close(() => {
				httpServer = http.createServer(app);
				if(conf.http.http_bind)
					httpServer.listen(conf.http.http_port, conf.http.http_bind);
				else{
					httpServer.listen(conf.http.http_port);
				}
				console.log("http server listening on port "+conf.http.http_port);
			});
		}else{
			httpServer = http.createServer(app);
			if(conf.http.http_bind)
				httpServer.listen(conf.http.http_port, conf.http.http_bind);
			else{
				httpServer.listen(conf.http.http_port);
			}
			console.log("http server listening on port "+conf.http.http_port);
		}
		ws = sipproxy.initializeProxyWS(httpServer, ws, conf.http.sipproxy_websocket_id);
	}else{
		ws = sipproxy.initializeProxyWS(false, ws);
		httpServer = false;
		console.log("no parameters for http server");
	}
	if(!conf.http.ses_ttl)
		conf.http.ses_ttl = 86400;	// default session time to live is 1 day
	sessionStore.store = new fileStore({ttl: conf.http.ses_ttl, path: os.homedir()+"/.audiorack/uisessions"});
	sessionUse = session({
		store: sessionStore.store,
		secret: conf.http.ses_secret,
		ttl: conf.http.ses_ttl,
		resave: false,
		saveUninitialized: false
	});	// this sets/changes the sessionUse dynamic var referenced in the initial session setup
	
	//	UDP socket setup for SIP websocket proxy, if any.
	udp = sipproxy.initializeProxyUDP(conf.http.sipproxy_udp_port, udp, conf.http.sipproxy_udp_id)
}

function handleConfigChanges(conf){
	lib.configure(conf);
	studio.configure(conf);
	let files = conf['files'];
	if(files){
		let tmpDir = files['tmpMediaDir'];
		let tmpAge = files['tmpMediaAgeLimitHrs'];
		if(tmpDir && tmpDir.length){
			if(tmpAge){
				let interval = tmpAge * 3600 * 1000; // hours to mSec.
				// check at 1/4 the age limit rate
				tmpDirIntervalObj = setInterval(clearTmpDirAgedFilesFunc, interval/4);
				// And fire it off right now too..
				clearTmpDirAgedFilesFunc();
			}else if(tmpDirIntervalObj){
				// stop temp media dir cleanout 
				clearInterval(tmpDirIntervalObj);
				tmpDirIntervalObj = false;
			}
			storage = multer.diskStorage({
				destination: function (req, file, cb){
					cb(null, tmpDir);
				},
				filename: function (req, file, cb){
					cb(null, cleanFileNameForUpload(file.originalname));
				}
			})
		}else{
			// disable uploading
			storage = false;
			// stop temp media dir cleanout 
			clearInterval(tmpDirIntervalObj);
			tmpDirIntervalObj = false;
		}
	}else{
		// disable uploading
		storage = false;
		// stop temp media dir cleanout 
		clearInterval(tmpDirIntervalObj);
		tmpDirIntervalObj = false;
	}
	applySettingsToApp(conf);
}

function loadConfiguration(){
	let confPath = path.join(os.homedir(), '.audiorack');
	if(!fs.existsSync(confPath)){
		console.log("Creating configuaration directory at "+ confPath);
		fs.mkdirSync(confPath);
	}
	fs.readFile(path.join(confPath, 'arui_config.json'), function(err, data){ 
		// Check for errors 
		if(!err){
			// Converting to JSON 
			try{
				config = JSON.parse(data);
			}catch(e){
				console.log('Configuration file JSON format error.');
			}
			handleConfigChanges(config);
		}else{
			// try to retreive default config file
			fs.readFile('/opt/audiorack/support/arui_defconf.json', function(err, data){
				if(err){
					console.log('No configuration found. Creating minimal configuarion file.');
					// default user: admin, password: configure
					let hash = generatePwdHash("configure");
					let str = "{ \"http\": {\"http_port\": 3000, \"ses_secret\": \""+crypto.randomBytes(64).toString('base64')+"\"}, ";
					str += " \"users\": { \"admin\": { \"salt\": \"";
					str += hash.salt;
					str += "\", \"password\": \""
					str += hash.hash;
					str += "\", \"permission\": \"admin\" } },";
					str += "\"studios\": { } }";
					saveConfiguration(JSON.parse(str));
				}else{
					console.log('No configuration found. Creating from default configuarion file.');
					// Converting to JSON 
					try{
						config = JSON.parse(data);
					}catch(e){
						console.log('Configuration file JSON format error.');
					}
					// replace session secret with new random value
					config.http.ses_secret = crypto.randomBytes(64).toString('base64');
					saveConfiguration(config);
				}
			});
		}
	});
}

function saveConfiguration(conf){
	let dir = path.join(os.homedir(), '.audiorack');
	// make directory if not already present
	if(!fs.existsSync(dir))
		fs.mkdirSync(dir);
		
	// stringify JSON Object
	let confContent = JSON.stringify(conf, null, 3);

	fs.writeFile(path.join(dir, 'arui_config.json'), confContent, 'utf8', function (err){
		if(err){
			console.log("An error occured while writing JSON Object to File.");
		}else{
			console.log("Configuration file has been saved.");
			config = conf;
			handleConfigChanges(conf);
		}
	});
}

function getConf(request, response){
	if(request.session.loggedin == true){
		let dirs = request.path.split('/');
		let obj = config;
		if(dirs.length < 3){
			// root list
			restObjectRequest(request, response, obj, true, false);
			return;
		}else{
			// travers to requested object
			let i;
			for(i = 2; i < dirs.length; i++){
				obj = obj[dirs[i]];
				if(!obj){
					// requested object doesn't exists... return empty result
					restObjectRequest(request, response, {}, true, false);
					return;
				}
			}
			
			let parKey;
			if(i > 3)
				// flatten results past level 3, with id = directory name as the specified id
				parKey = dirs[dirs.length-1];
			if((request.session.permission == "admin") || ((dirs[dirs.length-3] == "studios") && (parKey == "host"))){
				// full property access
				restObjectRequest(request, response, obj, false, parKey);
			}else{
				// access list only
				restObjectRequest(request, response, obj, true, parKey);
			}
			return;
		}
	}else{
		response.status(403);
		response.end("Unauthorized");
	}
}

function setConf(request, response){
// examples:
//		setconf/files/key?value=something
//		setconf/studios/StudioB?host=something&port=something-else

	if((request.session.loggedin == true) && (request.session.permission == 'admin')){
		let params = undefined;
		if(request.method == 'GET')
			params = request.query;
		else if(request.method == 'POST')
			params = request.body;
		if(params){
			let dirs = request.path.split('/');
			let locConf = _.cloneDeep(config);
			let child = locConf;
			let obj;
			if(dirs.length < 4){
				response.status(400);
				response.end("Bad request");
				return;
			}else{
				// travers to requested object
				let i = 0;
				for(i = 2; i < dirs.length; i++){
					obj = child;
					child = obj[dirs[i]];
					if(!child){
						// requested object doesn't exists... 
						if(i == (dirs.length-1)){
							// last dir: create it
							child = {};
							obj[dirs[i]] = child;
						}else{
							response.status(400);
							response.end("Bad request");
							return;
						}
					}
				}
				let value = params.value;
				if(value)
					obj[dirs[i-1]] = value;	// flat value
				else{
					delete params.id;	// remove the id object.
					obj[dirs[i-1]] = {...child, ...params};	// merge/replace key/values
				}
				saveConfiguration(locConf);
				response.status(201);
				response.end();
			}
		}else{
			response.status(400);
			response.end("Bad request");
		}
	}else{
		response.status(403);
		response.end("Unauthorized");
	}
}

function delConf(request, response){
// examples:
//		delconf/files/key
//		delconf/studios/StudioB

	if((request.session.loggedin == true) && (request.session.permission == 'admin')){
		let dirs = request.path.split('/');
		let locConf = _.cloneDeep(config);
		let child = locConf;
		let obj;
		if(dirs.length < 4){
			response.status(400);
			response.end("Bad request");
			return;
		}else{
			// travers to requested object
			let i = 0;
			for(i = 2; i < dirs.length; i++){
				obj = child;
				child = obj[dirs[i]];
				if(!child){
					// requested object doesn't exists... create it
					response.status(400);
					response.end("Bad request");
					return;
				}
			}
			delete obj[dirs[i-1]];
			saveConfiguration(locConf);
			response.status(201);
			response.end();
		}
	}else{
		response.status(403);
		response.end("Unauthorized");
	}
}

/********** Main Program and event callback dispatchers **********/

function sessionDispatcher(req, res, next){
	// this let us change the sessionUse function dynamically
	if(!sessionUse)
		next();
	else
		sessionUse(req, res, next);
}

// Read config file 
loadConfiguration();

var app = express();

app.use(bodyParser.urlencoded({extended : true}));
app.use(bodyParser.json());
app.use(sessionDispatcher);

/* HTML response codes:
	200 - OK
	201 - Created  # Response to successful POST or PUT
	302 - Found # Temporary redirect such as to /login
	303 - See Other # Redirect back to page after successful login
	304 - Not Modified
	400 - Bad Request
	401 - Unauthorized  # Not logged in
	403 - Forbidden  # Accessing another user's resource
	404 - Not Found
	500 - Internal Server Error
*/

app.get('/getconf\*', getConf);

app.post('/setconf\*', setConf);

app.get('/delconf\*', delConf);

app.post('/genpass', function(request, response){
	if((request.session.loggedin == true) && (request.session.permission == 'admin')){
		let password = request.body.password;
		if(password){
			let tmp = generatePwdHash(password);
			response.send(tmp);
			response.status(200);
		}else{
			response.status(400);
		}
	}else{
		response.status(403);
	}
	response.end();
});
// temp. for debuging have a get version too
app.get('/genpass', function(request, response){
	if((request.session.loggedin == true) && (request.session.permission == 'admin')){
		let password = request.query.password;
		if(password){
			let tmp = generatePwdHash(password);
			response.send(tmp);
			response.status(200);
		}else{
			response.status(400);
		}
	}else{
		response.status(403);
	}
	response.end();
});

/* permission string values:
	'admin': Administrator (All)
	'manager': Manager (Studio, Library, Traffic)
	'production': Production (Library, Traffic)
	'programming': Programming (Studio, Library)
	'traffic': Traffic only
	'library': Library only
	'studio': Studio only   */
app.post('/auth', function(request, response){
	let username = request.body.username;
	let password = request.body.password;
	if(username && password){
		let userlist = config['users'];
		if(userlist){
			let rec = userlist[username];
			if(rec){
				if(checkPwdHash(String(rec.salt), password, String(rec.password))){
					request.session.loggedin = true;
					request.session.username = username;
					request.session.permission = rec.permission;
					response.status(200);
					response.send();
				}else{
					response.status(401);
					response.end("Bad username and password combination");
					return;
				}
			}else{
				response.status(401);
				response.end("Bad username and password combination");
				return;
			}
		}else{
			response.status(401);
			response.end("Bad username and password combination");
			return;
		}
	}else{
		response.status(400);
	}
	response.end();
});

app.post('/unauth', function(request, response){
	if(request.session.loggedin){
		request.session.destroy();
		response.status(200);
	}else
		response.status(401);
	response.end();
});

app.get('/unauth', function(request, response){
	if(request.session.loggedin){
		request.session.destroy();
		response.status(200);
	}else
		response.status(401);
	response.end();
});

app.get('/library/\*', function(request, response){
	if(request.session.loggedin)
		lib.handleRequest(request, response);
	else{
		response.status(401);
		response.end();
	}
});

app.post('/library/\*', function(request, response){
	if(request.session.loggedin)
		lib.handleRequest(request, response);
	else{
		response.status(401);
		response.end();
	}
});

app.get('/studio/run/\*', function(request, response){
	if(request.session.loggedin)
		studio.runStudio(request, response);
	else{
		response.status(401);
		response.end();
	}
});

app.get('/studio/\*', function(request, response){
	if(request.session.loggedin)
		studio.handleRequest(request, response);
	else{
		response.status(401);
		response.end();
	}
});

app.post('/studio/\*', function(request, response){
	if(request.session.loggedin)
		studio.handleRequest(request, response);
	else{
		response.status(401);
		response.end();
	}
});

app.get('/who', function(request, response){
	//request.query.property
	if(request.session.loggedin){
		let li = {username: request.session.username, permission: request.session.permission};
		response.status(200);
		response.send(li);
	}else{
		response.status(401);
		response.send({});
	}
	response.end();
});

app.get('/stadmin', function(request, response){
	let html = "";
	if(request.session.permission === 'admin'){
		html = `
							<div class="scroll">
								<button class="tab active" data-id="stConsole" onclick="selectTabType(event, reloadStudioSection, 'console')">Console</button>
								<button class="tab" data-id="stConfOut" onclick="selectTabType(event, reloadStudioSection, 'outs')">Outputs</button>
								<button class="tab" data-id="stConfIn" onclick="selectTabType(event, reloadStudioSection, 'ins')">Live Inputs</button>
								<button class="tab" data-id="stConfLib" onclick="selectTabType(event, reloadStudioSection, 'library')">Library</button>
								<button class="tab" data-id="stConfMixer" onclick="selectTabType(event, reloadStudioSection, 'mixer')">Mixer</button>
								<button class="tab" data-id="stConfAuto" onclick="selectTabType(event, reloadStudioSection, 'automation')">Automation</button>
								<button class="tab" data-id="stConfSIP" onclick="selectTabType(event, reloadStudioSection, 'voip')">VoIP</button>
								<button class="tab" data-id="stConfRoute" onclick="selectTabType(event, reloadStudioSection, 'jconns')">Wire</button>
								<div id="stConsole" class="accpanel" style="display: flex;">
									<textarea id="stConsRep" rows="19" width=100% readonly=""></textarea>
									<div>
										<input type="text" id="conCommand" placeholder="command" style="float: left; width: calc(100% - 40px)"></input>
										<button class="editbutton" type="submit" onClick="stConsSend()" style="float: right; width: 32px;">Send</button>
									</div>
								</div>
								<div id="stConfOut" class="accpanel">
									<table>
										<tr>
											<td style="width: 130px;">
												<div id="stOutConfList" class="scroll" style="height: 283px; border: 1px; border-style: solid;">
													No Outputs
												</div>
											</td>
											<td>
												<div id="stConfOutSettings">
													Show Volume: <input id="stConfOutVolUI" type="checkbox"> Name: <input id="stConfOutName" type="text" size="20" value=""><button onclick="stConfOutDelete(event)">Delete</button>
													<div id="stConfOutPorts" style="height: 114px;">
													</div>
													<br>
													<table class="tableleftj">
														<tr>
															<td><div id="stConfOutBus"></div></td>
															<td style="width:60%"><div id="stConfOutMute"></div></td>
														</tr>
													</table>
													<span style='float: center;'><button onclick="stConfOutSave(event)">Save</button></span>
												</div>
											</td>
										</tr>
									</table>
								</div>
								<div id="stConfIn" class="accpanel">
									<table>
										<tr>
											<td style="width: 120px;">
												<div id="stInConfList" class="scroll" style="height: 262px; border: 1px; border-style: solid;">
													No Live Inputs
												</div>
												<button onclick="stConfInNew(event)">New</button>
											</td>
											<td>
												<div id="stConfInSettings" class="scroll" style="height: 290px;">
													Name: <input id="stConfInName" type="text" size="20" value=""><button onclick="stConfInDelete(event)">Delete</button>
													<div id="stConfInMain" class="boxed">
														<div id="stConfInPorts" style="height: 114px;">
														</div>
														<br>
														<table class="tableleftj">
															<tr>
																<td><div id="stConfInBus"></div></td>
																<td><div id="stConfInTB"></div></td>
																<td><div id="stConfInMute"></div></td>
															</tr>
														</table>
													</div>
													<div id="stConfInFeed" class="boxed">
														Feed (Mix-minus)
														<div id="stConfInFeedPorts" style="height: 114px;">
														</div>
														<table class="tableleftj">
															<tr>
																<td><div id="stConfInFeedBus"></div></td>
																<td><div id="stConfInFeedTB"></div></td>
																<td><div id="stConfInFeedVol">
																	Feed level (dBf): <input id="stConfInFeedLevel" type="text" size="4">
																</div></td>
															</tr>
														</table>
													</div>
													<span style='float: center;'><button onclick="stConfInSave(event)">Save</button></span>
												</div>
											</td>
										</tr>
									</table>
								</div>
								<div id="stConfLib" class="accpanel" style="text-align: left;">
									<div class="boxed">
										<button onclick="stConfDbCopy(event)">Copy & Apply from Library Settings</button><br>
										<table class="tableleftj">
											<tr><td>Type</td><td><div id="stConfDbType"></div></td></tr>
											<tr><td>Name</td><td><div id="stConfDbName"></div></td></tr>
											<tr><td>Host</td><td><input id="stConfDbHost" type="text" size="20" value=""><button onclick="stConfDbApplyHost(event)">Apply Host</button></td></tr>
											<tr><td>Port (Blank for default)</td><td><input id="stConfDbPort" type="text" size="5" value=""><button onclick="stConfDbApplyPort(event)">Apply Port</button></td></tr>
										</table>
									</div>
									<div class="boxed">
										<label for="stConfDbLoc">Library Location:</label>
										<select id="stConfDbLoc" onchange="stConfDbLocChange(event)"></select><br>
										<input id="stConfDbMark" type="checkbox" onchange="stConfDbMarkChange(event)"> Mark Missing -- only one studio should have this set
									</div>
								</div>
								<div id="stConfMixer" class="accpanel" style="text-align: left;">
									<table class="tableleftj">
										<tr>
											<td>Defaults:
												<div id="stConfMixDefBus" class="boxed" onchange="stConfMixDefBusChange(event)">
												
												</div>
												Recording Directory (defaults to home directory when empty):<br><input id="stConfMixRecDir" type="text" size="20"><button onclick="stConfMixRecDirChange(event)">Apply</button>
											</td>
											<td>Silence Detection:
												<div id="stConfMixSilenceBus" class="boxed" onchange="stConfMixSilenceBusChange(event)">

												</div>
												Silence level (dBf, must be negative): <input id="stConfMixSilenceLevel" type="text" size="4"><button onclick="stConfMixSilenceLevelChange(event)">Apply</button>
												<br><br>
												Silence time-out (sec.): <input id="stConfMixSilenceTO" type="number" min="0" max="600" value="30" onchange="stConfMixSilenceTOChange(event)"> Zero to disable.<br>
												When enabled, silence detection will first trigger a segue, then if silence persists, an arServer shutdown. 
												<br>When run normally, arSerever restarts when shutdown. Alternate silence behavior can be scripted by adding scripts to the triggers directory.
											</td>
										</tr>
									</table>
								</div>
								<div id="stConfAuto" class="accpanel" style="text-align: left;">
									<table class="tableleftj">
										<tr>
											<td>Automation state at Startup:
												<div id="stConfAutoStart" class="boxed">
												<ul style="list-style-type:none; text-align: left; padding-left: 0px;" onchange="stConfAutoChange(event)">
													<li><input id="stConfAutoStart1" type="radio" name="auto" value="1" checked="checked">On</li>
													<li><input id="stConfAutoStart0" type="radio" name="auto" value="0">Off</li>
												</ul>
												</div>
											</td>
											<td>Live Mode Queue Behavior:
												<div class="boxed">
													<ul id="stConfAutoLive" style="list-style-type:none; text-align: left; padding-left: 0px;" onchange="stConfLiveChange(event)">
														<li><input id="stConfAutoLive1" type="checkbox" name="1">Filling</li>
														<li><input id="stConfAutoLive2" type="checkbox" name="2">Inserting (Priority < 8)</li>
														<li><input id="stConfAutoLive8" type="checkbox" name="8">Target Time Reordering</li>
														<li><input id="stConfAutoLive4" type="checkbox" name="4">Allow stopping</li>
													</ul>
													Live mode inactivity time-out (minutes): <input id="stConfAutoLiveTO" type="number" min="1" max="120" value="30" onchange="stConfLiveTOChange(event)">
												</div>
											</td>
											<td>Queue:
												<div id="stConfAutoFill" class="boxed">
													Number of Fill items to keep in Queue: <input id="stConfAutoFillCount" type="number" min="1" max="20" value="8" onchange="stConfAutoFillCountChange(event)">
													<br><br>
													Default Segue time (seconds before end): <input id="stConfAutoSegTime" type="number" min="0" max="20" value="6" onchange="stConfAutoSegTimeChange(event)">
													<br><br>
													Level based Segue hold-off (dBf, must be negative): <input id="stConfAutoSegLevel" type="text" size="4"><button onclick="stConfAutoSegLevelChange(event)">Apply</button>
													<br>Leave blank to disable.
												</div>
											</td>
										</tr>
									</table>
								</div>
								<div id="stConfSIP" class="accpanel" style="text-align: left;">
									<table class="tableleftj">
										<tr>
											<td>
												BareSip Control Port Number (zero to disable): <input id="stConfSIPCtl" type="text" size="5"><button onclick="stConfSIPCtlChange(event)">Apply</button> <span id="stConfSIPstat"></span> 
												<div id="stConfSIPBus" class="boxed" onchange="stConfSIPBusChange(event)">
												</div>
												Default Volume (dB): <input id="stConfSIPVol" type="text" size="5"><button onclick="stConfSIPVolChange(event)">Apply</button>
											</td>
										</tr>
										<tr>
											<td>
												<div id="stConfSIPFeed" class="boxed" >
													<table class="tableleftj">
														<tr>
															<td><div id="stConfSIPFeedBus" onchange="stConfSIPFeedChange(event)"></div></td>
															<td><div id="stConfSIPFeedTB" onchange="stConfSIPFeedChange(event)"></div></td>
															<td><div>
																Feed level (dBf): <input id="stConfSIPFeedVol" type="text" size="4"><button onclick="stConfSIPFeedVolChange(event)">Apply</button>
															</div></td>
														</tr>
													</table>
												</div>
											</td>
										</tr>
									</table>
								</div>
								<div id="stConfRoute" class="accpanel" style="text-align: left; height: 305px;">
									<div id="stConfJackConns">
									</div>
								</div>
							</div>`;
	}
	response.status(200);
	response.send(html);
	response.end();
});

app.get('/nav', async function(request, response){
	let html; 
	if(!request.session.loggedin){
		html = `<button id="navlogin" class="tabitem" onclick="showTab(event, 'login')">Login</button>`;
	}else{
		html = "<h3><center>"+request.session.username+"</center></h3>"
		html += `<button class="tabitem" onclick="logOut()">Logout</button>
					<button class="tabitem" onclick="showTab(event, 'stash')">Stash</button>
					<button class="tabitem" onclick="showTab(event, 'files')">Files</button>`;
		request.session.username
/* permission string values:
	'admin': Administrator (All)
	'manager': Manager (Studio, Library, Traffic)
	'production': Production (Library, Traffic)
	'programming': Programming (Studio, Library)
	'traffic': Traffic only
	'library': Library only
	'studio': Studio and library logs and browse only   */
		if(['admin', 'manager', 'production', 'programming', 'library'].includes(request.session.permission)){
			// library access
			html += `<button class="tabitem" onclick="dropClick(event)" data-childdiv="libdiv">Library
							<i class="fa fa-caret-down"></i>
						</button>
						<div id="libdiv" class="dropdown-container">
						<label for="selloc">Library Location:</label>
						<select id='selloc' onchange="locMenuChange(event)">
							<option val="" onClick="getLocList()">Reload List</option>
						</select>
						<button class="tabitem" onclick="showTab(event, 'browse')">Browse</button>
						<button class="tabitem" onclick="showTab(event, 'schedule')">Schedule</button>
						<button class="tabitem" onclick="showTab(event, 'logs')">Logs</button>`;
			if(['admin', 'manager', 'library'].includes(request.session.permission)){
				html +=	`<button class="tabitem" onclick="showTab(event, 'query')">Queries</button>`;
			}
			html +=	`<button class="tabitem" onclick="showTab(event, 'libmanage')">Manage</button>
						</div>`;
		}
		if(request.session.permission === 'studio'){
			// browse and log library access 
			html += `<button class="tabitem" onclick="dropClick(event)" data-childdiv="libdiv">Library
							<i class="fa fa-caret-down"></i>
						</button>
						<div id="libdiv" class="dropdown-container">
							<label for="selloc">Library Location:</label>
							<select id='selloc' onchange="locMenuChange(event)">
								<option val="" onClick="getLocList()">Reload List</option>
							</select>
							<button class="tabitem" onclick="showTab(event, 'browse')">Browse</button>
							<button class="tabitem" onclick="showTab(event, 'logs')">Logs</button>
						</div>`;
		}
		
		if(['admin', 'manager', 'programming', 'studio'].includes(request.session.permission)){
			// studio access
			html += `<button class="tabitem" onclick="dropClick(event)" data-childdiv="studiodiv">Studio
							<i class="fa fa-caret-down"></i>
						</button>
						<div id="studiodiv" class="dropdown-container">`;
			let studios = config.studios;
			if(studios){
				let keys = Object.keys(studios);
				for(let i = 0; i < keys.length; i++){
					html += `<button class="tabitem" onclick="showTab(event, 'studio', '`+keys[i]+`')">`+keys[i]+`</button>`;
				}
			}
			html += `</div>`;
			// live remote 
			html += `<button class="tabitem" onclick="dropClick(event)" data-showcb="loadRemoteTab">Live Remote
					</button>
					<div id="remotediv"></div>`;
		}
		
		if(['admin', 'manager', 'production', 'traffic'].includes(request.session.permission)){
			// traffic access
			html += `<button class="tabitem" onclick="dropClick(event)" data-childdiv="trafdiv">Traffic
							<i class="fa fa-caret-down"></i>
						</button>
						<div id="trafdiv" class="dropdown-container">
							<button class="tabitem" onclick="showTab(event, 'cust')">Customers</button>
							<button class="tabitem" onclick="showTab(event, 'campaign')">Campaign</button>
							<button class="tabitem" onclick="showTab(event, 'invoice')">Orders</button>
							<button class="tabitem" onclick="showTab(event, 'trafsched')">Schedule</button>
							<button class="tabitem" onclick="showTab(event, 'trafdp')">Dayparts</button>
						</div>`;
		}
		if(request.session.permission === 'admin'){
			// configure access
			html += `<button id="navconf" class="tabitem" onclick="showTab(event, 'configure')">Configure</button>`;
		}
	}
	html += `<button id="navabout" class="tabitem" onclick="showTab(event, 'about')">About</button>`;
	response.status(200);
	response.send(html);
	response.end();
});

app.get('/tmplist', listTmpDirFilesFunc);
app.get('/tmplist\*', listTmpDirFilesFunc);

app.post('/tmpupload', function(req, res){
	if(!req.session.loggedin){
		res.status(401);
		res.end();
		return;
	}
	if(storage){
		var uploader = multer({storage: storage}).any('filestoupload');
		uploader(req,res,function(err){
			if(err){
				res.status(400);
				res.end("Failed");
				return;
			}
			// check for .zip file expantion
			for(let i = 0; i< req.files.length; i++){
				if(req.files[i].filename && (req.files[i].filename.lastIndexOf("zip") == (req.files[i].filename.length-3))){
					// we have a zip file... expand it.
					let path = req.files[i].path;
					let dest = req.files[i].destination;
					try{
						extract(path, {dir: dest}).then(result => {
							// remove the .zip file
							fs.unlink(path, function (err){
								if(err)
									console.log("Failed to remove temp .zip file after unzipping: " + path + "err="+ err);
							});
						});
					}catch(err){
						// handle any errors
					}
				}
			}
			res.status(200);
			res.json(req.files); // [].filename contains the name of the file placed in the temp directory
			res.end();
		});
	}else{
		res.status(500);
		res.end();
	}
});

// open an sse stream for event messages - login session SID ensures single stream/client
app.get('/ssestream', function(req, res){
	if(req.sessionID){
		let client = sse.clients[req.sessionID];
		if(client){
			if(client.response){
				res.status(400);
				res.end("Already connected");
			}else{
				// reconnecting
				client.response = res;
				req.on("close", function () {
					delete client.response;
				});
				// start the stream
				res.writeHead(200, {
					'Content-Type': 'text/event-stream',
					'Cache-Control': 'no-cache',
					'Connection': 'keep-alive'
				});
				res.write('\n');
			}
		}else{
			// new entry
			client = {response: res, registered: []};
			sse.clients[req.sessionID] = client;
			req.on("close", function () {
				delete client.response;
			});
			res.writeHead(200, {
				'Content-Type': 'text/event-stream',
				'Cache-Control': 'no-cache',
				'Connection': 'keep-alive'
			});
			res.write('\n');
		}
	}else{
		res.status(401);
		res.end("Not autherized");
		return;
	}
});

// list an event types your session is subscribed to
app.get('/sseget', function(req, res){
	if(req.session.id){
		let client = sse.clients[req.sessionID];
		if(client && client.response){
			res.status(200);
			res.json(client.registered);
		}else{
			res.status(400);
			res.end("No stream connected");
		}
	}else{
		res.status(401);
		res.end("Not autherized");
	}
});

// add an event type to receive via above sse stream
app.get('/sseadd/\*', function(req, res){
	if(req.session.id){
		let client = sse.clients[req.sessionID];
		if(client && client.response){
			let dirs = req.path.split('/');
			if(dirs[2].length){
				let i = client.registered.indexOf(dirs[2]);
				if(i > -1){
					res.status(200);
					res.end("Already registered");
				}else{
					client.registered.push(dirs[2]);
					res.status(200);
					res.end("Added");
				}
			}else{
				res.status(400);
				res.end("Bad request");
			}
		}else{
			res.status(404);
			res.end("No stream found");
		}
	}else{
		res.status(401);
		res.end("Not autherized");
	}
});

// remove an event type to receive via above sse stream
app.get('/sserem/\*', function(req, res){
	if(req.sessionID){
		let client = sse.clients[req.sessionID];
		if(client && client.response){
			let dirs = req.path.split('/');
			if(dirs[2].length){
				let i = client.registered.indexOf(dirs[2]);
				if(i > -1){
					client.registered.splice(i,1);
					res.status(200);
					res.end("Removed");
				}else{
					res.status(400);
					res.end("Bad request");
				}
			}else{
				res.status(400);
				res.end("Bad request");
			}
		}else{
			res.status(400);
			res.end("No stream connected");
		}
	}else{
		res.status(401);
		res.end("Not autherized");
		return;
	}
});

// the following loads the main ui application in a browser when the browser URL is an empty path
app.get('/', function(request, response){
	response.redirect('/index.html');
	response.end();
});

app.get('/remote.html', function(request, response){
	if(request.session.loggedin){
		if((request.session.permission == "admin") || (request.session.permission == "manager") || (request.session.permission == "studio") || (request.session.permission += "programming")){
			response.set('Origin-Agent-Cluster', '?1');
			response.sendFile(__dirname + '/client/remote.html');
			return;
		}
	}
	response.status(401);
	response.end("Not autherized");
});

// for loading control surface mapping js modules
app.use('/control/', express.static(__dirname + '/client/control'));
app.use('/control', listControlDirFilesFunc); // shows the file list
// for loading invoice tamplate pages
app.use('/inv-template/', express.static(__dirname + '/client/inv-template'));
app.use('/inv-template', listInvTemplateDirFilesFunc); // shows the file list

// everything else assumed to be requests from the client directory
app.use('/', express.static(__dirname + '/client'));

// periodically check for removing sseClient when logged out
sse.startSessionClearing(sessionStore, 60000);


