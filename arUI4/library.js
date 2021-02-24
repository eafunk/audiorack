const DefLimit = 50;

var mysql = require('mysql');
const _= require('lodash');
var fs = require('fs');
var path = require('path');
const readline = require('readline');
var crypto = require('crypto');
const crc = require('node-crc');
const url = require('url');
var glob = require('glob');
const { once } = require('events');

var prefix_list = [];
var libpool = undefined;
var locConf = undefined;
var tmpDir = "";
var mediaDir = "";
var dbFingerprint = "";

function hasWhiteSpace(str) {
  return /\s/g.test(str);
}

function asyncGetDBConnection(){
	return new Promise(function (resolve){
		libpool.getConnection((err, connection) => {
			if(err){
				reject(err);
			}else{
				resolve(connection);
			}
		});
	});
}

function asyncQuery(connection, query){
	return new Promise(function (resolve, reject){
		connection.query(query, function (err, result) {
			if(err){
				reject(err);
			}else{
				resolve(result);
			}
		});
	});
}

function asyncStartTransaction(connection){
	return new Promise(function (resolve){
		connection.beginTransaction(function (err, result) {
			if(err){
				resolve(err);
			}else{
				resolve(false);
			}
		});
	});
}

function asyncCommitTransaction(connection){
	return new Promise(function (resolve){
		connection.commit(function (err, result) {
			if(err){
				resolve(err);
			}else{
				resolve(false);
			}
		});
	});
}

function asyncRollback(connection){
	return new Promise(function (resolve){
		connection.rollback(function (err, result) {
			if(err){
				resolve(err);
			}else{
				resolve(false);
			}
		});
	});
}

async function sequencialQuery(connection, queries){	// given array of query strings
	let result = {};
	for(let i = 0; i < queries.length; i++){
		if(queries[i].length){
			try{
				result = await asyncQuery(connection, queries[i]);
			}catch(err){
				// roll back, just incase a transaction was explicidly started
				await asyncRollback(connection);
				return err;
			}
		}
	}
	return result;
}

String.prototype.replaceAll = function(search, replacement){
    var target = this;
    return target.split(search).join(replacement);
};

function replaceRangeWithString(original, start, end, replace){
	if(start > 0)
		return original.substring(0, start) + replace + original.substring(end+1);
	else
		return replace + original.substring(end);
}

function replaceQueryMacros(query, params, clearlf){
	let selIdx = 0;
	let prtIdx = 0;
	
	if(clearlf)
		query = query.replaceAll("\n", "");
	query = query.replaceAll("[prefix]", locConf['prefix']);
	query = query.replaceAll("[!prefix]", "[prefix]");

	if(params && params.prompt){
		if(Array.isArray(params.prompt) == false)
			params.prompt = [params.prompt];
		while(prtIdx < params.prompt.length){
			let start = query.indexOf("[prompt(");
			if(start > -1){
				let subs = query.substring(start);
				let end = subs.indexOf("]");
				if(end > -1){
					end = start + end;
					query = replaceRangeWithString(query, start, end, params.prompt[prtIdx]);
					prtIdx++;
				}
			}else
				break;
		}
	}
	if(params && params.select){
		if(Array.isArray(params.select) == false)
				params.select = [params.select];
		while(selIdx < params.select.length){
			let start = query.indexOf("[select(");
			if(start > -1){
				let subs = query.substring(start);
				let end = subs.indexOf("]");
				if(end > -1){
					end = start + end;
					query = replaceRangeWithString(query, start, end, params.select[selIdx]);
					selIdx++;
				}
			}else
				break;
		}
	}
	
	if(params && params.locid)
		query = query.replaceAll("[loc-id]", params.locid);
	query = query.replace("[!loc-id]", "[loc-id]");

	if(params && params.fingerprint)
		query = query.replaceAll("[fingerprint]", params.fingerprint);
	query = query.replaceAll("[!fingerprint]", "[fingerprint]");
	return query;
}

function buildSetString(setStr, key, value){
	if(setStr.length == 0)
		setStr += "SET ";
	else
		setStr += ", ";
	setStr += key+" = "+libpool.escape(value);
	return setStr;
}

function buildInsColumnString(colStr, key){
	if(colStr.length == 0)
		colStr += "(";
	else
		colStr += ", ";
	colStr += key;
	return colStr;
}

function buildInsValString(valStr, value){
	if(valStr.length == 0)
		valStr += "VALUES (";
	else
		valStr += ", ";
	valStr += libpool.escape(value);
	return valStr;
}

function buildWhereString(whereStr, table, key, value){
	if(whereStr.length == 0)
		whereStr += "WHERE ";
	else
		whereStr += "AND ";
	if(value.indexOf("%") == -1)
		whereStr += locConf['prefix']+table+"."+key+"="+libpool.escape(value)+" ";
	else
		whereStr += locConf['prefix']+table+"."+key+" LIKE "+libpool.escape(value)+" ";
	return whereStr;
}

function buildWhereDate(whereStr, table, key, dateStr){
	if(whereStr.length == 0)
		whereStr += "WHERE ";
	else
		whereStr += "AND ";
	whereStr += "DATE(FROM_UNIXTIME("+locConf['prefix']+table+"."+key+"))="+libpool.escape(dateStr)+" ";
	return whereStr;
}

function buildFromString(fromStr, table){
	table = locConf['prefix']+table;
	if(fromStr.indexOf(table) == -1){
		if(fromStr.length == 0)
			fromStr += "FROM (";
		else
			fromStr += ", ";
		fromStr += table;
	}
	return fromStr;
}

function includeJoin(whereStr, includeStr){
	if(whereStr.indexOf(includeStr) == -1){
		if(whereStr.length == 0)
			whereStr += "WHERE ";
		else
			whereStr += "AND ";
		whereStr += includeStr;
	}
	return whereStr;
}

function includeSearchKeys(queryParts, params){
	// additional "where" values: toc table is assumed to already be in the from clause.
	let keys = Object.keys(params);
	let vals = Object.values(params);
	for(let i=0; i < keys.length; i++){
		let key = keys[i];
		if(hasWhiteSpace(key))
			// keys can't have white spaced... skip if it does
			continue;
		//keys:track,playlist,task,added,category,artist,album,comment,missing,rested
		if(key == "title"){ // files by item name
			queryParts.where = buildWhereString(queryParts.where, "toc", "Type", "file");
			let val = vals[i];
			let valstr = val;
			if(Array.isArray(val)){
				for(let j=0; j < val.length; j++){
					valstr = val[j];
					queryParts.where = buildWhereString(queryParts.where, "toc", "Name", valstr);
				}
			}else{
				queryParts.where = buildWhereString(queryParts.where, "toc", "Name", valstr);
			}
		}else if(key == "artist"){
			queryParts.from = buildFromString(queryParts.from, "artist");
			queryParts.from = buildFromString(queryParts.from, "file");
			queryParts.where = includeJoin(queryParts.where, locConf['prefix']+"file.ID = "+locConf['prefix']+"toc.ID ");
			queryParts.where = includeJoin(queryParts.where, locConf['prefix']+"artist.ID = "+locConf['prefix']+"file.Artist ");
			let val = vals[i];
			let valstr = val;
			if(Array.isArray(val)){
				for(let j=0; j < val.length; j++){
					valstr = val[j];
					queryParts.where = buildWhereString(queryParts.where, "artist", "Name", valstr);
				}
			}else{
				queryParts.where = buildWhereString(queryParts.where, "artist", "Name", valstr);
			}
		}else if(key == "album"){
			queryParts.from = buildFromString(queryParts.from, "album");
			queryParts.from = buildFromString(queryParts.from, "file");
			queryParts.where = includeJoin(queryParts.where, locConf['prefix']+"file.ID = "+locConf['prefix']+"toc.ID ");
			queryParts.where = includeJoin(queryParts.where, locConf['prefix']+"album.ID = "+locConf['prefix']+"file.Album ");
			let val = vals[i];
			let valstr = val;
			if(Array.isArray(val)){
				for(let j=0; j < val.length; j++){
					valstr = val[j];
					queryParts.where = buildWhereString(queryParts.where, "album", "Name", valstr);
				}
			}else{
				queryParts.where = buildWhereString(queryParts.where, "album", "Name", valstr);
			}
		}else if(key == "playlist"){
			queryParts.where = buildWhereString(queryParts.where, "toc", "Type", "playlist");
			let val = vals[i];
			let valstr = val;
			if(Array.isArray(val)){
				for(let j=0; j < val.length; j++){
					valstr = val[j];
					queryParts.where = buildWhereString(queryParts.where, "toc", "Name", valstr);
				}
			}else{
				queryParts.where = buildWhereString(queryParts.where, "toc", "Name", valstr);
			}
		}else if(key == "task"){
			queryParts.where = buildWhereString(queryParts.where, "toc", "Type", "task");
			let val = vals[i];
			let valstr = val;
			if(Array.isArray(val)){
				for(let j=0; j < val.length; j++){
					valstr = val[j];
					queryParts.where = buildWhereString(queryParts.where, "toc", "Name", valstr);
				}
			}else{
				queryParts.where = buildWhereString(queryParts.where, "toc", "Name", valstr);
			}
		}else if(key == "added"){
			let val = vals[i];
			let valstr = val;
			if(Array.isArray(val)){
				for(let j=0; j < val.length; j++){
					valstr = val[j];
					queryParts.where = buildWhereDate(queryParts.where, "toc", "Added", valstr);
				}
			}else{
				queryParts.where = buildWhereDate(queryParts.where, "toc", "Added", valstr);
			}
			
		}else if(key == "category"){
			// assumes an item is only in a particular category once
			let val = vals[i];
			let valstr = val;
			if(Array.isArray(val)){
				// multi-category
				let subwhere = "";
				for(let j=0; j < val.length; j++){
					valstr = val[j];
					if(subwhere.length)
						subwhere += " OR ";
					subwhere += locConf['prefix']+"category.Name";
					if(valstr.indexOf("%") == -1)
						subwhere += "="+libpool.escape(valstr);
					else
						subwhere += " LIKE "+libpool.escape(valstr);
				}
				let subselect = locConf['prefix']+"toc.ID IN (SELECT "+locConf['prefix']+"toc.ID AS ID ";
				subselect += "FROM ("+locConf['prefix']+"toc, "+locConf['prefix']+"category, "+locConf['prefix']+"category_item) ";
				subselect += "WHERE "+locConf['prefix']+"category_item.Item = "+locConf['prefix']+"toc.ID ";
				subselect += "AND "+locConf['prefix']+"category.ID = "+locConf['prefix']+"category_item.Category AND ("; 
				subselect += subwhere+") ";
				subselect += "GROUP BY "+locConf['prefix']+"toc.ID HAVING COUNT("+locConf['prefix']+"toc.ID) = "+val.length+") ";
				if(queryParts.where.length == 0)
					queryParts.where += "WHERE ";
				else
					queryParts.where += "AND ";
				queryParts.where += subselect;
			}else{
				// single category
				queryParts.from = buildFromString(queryParts.from, "category");
				queryParts.from = buildFromString(queryParts.from, "category_item");
				queryParts.where = buildWhereString(queryParts.where, "category", "Name", valstr);
				queryParts.where = includeJoin(queryParts.where, locConf['prefix']+"category_item.Item = "+locConf['prefix']+"toc.ID ");
				queryParts.where = includeJoin(queryParts.where, locConf['prefix']+"category.ID = "+locConf['prefix']+"category_item.Category ");
			}
		}else if(key == "comment"){
			let val = vals[i];
			let valstr = val;
			if(Array.isArray(val)){
				for(let j=0; j < val.length; j++){
					valstr = val[j];
					queryParts.where = buildWhereString(queryParts.where, "toc", "Tag", valstr);
				}
			}else{
				queryParts.where = buildWhereString(queryParts.where, "toc", "Tag", valstr);
			}
		}else if(key == "missing"){
			queryParts.from = buildFromString(queryParts.from, "file");
			queryParts.where = includeJoin(queryParts.where, locConf['prefix']+"file.ID = "+locConf['prefix']+"toc.ID ");
			let val = vals[i];
			let valstr = val;
			if(Array.isArray(val)){
				for(let j=0; j < val.length; j++){
					valstr = val[j];
					queryParts.where = buildWhereString(queryParts.where, "file", "Missing", valstr);
				}
			}else{
				queryParts.where = buildWhereString(queryParts.where, "file", "Missing", valstr);
			}
		}else if(key == "rested"){
			queryParts.from = buildFromString(queryParts.from, "rest");
			queryParts.from = buildFromString(queryParts.from, "locations");
			queryParts.where = includeJoin(queryParts.where, locConf['prefix']+"toc.ID = "+locConf['prefix']+"rest.Item ");
			queryParts.where = includeJoin(queryParts.where, locConf['prefix']+"locations.ID = "+locConf['prefix']+"rest.Location ");
			let val = vals[i];
			let valstr = val;
			if(Array.isArray(val)){
				for(let j=0; j < val.length; j++){
					valstr = val[j];
					queryParts.where = buildWhereString(queryParts.where, "locations", "Name", valstr);
				}
			}else{
				queryParts.where = buildWhereString(queryParts.where, "locations", "Name", valstr);
			}
		}else
			// unknow key... ignore
			continue;
	}
}

