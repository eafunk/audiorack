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
var crypto = require('crypto');
var express = require('express');
var session = require('express-session');
var bodyParser = require('body-parser');
var fs = require('fs');
var path = require('path');
const os = require('os');
const _= require('lodash');
const multer = require('multer');
var slug = require('slug');
const extract = require('extract-zip')
var lib = require('./library');

var config = false;
var tmpDirIntervalObj = false;
var storage = false;

/********** Search For Object Functions **********/

function fineObjectWithValue(key, value, anArray){
	for(let i=0; i < anArray.length; i++){
		let obj = anArray[i];
		if(obj[key] === value)
			return obj;
	}
}

/********** Authentication Functions **********/

function generatePwdHash(password){
	let salt = crypto.randomBytes(64).toString('base64');
	
	// SHA512 the salt + password string
	let hash = crypto.createHmac('sha512', salt);
	hash.update(password);
	
	return {salt: salt, hash: hash.digest('base64')};
}

//console.log(generatePwdHash("configure"));

function checkPwdHash(salt, clearpw, hashedpw){
	let hash = crypto.createHmac('sha512', salt);
	hash.update(clearpw);
	let hashstr = String(hash.digest('base64'));
	return hashedpw == hashstr;
}

/********** Configuration Functions **********/

/* config file jason structure:
{	
	"prefixes": ["prefixpattern1", "prefixpattern2", "prefixpattern3"],	// optional to override the default prefix patterns for the OS
 	"tmpMediaDir": "/some/tmp/dir/",
	"tmpMediaAgeLimitHrs": 12.0,
	"mediaDir": "/default/media/dir/,
	"supportDir": "/path/to/audiorack/support/dir"  // optional to override the default /opt/audiorack/support location.
	"users": [ 
		{	"name": "admin",
			"salt": "abc123", 
			"password": "configure", 
			"permission": "admin" 		// user, manage, admin
		},{
				additional users
		}
	],
	"library": {
		"type": "mysql",
		"host": "localhost",
		"user": "username",
		"port": port-number,			// this is optional
		"password": "password",
		"database": "databasename",
		"prefix": "ar_"
	},
	"studios": [
		{	"name": "Studio A",
			"host": "localhost",
			"port": 9550,
			"dbloc": 1,
			"run": "/opt/audiorack/bin/arServer4 -k"
		},{
				additional studio locations
		}
	]
}
*/

function listTmpDirFilesFunc(request, response){
	if(request.session.loggedin == true){
		let tmpDir = config['tmpMediaDir'];
		if(tmpDir && tmpDir.length){
			let tail ="";
			let tailIdx =request.path.search("/tmplist/");
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
						obj.name = file;
						obj.isDir = finfo.isDirectory();
						obj.size = finfo.size;
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
		response.status(403);
		response.end();
	}
}

function clearTmpDirAgedFilesFunc(){
	let tmpDir = config['tmpMediaDir'];
	let tmpAge = config['tmpMediaAgeLimitHrs'];
	if(tmpAge && tmpDir && tmpDir.length){
		fs.readdir(tmpDir, function(err, files){
			if(err){
				console.log("Error getting temporary media directory listing from " + tmpDir);
			}else{
				files.forEach(function(file){
					let thisFile = path.join(tmpDir, file);
					fs.stat(thisFile, function(err, stats){
						let diffHrs = (Date.now() - stats.ctime.getTime()) / (1000 * 60 * 60); // gives hour difference 
						if(diffHrs > tmpAge){
							if(stats.isFile()){
								// delete this file, it's too old
								fs.unlink(thisFile, function (err){
									if(err)
										console.log("aged tempMediaDir file remove failed: " + file + "err="+ err);
									else
										console.log("aged tempMediaDir file removed: " + file);
								});
							}else if(stats.isDirectory()){
								// recursivly remove directory and contents regardless of contents age
								fs.rmdir(thisFile, {recursive: true}, function (err){ 
									if(err)
										console.log("aged tempMediaDir directory remove failed: " + file + "err="+ err);
									else
										console.log("aged tempMediaDir directory removed: " + file);
								});
							}
						}
					});
				});
			}
		})
	}
}

function cleanFileNameForUpload(file){
	// we want to keep '.', so be break the path into pieces, clean each piece and reassemble
	let dots = file.split(".");
	for(let i=0; i<dots.length; i++)
		dots[i] = slug(dots[i]);
	return dots.join(".");
}

function handleConfigChanges(conf){
	lib.configure(conf);
	let tmpDir = conf['tmpMediaDir'];
	let tmpAge = conf['tmpMediaAgeLimitHrs'];
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
}

function loadConfiguration(){
	fs.readFile(path.join(os.homedir(), '.audiorack', 'arui_config.json'), function(err, data){ 
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
			console.log('No configuration found. Creating minimal configuarion file.');
			let hash = generatePwdHash("configure");
			let str = "{ \"users\": [ {\"name\": \"admin\", \"salt\": \"";
			str += hash.salt;
			str += "\", \"password\": \""
			str += hash.hash;
			str += "\", \"permission\": \"admin\" } ] }";
			saveConfiguration(JSON.parse(str));
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
			handleConfigChanges(conf);
			config = conf;
		}
	});
}