function restQueryRequest(connection, request, response, select, from, where, tail, sort, noLimit){
	// handle Order By request
	if(sort){
		let parts = sort.split(',');
		if(parts.length){
			for(let i=0; i < parts.length; i++){
				if(i == 0)
					tail += "ORDER BY ";
				else
					tail += ", ";
				let str = parts[i];
				let j = str.search("-");
				if(j > -1){
					str = str.substring(j+1);
					str += " DESC";
				}
				tail += str;
			}
			tail += " ";
		}
	}

	// handle range request
	let offset = 0;
	let cnt = DefLimit;
	if(noLimit==0){	// only set a limit/offset if noLimit is 0
		if(request.headers.Range){
			let parts = request.headers.Range.path.split('=');
			if(parts[1]){
				fromto = parts[1].split('-');
				offset = fromto[0];
				cnt = fromto[1]-offset;
			}
		}
		tail += "LIMIT "+cnt+" OFFSET "+offset;
	}
	
	let query = select+from+where+tail;
	
	connection.query(select+from+where+tail,function (err, results, fields) {
		if(err){
			response.status(400);
			response.send(err.code);
			connection.release(); // return the connection to pool
			response.end();
		}else{
			let cquery = "SELECT COUNT(*) As Num FROM ("+select+from+where+") AS Subqry";
			connection.query(cquery,function (cerr, cres, cfields) {
				if(cerr){
					response.status(400);
					response.send(cerr.code);
				}else{
					let last = offset+results.length-1;
					if(last < 0)
						last = 0;
					let header = "items "+offset+"-"+last+"/"+cres[0].Num;
					response.setHeader('Content-Range', header);
					response.status(201);
					response.json(results);
				}
				connection.release(); // return the connection to pool
				response.end();
			});
		}
	});
}

function restObjectRequest(response, object){
	
	let last = object.length-1;
	if(last < 0)
		last = 0;
	let header = "items 0-"+last+"/"+object.length;
	response.setHeader('Content-Range', header);
	response.status(201);
	response.json(object);
	response.end();
}

function mkRecursiveDir(path){
	return new Promise(function (resolve){
		fs.mkdir(path, {recursive: true}, (err) => {
			if(err)
				resolve("");
			resolve(path);	// this is the full path to the deepest directory, with trailing slash
		});
	});
}

function copyFileToDir(fromFile, intoDir){
	return new Promise(function (resolve){
		let dest = intoDir+path.basename(fromFile);
		fs.copyFile(fromFile, dest, (err) => { 
			if(err)
				resolve(""); 	// return empt string
			resolve(dest);		// return full new path
		}); 
	}); 
}


function deleteFile(path){
	return new Promise(function (resolve){
		fs.unlink(path, function (err){
			if(err){
				resolve(false);
			}else{
				resolve(true);
			}
		});
	});
}

function openFile(path){
	return new Promise(function (resolve){
		fs.open(path, 'r', function(err, fd){
			if(err){
				resolve(0);
			}else{
				resolve(fd);
			}
		});
	});
}

function sizeOfFile(fd){
	return new Promise(function (resolve) {
		fs.fstat(fd, function (err, stats){
			if(err){
				resolve(0);
			}else{
				resolve(stats.size);
			}
		});
	});
}

function readFileIntoBuffer(fd, pos, len, buffer, offset){
	return new Promise(function (resolve){
		fs.read(fd, buffer, offset, len, pos, function(err, bytes){
			if(err){
				resolve(false);
			}else{
				resolve(bytes);
			}
		});
	});
}

function globSearch(path_pattern){
	return new Promise(function (resolve){
		glob(path_pattern, {nosort: 1}, function (err, files){
			if(err){
				resolve([]);
			}else{
				resolve(files);
			}
		});
	});
}

function closeFileDescriptor(fd){
	fs.close(fd, function(err){
		if(err){
			console.error("closeFileDescriptor error: "+err);
		} 
	});
}

async function fileLineByLine(file, params, linecb){
	try{
		const fileStream = fs.createReadStream(file);
		let rl = readline.createInterface({
			input: fileStream,
			crlfDelay: Infinity
		});
		rl.on('line', (line) => {
			linecb(line, params);
		});
		await once(rl, 'close');
		return true;
	}catch(err){
		console.error("fileLineByLine error: "+err);
		return false;
	}
}

async function getFileHash(path){
	// MD5 portion first
	let buf = new Buffer(4096);
	let md5sum = crypto.createHash('md5');
	
	let fd = await openFile(path);
	if(fd == 0)
		return "";
	let size = await sizeOfFile(fd);
	if(size == 0){
		closeFileDescriptor(fd);
		return "";
	}
	let result = false;
	len = Math.floor(size/3);
	b1 = Math.floor(len / 4096) * 4096;
	b2 = Math.floor(len / 2048) * 4096;
	result = await readFileIntoBuffer(fd, b1, 4096, buf, 0);
	if(!result){
		closeFileDescriptor(fd);
		return "";
	}
	md5sum.update(buf);
	result = await readFileIntoBuffer(fd, b2, 4096, buf, 0);
	if(!result){
		closeFileDescriptor(fd);
		return "";
	}
	md5sum.update(buf);
	let hash = md5sum.digest('hex');
	// Next we calculate the CRC portion
	len = Math.floor(size / 4);
	b1 = Math.floor(len / 4096) * 4096;
	b2 = Math.floor(len / 2048) * 4096;
	b3 = Math.floor(3 * len / 4096) * 4096;
	len = size & 0xffffffff;
	hash += len.toString(16).padStart(8, '0');
	result = await readFileIntoBuffer(fd, b1, 4096, buf, 0);
	if(!result){
		closeFileDescriptor(fd);
		return "";
	}
	hash += crc.crc32(buf).toString('hex').padStart(8, '0');
	result = await readFileIntoBuffer(fd, b2, 4096,buf, 0);
	if(!result){
		closeFileDescriptor(fd);
		return "";
	}
	hash += crc.crc32(buf).toString('hex').padStart(8, '0');
	result = await readFileIntoBuffer(fd, b3, 4096, buf, 0);
	if(!result){
		closeFileDescriptor(fd);
		return "";
	}
	hash += crc.crc32(buf).toString('hex').padStart(8, '0');
	return hash;
}

async function CheckFileHashMatch(path, hash){
	if(hash){
		if(hash.length == 0)	// ignore hash check for items with empy hash property
			return true;
		let fHash = await getFileHash(path);
		if(fHash.toLowerCase() === hash.toLowerCase())
				return true;
		return false;
	}else
		// ignore hash check for items with NULL hash property
		return true;
}

function strPrefixSpan(string, prefix){
	/* This function accepts '*' chars in the prefix as a wildcard
	 * to match any character or charactors.  This function returns
	 * an index within string to the first character that doesn't
	 * match the prefix pattern. This result point to the start
	 * of the string if there is no match. */ 
	let rem = string;
	let i = 0;
	let end = 0;
	let tok = prefix.split("*");
	for(i = 0; i < tok.length; i++){
		let segment = tok[i];
		let idx = rem.search(segment);
		if(idx == -1) // segment not found in string
			break;
		if((end == 0) && (idx > 0)){ 
				/* First segment match MUST be at start of string
				 * otherwise there is an implied wildcard at the start
				 * of the prefix, and we can't have that. */
				break;
		}
		end += idx + segment.length;
		rem = string.substring(end);
	}
	if(i != tok.length)
		end = 0; // incomplete match..., discard matches
	return end;
}

function getFilePrefixPoint(file){
	/* see if any of the prefixes in the prefix list match the file path.
	 * if so, return the prefix that matches. On return, path points 
	 * to the remaining  path after the prefix. Returns an empty prefix with 
	 * path set to input file path for no match */
	let result = {prefix: "", path: file};
	for(let i = 0; i < prefix_list.length; i++){
		let pattern = prefix_list[i];
		let slice = strPrefixSpan(file, pattern);
		if(slice > 0){
			result.prefix = file.substring(0, slice);
			result.path = file.substring(slice);
			break;
		}
	}
	return result;
}

async function resolveFileFromProperties(properties){
	// check file related properties in db against the file itself... find file if needed.
	// use file as final authority for properties.  Start with these 2 assumptions:
	let changed = 0;
	let missing = 1;
	let listSize = prefix_list.length;
	let path = "";
	let pathStr = "";
	let adjPathStr = "";
	let plen = 0;
	if(properties.URL == undefined)
		properties.URL = "";
	if(properties.Path && properties.Path.length){
		// try various prefixes to acomidate different disk mount locations
		// using new path & prefix method
		if(properties.Prefix){
			plen = properties.Prefix.length;
			if(plen)
				path = properties.Prefix;
			else
				path = "";
		}
		pathStr = properties.Path;
		path += pathStr;
	}else if(properties.URL && properties.Mount){
		// use old mount & URL method of searching for files. if sucessful,
		// set path & prefix properties to use new method next time.
		if(properties.URL.length == 0){
			// bad URL string and/or Mount string
			changed = 1; // missing flag is already set
			path = "";
		}else{
			path = url.fileURLToPath(properties.URL);
			if(properties.Mount && (properties.Mount.length > 1)){
				//  convert /a/path/with/mount -> mount
				let mountParts = properties.Mount.split("/");
				let mountName = "";
				if(mountParts.length > 1)
					mountName = mountParts[mountParts.length - 1];
				if(mountName.length){
					let i = path.search(mountName);
					if(i > 0){ 
						// given path = /longer/path/to/mount/some/file
						// and Mount = mount
						// pathStr -> mount/some/file */
						pathStr = path.substring(i);
						plen = 1;
					}else
						pathStr = "";
				}else
					pathStr = "";
			}else
				pathStr = "";
		}
	}

	if(pathStr.length){
		// escaping all *,?,[ chars found in pathStr to prevent paths
		// with these chars from being interpereted by the glob function
		// below as wild-card matches.*/
		pathStr = pathStr.replace('*', '\\*');
		pathStr = pathStr.replace('?', '\\?');
		pathStr = pathStr.replace('[', '\\[');
		
		// Copy the string and change the case of the first letter, if it's a letter
		// for an alternate comparison on systems that change the case of the mount
		if(pathStr.charAt(0) === pathStr.charAt(0).toUpperCase()){
			adjPathStr = pathStr.charAt(0).toLowerCase();
			if(pathStr.length > 1)
				adjPathStr += pathStr.substring(1);
		}else if(pathStr.charAt(0) === pathStr.charAt(0).toLowerCase()){
			adjPathStr = pathStr.charAt(0).toUpperCase();
			if(pathStr.length > 1)
				adjPathStr += pathStr.substring(1);
		}
	}
	// path is Full Path, pathStr is the relative path, if any
	// i.e. path = /longer/path/to/mount/some/file
	// and  pathStr = mount/some/file
	i = 0;
	p = 0;
	c = 0;
	let results = [];
	do{
		if(await CheckFileHashMatch(path, properties.Hash)){
			// Hash code agrees with database, not missing
			missing = 0;
			break;
		}else{
			changed = 1;
		}
		// try to create another path with next prefix or next glob items
		do{
			if(i < results.length){
				// next path in glob list
				path = results[i];
				i++;
				break;
			}else{
				let tmp = prefix_list[p];
				if(tmp && pathStr.length){
					path = tmp;
					if(adjPathStr.length && c){
						// trying the pathStr version with case adjustment
						path += adjPathStr;
						c = 0; // go back to original case next time
					}else{
						path += pathStr;
						if(adjPathStr.length)
							c = 1; // try case change next time
					}
					i = 0;
					results = await globSearch(path);
					if(results.length){
						// found a path in new glob list
						path = results[0];
						i++;
						if(!c)
							p++;
						break;
					}
					if(!c)
						p++;
				}else{
					// no more prefixes
					p = listSize+1;
				}
			}
		}while(p < listSize);
		
	}while(missing && plen && (p <= listSize));
	
	if(missing){
		// can't find it
		// set missing metadata flag
		properties.Missing = 1;
		stat = -2;
	}else{
		// found it... set URL if not set or if changed
		if(changed || ((properties.URL.length) == 0)){
			// re-encode URL
			properties.URL = url.pathToFileURL(path).href;
		}

		let pre = getFilePrefixPoint(path);
		properties.Prefix = pre.prefix;
		properties.Path = pre.path;
		// Old OSX: Mount -> Prefix + mountName (first dir in path) 
		// if path=/some/path, mountName = /
		// if path=some/path, mountName = some/
		let idx = pre.path.indexOf("/");
		let Mount = "";
		if(idx > -1)
			Mount = pre.prefix + pre.path.substring(0, idx+1);
		if(properties.Missing)
			// was flag as missing, but it's not really missing... nothing else is different
			changed = 1;
		
		// unset missing metadata flag
		properties.Missing = 0;
		stat = 0;
	}
	return properties;
}

// getFrom -> get/table{/ID}?key1=, key2, ...
// distinct=column or columns to return with distinct clause
// table=toc
// id=2345 (all if excluded)
// property=value (% in string for like)
function getFrom(request, response, params, dirs){
	if(dirs[3]){
		// table must NOT contain any white spaces, to prevent evil parameters being passed
		let table = dirs[3];
		if(hasWhiteSpace(table)){
			response.status(400);
			response.end();
			return;
		}
		libpool.getConnection((err, connection) => {
			if(err){
				response.status(400);
				response.send(err.code);
				response.end();
			}else{
				let sort = params.sortBy;
				if(params.sortBy)
					delete params.sortBy;
				else
					sort = "ID";
				
				let select = "";
				let distinct = params.distinct;
				if(params.distinct){
					let dstr = params.distinct;
					if(Array.isArray(params.distinct)){
						for(let j=0; j < params.distinct.length; j++){
							dstr = params.distinct[j];
							if(hasWhiteSpace(dstr))
								// columns can't have white spaced... skip if it does
								continue;
							if(select.length == 0)
								select = "SELECT DISTINCT "+dstr;
							else
								select += ", "+valstr;
						}
						select += " ";
					}else{
						if(hasWhiteSpace(dstr))
							// columns can't have white spaced... skip if it does
							select = "SELECT * ";
						else
							select = "SELECT DISTINCT "+dstr+" ";
					}
					delete params.distinct;
					
				}else{
					select = "SELECT * ";
				}
				
				let from = "FROM "+locConf['prefix']+table+" ";
				let where = "";
				let tail = "";

				if(dirs[4])
					where += "WHERE ID="+libpool.escape(dirs[4])+" ";

				// additional "where" values
				let keys = Object.keys(params);
				let vals = Object.values(params);
				for(let i=0; i < keys.length; i++){
					let key = keys[i];
					if(hasWhiteSpace(key))
						// keys can't have white spaced... skip if it does
						continue;
					let val = vals[i];
					let valstr = val;
					if(Array.isArray(val)){
						for(let j=0; j < val.length; j++){
							valstr = val[j];
							where = buildWhereString(where, table, key, valstr);
						}
					}else{
						where = buildWhereString(where, table, key, valstr);
					}
				}
				restQueryRequest(connection, request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.
			}
		});
	}else{
		response.status(400);
		response.end();
	}
}

// setIn -> set/table{/ID}?col1=, col2= ...
// table=toc
// id=2345 (if excluded, new row is created, ID id returned)
function setIn(request, response, params, dirs){
	if(dirs[3]){
		// table must NOT contain any white spaces, to prevent evil parameters being passed
		let table = dirs[3];
		if(hasWhiteSpace(table)){
			response.status(400);
			response.end();
			return;
		}
		// check for permission
		if((request.session.permission != "admin") &&  (request.session.permission != "manage")){
			response.status(401);
			response.end();
			return;
		}
		// check for allowed tables
		if(['artist', 'album', 'category', 'toc', 'meta', 'queries', 'rest', 'category_item', 'schedule', 'playlist', 'task', 'file'].includes(table) == false){
			response.status(400);
			response.end();
			return;
		}
		libpool.getConnection((err, connection) => {
			if(err){
				response.status(400);
				response.send(err.code);
				response.end();
			}else{
				// NOTE: client is responsable for calculating Total Playlist duration, and update when changes are made.
				if(dirs[4]){
					// we have an ID... update the row with the specified ID
					let update = "UPDATE "+locConf['prefix']+table+" ";
					let setstr = "";
					let where = "";
					if(['meta', 'rest', 'category_item', 'playlist', 'task'].includes(table))
						// tables where row ID is named RID, rather than just ID
						where = " WHERE RID ="+libpool.escape(dirs[4])+";";
					else
						where = " WHERE ID ="+libpool.escape(dirs[4])+";";
					
					let keys = Object.keys(params);
					let vals = Object.values(params);
					if(keys.length == 0){
						// Must be at least one column specified to update 
						response.status(400);
						response.end();
						connection.release(); // return the connection to pool
						return;
					}
					for(let i=0; i < keys.length; i++){
						let key = keys[i];
						if(hasWhiteSpace(key)){
							// keys (column names) can't have white spaces... 
							response.status(400);
							response.end();
							connection.release(); // return the connection to pool
							return;
						}
						let val = vals[i];
						let valstr = val;
						if(Array.isArray(val)){
							// values can't be arrays... single value per column only
							response.status(400);
							response.end();
							connection.release(); // return the connection to pool
							return;
						}else{
							setstr = buildSetString(setstr, key, valstr);
						}
					}
					// execute the query
					connection.query(update+setstr+where, function (err, res, fields) {
						if(err){
							response.status(400);
							response.send(err.code);
						}else{
							response.status(201);
							response.json(res);
						}
						connection.release(); // return the connection to pool
						response.end();
					});
				}else{
					if(table == 'toc'){
						// special new toc handling... must have type property set
						if((['file', 'task', 'playlist'].includes(params.Type) == false) && 
											((params.Name == undefined) || (params.Name.length == 0))){
							response.status(400);
							response.end();
							connection.release(); // return the connection to pool
							return;
						}
						if((params.Type) == 'file'){
							// For file type items, we will need to create a file table entry along with the toc entry.
							connection.beginTransaction(function(err){
								if(err){
									//Transaction Error (Rollback and release connection)
									connection.rollback(function(){
										connection.release();
										//Failure
									});
									response.status(304);
									response.send(err.code);
									response.end();
									return;
								}else{
									let insert = "INSERT INTO "+locConf['prefix']+table+" ";
									let colstr = "";
									let setstr = "";
									
									let keys = Object.keys(params);
									let vals = Object.values(params);
									if(keys.length == 0){
										// Must be at least one column specified to update 
										response.status(400);
										response.end();
										connection.release(); // return the connection to pool
										return;
									}
									for(let i=0; i < keys.length; i++){
										let key = keys[i];
										if(hasWhiteSpace(key)){
											// keys (column names) can't have white spaces... 
											response.status(400);
											response.end();
											connection.release(); // return the connection to pool
											return;
										}
										let val = vals[i];
										let valstr = val;
										if(Array.isArray(val)){
											// values can't be arrays... single value per column only
											response.status(400);
											response.end();
											connection.release(); // return the connection to pool
											return;
										}else{
											colstr = buildInsColumnString(colstr, key);
											setstr = buildInsValString(setstr, valstr);
										}
									}
									
									colstr += ", Added) ";
									setstr += ", UNIX_TIMESTAMP());";
									// execute the query
									connection.query(insert+colstr+setstr, function (err, res, fields) {
										if(err){
											//Query Error (Rollback and release connection)
											connection.rollback(function(){
												connection.release();
											});
											response.status(304);
											response.send(err.code);
											response.end();
											return;
										}else{
											// next we create an empty file entry row with the same ID as the new toc entry
											// The ID is res.insertID from the above INSERT into the toc table
											connection.query("INSERT INTO "+locConf['prefix']+"file (ID, Missing) VALUES ("+res.insertId+", 1);", function(err, fres){
												if(err){
													//Query Error (Rollback and release connection)
													connection.rollback(function(){
														connection.release();
													});
													response.status(304);
													response.send(err.code);
													response.end();
													return;
												}else{
													connection.commit(function(err){
														if(err){
															connection.rollback(function(){
																connection.release();
															});
															response.status(304);
															response.send(err.code);
															response.end();
															return;
														}else{
															connection.release();
															response.status(201);
															response.json(res);
															response.end();
														}
													});
												}
											});
										}
									});
								}
							});
							return;
						}
					}
					// no ID... insert new row
					let insert = "INSERT INTO "+locConf['prefix']+table+" ";
					let colstr = "";
					let setstr = "";
					
					let keys = Object.keys(params);
					let vals = Object.values(params);
					if(keys.length == 0){
						// Must be at least one column specified to update 
						response.status(400);
						response.end();
						connection.release(); // return the connection to pool
						return;
					}

					for(let i=0; i < keys.length; i++){
						let key = keys[i];
						if(hasWhiteSpace(key)){
							// keys (column names) can't have white spaces... 
							response.status(400);
							response.end();
							connection.release(); // return the connection to pool
							return;
						}
						let val = vals[i];
						let valstr = val;
						if(Array.isArray(val)){
							// values can't be arrays... single value per column only
							response.status(400);
							response.end();
							connection.release(); // return the connection to pool
							return;
						}else{
							colstr = buildInsColumnString(colstr, key);
							setstr = buildInsValString(setstr, valstr);
						}
					}
					
					if(['toc', 'rest', 'category_item'].includes(table)){
						// set Added column with current UNIX TIME
						colstr += ", Added";
						setstr += ", UNIX_TIMESTAMP()";
					}
					colstr += ") ";
					setstr += ");";
					// execute the query
					connection.query(insert+colstr+setstr, function (err, res, fields) {
						if(err){
							response.status(304);
							response.send(err.code);
						}else{
							response.status(201);
							response.json(res);
						}
						connection.release(); // return the connection to pool
						response.end();
					});
				}
			}
		});
	}else{
		response.status(400);
		response.end();
	}
}

// Used by deleteID function
function removeSubtype(connection, type, id, response){
	// remove subtype rows
	// schedule table
	connection.query("DELETE FROM "+locConf['prefix']+"schedule WHERE Item = "+id+";", function(err, results){
		if(err){
			connection.rollback(function(){
				connection.release();
			});
			response.status(304);
			response.send(err.code);
			response.end();
		}else{
			// rest table
			connection.query("DELETE FROM "+locConf['prefix']+"rest WHERE Item = "+id+";", function(err, results){
				if(err){
					connection.rollback(function(){
						connection.release();
					});
					response.status(304);
					response.send(err.code);
					response.end();
				}else{
					// category_item table
					connection.query("DELETE FROM "+locConf['prefix']+"category_item WHERE Item = "+id+";", function(err, results){
						if(err){
							connection.rollback(function(){
								connection.release();
							});
							response.status(304);
							response.send(err.code);
							response.end();
						}else{
							// [type] table
							connection.query("DELETE FROM "+locConf['prefix']+type+" WHERE ID = "+id+";", function(err, results){
								if(err){
									connection.rollback(function(){
										connection.release();
									});
									response.status(304);
									response.send(err.code);
									response.end();
								}else{
									// toc table
									connection.query("DELETE FROM "+locConf['prefix']+"toc WHERE ID = "+id+";", function(err, results){
										if(err){
											connection.rollback(function(){
												connection.release();
											});
											response.status(304);
											response.send(err.code);
											response.end();
										}else{
											connection.commit(function(err){
												if(err){
													connection.rollback(function(){
														connection.release();
													});
													response.status(304);
													response.send(err.code);
													response.end();
												}else{
													connection.release();
													response.status(201);
													response.end();
												}
											});
										}
									});
								}
							});
						}
					});
				}
			});
		}
	});
}

// delete/table/ID[?reassign=id][remove=1]	
//	reassign will reassign related items that contain this property as specified, or fail if not.
// remove will attempt to delete the associated file for file type items.
function deleteID(request, response, params, dirs){
	if(dirs[3] && dirs[4]){
		// table must NOT contain any white spaces, to prevent evil parameters being passed
		let table = dirs[3];
		if(hasWhiteSpace(table)){
			response.status(400);
			response.end();
			return;
		}
		// get ID and make sure it is a number.
		let ID = parseInt(dirs[4], 10);
		if(isNaN(ID)){
			response.status(400);
			response.end();
			return;
		}
		// check for permission
		if((request.session.permission != "admin") &&  (request.session.permission != "manage")){
			response.status(401);
			response.end();
			return;
		}
		let reassign = params.reassign;	// get the reassign id, if any
		if(isNaN(reassign))
			reassign = 0;
		// check for allowed tables, and handle accordingly
		if((table =='artist') && reassign){
			libpool.getConnection((err, connection) => {
				if(err){
					response.status(400);
					response.send(err.code);
					response.end();
				}else{
					connection.beginTransaction(function(err){
						if(err){
							//Transaction Error (Rollback and release connection)
							connection.rollback(function(){
								connection.release();
								//Failure
							});
							response.status(304);
							response.send(err.code);
							response.end();
						}else{
							// remove meta entries, if any
							connection.query("DELETE FROM "+locConf['prefix']+"meta WHERE Parent = '"+locConf['prefix']+"artist' AND ID = "+ID+";", function(err, results){
								if(err){
									//Query Error (Rollback and release connection)
									connection.rollback(function(){
										connection.release();
									});
									response.status(304);
									response.send(err.code);
									response.end();
								}else{
									// remove the actual artist
									connection.query("DELETE FROM "+locConf['prefix']+"artist WHERE ID = "+ID+";", function(err, results){
										if(err){
											//Query Error (Rollback and release connection)
											connection.rollback(function(){
												connection.release();
											});
											response.status(304);
											response.send(err.code);
											response.end();
										}else{
											connection.query("UPDATE "+locConf['prefix']+"file SET Artist = "+reassign+" WHERE Artist = "+ID+";", function(err, results){
												if(err){
													//Query Error (Rollback and release connection)
													connection.rollback(function(){
														connection.release();
													});
													response.status(304);
													response.send(err.code);
													response.end();
												}else{
													connection.commit(function(err){
														if(err){
															connection.rollback(function(){
																connection.release();
															});
															response.status(304);
															response.send(err.code);
															response.end();
														}else{
															connection.release();
															response.status(201);
															response.end();
														}
													});
												}
											});
										}
									});
								}
							});
						}
					});
				}
			});
		}else if((table =='album') && reassign){
			libpool.getConnection((err, connection) => {
				if(err){
					response.status(400);
					response.send(err.code);
					response.end();
				}else{
					connection.beginTransaction(function(err){
						if(err){
							//Transaction Error (Rollback and release connection)
							connection.rollback(function(){
								connection.release();
								//Failure
							});
							response.status(304);
							response.send(err.code);
							response.end();
						}else{
							// remove meta entries, if any
							connection.query("DELETE FROM "+locConf['prefix']+"meta WHERE Parent = '"+locConf['prefix']+"album' AND ID = "+ID+";", function(err, results){
								if(err){
									//Query Error (Rollback and release connection)
									connection.rollback(function(){
										connection.release();
									});
									response.status(304);
									response.send(err.code);
									response.end();
								}else{
									// remove the actual album
									connection.query("DELETE FROM "+locConf['prefix']+"album WHERE ID = "+ID+";", function(err, results){
										if(err){
											//Query Error (Rollback and release connection)
											connection.rollback(function(){
												connection.release();
											});
											response.status(304);
											response.send(err.code);
											response.end();
										}else{
											connection.query("UPDATE "+locConf['prefix']+"file SET Album = "+reassign+" WHERE Album = "+ID+";", function(err, results){
												if(err){
													//Query Error (Rollback and release connection)
													connection.rollback(function(){
														connection.release();
													});
													response.status(304);
													response.send(err.code);
													response.end();
												}else{
													connection.commit(function(err){
														if(err){
															connection.rollback(function(){
																connection.release();
															});
															response.status(304);
															response.send(err.code);
															response.end();
														}else{
															connection.release();
															response.status(201);
															response.end();
														}
													});
												}
											});
										}
									});
								}
							});
						}
					});
				}
			});
		}else if(table == 'category'){	// reassign is optional
			libpool.getConnection((err, connection) => {
				if(err){
					response.status(400);
					response.send(err.code);
					response.end();
				}else{
					connection.beginTransaction(function(err){
						if(err){
							//Transaction Error (Rollback and release connection)
							connection.rollback(function(){
								connection.release();
								//Failure
							});
							response.status(304);
							response.send(err.code);
							response.end();
						}else{
							// remove meta entries, if any
							connection.query("DELETE FROM "+locConf['prefix']+"meta WHERE Parent = '"+locConf['prefix']+"category' AND ID = "+ID+";", function(err, results){
								if(err){
									//Query Error (Rollback and release connection)
									connection.rollback(function(){
										connection.release();
									});
									response.status(304);
									response.send(err.code);
									response.end();
								}else{
									// and remove the actual category
									connection.query("DELETE FROM "+locConf['prefix']+"category WHERE ID = "+ID+";", function(err, results){
										if(err){
											//Query Error (Rollback and release connection)
											connection.rollback(function(){
												connection.release();
											});
											response.status(304);
											response.send(err.code);
											response.end();
										}else{
											if(reassign){
												// reassign items with this category to another
												// IGNORE will skip records where there is a UNIQUE conflict (Item is already in the reassign category)
												connection.query("UPDATE IGNORE "+locConf['prefix']+"category_item SET Category = "+reassign+" WHERE category_item = "+ID+";", function(err, results){
													if(err){
														//Query Error (Rollback and release connection)
														connection.rollback(function(){
															connection.release();
														});
														response.status(304);
														response.send(err.code);
														response.end();
													}else{
														// delete any remaining items from the deleted category
														connection.query("DELETE FROM "+locConf['prefix']+"category_item WHERE Category = "+ID+";", function(err, results){
															if(err){
																//Query Error (Rollback and release connection)
																connection.rollback(function(){
																	connection.release();
																});
																response.status(304);
																response.send(err.code);
																response.end();
															}else{
																connection.commit(function(err){
																	if(err){
																		connection.rollback(function(){
																			connection.release();
																		});
																		response.status(304);
																		response.send(err.code);
																		response.end();
																	}else{
																		connection.release();
																		response.status(201);
																		response.end();
																	}
																});
															}
														});
													}
												});
											}else{
												// removed all items from the deleted category
												connection.query("DELETE FROM "+locConf['prefix']+"category_item WHERE Category = "+ID+";", function(err, results){
													if(err){
														//Query Error (Rollback and release connection)
														connection.rollback(function(){
															connection.release();
														});
														response.status(304);
														response.send(err.code);
														response.end();
													}else{
														connection.commit(function(err){
															if(err){
																connection.rollback(function(){
																	connection.release();
																});
																response.status(304);
																response.send(err.code);
																response.end();
															}else{
																connection.release();
																response.status(201);
																response.end();
															}
														});
													}
												});
											}
										}
									});
								}
							});
						}
					});
				}
			});
		}else if(table == 'category_item'){
			libpool.getConnection((err, connection) => {
				if(err){
					response.status(400);
					response.send(err.code);
					response.end();
				}else{
					connection.query("DELETE FROM "+locConf['prefix']+"category_item WHERE RID = "+ID+";", function(err, results){
						if(err){
							connection.release();
							response.status(304);
							response.send(err.code);
							response.end();
						}else{
							connection.release();
							response.status(201);
							response.end();
						}
					});
				}
			});
		}else if(table == 'rest'){
			libpool.getConnection((err, connection) => {
				if(err){
					response.status(400);
					response.send(err.code);
					response.end();
				}else{
					connection.query("DELETE FROM "+locConf['prefix']+"rest WHERE RID = "+ID+";", function(err, results){
						if(err){
							connection.release();
							response.status(304);
							response.send(err.code);
							response.end();
						}else{
							connection.release();
							response.status(201);
							response.end();
						}
					});
				}
			});
		}else if(table == 'schedule'){
			libpool.getConnection((err, connection) => {
				if(err){
					response.status(400);
					response.send(err.code);
					response.end();
				}else{
					connection.query("DELETE FROM "+locConf['prefix']+"schedule WHERE ID = "+ID+";", function(err, results){
						if(err){
							connection.release();
							response.status(304);
							response.send(err.code);
							response.end();
						}else{
							connection.release();
							response.status(201);
							response.end();
						}
					});
				}
			});
		}else if(table == 'meta'){
			libpool.getConnection((err, connection) => {
				if(err){
					response.status(400);
					response.send(err.code);
					response.end();
				}else{
					connection.query("DELETE FROM "+locConf['prefix']+"meta WHERE RID = "+ID+";", function(err, results){
						if(err){
							connection.release();
							response.status(304);
							response.send(err.code);
							response.end();
						}else{
							connection.release();
							response.status(201);
							response.end();
						}
					});
				}
			});
		}else if(table == 'queries'){
			libpool.getConnection((err, connection) => {
				if(err){
					response.status(400);
					response.send(err.code);
					response.end();
				}else{
					connection.query("DELETE FROM "+locConf['prefix']+"queries WHERE ID = "+ID+";", function(err, results){
						if(err){
							connection.release();
							response.status(304);
							response.send(err.code);
							response.end();
						}else{
							connection.release();
							response.status(201);
							response.end();
						}
					});
				}
			});
		}else if(table == 'toc'){
			// note: subtypes can't be deleted.  Instead the parent toc item 
			// is deleted, and this code handle removing the type sub-properties
			libpool.getConnection((err, connection) => {
				if(err){
					response.status(400);
					response.send(err.code);
					response.end();
				}else{
					connection.beginTransaction(function(err){
						if(err){
							//Transaction Error (Rollback and release connection)
							connection.rollback(function(){
								connection.release();
								//Failure
							});
							response.status(304);
							response.send(err.code);
							response.end();
						}else{
							// remove meta entries, if any
							connection.query("DELETE FROM "+locConf['prefix']+"meta WHERE Parent = '"+locConf['prefix']+"toc' AND ID = "+ID+";", function(err, results){
								if(err){
									//Query Error (Rollback and release connection)
									connection.rollback(function(){
										connection.release();
									});
									response.status(304);
									response.send(err.code);
									response.end();
								}else{
									// first get the type
									connection.query("SELECT Type FROM "+locConf['prefix']+"toc WHERE ID = "+ID+";", function(err, results){
										if(err){
											//Query Error (Rollback and release connection)
											connection.rollback(function(){
												connection.release();
											});
											response.status(304);
											response.send(err.code);
											response.end();
										}else{
											let type = results[0].Type;
											if(type.length){
												if(hasWhiteSpace(type)){
													//type Error (Rollback and release connection)
													connection.rollback(function(){
														connection.release();
													});
													response.status(304);
													response.end();
													return;
												}
												// removing the file here too, if it's a file, as a side task.
												if((type == "file") && params.remove && (params.remove != 0)){
													connection.query("SELECT Hash, Path, Prefix, URL, Mount FROM "+locConf['prefix']+"file WHERE ID = "+ID+";", function(err, results){
														if(err){
															console.log("Failed to get library properties for file item #"+ID+", to delete the file."); 
														}else if(results && results.length){
															// require that the file is prefixed, for security reasons
															resolveFileFromProperties(results[0]).then(result => {
																if(result.Missing == 0){
																	let fullpath = result.Prefix + result.Path;
																	fs.unlink(fullpath, (err) => {
																		if(err){
																			console.log(err+"-Failed to delete the file for item #"+ID+", at "+fullpath); 
																			removeSubtype(connection, type, ID, response);	// remove from library anyway.
																		}else{
																			console.log("File for item #"+ID+" has been deleted at "+fullpath);
																			removeSubtype(connection, type, ID, response);	// remove from library.
																		}
																	});
																}else{
																	console.log("File for item #"+ID+" is missing and can not be deleted.");
																	removeSubtype(connection, type, ID, response); // remove from library anyway.
																}
															});
														}
													});
												}else
													removeSubtype(connection, type, ID, response);
											}else{
												//type Error (Rollback and release connection)
												connection.rollback(function(){
													connection.release();
												});
												response.status(304);
												response.end();
											}
										}
									});
								}
							});
						}
					});
				}
			});
		}else{
			response.status(400);
			response.end();
			return;
		}
		
	}else{
		response.status(400);
		response.end();
	}
}

// getLogs -> location=(name), key1=, key2=, key3=, etc.
// datetime=yyyy-mm-dd-hh:mm:ss
// property=value (% for like)
// added=true (any value for added is taken as true.  False assumed if added is missing)
function getLogs(request, response, params){
	libpool.getConnection((err, connection) => {
		if(err){
			response.status(400);
			response.send(err.code);
			response.end();
		}else{
			if(params.location){	// location specified
				let loc = params.location;
				delete params.location;
				let sort = "-Time";
				if(params.sortBy)
					delete params.sortBy;
					
				let select = "SELECT FROM_UNIXTIME("+locConf['prefix']+"logs.Time) As TimeStr, "+locConf['prefix']+"logs.Name As Label, "+locConf['prefix']+"logs.* ";
				let from = "FROM ("+locConf['prefix']+"logs, "+locConf['prefix']+"locations) ";
				let where = "WHERE "+locConf['prefix']+"locations.Name = "+libpool.escape(loc)+" AND "+locConf['prefix']+"logs.Location = "+locConf['prefix']+"locations.ID ";
				if(params.added){
					// include added items that have not played
					where += "AND ("+locConf['prefix']+"logs.Added & 0x2) = 0 ";
					delete params.added;
				}else
					// exclude items that have not played
					where += "AND ("+locConf['prefix']+"logs.Added & 0x3) = 0 ";
				
				if(params.datetime){
					// include datetime range
					where += "AND "+locConf['prefix']+"logs.Time BETWEEN UNIX_TIMESTAMP(DATE("+libpool.escape(params.datetime)+")) AND UNIX_TIMESTAMP("+libpool.escape(params.datetime)+") ";
					delete params.datetime;
				}else
					where += "AND "+locConf['prefix']+"logs.Time BETWEEN UNIX_TIMESTAMP(DATE(NOW())) AND UNIX_TIMESTAMP(NOW()) ";
				let tail = ""; 

				// additional "where" values
				let keys = Object.keys(params);
				let vals = Object.values(params);
				for(let i=0; i < keys.length; i++){
					let key = keys[i];
					if(hasWhiteSpace(key))
						// keys can't have white spaced... skip if it does
						continue;
					let val = vals[i];
					let valstr = val;
					if(Array.isArray(val)){
						for(let j=0; j < val.length; j++){
							valstr = val[j];
							where = buildWhereString(where, "logs", key, valstr);
						}
					}else{
						where = buildWhereString(where, "logs", key, valstr);
					}
				}
				restQueryRequest(connection, request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.

			}else{	// no location given
				let sort = "Label";
				let select = "SELECT DISTINCT "+locConf['prefix']+"locations.Name AS Label, "+locConf['prefix']+"locations.ID AS ID, "+locConf['prefix']+"locations.Name AS Branch ";
				let from = "FROM ("+locConf['prefix']+"logs, "+locConf['prefix']+"locations) ";
				let where = "WHERE "+locConf['prefix']+"logs.Location = "+locConf['prefix']+"locations.ID ";
				let tail = ""; 
				
				restQueryRequest(connection, request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.
			}
		}
	});
}

// getSched -> date=(YYYY-MM-DD), location=(name), fill=(1/0)
function getSched(request, response, params){
	libpool.getConnection((err, connection) => {
		if(err){
			response.status(400);
			response.send(err.code);
			response.end();
		}else{
			if(params.location){	// location specified
				let loc = params.location;
				if(params.fill){
					// Fill items
					let sort = "";
					let select = "SELECT DISTINCT "+locConf['prefix']+"schedule.Item AS ItemID, ";
					select += locConf['prefix']+"toc.Name AS Label, ";
					select += locConf['prefix']+"schedule.Fill As Fill, ";
					select += locConf['prefix']+"hourmap.Map AS mapHour, ";
					select += locConf['prefix']+"schedule.Hour AS dbHour, ";
					select += locConf['prefix']+"daymap.Map AS mapDay, ";
					select += locConf['prefix']+"schedule.Day AS dbDay, ";
					select += locConf['prefix']+"schedule.Priority AS Priority, ";
					select += locConf['prefix']+"schedule.Month AS Month, ";
					select += locConf['prefix']+"schedule.Date AS Date, ";
					select += locConf['prefix']+"schedule.Minute AS Minute ";
					
					let from = "FROM ("+locConf['prefix']+"schedule, ";
					from += locConf['prefix']+"toc, ";
					from += locConf['prefix']+"hourmap, ";
					from += locConf['prefix']+"daymap) ";
					
					from += "LEFT JOIN "+locConf['prefix']+"locations ON ("+locConf['prefix']+"locations.Name = "+libpool.escape(params.location)+" AND "+locConf['prefix']+"schedule.Location = "+locConf['prefix']+"locations.ID) ";
					from += "LEFT JOIN "+locConf['prefix']+"rest ON ("+locConf['prefix']+"schedule.Item = "+locConf['prefix']+"rest.Item AND "+locConf['prefix']+"rest.Location = "+locConf['prefix']+"locations.ID) ";

					let where = "WHERE "+locConf['prefix']+"rest.Added IS NULL ";
					where += "AND "+locConf['prefix']+"schedule.Item = "+locConf['prefix']+"toc.ID ";
					where += "AND "+locConf['prefix']+"schedule.Fill > 0 ";
					where += "AND "+locConf['prefix']+"schedule.Day = "+locConf['prefix']+"daymap.Day ";
					where += "AND "+locConf['prefix']+"schedule.Hour = "+locConf['prefix']+"hourmap.Hour ";
					where += "AND ("+locConf['prefix']+"schedule.Location IS NULL OR "+locConf['prefix']+"schedule.Location = "+locConf['prefix']+"locations.ID) ";
					where += "AND "+locConf['prefix']+"schedule.Priority > 0 ";
					if(params.date){
						where += "AND ("+locConf['prefix']+"schedule.Date = 0 OR "+locConf['prefix']+"schedule.Date = DATE_FORMAT("+libpool.escape(params.date)+", '%e') ) ";
						where += "AND ("+locConf['prefix']+"schedule.Month = 0 OR "+locConf['prefix']+"schedule.Month = DATE_FORMAT("+libpool.escape(params.date)+", '%c') ) ";
						where += "AND "+locConf['prefix']+"daymap.Map = 1 + DATE_FORMAT("+libpool.escape(params.date)+", '%w') ";
					}else{
						where += "AND ("+locConf['prefix']+"schedule.Date = 0 OR "+locConf['prefix']+"schedule.Date = DATE_FORMAT(NOW(), '%e') ) ";
						where += "AND ("+locConf['prefix']+"schedule.Month = 0 OR "+locConf['prefix']+"schedule.Month = DATE_FORMAT(NOW(), '%c') ) ";
						where += "AND "+locConf['prefix']+"daymap.Map = 1 + DATE_FORMAT(NOW(), '%w') ";
					}
					let tail = "ORDER BY mapHour ASC, Minute ASC, Priority DESC ";
					
					restQueryRequest(connection, request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.
					
				}else{
					// Insert Items
					let sort = "";
					let select = "SELECT DISTINCT "+locConf['prefix']+"schedule.Item AS ItemID, ";
					select += locConf['prefix']+"toc.Name AS Label, ";
					select += locConf['prefix']+"toc.Duration / 60 AS Duration, ";
					select += locConf['prefix']+"hourmap.Map AS mapHour, ";
					select += locConf['prefix']+"schedule.Hour AS dbHour, ";
					select += locConf['prefix']+"daymap.Map AS mapDay, ";
					select += locConf['prefix']+"schedule.Day AS dbDay, ";
					select += locConf['prefix']+"schedule.Priority AS Priority, ";
					select += locConf['prefix']+"schedule.Month AS Month, ";
					select += locConf['prefix']+"schedule.Date AS Date, ";
					select += locConf['prefix']+"schedule.Minute AS Minute ";
					
					let from = "FROM ("+locConf['prefix']+"schedule, ";
					from += locConf['prefix']+"toc, ";
					from += locConf['prefix']+"hourmap, ";
					from += locConf['prefix']+"daymap) ";
					
					from += "LEFT JOIN "+locConf['prefix']+"locations ON ("+locConf['prefix']+"locations.Name = "+libpool.escape(params.location)+" AND "+locConf['prefix']+"schedule.Location = "+locConf['prefix']+"locations.ID) ";
					from += "LEFT JOIN "+locConf['prefix']+"rest ON ("+locConf['prefix']+"schedule.Item = "+locConf['prefix']+"rest.Item AND "+locConf['prefix']+"rest.Location = "+locConf['prefix']+"locations.ID) ";

					let where = "WHERE "+locConf['prefix']+"rest.Added IS NULL ";
					where += "AND "+locConf['prefix']+"schedule.Item = "+locConf['prefix']+"toc.ID ";
					where += "AND "+locConf['prefix']+"schedule.Fill = 0 ";
					where += "AND "+locConf['prefix']+"schedule.Day = "+locConf['prefix']+"daymap.Day ";
					where += "AND "+locConf['prefix']+"schedule.Hour = "+locConf['prefix']+"hourmap.Hour ";
					where += "AND ("+locConf['prefix']+"schedule.Location IS NULL OR "+locConf['prefix']+"schedule.Location = "+locConf['prefix']+"locations.ID) ";
					where += "AND "+locConf['prefix']+"schedule.Priority > 0 ";
					if(params.date){
						where += "AND ("+locConf['prefix']+"schedule.Date = 0 OR "+locConf['prefix']+"schedule.Date = DATE_FORMAT("+libpool.escape(params.date)+", '%e') ) ";
						where += "AND ("+locConf['prefix']+"schedule.Month = 0 OR "+locConf['prefix']+"schedule.Month = DATE_FORMAT("+libpool.escape(params.date)+", '%c') ) ";
						where += "AND "+locConf['prefix']+"daymap.Map = 1 + DATE_FORMAT("+libpool.escape(params.date)+", '%w') ";
					}else{
						where += "AND ("+locConf['prefix']+"schedule.Date = 0 OR "+locConf['prefix']+"schedule.Date = DATE_FORMAT(NOW(), '%e') ) ";
						where += "AND ("+locConf['prefix']+"schedule.Month = 0 OR "+locConf['prefix']+"schedule.Month = DATE_FORMAT(NOW(), '%c') ) ";
						where += "AND "+locConf['prefix']+"daymap.Map = 1 + DATE_FORMAT(NOW(), '%w') ";
					}
					let tail = "ORDER BY mapHour ASC, Minute ASC, Priority DESC ";
					
					restQueryRequest(connection, request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.
					
				}
			}else{
				// location list
				let select = "SELECT DISTINCT "+locConf['prefix']+"locations.Name AS Label, "+locConf['prefix']+"locations.ID AS ID, "+locConf['prefix']+"locations.Name AS Branch ";
				let from = "FROM ("+locConf['prefix']+"schedule, "+locConf['prefix']+"locations) ";
				let where = "WHERE "+locConf['prefix']+"schedule.Location = "+locConf['prefix']+"locations.ID ";
				let sort = "Label";
				let tail = ""; 

				restQueryRequest(connection, request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.
			}
		}
	});
}

// !searchFor -> type=(title,playlist,task,added,category,artist,album,comment,missing,rested), match=(type name match), key1=, key2=, ...
// where key is a type
// match=wind%hair (% triggers a like match)
// artist=ethan
// category=blue
// meta=key,value (parent is table)
// added=yyyy-mm-dd
// missing=true/false (joins file table only)
// limit=10 (default for no limit)

function searchFor(request, response, params, dirs){
	if(params.type){
		var type = params.type;
		libpool.getConnection((err, connection) => {
			if(err){
				response.status(400);
				response.send(err.code);
				response.end();
			}else{
				delete params.type;
				if(type == 'artist'){
					var sort = "Label";
					var select = "SELECT DISTINCT "+locConf['prefix']+"artist.Name AS Label, ";
					select += locConf['prefix']+"artist.ID AS ID ";
					var from = "FROM ("+locConf['prefix']+"toc, "+locConf['prefix']+"file, "+locConf['prefix']+"artist";
					var where = "WHERE "+locConf['prefix']+"toc.Type = 'file' AND "+locConf['prefix']+"file.ID = "+locConf['prefix']+"toc.ID ";
					where += "AND "+locConf['prefix']+"artist.ID = "+locConf['prefix']+"file.Artist ";
					if(params.match){ 
						where = buildWhereString(where, "artist", "Name", params.match);
						delete params.match;
					}
					var tail = ""; 
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					where = parts.where;
					from += ") ";
					
				}else if(type == 'album'){
					var sort = "Label";
					var select = "SELECT DISTINCT "+locConf['prefix']+"album.Name AS Label, ";
					select += locConf['prefix']+"album.ID AS ID ";
					var from = "FROM ("+locConf['prefix']+"toc, "+locConf['prefix']+"file, "+locConf['prefix']+"album";
					var where = "WHERE "+locConf['prefix']+"toc.Type = 'file' AND "+locConf['prefix']+"file.ID = "+locConf['prefix']+"toc.ID ";
					where += "AND "+locConf['prefix']+"album.ID = "+locConf['prefix']+"file.Album ";
					if(params.match){ 
						where = buildWhereString(where, "album", "Name", params.match);
						delete params.match;
					}
					var tail = ""; 
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					where = parts.where;
					from += ") ";
				}else if(type == 'title'){
					var sort = "Label";
					var select = "SELECT DISTINCT "+locConf['prefix']+"toc.Name AS Label, ";
					select += locConf['prefix']+"toc.ID AS tocID, "+locConf['prefix']+"toc.Duration AS Duration ";
					var from = "FROM ("+locConf['prefix']+"toc";
					var where = "WHERE "+locConf['prefix']+"toc.Type = 'file' ";
					if(params.match){ 
						where = buildWhereString(where, "toc", "Name", params.match);
						delete params.match;
					}
					var tail = ""; 
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					where = parts.where;
					from += ") ";
				}else if(type == 'task'){
					var sort = "Label";
					var select = "SELECT DISTINCT "+locConf['prefix']+"toc.Name AS Label, ";
					select += locConf['prefix']+"toc.ID AS tocID, "+locConf['prefix']+"toc.Duration AS Duration ";
					var from = "FROM ("+locConf['prefix']+"toc";
					var where = "WHERE "+locConf['prefix']+"toc.Type = 'task' ";
					if(params.match){ 
						where = buildWhereString(where, "toc", "Name", params.match);
						delete params.match;
					}
					var tail = ""; 
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					where = parts.where;
					from += ") ";
				}else if(type == 'category'){
					var sort = "Label";
					var select = "SELECT DISTINCT "+locConf['prefix']+"category.Name AS Label, ";
					select += locConf['prefix']+"category.ID AS ID ";
					var from = "FROM ("+locConf['prefix']+"toc, "+locConf['prefix']+"category, "+locConf['prefix']+"category_item";
					var where = "WHERE "+locConf['prefix']+"category_item.Item = "+locConf['prefix']+"toc.ID ";
					where += "AND "+locConf['prefix']+"category.ID = "+locConf['prefix']+"category_item.Category "; 
					if(params.match){ 
						where = buildWhereString(where, "category", "Name", params.match);
						delete params.match;
					}
					var tail = ""; 
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					where = parts.where;
					from += ") ";
				}else if(type == 'playlist'){
					var sort = "Label";
					var select = "SELECT DISTINCT "+locConf['prefix']+"toc.Name AS Label, ";
					select += locConf['prefix']+"toc.ID AS tocID, "+locConf['prefix']+"toc.Duration AS Duration ";
					var from = "FROM ("+locConf['prefix']+"toc";
					var where = "WHERE "+locConf['prefix']+"toc.Type = 'playlist' ";
					if(params.match){ 
						where = buildWhereString(where, "toc", "Name", params.match);
						delete params.match;
					}
					var tail = ""; 
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					where = parts.where;
					from += ") ";
				}else if(type == 'added'){
					var sort = "-"+locConf['prefix']+"toc.Added";
					var select = "SELECT DISTINCT DATE(FROM_UNIXTIME("+locConf['prefix']+"toc.Added)) AS Label ";
					var from = "FROM ("+locConf['prefix']+"toc";
					var where = "";
					if(params.match){ 
						where = buildWhereDate(where, "toc", "Added", params.match);
						delete params.match;
					}
					var tail = ""; 
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					where = parts.where;
					from += ") ";
				}else if(type == 'comment'){
					var sort = "Label";
					var select = "SELECT DISTINCT "+locConf['prefix']+"toc.Tag AS Label ";
					var from = "FROM ("+locConf['prefix']+"toc";
					var where = "";
					if(params.match){ 
						where = buildWhereString(where, "toc", "Tag", params.match);
						delete params.match;
					}
					var tail = ""; 
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					where = parts.where;
					from += ") ";
				}else if(type == 'missing'){
					var sort = "Label";
					var select = "SELECT DISTINCT "+locConf['prefix']+"toc.Name AS Label, ";
					select += locConf['prefix']+"toc.ID AS tocID, "+locConf['prefix']+"toc.Duration AS Duration ";
					var from = "FROM ("+locConf['prefix']+"toc, "+locConf['prefix']+"file";
					var where = "WHERE "+locConf['prefix']+"toc.Type = 'file' AND "+locConf['prefix']+"file.ID = "+locConf['prefix']+"toc.ID ";
					where += "AND "+locConf['prefix']+"file.Missing = 1 ";
					if(params.match){ 
						where = buildWhereString(where, "toc", "Name", params.match);
						delete params.match;
					}
					var tail = ""; 
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					where = parts.where;
					from += ") ";
				}else if(type == 'rested'){
					var sort = "Label";
					var select = "SELECT DISTINCT "+locConf['prefix']+"locations.Name AS Label, ";
					select += locConf['prefix']+"locations.ID AS ID ";
					var from = "FROM ("+locConf['prefix']+"toc, "+locConf['prefix']+"rest, "+locConf['prefix']+"locations";
					var where = "WHERE "+locConf['prefix']+"toc.ID = "+locConf['prefix']+"rest.Item ";
					where += "AND "+locConf['prefix']+"locations.ID = "+locConf['prefix']+"rest.Location "
					if(params.match){ 
						where = buildWhereString(where, "locations", "Name", params.match);
						delete params.match;
					}
					var tail = ""; 
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					where = parts.where;
					from += ") ";
				}else{
					response.status(400);
					response.end();
					connection.release(); // return the connection to pool
					return;
				}
				
				restQueryRequest(connection, request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.
				
			}
		});
	}else{
		// no type... generate type list
		let list = [{Label: "Artist", Branch: "artist"}, {Label: "Album", Branch: "album"}, {Label: "Title", Branch: "title"}, 
		{Label: "Category", Branch: "category"}, {Label: "Playlist", Branch: "playlist"}, {Label: "Task", Branch: "task"}, 
		{Label: "Added", Branch: "added"}, {Label: "Comment", Branch: "comment"}, {Label: "Missing", Branch: "missing"},
		{Label: "Rested", Branch: "rested"}];
		restObjectRequest(response, list);
	}
}

// item/ID{?resolve=(1,0)}		gets full item info, If resolve is true (default 0), file item paths and 
// 									missing will be re-writen making use of local prefix search settings.
// 									Use separate get queries for last played/history and schedule entries. 
function getItem(request, response, params, dirs){
	if(dirs[3]){
		// get ID and make sure it is a number.
		let ID = parseInt(dirs[3], 10);
		if(isNaN(ID)){
			response.status(400);
			response.end();
			return;
		}
		libpool.getConnection((err, connection) => {
			if(err){
				response.status(400);
				response.send(err.code);
				response.end();
				return;
			}else{
				connection.query("SELECT * FROM "+locConf['prefix']+"toc WHERE ID = "+ID+";", function(err, results){
					if(err){
						response.status(304);
						response.send(err.code);
						connection.release();
						response.end();
						return;
					}else if(results[0]){
						let type = results[0].Type;
						if(type.length){
							if(hasWhiteSpace(type)){
								//type Error
								response.status(304);
								connection.release();
								response.end();
								return;
							}
							// get subtype rows
							if(type == "file"){
								let fitb = locConf['prefix']+"file";
								let artb = locConf['prefix']+"artist";
								let altb = locConf['prefix']+"album";
								let query = "SELECT "+fitb+".Missing AS Missing, "+fitb+".Volume AS Volume, "+fitb+".Hash AS Hash, "+fitb+".Prefix AS Prefix, ";
								query += fitb+".Path AS Path, "+fitb+".Mount AS Mount, "+fitb+".URL AS URL, "+fitb+".SegIn AS SegIn, "+fitb+".SegOut SegOut, ";
								query += fitb+".FadeOut AS FadeOut, "+fitb+".Intro AS Intro, "+fitb+".Outcue AS Outcue, "+fitb+".Track AS Track, ";
								query += fitb+".Artist AS ArtistID, "+fitb+".Album AS AlbumID, "+artb+".Name AS Artist, "+altb+".Name as Album ";
								query += "FROM "+fitb+" LEFT JOIN "+artb+" ON "+fitb+".Artist = "+artb+".ID LEFT JOIN "+altb+" ON "+fitb+".Album = "+altb+".ID ";
								query += "WHERE "+fitb+".ID = "+ID+";";
								connection.query(query, function(err, subresults){
									if(err){
										response.status(304);
										response.send(err.code);
										connection.release();
										response.end();
										return;
									}else if(subresults[0]){
										let final = results[0];
										if(params.resolve){
											// handle file type resolve option
											resolveFileFromProperties(subresults[0]).then(result => {
												final[type] = result;
												response.status(201);
												response.json(final);
												connection.release();
												response.end();
												return;
											});
											
										}else{
											final[type] = subresults[0];
											response.status(201);
											response.json(final);
											connection.release();
											response.end();
											return;
										}
									}else{
										// no file properties!
										response.status(404);
										connection.release();
										response.end();
									}
								});
							}else if(type == "playlist"){
								connection.query("SELECT Position, RID, Property, Value FROM "+locConf['prefix']+type+" WHERE ID = "+ID+" ORDER BY Position ASC;", function(err, subresults){
									if(err){
										response.status(304);
										response.send(err.code);
										connection.release();
										response.end();
										return;
									}else{
										let final = results[0];
										let re = [];
										let pos = -1;
										for(let i=0; i < subresults.length; i++){
											if(subresults[i].Position != pos){
												pos = subresults[i].Position;
												re[pos] = [];
											}
											delete subresults[i].Position;
											re[pos].push(subresults[i]);
										}
										final[type] = re;
										response.status(201);
										response.json(final);
										connection.release();
										response.end();
									}
								});
							}else if(type == "task"){
								connection.query("SELECT RID, Property, Value FROM "+locConf['prefix']+type+" WHERE ID = "+ID+" ORDER BY Property ASC;", function(err, subresults){
									if(err){
										response.status(304);
										response.send(err.code);
										connection.release();
										response.end();
										return;
									}else{
										let final = results[0];
										final[type] = subresults;
										response.status(201);
										response.json(final);
										connection.release();
										response.end();
									}
								});
							}else{
								connection.query("SELECT * FROM "+locConf['prefix']+type+" WHERE ID = "+ID+";", function(err, subresults){
									if(err){
										response.status(304);
										response.send(err.code);
										connection.release();
										response.end();
										return;
									}else{
										let final = results[0];
										final[type] = subresults;
										response.status(201);
										response.json(final);
										connection.release();
										response.end();
									}
								});
							}
						}else{
							//type Error
							response.status(304);
							response.send(err.code);
							connection.release();
							response.end();
							return;
						}
					}else{
						// No result!
						response.status(404);
						connection.release();
						response.end();
						return;
					}
				});
			}
		});
	}else{
		response.status(400);
		response.end();
	}
}

// download/ID{?save=1}	if download is specified, then the file is downlaoded, no just sent
function getDownload(request, response, params, dirs){
	if(dirs[3]){
		// get ID and make sure it is a number.
		let ID = parseInt(dirs[3], 10);
		if(isNaN(ID) || (ID == 0)){
			response.status(400);
			response.end();
			return;
		}
		if((request.session.permission != "admin") &&  (request.session.permission != "manage")){
			response.status(401);
			response.end();
			return;
		}
		libpool.getConnection((err, connection) => {
			if(err){
				response.status(400);
				response.send(err.code);
				response.end();
				return;
			}else{
				// check for type = file or playlist and handle accordingly
				connection.query("SELECT Type FROM "+locConf['prefix']+"toc WHERE ID = "+ID+";", function(err, tresults){
					if(err){
						response.status(404);
						response.send(err.code);
						connection.release();
						response.end();
						return;
					}
					if(tresults && Array.isArray(tresults) && tresults.length){
						if(tresults[0].Type == "file"){
							connection.query("SELECT Path, Prefix, Hash, URL, Mount FROM "+locConf['prefix']+"file WHERE ID = "+ID+";", function(err, results){
								if(err){
									response.status(404);
									response.send(err.code);
									connection.release();
									response.end();
									return;
								}else if(results[0]){
									if(results && Array.isArray(results) && results[0].Prefix && results[0].Prefix.length && results[0].Path && results[0].Path.length){
										// require that the file is prefixed, for security reasons
										resolveFileFromProperties(results[0]).then(result => {
											if(result.Missing == 0){
												let fullpath = result.Prefix + result.Path;
												if(params.save && (params.save != 0))
													response.download(fullpath);
												else
													response.sendFile(fullpath);
												return;
											}else{
												response.status(400);
												response.end();
												connection.release();
												return;
											}
										});
									}else{
										response.status(400);
										response.end();
										connection.release();
										return;
									}
								}else{
									response.status(404);
									connection.release();
									response.end();
									return;
								}
							});
						}else if(tresults[0].Type == "playlist"){
							//!! exprot params type
						}else{
							// Missing Type, or not a file or playlist
							response.status(404);
							connection.release();
							response.end();
							return;
						}
					}
				});
			}
		});
	}else{
		response.status(400);
		response.end();
	}
}

function executeQuery(request, response, params, dirs){
	if(dirs[3]){
		// get ID and make sure it is a number.
		let ID = parseInt(dirs[3], 10);
		if(isNaN(ID) || (ID == 0)){
			response.status(400);
			response.end();
			return;
		}
		if((request.session.permission != "admin") &&  (request.session.permission != "manage")){
			response.status(401);
			response.end();
			return;
		}
		libpool.getConnection((err, connection) => {
			if(err){
				response.status(400);
				response.send(err.code);
				response.end();
				return;
			}else{
				connection.query("SELECT SQLText FROM "+locConf['prefix']+"queries WHERE ID = "+ID+";", function(err, results){
					if(err){
						response.status(404);
						response.send(err.code);
						connection.release();
						response.end();
						return;
					}else if(results[0]){
						if(results[0].SQLText && results[0].SQLText.length){
							let queries = replaceQueryMacros(results[0].SQLText, params, true);
							let qArray = queries.split(";");
							sequencialQuery(connection, qArray).then(result => {
								response.status(201);
								response.json(result);
								connection.release();
								response.end();
							});
						}else{
							response.status(400);
							response.end();
							connection.release();
							return;
						}
					}else{
						response.status(404);
						connection.release();
						response.end();
						return;
					}
				});
			}
		});
	}else{
		response.status(400);
		response.end();
	}
}

function execShellCommand(cmd, params) {
	const exec = require("child_process").execFile;
	return new Promise((resolve, reject) => {
		exec(cmd, params, (error, stdout, stderr) => {
			if(error){
				reject(error);
			}else if(stdout){
				resolve(stdout); 
			}else{
				reject(stderr);
			}
		});
	});
}

function fplLineInterp(line, params){
	let pos = line.indexOf("\t");
	let key = "";
	let val = "";
	if(pos > -1){
		key = line.substring(0, pos);
		if(line.length > pos)
			val = line.substring(pos+1);
	}
	if(key.length){
		if(params.index){	// playlist item level properties
			if(params.props.filepl == undefined)
				params.props.filepl = [];
			if(params.props.filepl[params.index-1] == undefined)
				params.props.filepl[params.index-1] = {};
			params.props.filepl[params.index-1][key] = val;
		}else{		// top level playlist properties
			params.props[key] = val;
		}
	}else if(line.length == 0){
		// empty line...
		params.index++;
	}
}

async function filePLGetMetaForFile(filepl, props){
	var index = 0;	// start at zero for parent level.  1, 2,.. are item indexes in the playlist
	let params ={index: 0, props: props};
	let status = await fileLineByLine(filepl, params, fplLineInterp);
	if(status = true){
		return props;
	}else{
		return false
	}
}


async function getFileMeta(tmpDirFileName){
	const trans = {Duration: "Duration", title: "Name", artist: "Artist", album: "Album", "track number": "Track", composer: "Composer", audio: "Audio", Seekable: "Seekable"};
	let obj = {};
	if(tmpDir.length){
		let path = tmpDir+tmpDirFileName;
		let params = [path];
		// check for playlist types
		let fd = await openFile(path);
		if(fd > -1){
			let buf = new Buffer(11);
			let result = await readFileIntoBuffer(fd, 0, 11, buf, 0);
			closeFileDescriptor(fd);
			if(result){
				let str = buf.toString();
				if(str == "Type\tfilepl"){
					// we have an audiorack playlist file
					obj.Type = "filepl";
					let full = await filePLGetMetaForFile(path, obj);
					if(full)
						return full;	// all results
					else
						return obj;	// empty list
				}
			}
		}
		
		// check for audio file types
		let cmdresult = "";
		try {
			cmdresult = await execShellCommand("gst-discoverer-1.0", params);
		}catch (error){
			return {};	// failed-> results are empty
		}
		let lines = cmdresult.split("\n");
		let hasAudio = false;
		for(let i = 0; i < lines.length; i++){
			let line = lines[i].trim();	// strip white space
			if(line.length > 5){
				let keyval = line.split(": ");
				if(keyval.length > 1){
					// convert key names for keys we want, ignore the rest
					if(keyval[0] = trans[keyval[0]]){
						if(keyval[0] == "Duration"){
							// convert time to seconds
							let parts = keyval[1].split(":");
							let sec = 0.0;
							for(let p = 0; p < parts.length; p++)
								sec = (sec * 60.0) + parseFloat(parts[p]);
							keyval[1] = sec;
						}else if(keyval[0] == "Audio"){
							hasAudio = true;
							keyval[0] = "Type";
							keyval[1] = "file";
						}
						obj[keyval[0]] = keyval[1];
					}
				}
			}
		}
		if(hasAudio){
			if((obj.Name == undefined) || (obj.Name.length == 0))
				obj.Name = path.basename(path, path.extname(path));
			obj.Hash = await getFileHash(path);
			let pre = getFilePrefixPoint(path);
			obj.Prefix = pre.prefix;
			obj.Path = pre.path;
			return obj;	// OK -> send results
		}else
			return {};	// no audio -> results are empty
	}else
		return obj;	// failed-> results are empty
}

async function mkMediaDirs(){
	const zeroPad = (num, places) => String(num).padStart(places, '0')
	if(mediaDir.length){
		let present = new Date();
		var year = present.getFullYear();
		var month = present.getMonth();
		var date = present.getDate();
		let path = mediaDir+zeroPad(year, 2)+"/"+zeroPad(month, 2)+"/"+zeroPad(date, 2)+"/";		// Format: YYYY/MM/DD/
		// recursively create multiple directories
		return await mkRecursiveDir(path); // this returns the full path to the deepest directory, with trailing slash, or empty if we failed
	}
	return "";
}

function calcPLDuration(plprops){
	let duration = 0.0;
	let offset = 0.0;
	let calDur = 0.0;
	if(plprops && plprops.length){
		// use Duration, Offset, SegIn, SegOut and FadeOut properties
		for(let i=0; i<plprops.length; i++){
			if(plprops[i].Offset)
				duration = plprops[i].Offset; // override time to last item with start of this one
			if(plprops[i].Duration){
				duration = duration + plprops[i].Duration;
				if(plprops[i].SegIn)
					duration = duration - plprops[i].SegIn;
				if(plprops[i].FadeOut)
					duration = duration - plprops[i].FadeOut;
				else if(plprops[i].SegOut)
					duration = duration - plprops[i].SegOut;
				else
					duration = duration - 5.0; // default segout time
			}else if(((i+1) < plprops.length) && (plprops[i+1].Offset))
					duration = plprops[i+1].Offset // use offset of next item, if present
		}
	}
	return duration;
}

async function checkTypeMatch(connection, newType, oldID){
	let result = false;
	if(newType && oldID){
		let query = "";
		if(newType == "filepl")
			newType = "playlist";
		if(newType == "file")
			query = "SELECT * FROM "+locConf['prefix']+"file WHERE ID = "+libpool.escape(oldID)+";";
		else
			query = "SELECT * FROM "+locConf['prefix']+"toc WHERE Type = "+libpool.escape(newType)+" AND ID = "+libpool.escape(oldID)+";";
		try{
			result = await asyncQuery(connection, query);
		}catch(err){
			return false;
		}
		if(result && Array.isArray(result) && result.length && result[0].ID)
			return result[0];
	}
	return false;
}

async function checkDupPlName(connection, Name){
	let result = false;
	let query = "Select ID FROM "+locConf['prefix']+"toc WHERE Type = 'playlist' AND Name = "+libpool.escape(Name)+";";
	try{
		result = await asyncQuery(connection, query);
	}catch(err){
		return false;
	}
	return result;
}

async function checkDupFileHash(connection, Hash){
	let result = false;
	let query = "Select ID, Missing, Prefix, Path FROM "+locConf['prefix']+"file WHERE Hash = "+libpool.escape(Hash)+";";
	try{
		result = await asyncQuery(connection, query);
	}catch(err){
		return false;
	}
	return result;
}

async function findAddNameTable(connection, table, name){
	// find or if missing, and new table entry with the given name (i.e. Artist, Album, etc.)
	let result = false;
	// search...
	let query = "SELECT ID FROM "+locConf['prefix']+table+" WHERE Name = "+libpool.escape(name)+";";
	try{
		result = await asyncQuery(connection, query);
	}catch(err){
		console.log("findAddNameTable err="+err);
		result = false;
	}
	if(result && Array.isArray(result) && result[0].ID)
		return result[0].ID;
		
	// Not found, add...
	query = "INSERT INTO "+locConf['prefix']+table+" (Name) VALUES ("+libpool.escape(name)+");";
	try{
		result = await asyncQuery(connection, query);
	}catch(err){
		console.log("findAddNameTable err="+err);
		result = false;
	}
	if(result && result.insertId)
		return result.insertId;
	else
		return 0; 	// failed for some reason!
}

async function addToCatID(connection, ItemID, catIDs){
	let insert = "";
	if(Array.isArray(catIDs)){
		for(let i=0; i < catIDs.length; i++){
			let val = catIDs[i];
			insert = "INSERT IGNORE INTO "+locConf['prefix']+"category_item (Item, Category, Added) VALUES ("+ItemID+", "+val+", UNIX_TIMESTAMP());";
			try{
				let result = await asyncQuery(connection, insert);
			}catch(err){
				// roll back
				await asyncRollback(conn);
				return false;
			}
		}
	}else{
		insert = "INSERT IGNORE INTO "+locConf['prefix']+"category_item (Item, Category, Added) VALUES ("+ItemID+", "+catIDs+", UNIX_TIMESTAMP());";
		try{
			let result = await asyncQuery(connection, insert);
		}catch(err){
			// roll back
			await asyncRollback(conn);
			return false;
		}
	}
	return true;
}

async function importFileIntoLibrary(path, params){
	let pass = {id:0, status:-2}; // status=-3 file copy failed, -2, bad file, -1 error, 0 skipped, 1 new/added, 2 cat update existing, 3 replaced existing, 4 fixed existing
	let ID = 0;
	let result = {};
	let meta = {};
	let dupmode = 0;
	let dupInfo = false;
	
	if(tmpDir.length){
		// Convert dup parameter to number: skip,catset,replace(delete old),update(if missing)
		if(params.dup == undefined) dupmode = 0;
		else if((params.dup == "catset") && params.catid && params.catid.length) dupmode = 2;
		else if(params.dup == "replace") dupmode = 3;
		else if(params.dup == "update") dupmode = 4;

		meta = await getFileMeta(path);
		if(meta && Object.keys(meta).length){
			let conn = false;
			// valid meta data.  Handle according to type
			if((meta.Name == undefined) || (meta.Name.length == 0))
				meta.Name = basename(path, path.extname(path));
				
			if(params.id){ // handle forced replace of specified item
				conn = await asyncGetDBConnection();
				if(!conn){
					pass.status = -1;
					return pass;
				}
				result = await checkTypeMatch(conn, meta.Type, params.id);
				if(result && result.ID){
					ID = result.ID;
					dupInfo = result;
					dupmode = 3;
				}else{
					conn.release();
					pass.status = -2;
					return pass;
				}
			}
			
			if(meta.Type == "filepl"){		// handle playlist file import
				let dbMatch = false;
				if(dbFingerprint == meta.Fingerprint)
					dbMatch = true;	// itemID, artistID albumID, etc., are valid... keep them in the conversion
				let dur = 0.0;
				if(meta.Duration)
					dur = parseFloat(meta.Duration);	// use the top level Duration, if provided
				if(dur == 0.0)
					dur = calcPLDuration(meta.filepl);
				// convert into a library playlist
				if(conn == false){ // we don't have a db connection yet
					conn = await asyncGetDBConnection();
					if(!conn){
						pass.status = -1;
						return pass;
					}
				}
					
				if(!ID){
					result = await checkDupPlName(conn, meta.Name);
					if(result && Array.isArray(result) && result.length && result[0].ID)
						ID = result[0].ID; // we have a dup: keep dupmode as is to handle according to requested mode.
					else
						dupmode = 1; // no dup, override dupmode to force adding of this list
				}
				if(dupmode == 0){	// skip.  We are done.
					conn.release();
					pass.id = ID;	// ID of the dup. list
					pass.status = 0;
					return pass;
				}
				
				result = await asyncStartTransaction(conn);
				if(result){
					conn.release();
					pass.status = -1;
					return pass;
				}
				
				if(dupmode == 1){	// insert new into toc
					let insert = "INSERT INTO "+locConf['prefix']+"toc "
					let colstr = "";
					let setstr = "";
					colstr = buildInsColumnString(colstr, "Type");
					setstr = buildInsValString(setstr, "playlist");
					colstr = buildInsColumnString(colstr, "Name");
					setstr = buildInsValString(setstr, meta.Name);
					colstr = buildInsColumnString(colstr, "Duration");
					setstr = buildInsValString(setstr, dur);
					colstr += ", Added) ";
					setstr += ", UNIX_TIMESTAMP());";

					try{
						result = await asyncQuery(conn, insert+colstr+setstr);
					}catch(err){
						// roll back
						await asyncRollback(conn);
						conn.release();
						pass.status = -1;
						return pass;
					}
					if(result && result.insertId){
						ID = result.insertId;
					}else{
						// roll back
						await asyncRollback(conn);
						conn.release();
						pass.status = -1;
						return pass;
					}
				}else if(dupmode > 2){	// update existing duration and remove existing entries
					let query = "UPDATE "+locConf['prefix']+"toc SET Duration = "+dur+" WHERE ID = "+ID+";";
					try{
						result = await asyncQuery(conn, query);
					}catch(err){
						// roll back
						await asyncRollback(conn);
						conn.release();
						pass.status = -1;
						return pass;
					}
					query = "DELETE FROM "+locConf['prefix']+"playlist WHERE ID = "+ID+";";
					try{
						result = await asyncQuery(conn, query);
					}catch(err){
						// roll back
						await asyncRollback(conn);
						conn.release();
						pass.status = -1;
						return pass;
					}
				}
				if(dupmode != 2){	// add these entries
					if(meta.filepl && meta.filepl.length){
						for(let pos=0; pos<meta.filepl.length; pos++){
							let entry = meta.filepl[pos];
							let keys = Object.keys(entry);
							for(let i=0; i<keys.length; i++){
								if(!dbMatch && ["ArtistID", "AlbumID", "ID"].includes(keys[i]))	// skip these if different library
									continue; // arserver will use remaining properties to try to find item
								insert = "INSERT INTO "+locConf['prefix']+"playlist (ID, Position, Property, Value) "
								setstr = " VALUES ("+ID;
								setstr = buildInsValString(setstr, pos);
								setstr = buildInsValString(setstr, keys[i]);
								setstr = buildInsValString(setstr, entry[keys[i]]);
								setstr += ");";
								try{
									result = await asyncQuery(conn, insert+setstr);
								}catch(err){
									// roll back
									await asyncRollback(conn);
									conn.release();
									pass.status = -1;
									return pass;
								}
							}
						}
					}
				}
				
			}else if(meta.Type == "file"){		// handle media file import
				let artID = 0;
				let albID = 0;
				if(conn == false){ // we don't have a db connection yet
					conn = await asyncGetDBConnection();
					if(!conn){
						pass.status = -1;
						return pass;
					}
				}
				if(!ID){
					result = await checkDupFileHash(conn, meta.Hash);
					if(result && Array.isArray(result) && result.length && result[0].ID){
						dupInfo = result[0];
						ID = result[0].ID;
					}else
						dupmode = 1; // no dup, override dupmode to force adding of this file
				}
				if(dupmode == 0){	// skip.  We are done.
					conn.release();
					pass.id = ID;	// ID of the dup. list
					pass.status = 0;
					return pass;
				}
				if((dupmode == 4) && (dupInfo.Missing == false)){
					// dupmode is "update if missing", and dup is not missing:  Skip.
					conn.release();
					pass.id = ID;	// ID of the dup. list
					pass.status = 0;
					return pass;
				}
				
				result = await asyncStartTransaction(conn);
				if(result){
					conn.release();
					pass.status = -1;
					return pass;
				}
				
				if(dupmode == 1){	// insert new into toc
					// get/create artist and album IDs only for new items in prep fir file property inserts later
					artID = await findAddNameTable(conn, "artist", meta.Artist);
					if(artID == 0){
						await asyncRollback(conn);
						conn.release();
						pass.status = -1;
						return pass;
					}
					albID = await findAddNameTable(conn, "album", meta.Album);
					if(albID == 0){
						await asyncRollback(conn);
						conn.release();
						pass.status = -1;
						return pass;
					}
					let insert = "INSERT INTO "+locConf['prefix']+"toc "
					let colstr = "";
					let setstr = "";
					colstr = buildInsColumnString(colstr, "Type");
					setstr = buildInsValString(setstr, "file");
					colstr = buildInsColumnString(colstr, "Name");
					setstr = buildInsValString(setstr, meta.Name);
					colstr = buildInsColumnString(colstr, "Duration");
					setstr = buildInsValString(setstr, meta.Duration);
					colstr += ", Added) ";
					setstr += ", UNIX_TIMESTAMP());";
					try{
						result = await asyncQuery(conn, insert+colstr+setstr);
					}catch(err){
						// roll back
						await asyncRollback(conn);
						conn.release();
						pass.status = -1;
						return pass;
					}
					if(result && result.insertId){
						ID = result.insertId;
					}else{
						// roll back
						await asyncRollback(conn);
						conn.release();
						pass.status = -1;
						return pass;
					}					
				}else if(dupmode > 2){	// update existing duration
					// 3 replace/delete old or 4 update if missing, missing already checked.
					let query = "UPDATE "+locConf['prefix']+"toc SET Duration = "+meta.Duration+" WHERE ID = "+ID+";";
					try{
						result = await asyncQuery(conn, query);
					}catch(err){
						// roll back
						await asyncRollback(conn);
						conn.release();
						pass.status = -1;
						return pass;
					}
					// try to delete original file
					result = await resolveFileFromProperties(dupInfo);
					if(!result.Missing){
						let ok = await deleteFile(result.Path + result.Prefix);
						if(ok)
							console.log("replace file: deleteed original file " + result.Path + result.Prefix);
						else
							console.log("replace file: failed to delete original file " + result.Path + result.Prefix);
					}
				}
				if(dupmode != 2){	// add new file to media dir and refernce it in the file properties
					let newPath = await mkMediaDirs();
					if(newPath.length){
						path = tmpDir+path;
						newPath = await copyFileToDir(path, newPath);
						// attempt to copy an associated fileplaylist as well, if it exists.
						await copyFileToDir(path+".fpl", newPath);
					}
					if(newPath.length == 0){
						// copy failed!
						await asyncRollback(conn);
						conn.release();
						pass.status = -3;
						return pass;
					}
					let pre = getFilePrefixPoint(newPath);
					meta.Prefix = pre.prefix;
					meta.Path = pre.path;
					meta.URL = url.pathToFileURL(newPath).href;
					// Old OSX: Mount -> Prefix + mountName (first dir in path) 
					// if path=/some/path, mountName = /
					// if path=some/path, mountName = some/
					let Mount = "/";
					if(pre.prefix.length){
						let idx = meta.Path.indexOf("/");
						if(idx > 0)
							Mount = meta.Prefix + meta.Path.substring(0, idx);
					}
					if(dupmode == 1){ //insert new file row
						let insert = "INSERT INTO "+locConf['prefix']+"file "
						let colstr = "";
						let setstr = "";
						colstr = buildInsColumnString(colstr, "ID");
						setstr = buildInsValString(setstr, ID);
						colstr = buildInsColumnString(colstr, "Artist");
						setstr = buildInsValString(setstr, artID);
						colstr = buildInsColumnString(colstr, "Album");
						setstr = buildInsValString(setstr, albID);
						colstr = buildInsColumnString(colstr, "Track");
						setstr = buildInsValString(setstr, meta.Track);
						colstr = buildInsColumnString(colstr, "Hash");
						setstr = buildInsValString(setstr, meta.Hash);
						colstr = buildInsColumnString(colstr, "Path");
						setstr = buildInsValString(setstr, meta.Path);
						colstr = buildInsColumnString(colstr, "Prefix");
						setstr = buildInsValString(setstr, meta.Prefix);
						colstr = buildInsColumnString(colstr, "URL");
						setstr = buildInsValString(setstr, meta.URL);
						colstr = buildInsColumnString(colstr, "Mount");
						setstr = buildInsValString(setstr, Mount);
						colstr = buildInsColumnString(colstr, "Missing");
						setstr = buildInsValString(setstr, "0");
						colstr += ") ";
						setstr += ");";
						try{
							result = await asyncQuery(conn, insert+colstr+setstr);
						}catch(err){
							// roll back
							await asyncRollback(conn);
							conn.release();
							pass.status = -1;
							return pass;
						}
					}else{	// update existing file row, leave segs, fades, vol, etc alone.
						let query = "UPDATE "+locConf['prefix']+"file ";
						query += "SET Hash = "+libpool.escape(meta.Hash)+" ";
						query += ", Path = "+libpool.escape(meta.Path)+" ";
						query += ", Prefix = "+libpool.escape(meta.Prefix)+" ";
						query += ", URL = "+libpool.escape(meta.URL)+" ";
						query += ", Mount = "+libpool.escape(Mount)+" ";
						query += ", Missing = 0 ";
						query += "WHERE ID = "+ID+";";
						try{
							result = await asyncQuery(conn, query);
						}catch(err){
							// roll back
							await asyncRollback(conn);
							conn.release();
							pass.status = -1;
							return pass;
						}
					}
				}

			}else{
				// invalid type
				if(conn)
					conn.release();
				return pass;
			}
			// add to categories if catid is set
			if(params.catid && params.catid.length){
				result = await addToCatID(conn, ID, params.catid);
				if(!result){
					await asyncRollback(conn);
					conn.release();
					pass.status = -1;
					return pass;
				}
			}
			// all done.
			await asyncCommitTransaction(conn);
			conn.release();
			pass.id = ID;
			pass.status = dupmode;
			return pass;
		}
	}
	return pass;
}

function getFileInfo(request, response, params, dirs){
	let path = "";
	let tailIdx = request.path.search(dirs[2]+"/");
	let tailLen = request.path.length-tailIdx-(dirs[2].length)-1;
	if((tailIdx < 0) || (tailLen > 0)){
		path = request.path.substring(tailIdx + dirs[2].length+1);
		path = decodeURIComponent(path);
		if((request.session.permission != "admin") &&  (request.session.permission != "manage")){
			response.status(401);
			response.end();
			return;
		}
		getFileMeta(path).then(result => {
			if(result && Object.keys(result).length){
				response.status(200);
				response.json(result);
				response.end();
			}else{
				response.status(400);
				response.end();
			}
		});
	}else{
		response.status(400);
		response.end();
	}
}

function importFile(request, response, params, dirs){
	let path = "";
	let tailIdx = request.path.search(dirs[2]+"/");
	let tailLen = request.path.length-tailIdx-(dirs[2].length)-1;
	if((tailIdx < 0) || (tailLen > 0)){
		path = request.path.substring(tailIdx + dirs[2].length+1);
		path = decodeURIComponent(path);
		if((request.session.permission != "admin") &&  (request.session.permission != "manage")){
			response.status(401);
			response.end();
			return;
		}
		importFileIntoLibrary(path, params).then(result => {
			if(result){
				response.status(200); 
				response.json(result);
				// result.status=-3 file copy failed, -2, bad file, -1 error, 0 skipped, 1 new/added, 2 cat update existing, 3 replaced existing, 4 fixed existing
				response.end();
			}else{
				response.status(400);
				response.end();
			}
		});
	}else{
		response.status(400);
		response.end();
	}
}

function getPrefix(request, response, params){
	if(params.path && params.path.length){
		let path = params.path;
		if((request.session.permission != "admin") &&  (request.session.permission != "manage")){
			response.status(401);
			response.end();
			return;
		}
		let result = getFilePrefixPoint(path);
		if(result){
			response.status(200); 
			response.json(result);
			response.end();
		}else{
			response.status(400);
			response.end();
		}
	}else{
		response.status(400);
		response.end();
	}
}

function getLibraryFingerprint(){
	libpool.getConnection((err, connection) => {
		if(err){
			dbFingerprint = "";
			return;
		}else{
			connection.query("SELECT Value FROM "+locConf['prefix']+"info WHERE Property = 'Fingerprint';", function(err, results){
				if(err){
					connection.release();
					dbFingerprint = "";
					return;
				}else if(results[0]){
					if(results[0].Value && results[0].Value.length){
						connection.release();
						dbFingerprint = results[0].Value;
						return;
					}
				}
				connection.release();
				dbFingerprint = "";
				return;
			});
		}
	});
}

module.exports = {
	configure: function (config) {
		let prx_list = config['prefixes'];
		if(prx_list && Array.isArray(prx_list) && prx_list.length)
			prefix_list = prx_list;
		else{
			if(process.platform == "darwin")
				prefix_list = ["/Volumes/","/private/var/automount/Network/","/Network/"];		// OSX prefix list
			else
				prefix_list = ["/mnt/","/media/*/","/run/user/*/gvfs/*share="];	// all other POSIX prefix list
		}
		if(config['tmpMediaDir'] && config['tmpMediaDir'].length)
			tmpDir = config['tmpMediaDir'];
		else
			tmpDir = "";
		if(config['mediaDir'] && config['mediaDir'].length)
			mediaDir = config['mediaDir'];
		else
			mediaDir = "";
		let libc = config['library'];
		if(libc){
			if(_.isEqual(libc, locConf) == false){
				console.log('Lib Configuration changed.');
				locConf = _.cloneDeep(libc);
				if(libpool)
					if(locConf['type'] == 'mysql')
						libpool.end();	// when pool is no longer used -> has been replaced by a new pool due to settings change
				if(libc['type'] == 'mysql'){
					libpool = mysql.createPool({
						connectionLimit : libc['conLimit'],
						multipleStatements: true,
						host: libc['host'],
						user: libc['user'],
						password: libc['password'],
						database: libc['database'],
						dateStrings: true
					});
				}
			}
			console.log('Lib Configuration done.');
		}
		getLibraryFingerprint();
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
		dirs = request.path.split('/');
		if(dirs[2] == 'get'){
			getFrom(request, response, params, dirs);	// /get/table/{ID/}?column=value1&...
		}else if(dirs[2] == 'set'){
			setIn(request, response, params, dirs);	// /set/table/{ID/}?column=value1&... If ID is missing, a new row is created
		}else if(dirs[2] == 'delete'){
			deleteID(request, response, params, dirs);	// delete/table/ID?reassign=id	will reassign related items that contain this property as specified, or fail if not.
																		// 					remove=1 will attempt to delete an associated file.
		}else if(dirs[2] == 'logs'){
			getLogs(request, response, params);			// /logs{?location=loc-name}{&datetime=YYYY-MM-DD-HH:MM}{&column=value1&...} 
																	// Range is from specified date/time to start of that day
																	//	today assumed if no date specified
																	// ?added=0 assumed (playFed only) if added not specified
																	// /logs alone returns list of location names that have log entries
		}else if(dirs[2] == 'sched'){
			getSched(request, response, params);			// /sched{?location=loc-name}{&fill=1}{&datetime=YYYY-MM-DD} 
																		// today assumed if no date specified
																		// fill = 0 assumed if not specified
																		// /sched alone returns list of location names that have schedules entries
		}else if(dirs[2] == 'browse'){
			searchFor(request, response, params, dirs);	// /browse{?type=resultType}{&match=name}{&other-type1=value1&...}
																		// /browse alone returns a list of types, less the "item" type
		}else if(dirs[2] == 'item'){				// item/ID{?resolve=(1,0)}		
													// gets full item info, If resolve is true (default 0), file item paths and 
													// missing will be re-writen making use of local prefix search settings.
													// Use separate get queries for last played/history and schedule entries
			getItem(request, response, params, dirs);
		}else if(dirs[2] == 'download'){
			getDownload(request, response, params, dirs);	// /download/ID	triggers a download  of the file associated with the file type item
																	//			?save=1 to save the file instead of showing contents in page
																	//			?export=[fpl,fplmedia,cue,pls,m3u] convert playlist to type
		}else if(dirs[2] == 'query'){
			executeQuery(request, response, params, dirs);	// /query/ID?select=[selval1, selval2,..]&prompt=[promptval1, promptval2,..]
																			//		runs the specified query number, replacing macros [select(...)],
																			//		and [prompt(..)] text with the parameter array values in sequence as 
																			//		they occur. And replacing [loc-id] with it's single parameter.  And
																			//		finally, replacing [prefix] with the database table refix setting value.
																			//		It is the clients job to ensure that matching parameters for macros are set.
		}else if(dirs[2] == 'info'){
			getFileInfo(request, response, params, dirs);	// /info/filename{/possible/sub/directory/filename}
		}else if(dirs[2] == 'import'){
			importFile(request, response, params, dirs);	// /import/filename{/possible/sub/directory/filename}{?catid=id-of-cat}
																		//						{&dup=[skip,catset,replace(delete old),update(if missing)]}
																		//						{&id=[itemID to replace}
																		// note: skip assumed if dup is not specified
																		// note: if id is set, type of new and origin must match.  For files, only 
																		// 		file properties and duration are changed, with the new file replacing
																		//			the old at the old file's location.
		}else if(dirs[2] == 'getprefix'){
			getPrefix(request, response, params);			// /getprefix?path=the/path/to/find/prefix/of/if/any
		}else{
			response.status(400);
			response.end();
		}
		
		// dbSync
		
		// dbSearch
				
		// initdb - name
	}
};
/*
// singleton db access:
var mysql = require('mysql');

var con = mysql.createConnection({
	host: locConf['host'],
	user: locConf['user'],
	password: locConf['password']
});

con.connect(function(err) {
  if (err) throw err;
  console.log("Connected!");
});
 

async function db_initialize(conn, conf){
	dbInstance *newinst;
	char *dbName;
	char *sqlstr = NULL;
	char *typeStr;
	char *versionStr = NULL;
	char *ini_file_path = NULL;
	char *tmp = NULL;
	FILE *fp;
	char line[4096];
	
	newinst = NULL; // used as a flag to clean up new db at the end of the first call of the call recursive chain
	fp = NULL;
	typeStr = GetMetaData(0, "db_type", 0);
	dbName = GetMetaData(0, "db_name", 0);
	if(!strlen(typeStr) || !strlen(dbName))
		goto cleanup;

	if(!conn){
		var mysql = require('mysql');
		* 
		* "type": "mysql",
			"host": "127.0.0.1",
			"user": "root",
			"password": "wlvs1035fm",
			"database": "mtnmedia",
			"prefix": "ar_"
		* 
		* 
		* 
		var con = mysql.createConnection({
			host: conf['host'],
			user: conf['user'],
			password: conf['password']
		});

		con.connect(function(err) {
			if(err) 
				throw err;
			console.log("Connected!");
		}
	}
	
	if(!db_use_database(db, dbName))
		versionStr = dbGetInfo("Version");
	if(versionStr && strlen(versionStr)){
		// this is an upgrade to an existing database
		str_setstr(&ini_file_path, AppSupportDirectory);
		str_appendstr(&ini_file_path, typeStr);
		str_appendstr(&ini_file_path, versionStr);
		str_appendstr(&ini_file_path, ".dbi");
	}else{
		// create new database if one with the given name doesn't already exist
		str_setstr(&ini_file_path, AppSupportDirectory);
		str_appendstr(&ini_file_path, typeStr);
		str_appendstr(&ini_file_path, ".dbi");
		str_setstr(&sqlstr, "CREATE DATABASE IF NOT EXISTS ");
		str_appendstr(&sqlstr, dbName);
		
		if(db_query(db, sqlstr))
			goto cleanup;
		if(db_use_database(db, dbName))
			goto cleanup;
	}
	
	if((fp = fopen(ini_file_path, "r")) == NULL){
		if(!versionStr || !strlen(versionStr)){
			str_setstr(&tmp, "[database] dbInitialize- template file '");
			str_appendstr(&tmp, ini_file_path);
			str_appendstr(&tmp, "': Could not open file for reading");
			serverLogMakeEntry(tmp);
			free(tmp); 
		}else
			db_set_errtag(db, "dbInitialize");
		goto cleanup;
	}
	
	db_set_errtag(db, "dbInitialize");
	while(fgets(line, sizeof line, fp) != NULL){
		// each line in the .dbi file is an sql command to execute
		str_setstr(&sqlstr, line);
		dbMacroReplace(&sqlstr);
		if(!db_query(db, sqlstr))
			db_result_free(db);
	}
	if(versionStr && strlen(versionStr))
		// re-enter for another go-around so we upgrade
		// all they way to the latest version.
		db_initialize(db);
	
cleanup:
	free(dbName);
	free(typeStr);
	if(sqlstr)
		free(sqlstr);
	if(ini_file_path)
		free(ini_file_path);
	if(fp)
		fclose(fp);
	if(!db->errRec.flag){
		versionStr = dbGetInfo("Version");
		str_setstr(&tmp, " [database] dbInitialize-");
		str_appendstr(&tmp, dbName);
		str_appendstr(&tmp, ": initialized/updated to version ");
		str_appendstr(&tmp, versionStr);
		serverLogMakeEntry(tmp);
		free(dbName);
		free(versionStr);
		if(newinst)
			db_instance_free(newinst);
		return 1;
	}
	if(versionStr)
		free(versionStr);
	if(dbName)
		free(dbName);
	if(newinst)
		db_instance_free(newinst);
	return 0;
}
*/