function getConf(request, response){
	if(request.session.loggedin == true){
		let loc = _.cloneDeep(config);	// copy object
		loc['username'] = request.session.username;	// this and the next property are local to the 
		loc['permission'] = request.session.permission;		// web client only
		if(request.session.permission == "admin"){
			// send full config data
			response.status(200);
			response.send(loc);
		}else{
			// remove private items for non-admin users
			let sub = loc['library'];
			if(sub){
				delete sub['password'];
				delete sub['user'];
				delete sub['host'];
			}
			sub = loc['studios'];
			if(sub){
				for(let i=0; i<sub.length; i++) {
					let obj = sub[i];
					if(obj){
						delete obj['host'];
						delete obj['port'];
					}
				}
			}
			delete loc['users'];
			response.status(200);
			response.send(loc);
		}
	}else{
		response.status(403);
	}
	response.end();
}

/********** Main Program and event callback dispatchers **********/

// Read config file 
loadConfiguration();

var app = express();

app.use(session({
	secret: crypto.randomBytes(64).toString('base64'),
	resave: true,
	saveUninitialized: true
}));

app.use(bodyParser.urlencoded({extended : true}));
app.use(bodyParser.json());

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

app.get('/getconf', getConf);

app.post('/getconf', getConf);

app.post('/setconf', function(request, response){
	if((request.session.loggedin == true) && (request.session.permission == 'admin')){
		let locConf = request.body.conf;
		if(locConf){
			delete locConf['username'];
			delete locConf['permission'];
			if(saveConfiguration(locConf))
				response.status(201);
			else
				response.status(304);
		}else{
			response.status(400);
		}
	}else{
		response.status(403);
	}
	response.end();
});

app.post('/genpass', function(request, response){
	if((request.session.loggedin == true) && (request.session.permission == 'admin')){
		let password = request.body.password;
		if(password){
			let tmp = generatePwdHash(password);
			response.send(tmp);
			response.status(201);
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
			response.status(201);
		}else{
			response.status(400);
		}
	}else{
		response.status(403);
	}
	response.end();
});
	
app.post('/auth', function(request, response){
	let username = request.body.username;
	let password = request.body.password;
	if(username && password){
		let rec = fineObjectWithValue('name', username, config['users']);
		if(rec){
			if(checkPwdHash(String(rec.salt), password, String(rec.password))){
				request.session.loggedin = true;
				request.session.username = username;
				request.session.permission = rec.permission;
				getConf(request, response);
			}else{
				response.status(401);
			}
		}else{
			response.status(401);
		}
	}else{
		response.status(400);
	}
	response.end();
});

app.post('/unauth', function(request, response){
	if(request.session.loggedin){
		request.session.destroy();
		response.status(201);
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

app.get('/who', function(request, response){
	//request.query.property
	if(request.session.loggedin){
		response.send('Welcome back, ' + request.session.username + '.');
	}else{
		response.send('You are not logged in.');
	}
	response.status(201);
	response.end();
});

app.get('/tmplist', listTmpDirFilesFunc);
app.get('/tmplist\*', listTmpDirFilesFunc);

app.post('/tmpupload', function(req, res){
	if((req.session.permission != "admin") &&  (req.session.permission != "manage")){
		res.status(401);
		res.end();
		return;
	}
	if(storage){
		var uploader = multer({storage: storage}).array('filestoupload', 25);
		uploader(req,res,function(err){
			if(err){
				res.status(400);
				res.end("Failed");
			}
			// check for .zip file expantion
			for(let i = 0; i< req.files.length; i++){
				if(req.files[i].filename.lastIndexOf("zip") == (req.files[i].filename.length-3)){
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
			res.end("Complete");
		});
	}else{
		res.status(500);
		res.end();
	}
});

// the following loads the main ui application in a browser when the browser URL is an empty path
app.get('/', function(request, response){
	response.redirect('/app.html');
	response.end();
});

// everything else assumed to be requests from the client directory
app.use('/', express.static('client'));

app.listen(3000);

