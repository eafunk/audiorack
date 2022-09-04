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

const DefLimit = 0;	// zero for no default limit: return all results

var mysql = require('mysql');
const _= require('lodash');
var fs = require('fs');
var path = require('path');
var linereader = require('line-reader');
var crypto = require('crypto');
const crc = require('node-crc');
const url = require('url');
var glob = require('glob');
const { once } = require('events');
var archiver = require('archiver');
var sse = require('./sse.js');

var prefix_list = [];
var libpool = undefined;
var locConf = undefined;
var tmpDir = "";
var mediaDir = "";
var supportDir = "";
var dbFingerprint = "";
var fileSettings = [];

var dbSyncRunning = false;	// set to a function if dbSync is running in the background, otherwise false
var dbSearchRunning = false;	// set to a function if dbSearch is running in the background, otherwise false

function hasWhiteSpace(str) {
  return /\s/g.test(str);
}

function changeKey(oldKey, newKey, obj){
	if(obj.oldKey){
		obj.newKey = obj.oldKey;
		delete obj.oldKey;
	}
}

function asyncGetDBConnection(){
	return new Promise(function (resolve, reject){
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
		query = query.replaceAll("\n", " "); // replace with a space to preserve "white space-ness" of \n
	query = query.replaceAll("[prefix]", locConf['prefix']);
	query = query.replaceAll("[!prefix]", "[prefix]");

	if(params && params.prompt){
		if(Array.isArray(params.prompt) == false)
			params.prompt = [params.prompt];
		while(prtIdx < params.prompt.length){
			let start = query.indexOf("[prompt(");
			if(start > -1){
				if((start > 0) && (query.charAt(start-1) === "'"))	// if previous char is ', include it in replacement
					start--;
				let subs = query.substring(start);
				let end = subs.indexOf("]");
				if(end > -1){
					if((end < (subs.length-1)) && (subs.charAt(end+1) === "'"))	// if next char is ', include it in replacement
						end++;
					end = start + end;
					query = replaceRangeWithString(query, start, end, libpool.escape(params.prompt[prtIdx]));
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
				if((start > 0) && (query.charAt(start-1) === "'"))	// if previous char is ', include it in replacement
					start--;
				let subs = query.substring(start);
				let end = subs.indexOf("]");
				if(end > -1){
					if((end < (subs.length-1)) && (subs.charAt(end+1) === "'"))	// if next char is ', include it in replacement
						end++;
					end = start + end;
					query = replaceRangeWithString(query, start, end, libpool.escape(params.select[selIdx]));
					selIdx++;
				}
			}else
				break;
		}
	}
	
	if(params && params.locid)
		query = query.replaceAll("[loc-id]", libpool.escape(params.locid));
	query = query.replace("[!loc-id]", "[loc-id]");

	if(params && params.fingerprint)
		query = query.replaceAll("[fingerprint]", libpool.escape(params.fingerprint));
	query = query.replaceAll("[!fingerprint]", "[fingerprint]");
	return query;
}

function buildSetString(setStr, key, value){
	if(setStr.length == 0)
		setStr += "SET ";
	else
		setStr += ", ";
	setStr += libpool.escapeId(key)+" = "+libpool.escape(value);
	return setStr;
}

function buildInsColumnString(colStr, key){
	if(colStr.length == 0)
		colStr += "(";
	else
		colStr += ", ";
	colStr += libpool.escapeId(key);
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
		whereStr += libpool.escapeId(locConf['prefix']+table)+"."+libpool.escapeId(key)+"="+libpool.escape(value)+" ";
	else
		whereStr += libpool.escapeId(locConf['prefix']+table)+"."+libpool.escapeId(key)+" LIKE "+libpool.escape(value)+" ";
	return whereStr;
}

function buildWhereDate(whereStr, table, key, dateStr){
	if(whereStr.length == 0)
		whereStr += "WHERE ";
	else
		whereStr += "AND ";
	whereStr += "DATE(FROM_UNIXTIME("+libpool.escapeId(locConf['prefix']+table)+"."+libpool.escapeId(key)+"))="+libpool.escape(dateStr)+" ";
	return whereStr;
}

// Make sure hard coded string befor call use escaped names
function buildFromString(fromStr, table, query){
	// query should be added after all tables have already been added
	let value;
	if(query && query.length)
		value = query;
	else
		value = libpool.escapeId(locConf['prefix']+table);
	if(!fromStr)
		fromStr = "";
	if(table && (fromStr.indexOf(table) > -1))
		return fromStr;
	if(fromStr.length == 0)
		fromStr += "FROM (";
	else
		fromStr += ", ";
	fromStr += value;
	return fromStr;
}

function includeJoin(whereStr, includeStr){
	if(!whereStr)
		whereStr = "";
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
	let subselect = "";
	let keys = Object.keys(params);
	let vals = Object.values(params);
	let added = false;
	for(let i=0; i < keys.length; i++){
		let key = keys[i];
		if(hasWhiteSpace(key))
			// keys can't have white spaced... skip if it does
			continue;
		if(key == "range")	// range is handled by the restQueryRequest function
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
			added = true;
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
			added = true;
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
			added = true;
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
			added = true;
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
			added = true;
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
			added = true;
		}else if(key == "category"){
			// assumes an item is only in a particular category once
			let val = vals[i];
			let valstr = val;
			let subwhere = "";
			let count;
			if(Array.isArray(val)){
				// multi-category
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
				subwhere = "("+subwhere+") ";
				count = val.length;
			}else{
				subwhere = locConf['prefix']+"category.Name";
				if(valstr.indexOf("%") == -1)
					subwhere += "="+libpool.escape(valstr)+" ";
				else
					subwhere += " LIKE "+libpool.escape(valstr)+" ";
				count = 1;
			}
			subselect = "(SELECT "+locConf['prefix']+"toc.ID AS ID ";
			subselect += "FROM ("+locConf['prefix']+"toc, "+locConf['prefix']+"category, "+locConf['prefix']+"category_item) ";
			subselect += "WHERE "+locConf['prefix']+"category_item.Item = "+locConf['prefix']+"toc.ID ";
			subselect += "AND "+locConf['prefix']+"category.ID = "+locConf['prefix']+"category_item.Category AND "; 
			subselect += subwhere;
			subselect += "GROUP BY "+locConf['prefix']+"toc.ID HAVING COUNT("+locConf['prefix']+"toc.ID) = "+count+") AS subcat";
			
			if(queryParts.where.length == 0)
				queryParts.where += "WHERE ";
			else
				queryParts.where += "AND ";
			queryParts.where += locConf['prefix']+"toc.ID = subcat.ID ";
			added = true;
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
			added = true;
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
			added = true;
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
			}added = true;
		}else
			// unknow key... ignore
			continue;
	}
	if(subselect.length)
		queryParts.subquery = subselect;
	return added;
}

function restQueryRequest(connection, table, request, response, select, from, where, tail, sort, noLimit){
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
					tail += libpool.escapeId(str)+" DESC";
				}else
					tail += libpool.escapeId(str);
			}
			tail += " ";
		}
	}

	// handle range request
	let params = {};
	if(request.method == 'GET')
		params = request.query;
	else if(request.method == 'POST')
		params = request.body;
	let offset = 0;
	let cnt = DefLimit;
	if(noLimit==0){	// only set a limit/offset if noLimit is 0
		if(params.range){
			let parts = params.range.split('/');
			cnt = parts[1];
			offset = parts[0] * cnt;
		}
		if(cnt)
			tail += "LIMIT "+cnt+" OFFSET "+offset;
	}
	let query = select+from+where+tail;
//console.log(query); // Used to help debug SQL queries
	connection.query(query, function (err, results, fields) {
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
					// convert table column results back to API names
					tableToApiReformat(table, results);
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

function mkRecursiveDir(fpath){
	return new Promise(function (resolve){
		fs.mkdir(fpath, {recursive: true}, (err) => {
			if(err)
				resolve("");
			resolve(fpath);	// this is the full path to the deepest directory, with trailing slash
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

function deleteFile(fpath){
	return new Promise(function (resolve){
		fs.unlink(fpath, function (err){
			if(err){
				resolve(false);
			}else{
				resolve(true);
			}
		});
	});
}

function openFile(fpath){
	return new Promise(function (resolve){
		fs.open(fpath, 'r', function(err, fd){
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

function createReadStream(filename){
	return new Promise(function(resolve, reject){
		function onError(err){
			reject(err);
		}
		function onReadable(){
			cleanup();
			resolve(stream);
		}
		function cleanup(){
			stream.removeListener('error', onError);
			stream.removeListener('readable', onReadable);
		}
		
		var stream = fs.createReadStream(filename);
		stream.on('error', onError);
		stream.on('readable', onReadable);    // <-- small correction
	});
}

function processFileLines(filename, options, params, linecb){
	return new Promise(function(resolve, reject) {
		linereader.eachLine(filename, options, linecb.bind({params: params}), function(err){
			if(err){
				reject(err);
			}else{
				resolve(true);
			}
		});
	});
}

async function getFileHash(fpath){
	// MD5 portion first
	let buf = new Buffer(4096);
	let md5sum = crypto.createHash('md5');
	
	let fd = await openFile(fpath);
	if(fd == 0)
		return "";
	let size = await sizeOfFile(fd);
	if(size == 0){
		closeFileDescriptor(fd);
		return "";
	}
	let result = false;
	let len = Math.floor(size/3);
	let b1 = Math.floor(len / 4096) * 4096;
	let b2 = Math.floor(len / 2048) * 4096;
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
	let b3 = Math.floor(3 * len / 4096) * 4096;
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
	closeFileDescriptor(fd);
	return hash;
}

async function CheckFileHashMatch(fpath, hash){
	let fHash = await getFileHash(fpath);
	if(!fHash.length)
		fHash = false;
	if(!fHash)
		return fHash;
	if(hash){
		if(hash.length == 0)	// ignore hash check for items with empy hash property
			return fHash;
		if(fHash.toLowerCase() === hash.toLowerCase())
			return fHash;
		return false;
	}else
		// ignore hash check for items with NULL hash property
		return fHash;
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
	// Check file related properties in db against the file itself... find file if needed.
	// use file as final authority for properties. This function may modify the properties object.
	// Returns a status of 0 for unchanged, 1 for updated/fixed, 2 for lost, 3 for found.
	// Start with these 2 assumptions:
	let newHash;

	if(properties.Missing && (typeof properties.Missing === 'string'))
		properties.Missing = parseInt(properties.Missing);
	let status = 0;
	let changed = 0;
	let missing = 1;
	let listSize = prefix_list.length;
	let fpath = "";
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
				fpath = properties.Prefix;
		}
		pathStr = properties.Path;
		fpath += pathStr;
	}else if(properties.URL && properties.Mount){
		// use old mount & URL method of searching for files. if sucessful,
		// set path & prefix properties to use new method next time.
		if(properties.URL.length == 0){
			// bad URL string and/or Mount string
			changed = 1; // missing flag is already set
			fpath = "";
		}else{
			fpath = url.fileURLToPath(properties.URL);
			if(properties.Mount && (properties.Mount.length > 1)){
				//  convert /a/path/with/mount -> mount
				let mountParts = properties.Mount.split("/");
				let mountName = "";
				if(mountParts.length > 1)
					mountName = mountParts[mountParts.length - 1];
				if(mountName.length){
					let i = fpath.search(mountName);
					if(i > 0){ 
						// given fpath = /longer/path/to/mount/some/file
						// and Mount = mount
						// pathStr -> mount/some/file */
						pathStr = fpath.substring(i);
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
		pathStr = pathStr.replaceAll('*', '\\*');
		pathStr = pathStr.replaceAll('?', '\\?');
		pathStr = pathStr.replaceAll('[', '\\[');
		
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
	// fpath is Full Path, pathStr is the relative path, if any
	// i.e. fpath = /longer/path/to/mount/some/file
	// and  pathStr = mount/some/file
	let i = 0;
	let p = 0;
	let c = 0;
	let results = [];
	do{
		newHash = await CheckFileHashMatch(fpath, properties.Hash);
		if(newHash){
			// Hash code agrees with or was empty with/in database, not missing
			missing = 0;
			break;
		}else{
			changed = 1;
		}
		// try to create another path with next prefix or next glob items
		do{
			if(i < results.length){
				// next path in glob list
				fpath = results[i];
				i++;
				break;
			}else{
				let tmp = prefix_list[p];
				if(tmp && pathStr.length){
					fpath = tmp;
					if(adjPathStr.length && c){
						// trying the pathStr version with case adjustment
						fpath += adjPathStr;
						c = 0; // go back to original case next time
					}else{
						fpath += pathStr;
						if(adjPathStr.length)
							c = 1; // try case change next time
					}
					i = 0;
					results = await globSearch(fpath);
					if(results.length){
						// found a path in new glob list
						fpath = results[0];
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
		if(properties.Missing)
			status = 0; // was missing, still is: no change
		else{
			// set missing metadata flag
			properties.Missing = 1;
			status = 2; // was not missing, is now: lost
		}
	}else{
		// found it... set URL if not set or if changed
		if(properties.URL.length == 0)
			changed = 1;	// fix missing URL
		if(changed){
			// re-encode URL from path change or from being missing
			properties.URL = url.pathToFileURL(fpath).href;
			status = 1;
		}else
			status = 0;

		let pre = getFilePrefixPoint(fpath);
		properties.Prefix = pre.prefix;
		properties.Path = pre.path;
		// Old OSX: Mount -> Prefix + mountName (first dir in path) 
		// if fpath=/some/path, mountName = /
		// if fpath=some/path, mountName = Some, Mount = /Volumes/Some
		properties.Mount = "/";
		if(pre.prefix.length){
			let idx = pre.path.indexOf("/");
			if(idx > 0){
				let mountName = pre.path.substring(0, idx);
				mountName = mountName[0].toUpperCase() + mountName.substr(1);
				properties.Mount = "/Volumes/" + mountName;
			}
		}
		// setthe Hash... it may have been empty going in
		properties.Hash = newHash;

		if(properties.Missing){
			// was missing, but has been found... 
			status = 3; // missing now found
		}
		// unset missing metadata flag
		properties.Missing = 0;
	}
	return status;
}

function apiToTableReformat(table, params){
	// handle inconsistent table ID name usage, convert ALL database row id column names to 'id', 
	// and some rational columns from ID to 'Item' to our client API side
	let idCol = 'id';
	// tables for which the id column is named ID
	if(['artist', 'album', 'category', 'toc', 'daypart', 'queries', 'favs', 'locations', 'schedule', 'file', 'logs', 'orders', 'request', 'users'].includes(table) == true){
		idCol = 'ID';
		//	and handle parameters too...
		if(params.id){
			params.ID = params.id;
			delete params.id;
		}
		if(params.sortBy){
			let parts = params.sortBy.split(',');
			if(parts.length){
				for(let i=0; i < parts.length; i++){
					let str = parts[i];
					let j = str.search("-");
					if(j > -1)
						str = str.substring(j+1);
					if(str == "id")
						str = "ID";
					if(j > -1)
						str = "-"+str;
					parts[i] = str;
				}
			}
			params.sortBy = parts.join(",");
		}
		if(params.distinct){
			if(Array.isArray(params.distinct)){
				for(let j=0; j < params.distinct.length; j++){
					if(params.distinct[j] == "id")
						params.distinct[j] = "ID";
				}
			}else if(params.distinct == "id"){
				params.distinct = "ID";
			}
		}
	}
	// tables for which the id column is named RID
	else if(['category_item', 'meta', 'playlist', 'rest', 'task'].includes(table) == true){
		idCol = 'RID';
		//	and handle parameters too...
		if(params.id){
			params.RID = params.id;
			delete params.id;
		}
		if(params.sortBy){
			let parts = params.sortBy.split(',');
			if(parts.length){
				for(let i=0; i < parts.length; i++){
					let str = parts[i];
					let j = str.search("-");
					if(j > -1)
						str = str.substring(j+1);
					if(str == "id")
						str = "RID";
					if(j > -1)
						str = "-"+str;
					parts[i] = str;
				}
			}
			params.sortBy = parts.join(",");
		}
		if(params.distinct){
			if(Array.isArray(params.distinct)){
				for(let j=0; j < params.distinct.length; j++){
					if(params.distinct[j] == "id")
						params.distinct[j] = "RID";
				}
			}else if(params.distinct == "id"){
				params.distinct = "RID";
			}
		}
		// translate parameter Item to ID for some tables
		if(['meta', 'playlist', 'task'].includes(table) == true){
			if(params.Item){
				params.ID = params.Item;
				delete params.Item;
			}
			if(params.sortBy){
				let parts = params.sortBy.split(',');
				if(parts.length){
					for(let i=0; i < parts.length; i++){
						let str = parts[i];
						let j = str.search("-");
						if(j > -1)
							str = str.substring(j+1);
						if(str == "Item")
							str = "ID";
						if(j > -1)
							str = "-"+str;
						parts[i] = str;
					}
				}
				params.sortBy = parts.join(",");
			}
			if(params.distinct){
				if(Array.isArray(params.distinct)){
					for(let j=0; j < params.distinct.length; j++){
						if(params.distinct[j] == "Item")
							params.distinct[j] = "ID";
					}
				}else if(params.distinct == "Item"){
					params.distinct = "ID";
				}
			}
		}
	}
	return idCol;
}

function tableToApiReformat(table, result){
	// tables for which the id column is named ID
	if(['artist', 'album', 'category', 'toc', 'daypart', 'queries', 'favs', 'locations', 'schedule', 'file', 'logs', 'orders', 'request', 'users'].includes(table) == true){
		result.forEach(function(item){ 
			if(item.ID){
				item.id = item.ID;
				delete item.ID;
			}
		});
	// tables for which the id column is named RID
	}else if(['category_item', 'meta', 'playlist', 'rest', 'task'].includes(table) == true){
		result.forEach(function(item){ 
			if(item.RID){
				item.id = item.RID;
				delete item.RID;
			}
			// translate parameter ID to tocID for some tables
			if(['meta', 'playlist', 'task'].includes(table) == true){
				if(item.ID){
					item.tocID = item.ID;
					delete item.ID;
				}
			}
		});
	}
}
// getFrom -> get/table{/id}?key1=, key2, ...
// distinct=column or columns to return with distinct clause
// table=toc
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
		if(params.Parent)
			params.Parent = locConf['prefix'] + params.Parent;
		libpool.getConnection((err, connection) => {
			// convert API params to table column names
			let idCol = apiToTableReformat(table, params);
			
			if(err){
				response.status(400);
				response.send(err.code);
				response.end();
			}else{
				let sort = params.sortBy;
				if(params.sortBy)
					delete params.sortBy;
				else
					sort = idCol;
				
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
								select = "SELECT DISTINCT "+libpool.escapeId(dstr);
							else
								select += ", "+libpool.escapeId(dstr);
						}
						select += " ";
					}else{
						if(hasWhiteSpace(dstr))
							// columns can't have white spaced... skip if it does
							select = "SELECT * ";
						else
							select = "SELECT DISTINCT "+libpool.escapeId(dstr)+" ";
					}
					delete params.distinct;
					
				}else{
					select = "SELECT * ";
				}
				
				let from = "FROM "+libpool.escapeId(locConf['prefix']+table)+" ";
				let where = "";
				let tail = "";
				
				if(dirs[4])
					where += "WHERE "+idCol+"="+libpool.escape(dirs[4])+" ";
				
				// additional "where" values
				let keys = Object.keys(params);
				let vals = Object.values(params);
				for(let i=0; i < keys.length; i++){
					let key = keys[i];
					if(hasWhiteSpace(key))
						// keys can't have white spaced... skip if it does
						continue;
					if(key == "range")	// range is handled by the restQueryRequest function
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
				restQueryRequest(connection, table, request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.
			}
		});
	}else{
		response.status(400);
		response.end();
	}
}

// setIn -> set/table{/id}?col1=, col2= ...
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
		if(request.session.permission == "studio"){	// studio permission is not allowed to make changes, all others are
			response.status(401);
			response.end();
			return;
		}
		// check for allowed tables
		if(['artist', 'album', 'category', 'locations', 'toc', 'meta', 'queries', 'rest', 'category_item', 'schedule', 'playlist', 'task', 'file'].includes(table) == false){
			response.status(400);
			response.end();
			return;
		}
		if(params.Parent)
			params.Parent = locConf['prefix'] + params.Parent;
		libpool.getConnection((err, connection) => {
			if(err){
				response.status(400);
				response.send(err.code);
				response.end();
			}else{
				// NOTE: client is responsable for updating Total Playlist duration, and update when changes are made.
				// after all moves, additions and deletions are done, use the get item's calcDur property (recalculated duration) 
				// to then update the item's duration property
				
				// modify parameters for table where columns and API keys don't match (id, RID, Item, etc.)
				apiToTableReformat(table, params);
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
									let insert = "INSERT INTO "+locConf['prefix']+"toc ";
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
		if(request.session.permission == "studio"){	// studio permission is not allowed to make changes, all others are
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
												connection.query("UPDATE IGNORE "+locConf['prefix']+"category_item SET Category = "+reassign+" WHERE Category = "+ID+";", function(err, results){
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
		}else if(table == 'playlist'){
			libpool.getConnection((err, connection) => {
				if(err){
					response.status(400);
					response.send(err.code);
					response.end();
				}else{
					connection.query("DELETE FROM "+locConf['prefix']+"playlist WHERE RID = "+ID+";", function(err, results){
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
		}else if(table == 'task'){
			libpool.getConnection((err, connection) => {
				if(err){
					response.status(400);
					response.send(err.code);
					response.end();
				}else{
					connection.query("DELETE FROM "+locConf['prefix']+"task WHERE RID = "+ID+";", function(err, results){
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
													connection.query("SELECT Hash, Path, Prefix, URL, Mount, Missing FROM "+locConf['prefix']+"file WHERE ID = "+ID+";", function(err, results){
														if(err){
															console.log("Failed to get library properties for file item #"+ID+", to delete the file."); 
														}else if(results && results.length){
															// require that the file is prefixed, for security reasons
															let properties = results[0];
															resolveFileFromProperties(properties).then(result => {
																if(properties.Missing == 0){
																	let fullpath = properties.Prefix + properties.Path;
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
// getLogs -> location=(name) or locID=(number), key1=, key2=, key3=, etc.
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
			if(params.location || params.locID){	// location specified
				let loc = params.location;
				delete params.location;
				let locID = params.locID;
				delete params.locID;
				let sort = "-Time";
				if(params.sortBy)
					delete params.sortBy;
					
				let select = "SELECT TIME(FROM_UNIXTIME("+locConf['prefix']+"logs.Time)) As TimeStr, "+locConf['prefix']+"logs.Name As Label, "+locConf['prefix']+"logs.* ";
				let from = "FROM ("+locConf['prefix']+"logs, "+locConf['prefix']+"locations) ";
				let where = "WHERE "+locConf['prefix']+"locations.Name = "+libpool.escape(loc)+" AND "+locConf['prefix']+"logs.Location = "+locConf['prefix']+"locations.ID ";
				if(locID){
					locID = parseInt(locID);
					from = "FROM ("+locConf['prefix']+"logs) ";
					where = "WHERE "+locConf['prefix']+"logs.Location = "+locID+" ";
				}
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
					if(key == "range")	// range is handled by the restQueryRequest function
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
				restQueryRequest(connection, "logs", request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.

			}else{	// no location given
				let sort = "Label";
				let select = "SELECT DISTINCT "+locConf['prefix']+"locations.Name AS Label, "+locConf['prefix']+"locations.ID AS ID, "+locConf['prefix']+"locations.Name AS Branch ";
				let from = "FROM ("+locConf['prefix']+"logs, "+locConf['prefix']+"locations) ";
				let where = "WHERE "+locConf['prefix']+"logs.Location = "+locConf['prefix']+"locations.ID ";
				let tail = ""; 
				
				restQueryRequest(connection, "location", request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.
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
					select += locConf['prefix']+"schedule.Day AS dbDay, ";
					select += locConf['prefix']+"schedule.Priority AS Priority, ";
					select += locConf['prefix']+"schedule.Month AS Month, ";
					select += locConf['prefix']+"schedule.Date AS Date, ";
					select += locConf['prefix']+"schedule.Minute AS Minute ";
					
					let from = "FROM ("+locConf['prefix']+"schedule, ";
					from += locConf['prefix']+"toc, ";
					from += locConf['prefix']+"hourmap) ";
					
					from += "LEFT JOIN "+locConf['prefix']+"locations ON ("+locConf['prefix']+"locations.Name = "+libpool.escape(params.location)+" AND "+locConf['prefix']+"schedule.Location = "+locConf['prefix']+"locations.ID) ";
					from += "LEFT JOIN "+locConf['prefix']+"rest ON ("+locConf['prefix']+"schedule.Item = "+locConf['prefix']+"rest.Item AND "+locConf['prefix']+"rest.Location = "+locConf['prefix']+"locations.ID) ";

					let where = "WHERE "+locConf['prefix']+"rest.Added IS NULL ";
					where += "AND "+locConf['prefix']+"schedule.Item = "+locConf['prefix']+"toc.ID ";
					where += "AND "+locConf['prefix']+"schedule.Fill > 0 ";
					where += "AND "+locConf['prefix']+"schedule.Hour = "+locConf['prefix']+"hourmap.Hour ";
					where += "AND ("+locConf['prefix']+"schedule.Location IS NULL OR "+locConf['prefix']+"schedule.Location = "+locConf['prefix']+"locations.ID) ";
					where += "AND "+locConf['prefix']+"schedule.Priority > 0 ";
					if(params.date){
						where += "AND ("+locConf['prefix']+"schedule.Day = 0 OR (";
						where += locConf['prefix']+"schedule.Day = (FLOOR((DATE_FORMAT("+libpool.escape(params.date)+",'%e')-1)/7)*7+(DATE_FORMAT("+libpool.escape(params.date)+", '%w')+8)) ) OR (";
						where += locConf['prefix']+"schedule.Day = ((DATE_FORMAT("+libpool.escape(params.date)+", '%w')+1)) ) ) ";
						where += "AND ("+locConf['prefix']+"schedule.Date = 0 OR "+locConf['prefix']+"schedule.Date = DATE_FORMAT("+libpool.escape(params.date)+", '%e') ) ";
						where += "AND ("+locConf['prefix']+"schedule.Month = 0 OR "+locConf['prefix']+"schedule.Month = DATE_FORMAT("+libpool.escape(params.date)+", '%c') ) ";
					}else{
						where += "AND ("+locConf['prefix']+"schedule.Day = 0 OR (";
						where += locConf['prefix']+"schedule.Day = (FLOOR((DATE_FORMAT(NOW(),'%e')-1)/7)*7+(DATE_FORMAT(NOW(), '%w')+8)) ) OR (";
						where += locConf['prefix']+"schedule.Day = ((DATE_FORMAT(NOW(), '%w')+1)) ) ) ";
						where += "AND ("+locConf['prefix']+"schedule.Date = 0 OR "+locConf['prefix']+"schedule.Date = DATE_FORMAT(NOW(), '%e') ) ";
						where += "AND ("+locConf['prefix']+"schedule.Month = 0 OR "+locConf['prefix']+"schedule.Month = DATE_FORMAT(NOW(), '%c') ) ";
					}
					let tail = "ORDER BY mapHour ASC, Minute ASC, Priority DESC ";
					restQueryRequest(connection, "schedule", request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.
					
				}else{
					// Insert Items
					let sort = "";
					let select = "SELECT DISTINCT "+locConf['prefix']+"schedule.Item AS ItemID, ";
					select += locConf['prefix']+"toc.Name AS Label, ";
					select += locConf['prefix']+"toc.Duration / 60 AS Duration, ";
					select += locConf['prefix']+"hourmap.Map AS mapHour, ";
					select += locConf['prefix']+"schedule.Hour AS dbHour, ";
					select += locConf['prefix']+"schedule.Day AS dbDay, ";
					select += locConf['prefix']+"schedule.Priority AS Priority, ";
					select += locConf['prefix']+"schedule.Month AS Month, ";
					select += locConf['prefix']+"schedule.Date AS Date, ";
					select += locConf['prefix']+"schedule.Minute AS Minute ";
					
					let from = "FROM ("+locConf['prefix']+"schedule, ";
					from += locConf['prefix']+"toc, ";
					from += locConf['prefix']+"hourmap) ";
					
					from += "LEFT JOIN "+locConf['prefix']+"locations ON ("+locConf['prefix']+"locations.Name = "+libpool.escape(params.location)+" AND "+locConf['prefix']+"schedule.Location = "+locConf['prefix']+"locations.ID) ";
					from += "LEFT JOIN "+locConf['prefix']+"rest ON ("+locConf['prefix']+"schedule.Item = "+locConf['prefix']+"rest.Item AND "+locConf['prefix']+"rest.Location = "+locConf['prefix']+"locations.ID) ";

					let where = "WHERE "+locConf['prefix']+"rest.Added IS NULL ";
					where += "AND "+locConf['prefix']+"schedule.Item = "+locConf['prefix']+"toc.ID ";
					where += "AND "+locConf['prefix']+"schedule.Fill = 0 ";
					where += "AND "+locConf['prefix']+"schedule.Hour = "+locConf['prefix']+"hourmap.Hour ";
					where += "AND ("+locConf['prefix']+"schedule.Location IS NULL OR "+locConf['prefix']+"schedule.Location = "+locConf['prefix']+"locations.ID) ";
					where += "AND "+locConf['prefix']+"schedule.Priority > 0 ";
					if(params.date){
						where += "AND ("+locConf['prefix']+"schedule.Day = 0 OR (";
						where += locConf['prefix']+"schedule.Day = (FLOOR((DATE_FORMAT("+libpool.escape(params.date)+",'%e')-1)/7)*7+(DATE_FORMAT("+libpool.escape(params.date)+", '%w')+8)) ) OR (";
						where += locConf['prefix']+"schedule.Day = ((DATE_FORMAT("+libpool.escape(params.date)+", '%w')+1)) ) ) ";
						where += "AND ("+locConf['prefix']+"schedule.Date = 0 OR "+locConf['prefix']+"schedule.Date = DATE_FORMAT("+libpool.escape(params.date)+", '%e') ) ";
						where += "AND ("+locConf['prefix']+"schedule.Month = 0 OR "+locConf['prefix']+"schedule.Month = DATE_FORMAT("+libpool.escape(params.date)+", '%c') ) ";
					}else{
						where += "AND ("+locConf['prefix']+"schedule.Day = 0 OR (";
						where += locConf['prefix']+"schedule.Day = (FLOOR((DATE_FORMAT(NOW(),'%e')-1)/7)*7+(DATE_FORMAT(NOW(), '%w')+8)) ) OR (";
						where += locConf['prefix']+"schedule.Day = ((DATE_FORMAT(NOW(), '%w')+1)) ) ) ";
						where += "AND ("+locConf['prefix']+"schedule.Date = 0 OR "+locConf['prefix']+"schedule.Date = DATE_FORMAT(NOW(), '%e') ) ";
						where += "AND ("+locConf['prefix']+"schedule.Month = 0 OR "+locConf['prefix']+"schedule.Month = DATE_FORMAT(NOW(), '%c') ) ";
					}
					let tail = "ORDER BY mapHour ASC, Minute ASC, Priority DESC ";
					restQueryRequest(connection, "schedule", request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.
				}
			}else{
				// location list
				let select = "SELECT DISTINCT "+locConf['prefix']+"locations.Name AS Label, "+locConf['prefix']+"locations.ID AS ID, "+locConf['prefix']+"locations.Name AS Branch ";
				let from = "FROM ("+locConf['prefix']+"schedule, "+locConf['prefix']+"locations) ";
				let where = "WHERE "+locConf['prefix']+"schedule.Location = "+locConf['prefix']+"locations.ID ";
				let sort = "Label";
				let tail = ""; 
				restQueryRequest(connection, "location", request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.
			}
		}
	});
}

// !searchFor -> type=(title,playlist,task,added,category,artist,album,comment,missing,rested), match=(type name match), key1=, key2=, ...
// where key is a type
// match=wind%hair (% triggers a like match)
// artist=ethan
// category=blue
// meta=key,value (parent is type table)
// added=yyyy-mm-dd
// missing=true/false (joins file table only)
// limit=10 (default for no limit)
// sortBy=Label or Duration (prepend - for descending)

function searchFor(request, response, params, dirs){
	if(params.type){
		let type = params.type;
		let sort = params.sortBy;
		libpool.getConnection((err, connection) => {
			if(err){
				response.status(400);
				response.send(err.code);
				response.end();
			}else{
				delete params.type;
				let table = type;
				let select;
				let from;
				let where = "";
				let tail = "";
				if(type == 'artist'){
					if(!sort)
						sort = "Label";
					select = "SELECT DISTINCT "+locConf['prefix']+"artist.Name AS Label, ";
					select += locConf['prefix']+"artist.ID AS ID, '"+type+"' AS qtype ";
					from = buildFromString(from, "artist");
					if(params.match){ 
						where = buildWhereString(where, "artist", "Name", params.match);
						delete params.match;
					}
					let parts = {from: from, where: where};
					let added = includeSearchKeys(parts, params);
					if(added){
						from = parts.from;
						where = parts.where;
						// if there are seach fields, we don't show all, only those that match
						from = buildFromString(from, "toc");
						from = buildFromString(from, "file");
						if(parts.subquery && parts.subquery.length)
							from = buildFromString(from, false, parts.subquery);
						where += "AND "+locConf['prefix']+"toc.Type = 'file' AND "+locConf['prefix']+"file.ID = "+locConf['prefix']+"toc.ID ";
						where += "AND "+locConf['prefix']+"artist.ID = "+locConf['prefix']+"file.Artist "; 
					}
					from += ") ";
					
				}else if(type == 'album'){
					if(!sort)
						sort = "Label";
					select = "SELECT DISTINCT "+locConf['prefix']+"album.Name AS Label, ";
					select += locConf['prefix']+"album.ID AS ID, '"+type+"' AS qtype ";
					from = buildFromString(from, "album");
					if(params.match){ 
						where = buildWhereString(where, "album", "Name", params.match);
						delete params.match;
					}
					let parts = {from: from, where: where};
					let added = includeSearchKeys(parts, params);
					if(added){
						from = parts.from;
						where = parts.where;
						// if there are seach fields, we don't show all, only those that match
						from = buildFromString(from, "toc");
						from = buildFromString(from, "file");
						if(parts.subquery && parts.subquery.length)
							from = buildFromString(from, false, parts.subquery);
						where += "AND "+locConf['prefix']+"toc.Type = 'file' AND "+locConf['prefix']+"file.ID = "+locConf['prefix']+"toc.ID ";
						where += "AND "+locConf['prefix']+"album.ID = "+locConf['prefix']+"file.Album "; 
					}
					from += ") ";
				}else if(type == 'title'){
					if(!sort)
						sort = "Label";
					table = "toc";
					select = "SELECT DISTINCT "+locConf['prefix']+"toc.Name AS Label, '"+type+"' AS qtype, ";
					select += locConf['prefix']+"toc.ID AS tocID, "+locConf['prefix']+"toc.ID AS ID, "+locConf['prefix']+"toc.Duration AS Duration ";
					from = buildFromString(from, "toc");
					where = "WHERE "+locConf['prefix']+"toc.Type = 'file' ";
					if(params.match){ 
						where = buildWhereString(where, "toc", "Name", params.match);
						delete params.match;
					}
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					if(parts.subquery && parts.subquery.length)
						from = buildFromString(from, false, parts.subquery);
					where = parts.where;
					from += ") ";
				}else if(type == 'task'){
					table = "toc";
					if(!sort)
						sort = "Label";
					select = "SELECT DISTINCT "+locConf['prefix']+"toc.Name AS Label, '"+type+"' AS qtype, ";
					select += locConf['prefix']+"toc.ID AS tocID, "+locConf['prefix']+"toc.ID AS ID, "+locConf['prefix']+"toc.Duration AS Duration ";
					from = buildFromString(from, "toc");
					where = "WHERE "+locConf['prefix']+"toc.Type = 'task' ";
					if(params.match){ 
						where = buildWhereString(where, "toc", "Name", params.match);
						delete params.match;
					}
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					if(parts.subquery && parts.subquery.length)
						from = buildFromString(from, false, parts.subquery);
					where = parts.where;
					from += ") ";
				}else if(type == 'category'){
					table = "category"
					if(!sort)
						sort = "Label";
					select = "SELECT DISTINCT "+locConf['prefix']+"category.Name AS Label, ";
					select += locConf['prefix']+"category.ID AS ID, '"+type+"' AS qtype ";
					from = buildFromString(from, "category");
					if(params.match){ 
						where = buildWhereString(where, "category", "Name", params.match);
						delete params.match;
					}
					let parts = {from: from, where: where};
					let added = includeSearchKeys(parts, params);
					if(added){
						from = parts.from;
						where = parts.where;
						// if there are seach fields, we don't show all, only those that match
						from = buildFromString(from, "toc");
						from = buildFromString(from, "category_item");
						if(parts.subquery && parts.subquery.length)
							from = buildFromString(from, false, parts.subquery);
						where += "AND "+locConf['prefix']+"category_item.Item = "+locConf['prefix']+"toc.ID ";
						where += "AND "+locConf['prefix']+"category.ID = "+locConf['prefix']+"category_item.Category "; 
					}
					from += ") ";
				}else if(type == 'playlist'){
					table = "toc";
					if(!sort)
						sort = "Label";
					select = "SELECT DISTINCT "+locConf['prefix']+"toc.Name AS Label, '"+type+"' AS qtype, ";
					select += locConf['prefix']+"toc.ID AS tocID, "+locConf['prefix']+"toc.ID AS ID, "+locConf['prefix']+"toc.Duration AS Duration ";
					from = buildFromString(from, "toc");
					where = "WHERE "+locConf['prefix']+"toc.Type = 'playlist' ";
					if(params.match){ 
						where = buildWhereString(where, "toc", "Name", params.match);
						delete params.match;
					}
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					if(parts.subquery && parts.subquery.length)
						from = buildFromString(from, false, parts.subquery);
					where = parts.where;
					from += ") ";
				}else if(type == 'added'){
					table = "toc";
					if(!sort)
						sort = "Label";
					select = "SELECT DISTINCT DATE(FROM_UNIXTIME("+locConf['prefix']+"toc.Added)) AS Label, '"+type+"' AS qtype ";
					from = buildFromString(from, "toc");
					where = "";
					if(params.match){ 
						where = buildWhereDate(where, "toc", "Added", params.match);
						delete params.match;
					}
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					if(parts.subquery && parts.subquery.length)
						from = buildFromString(from, false, parts.subquery);
					where = parts.where;
					from += ") ";
				}else if(type == 'comment'){
					table = "toc";
					if(!sort)
						sort = "Label";
					select = "SELECT DISTINCT "+locConf['prefix']+"toc.Tag AS Label, '"+type+"' AS qtype ";
					from = buildFromString(from, "toc");
					where = "WHERE "+locConf['prefix']+"toc.Tag IS NOT NULL "
					if(params.match){ 
						where = buildWhereString(where, "toc", "Tag", params.match);
						delete params.match;
					}
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					if(parts.subquery && parts.subquery.length)
						from = buildFromString(from, false, parts.subquery);
					where = parts.where;
					from += ") ";
				}else if(type == 'missing'){
					table = "toc";
					if(!sort)
						sort = "Label";
					select = "SELECT DISTINCT "+locConf['prefix']+"toc.Name AS Label, '"+type+"' AS qtype, ";
					select += locConf['prefix']+"toc.ID AS tocID, "+locConf['prefix']+"toc.ID AS ID, "+locConf['prefix']+"toc.Duration AS Duration ";
					from = buildFromString(from, "toc");
					from = buildFromString(from, "file");
					where = "WHERE "+locConf['prefix']+"toc.Type = 'file' AND "+locConf['prefix']+"file.ID = "+locConf['prefix']+"toc.ID ";
					where += "AND "+locConf['prefix']+"file.Missing = 1 ";
					if(params.match){ 
						where = buildWhereString(where, "toc", "Name", params.match);
						delete params.match;
					}
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					if(parts.subquery && parts.subquery.length)
						from = buildFromString(from, false, parts.subquery);
					where = parts.where;
					from += ") ";
				}else if(type == 'rested'){
					table = "locations";
					if(!sort)
						sort = "Label";
					select = "SELECT DISTINCT "+locConf['prefix']+"locations.Name AS Label, '"+type+"' AS qtype ";
					from = buildFromString(from, "toc");
					from = buildFromString(from, "rest");
					from = buildFromString(from, "locations");
					where = "WHERE "+locConf['prefix']+"toc.ID = "+locConf['prefix']+"rest.Item ";
					where += "AND "+locConf['prefix']+"locations.ID = "+locConf['prefix']+"rest.Location "
					if(params.match){ 
						where = buildWhereString(where, "locations", "Name", params.match);
						delete params.match;
					}
					let parts = {from: from, where: where};
					includeSearchKeys(parts, params);
					from = parts.from;
					if(parts.subquery && parts.subquery.length)
						from = buildFromString(from, false, parts.subquery);
					where = parts.where;
					from += ") ";
				}else{
					response.status(400);
					response.end();
					connection.release(); // return the connection to pool
					return;
				}
				restQueryRequest(connection, table, request, response, select, from, where, tail, sort, 0); // NOTE: connection will be returned to the pool.
				
			}
		});
	}else{
		// no type... generate type list
		let list = [{Type: "Artist", qtype: "artist"}, {Type: "Album", qtype: "album"}, {Type: "Title", qtype: "title"}, 
		{Type: "Category", qtype: "category"}, {Type: "Playlist", qtype: "playlist"}, {Type: "Task", qtype: "task"}, 
		{Type: "Added", qtype: "added"}, {Type: "Comment", qtype: "comment"}, {Type: "Missing", qtype: "missing"},
		{Type: "Rested", qtype: "rested"}];
		restObjectRequest(response, list);
	}
}

async function getItemObject(ID, params){  // params.resolve, params.locname, params.histlimit, params.histdate
	if(!ID)
		return 400;
	let connection;
	let results;
	let final;
	try{
		connection = await asyncGetDBConnection();
		if(!connection)
			return 400;
	}catch(err){
		return 400;
	}
	try{
		results = await asyncQuery(connection, "SELECT * FROM "+locConf['prefix']+"toc WHERE ID = "+ID+";");
	}catch(err){
		connection.release();
		return 304;
	}
	if(results[0]){
		final = results[0];
		let type = results[0].Type;
		if(type.length){
			if(hasWhiteSpace(type)){
				//type Error
				connection.release();
				return 304;
			}
			let subresults;
			// get categories
			let query = "SELECT Name, ID, RID, Added FROM "+locConf['prefix']+"category_item ";
			query += "LEFT JOIN "+locConf['prefix']+"category ON "+locConf['prefix']+"category.ID = "+locConf['prefix']+"category_item.Category ";
			query += "WHERE "+locConf['prefix']+"category_item.Item = "+ID+";"
			try{
				subresults = await asyncQuery(connection, query);
			}catch(err){
				connection.release();
				return 304;
			}
			final["categories"] = subresults;
			// get rested
			query = "SELECT Name, ID, RID, Added FROM "+locConf['prefix']+"rest ";
			query += "LEFT JOIN "+locConf['prefix']+"locations ON "+locConf['prefix']+"locations.ID = "+locConf['prefix']+"rest.Location ";
			query += "WHERE "+locConf['prefix']+"rest.Item = "+ID+";"
			try{
				subresults = await asyncQuery(connection, query);
			}catch(err){
				connection.release();
				return 304;
			}
			final["rest"] = subresults;
			// get custom (metadata)
			query = "SELECT Property, Value, RID FROM "+locConf['prefix']+"meta ";
			query += "WHERE Parent = '"+locConf['prefix']+"toc' AND ID = "+ID+";"
			try{
				subresults = await asyncQuery(connection, query);
			}catch(err){
				connection.release();
				return 304;
			}
			final["meta"] = subresults;
			if(params && params.locname && params.locname.length){
				query = "SELECT ID FROM "+locConf['prefix']+"locations WHERE Name = "+libpool.escape(params.locname)+";"
				try{
					subresults = await asyncQuery(connection, query);
				}catch(err){
					connection.release();
					return 304;
				}
				if(subresults.length){
					let LocID = subresults[0].ID;
					// get schedule for location
					query = "SELECT Day, Date, Month, Hour, Minute, Fill, Priority, ID AS RID FROM "+locConf['prefix']+"schedule ";
					query += "WHERE Location = "+LocID+" AND Item = "+ID+" ";
					query += "ORDER BY Hour ASC, Minute ASC, Priority DESC;";
					try{
						subresults = await asyncQuery(connection, query);
					}catch(err){
						connection.release();
						return 304;
					}
					final["sched"] = subresults;
					// get play history for location
					query = "SELECT FROM_UNIXTIME(Time) AS Played FROM "+locConf['prefix']+"logs ";
					query += "WHERE (Added & 0x3) = 0 AND Item = "+ID+" AND Location = "+LocID+" ";
					if(params.histdate){
						query += "AND Time < UNIX_TIMESTAMP("+libpool.escape(params.histdate)+") ";
					}
					query += "ORDER BY Time DESC "
					if(params.histlimit){
						if(typeof params.histlimit === 'string')
							params.histlimit = parseInt(params.histlimit);
						query += "LIMIT "+params.histlimit+";";
					}else
						query += "LIMIT 10;"
					try{
						subresults = await asyncQuery(connection, query);
					}catch(err){
						connection.release();
						return 304;
					}
					for(let i=0; i<subresults.length; i++) // flatten array
						subresults[i] = subresults[i].Played;
					final["history"] = subresults;
				}
			}
			// get type rows
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
				try{
					subresults = await asyncQuery(connection, query);
				}catch(err){
					connection.release();
					return 304;
				}
				if(subresults[0]){
					if(params && params.resolve){
						// handle file type resolve option
						await resolveFileFromProperties(subresults[0]);
					}
					final[type] = subresults[0];
					connection.release();
					return final;
				}else{
					// no file properties!
					connection.release();
					return 404;
				}
				
				
			}else if(type == "playlist"){
				try{
					subresults = await asyncQuery(connection, "SELECT Position, RID, Property, Value FROM "+locConf['prefix']+type+" WHERE ID = "+ID+" ORDER BY Position ASC;");
				}catch(err){
					connection.release();
					return 304;
				}
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
				connection.release();
				return final;

			}else if(type == "task"){
				try{
					subresults = await asyncQuery(connection, "SELECT RID, Property, Value FROM "+locConf['prefix']+type+" WHERE ID = "+ID+" ORDER BY Property ASC;");
				}catch(err){
					connection.release();
					return 304;
				}
				final[type] = subresults;
				connection.release();
				return final;
			}else{
				try{
					subresults = await asyncQuery(connection, "SELECT * FROM "+locConf['prefix']+libpool.escapeId(type)+" WHERE ID = "+ID+";");
				}catch(err){
					connection.release();
					return 304;
				}
				final[type] = subresults;
				connection.release();
				return final;
			}
		}else{
			//type Error
			connection.release();
			return 304;
		}
	}else{
		// No result!
		connection.release();
		return 404;
	}

}

// item/ID{?resolve=(1,0)}		gets full item info, If resolve is true (default 0 <false>), file item paths and 
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
		let resolve = false;
		if(params.resolve)
			resolve = true;
		getItemObject(ID, params).then(result => {
			if(typeof result === 'number'){
				response.status(result);
				response.end();
			}else{
				response.status(201);
				response.json(result);
				response.end();
			}
		});
	}else{
		response.status(400);
		response.end();
	}
}

function calcLPLDuration(request, response, dirs){
	let ID = 0;
	if(dirs[3]  && (ID = parseInt(dirs[3], 10))){
		libpool.getConnection((err, connection) => {
			if(err){
				response.status(400);
				response.send(err.code);
				response.end();
				return;
			}else{
				connection.query("SELECT Position, Property, Value FROM "+locConf['prefix']+"playlist WHERE ID = "+ID+" ORDER BY Position ASC;", function(err, results){
					if(err){
						response.status(304);
						response.send(err.code);
						connection.release();
						response.end();
						return;
					}else{
						let re = [];
						let pos = -1;
						let obj = {};
						for(let i=0; i < results.length; i++){
							if(results[i].Position != pos){
								pos = results[i].Position;
								obj = {};
								re[pos] = obj;
							}
							obj[results[i].Property] = results[i].Value;
						}
						let dur = calcFPLDuration(re, 0);
						// check for permission to update
						if(request.session.permission == "studio"){	// studio permission is not allowed to make changes, all others are
							response.status(401);
							response.end();
							connection.release(); // return the connection to pool
							return;
						}
						// Update Duration in the item's toc properties
						let query = "UPDATE "+locConf['prefix']+"toc";
						query += " SET Duration = "+dur;
						query += " WHERE ID ="+libpool.escape(ID)+";";
						connection.query(query, function (err, res, fields) {
							if(err){
								response.status(400);
								response.send(err.code);
							}else{
								response.status(201);
								response.json(dur);
							}
							connection.release(); // return the connection to pool
							response.end();
						});
					}
				});
			}
		});
	}else{
		response.status(400);
		response.end();
	}
}

function calcFPLDuration(plprops, offsetAdj){
	// this also addes/updates the Offset property + offsetAdj if it doesn't exist or offsetAdj is not zero
	let duration = 0.0;
	let last = 0.0;
	let offset = 0.0;
	let calDur = 0.0;
	let next = 0.0;
	if(plprops && plprops.length){
		// use Duration, Offset, SegIn, SegOut and FadeOut properties
		for(let i=0; i<plprops.length; i++){
			if(plprops[i]){
				// possibly readjust the start time
				if(plprops[i].Offset)
					duration = parseFloat(plprops[i].Offset); // override time to last item with start of this one
					
				last = duration;
				// and add the next item's offset or this item's duration for the end time
				if(((i+1) < plprops.length) && plprops[i+1] && (plprops[i+1].Offset)){
					next = parseFloat(plprops[i+1].Offset) // use offset of next item, if present
					// add a duration if missing
					if((plprops[i].Duration == undefined) || (plprops[i].Duration == null) || (parseFloat(plprops[i].Duration) == 0)){
						let dur = next - last;
						// adjust for seg values, etc.
						if(plprops[i].SegIn)
							dur = dur +parseFloat(plprops[i].SegIn);
						if(plprops[i].FadeOut)
							dur = dur + parseFloat(plprops[i].FadeOut);
						else if(plprops[i].SegOut)
							dur = dur + parseFloat(plprops[i].SegOut);
						else
							dur = dur + 5.0; // default segout time
						plprops[i].Duration = dur;
					}
					duration = next;
				}else if(plprops[i].Duration){
					duration = duration + parseFloat(plprops[i].Duration);
					if(plprops[i].SegIn)
						duration = duration - parseFloat(plprops[i].SegIn);
					if(plprops[i].FadeOut)
						duration = duration - parseFloat(plprops[i].FadeOut);
					else if(plprops[i].SegOut)
						duration = duration - parseFloat(plprops[i].SegOut);
					else
						duration = duration - 5.0; // default segout time
				}
				// Add an offset value, if missing
				if(offsetAdj || (plprops[i].Offset == undefined) || (plprops[i].Offset == null) || (parseFloat(plprops[i].Offset) == 0))
					plprops[i].Offset = last + offsetAdj;
			}
		}
	}
	return duration;
}


function createFilePlaylistBuffer(name, properties, offsetAdj){
	// properties is an object with over all peroperties as key value pairs,
	// and a single "playlist" array for each playlist item.
	// playlist items are themselves, objects of key value pairs.
	
	// calculate total duration and set Offset property, if missing
	let duration = calcFPLDuration(properties.playlist, offsetAdj);
	let plist = properties.playlist;
	// set header
	let fdata = "Type\tfilepl\nName\t" + name + "\nDuration\t" + duration + "\n";
	fdata += "Fingerprint\t" + properties.Fingerprint + "\nRevision\t1.0\n";
	// build item list
	for(let i = 0; i < plist.length; i++){
		let keys = Object.keys(plist[i]);
		let vals = Object.values(plist[i]);
		fdata += "\n";
		for(let j=0; j<keys.length; j++){
			//exclude old properties we no longer care about
			if(['MD5', 'FPL', 'Missing', 'FileID', 'Memory'].includes(keys[j]) == false){
				fdata += keys[j] + "\t" + vals[j] + "\n";
			}
		}
	}
	return fdata;
}

function cueSheetTimeString(seconds){
	let negative = false;
	if(seconds < 0){
		seconds = -seconds;
		negative = true;
	}
	let mins = Math.floor(seconds / 60);
	let rem = seconds - mins * 60;
	let secs = Math.floor(rem);
	let frames = Math.floor((rem - secs) * 75.0);
	
	let result = "";
	if(negative)
		result = "-";
	
	if(mins < 10)
		result += "0";
	result += mins;
	
	if(secs < 10)
		result += ":0";
	else
		result += ":";
	result += secs;
	
	if(frames < 10)
		result += ":0";
	else
		result +=":";
	result += frames;
	
	return result;
}

function createCuePlaylistBuffer(name, properties, offsetAdj){
	// properties is an object with over all peroperties as key value pairs,
	// and a single "playlist" array for each playlist item.
	// playlist items are themselves, objects of key value pairs.
	
	// calculate total duration and set Offset property, if missing
	let duration = calcFPLDuration(properties.playlist, offsetAdj);
	let plist = properties.playlist;
	// set header
	let fdata = "FILE \"" + name.replaceAll("\"", "'") + "\"\n";

	for(let i = 0; i < plist.length; i++){
		let entry = plist[i];
		if(i < 9)
			fdata += "\tTRACK 0";
		else
			fdata += "\tTRACK ";
		fdata += (i+1) + " AUDIO\n";
		let title = entry.Name;
		let perf = entry.Artist;
		fdata += "\t\tTITLE \"";
		if(title)
			fdata += title.replaceAll("\"", "'");
		fdata += "\"\n\t\tPERFORMER \"";
		if(perf)
			fdata += perf.replaceAll("\"", "'");
		fdata += "\"\n";
		
		fdata += "\t\tINDEX 01 " + cueSheetTimeString(entry.Offset) + "\n";
	}
	return fdata;
}

async function pathForID(connection, ID){
	let result = false;
	try{
		result = await asyncQuery(connection, "SELECT Path, Prefix, Hash, URL, Mount, Missing FROM "+locConf['prefix']+"file WHERE ID = "+ID+";");
	}catch(err){
		return "";
	}
	if(result[0]){
		if(result && Array.isArray(result) && ((result[0].Mount && result[0].Mount.length) ||
			(result[0].Prefix && result[0].Prefix.length && result[0].Path && (result[0].Path.length > 3)))){
			// require that the file is prefixed, or has an old Mount for security reasons
			let properties = result[0];
			await resolveFileFromProperties(properties);
			if(properties.Missing == 0)
				return properties.Prefix + properties.Path;
		}
	}
	return "";
}

function getHashForFileID(response, dirs){
	let ID = 0;
	if(dirs[3]){
		// get ID and make sure it is a number.
		ID = parseInt(dirs[3], 10);
	}
	if(!ID){
		response.status(400);
		response.end();
		return;
	}
	hashForID(ID).then(result => {
			if(typeof result === 'number'){
				response.status(result);
				response.end();
			}else{
				response.status(200);
				response.send(result);
				response.end();
			}
		});
}

async function hashForID(ID){
	let result = false;
	let connection;
	try{
		connection = await asyncGetDBConnection();
		if(!connection)
			return 400;
	}catch(err){
		return 400;
	}
	try{
		result = await asyncQuery(connection, "SELECT Path, Prefix, Hash, URL, Mount, Missing FROM "+locConf['prefix']+"file WHERE ID = "+ID+";");
	}catch(err){
		connection.release();
		return 404;
	}
	if(result[0]){
		if(result && Array.isArray(result) && result[0].Prefix && result[0].Prefix.length && result[0].Path && result[0].Path.length){
			// require that the file is prefixed, for security reasons
			let properties = result[0];
			properties.Hash = ""; // force recalculate of hash
			await resolveFileFromProperties(properties);
			if(properties.Missing == 0){
				connection.release();
				return properties.Hash;
			}
		}
	}
	connection.release();
	return 404;
}

async function pathForProperties(connection, props){
	await resolveFileFromProperties(props);
	if(props.Missing == 0)
		// found it from the given properties
		return props.Prefix + props.Path;
	if(props.ID){
		// didn't find it. It has a database ID: try using that to get the path via current properties
		let result = await pathForID(connection, props.ID);
		return result;
	}
	// can't find the file
	return "";
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
		libpool.getConnection((err, connection) => {
			if(err){
				response.status(400);
				response.send(err.code);
				response.end();
				return;
			}else{
				connection.query("SELECT * FROM "+locConf['prefix']+"toc WHERE ID = "+ID+";", function(err, tresults){
					if(err){
						response.status(404);
						response.send(err.code);
						connection.release();
						response.end();
						return;
					}
					if(tresults && Array.isArray(tresults) && tresults.length){
						if(params.export == "json"){
							getItemObject(ID, params).then(result => {
								connection.release();
								response.type("application/octet-stream");
								response.attachment("item"+ID+".json");
								response.send(JSON.stringify(result, null, 3));
								response.end();
							});
						// check for type = file or playlist and handle accordingly
						}else if(tresults[0].Type == "file"){
							pathForID(connection, ID).then(result => {
								connection.release();
								if(result.length){
									response.header('Cache-Control', 'no-cache');
									if(params.save && (params.save != 0))
										response.download(result);
									else
										response.sendFile(result);
									return;
								}else{
									response.status(400);
									response.end();
									return;
								}
							});
						}else if(tresults[0].Type == "playlist"){
							connection.query("SELECT Position, Property, Value FROM "+locConf['prefix']+"playlist WHERE ID = "+ID+" ORDER BY Position ASC;", function(err, subresults){
								if(err){
									response.status(304);
									response.send(err.code);
									connection.release();
									response.end();
									return;
								}else{
									let final = tresults[0];
									if(dbFingerprint.length)
										final["Fingerprint"] = dbFingerprint;
									else
										final["Fingerprint"] = "0";
									let re = [];
									let pos = -1;
									for(let i=0; i < subresults.length; i++){
										if(subresults[i].Position != pos){
											pos = subresults[i].Position;
											re[pos] = {};
										}
										re[pos][subresults[i].Property] = subresults[i].Value;
									}
									final["playlist"] = re;
									if(!params.export || (params.export == "fpl")){
										let data = createFilePlaylistBuffer(final.Name, final, 0.0);
										response.set({
											'Cache-Control': 'no-cache',
											'Content-Type': 'text/plain',
											'Content-Length': Buffer.byteLength(data, 'utf8'),
											'Content-Disposition': 'attachment; filename=' + final.Name + '.fpl'
										});
										response.status(201);
										response.send(data);
										connection.release();
										response.end();
									}else if(params.export == "fplmedia"){
										var archive = archiver('zip');
										let data = createFilePlaylistBuffer(final.Name, final, 0.0);
										response.attachment(final.Name+".zip");
										archive.pipe(response);
										// add the .fpl data as a file to the zip root
										archive.append(data, {name: final.Name + ".fpl"});
										// add file media to the media subdirectory
										let dir = final.Name + "_media/";
										let itemsProcessed = 0;
										let rpt = "";
										final.playlist.forEach((item, index, array) => {
											pathForProperties(connection, item).then(result => {
												if(result.length){
													archive.file(result, {name: dir + path.basename(result)});
													rpt += "item "+index+" added to media directory\n";
												}else{
													rpt += "item "+index+" failed to add to media directory\n";
												}
												itemsProcessed++;
												if(itemsProcessed === array.length){
													connection.release();
													// add the report as a file to media directory
													archive.append(rpt, {name: dir + "report.txt"});
													archive.finalize();
												}
											});
										});
									}else if(params.export == "cue"){
										let data = createCuePlaylistBuffer(final.Name, final, 0.0);
										response.set({
											'Cache-Control': 'no-cache',
											'Content-Type': 'text/plain',
											'Content-Length': Buffer.byteLength(data, 'utf8'),
											'Content-Disposition': 'attachment; filename=' + final.Name + '.cue'
										});
										response.status(201);
										response.send(data);
										connection.release();
										response.end();
									}else{
										// bad export type
										response.status(404);
										connection.release();
										response.end();
										return;
									} 
								}
							});
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
								if(result.errno){
									response.status(404);
									response.send(result.sqlMessage);
									connection.release();
									response.end();
									return;
								}else{
									response.status(201);
									response.json(result);
									connection.release();
									response.end();
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

function fplLineInterp(line, last, donecb){
	// params inherited from processFileLines function via this.params
	let pos = line.indexOf("\t");
	let key = "";
	let val = "";
	if(pos > -1){
		key = line.substring(0, pos);
		if(line.length > pos)
			val = line.substring(pos+1);
	}
	if(key.length){
		if(key === "type")
			val = val.toLowerCase();	// enforce lower case requitrment for tyep value
		if(this.params.index){	// playlist item level properties
			if((["ArtistID", "AlbumID", "ID"].includes(key)) && !this.params.dbMatch){
				// ignore these properties if db fingerprint doesn't match
				donecb();
				return;
			}
			if(!this.params.props.playlist[this.params.index-1])
				this.params.props.playlist[this.params.index-1] = [];
			this.params.props.playlist[this.params.index-1].push({Property: key, Value: val});
		}else{		// top level playlist properties
			this.params.props[key] = val;
			if((key === "Fingerprint") && (dbFingerprint === val))
				this.params.dbMatch = true;
		}
	}else if(line.length == 0){
		// empty line...
		this.params.index++;
		if(this.params.props.playlist == undefined)
			this.params.props.playlist = [];
	}
	donecb();
}

async function filePLGetMetaForFile(filepl, props){
	var index = 0;	// start at zero for parent level.  1, 2,.. are item indexes in the playlist
	var status = false;
	let params = {index: 0, dbMatch: false, props: props};
	try{
		status = await processFileLines(filepl, {}, params, fplLineInterp);
	}catch(err){
		status = false;
	}
	if(status == true){
		return props;
	}else{
		return false
	}
}

async function getFileMeta(tmpDirFileName, fullpath){
	const trans = {isrc: "ISRC", Duration: "Duration", title: "Name", artist: "Artist", album: "Album", "track number": "Track", composer: "Composer", audio: "Audio", Seekable: "Seekable"};
	let obj = {};
	let full = false;
	if(tmpDir.length || fullpath){
		let fpath = tmpDirFileName;
		if(!fullpath)
			fpath = tmpDir+tmpDirFileName;
		let res = getFilePrefixPoint(fpath);
		if(res.prefix.length)
			res.path = res.prefix + "/" + res.path; // double slash between prefix and path as prefix separator in URL, if any
		let URL = url.pathToFileURL(res.path).href;
		let params = [fpath];
		// check for playlist types
		let fd = await openFile(fpath);
		if(fd > -1){
			let buf = new Buffer(11);
			let result = await readFileIntoBuffer(fd, 0, 11, buf, 0);
			closeFileDescriptor(fd);
			if(result){
				let str = buf.toString();
				if(str == "Type\tfilepl"){
					// we have an audiorack playlist file
					try{
						full = await filePLGetMetaForFile(fpath, obj);
					}catch(err){
						full = false;
					}
					if(full){
						full.Type = "playlist";	// change type to toc playlist so it displays in client
						full.URL = URL;
						return full;	// all results
					}else
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
				obj.Name = path.basename(fpath, path.extname(fpath));
			obj.Hash = await getFileHash(fpath);
			let pre = getFilePrefixPoint(fpath);
			obj.Prefix = pre.prefix;
			obj.Path = pre.path;
			obj.URL = URL;
			return obj;	// OK -> send results
		}else
			return {};	// no audio -> results are empty
	}else
		return obj;	// failed-> results are empty
}

async function mkMediaDirs(mdir){
	const zeroPad = (num, places) => String(num).padStart(places, '0')
	let dir = mediaDir;
	let fpath = dir;
	if(mdir && mdir.length)
		dir = fileSettings['mediaDir-'+mdir];
	if(dir && dir.length){
		if(dir && dir.length && (dir.substr(-1) != '/')){
			// if trailing slash if not present, add it, and add year/month/day to the path as well
			dir += '/';
			let present = new Date();
			var year = present.getFullYear();
			var month = present.getMonth()+1;
			var date = present.getDate();
			fpath = dir+zeroPad(year, 2)+"/"+zeroPad(month, 2)+"/"+zeroPad(date, 2)+"/";		// Format: YYYY/MM/DD/
		}
		// recursively create multiple directories
		return await mkRecursiveDir(fpath); // this returns the full path to the deepest directory, with trailing slash, or empty if we failed
	}
	return "";
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
	if(!name || (name.length == 0))
		name = "[NONE]";
	// search...
	let query = "SELECT ID FROM "+locConf['prefix']+table+" WHERE Name = "+libpool.escape(name)+";";
	try{
		result = await asyncQuery(connection, query);
	}catch(err){
		console.log("findAddNameTable err="+err);
		result = false;
	}
	if(result && Array.isArray(result) && result.length && result[0].ID)
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

async function importFileIntoLibrary(fpath, params, fullpath){
	let pass = {id:0, status:-2}; // status=-3 file copy failed, -2 bad file, -1 error, 0 skipped, 1 new/added, 2 cat update existing, 3 replaced existing, 4 fixed existing
	let ID = 0;
	let result = {};
	let meta = {};
	let dupmode = 0;
	let dupInfo = false;
	
	if(tmpDir.length || fullpath){
		// Convert dup parameter to number: skip,catset,replace(delete old),update(if missing)
		if(params.dup === undefined) dupmode = 0;
		else if((params.dup === "catset") && params.catid && (params.catid > 0)) dupmode = 2;
		else if(params.dup === "replace") dupmode = 3;
		else if(params.dup === "update") dupmode = 4;
		meta = await getFileMeta(fpath, fullpath);
		if(meta && Object.keys(meta).length){
			let conn = false;
			// valid meta data.  Handle according to type
			if((meta.Name == undefined) || (meta.Name.length == 0))
				meta.Name = path.basename(fpath, path.extname(fpath));
				
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
					if(dupmode != 5)
						dupmode = 3;
				}else{
					conn.release();
					pass.status = -2;
					return pass;
				}
			}
			
			if(meta.Type == "playlist"){		// handle playlist file import
				let dbMatch = false;
				if(dbFingerprint == meta.Fingerprint)
					dbMatch = true;	// itemID, artistID albumID, etc., are valid... keep them in the conversion
				let dur = 0.0;
				if(meta.Duration)
					dur = parseFloat(meta.Duration);	// use the top level Duration, if provided
				if(dur == 0.0)
					dur = calcFPLDuration(meta.filepl, 0);
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
					if(meta.playlist && meta.playlist.length){
						for(let pos=0; pos<meta.playlist.length; pos++){
							let entry = meta.playlist[pos];
							for(let i=0; i<entry.length; i++){
								let prop = entry[i].Property;
								if(!dbMatch && ["ArtistID", "AlbumID", "ID"].includes(prop))	// skip these if different library
									continue; // arserver will use remaining properties to try to find item
								let insert = "INSERT INTO "+locConf['prefix']+"playlist (ID, Position, Property, Value) "
								let setstr = "VALUES ("+ID;
								setstr = buildInsValString(setstr, pos);
								setstr = buildInsValString(setstr, prop);
								setstr = buildInsValString(setstr, entry[i].Value);
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
				
			}else if(meta.Type == "file"){	// handle media file import
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
					setstr += ", UNIX_TIMESTAMP(CURDATE()));";
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
					await resolveFileFromProperties(dupInfo);
					if(!dupInfo.Missing){
						let ok = await deleteFile(dupInfo.Prefix + dupInfo.Path);
						if(ok)
							console.log("replace file: deleteed original file " + dupInfo.Prefix + dupInfo.Path);
						else
							console.log("replace file: failed to delete original file " + dupInfo.Prefix + dupInfo.Path);
					}
				}
				if(dupmode != 2){
					let newPath = fpath;
					// add new file to media dir and refernce it in the file properties
					if(!fullpath){ // only copy file if not using full path and instead using temp dir relative path.
						newPath = await mkMediaDirs(params.mdir);
						if(newPath.length){
							fpath = tmpDir+fpath;
							newPath = await copyFileToDir(fpath, newPath);
							// attempt to copy an associated fileplaylist as well, if it exists.
							await copyFileToDir(fpath+".fpl", newPath);
						}
						if(newPath.length == 0){
							// copy failed!
							await asyncRollback(conn);
							conn.release();
							pass.status = -3;
							return pass;
						}
					}
					let pre = getFilePrefixPoint(newPath);
					meta.Prefix = pre.prefix;
					meta.Path = pre.path;
					// Old OSX: Mount -> prefix + mountName (first dir in path) 
					// if prefix = (empty), path=/some/path/etc, mountName = /, Mount = /
					// if prefix = /pre/fix/of/ path=some/path/etc, mountName = Some, Mount = /Volumes/Some
					// Note: first char of mountName is upper-case.
					// URL -> 
					// if prefix = (empty), URL = path
					// if prefix = /pre/fix/of/, URL = Mount (from above) + /path/etc (everything in path including and beyond first /)
					let Mount = "/";
					let urlPath = pre.path;
					if(pre.prefix.length){
						let idx = pre.path.indexOf("/");
						if(idx > 0){
							let mountName = pre.path.substring(0, idx);
							mountName = mountName[0].toUpperCase() + mountName.substr(1);
							Mount = "/Volumes/" + mountName;
							urlPath = Mount + pre.path.substring(idx);
						}
					}
					meta.URL = url.pathToFileURL(urlPath).href;
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
						if(meta.ISRC && (meta.ISRC.length > 0)){
							// Insert the ISRC if that is included in the file's meta data
							insert = "INSERT INTO "+locConf['prefix']+"meta "
							colstr = "";
							setstr = "";
							colstr = buildInsColumnString(colstr, "ID");
							setstr = buildInsValString(setstr, ID);
							colstr = buildInsColumnString(colstr, "Property");
							setstr = buildInsValString(setstr, "ISRC");
							colstr = buildInsColumnString(colstr, "Parent");
							setstr = buildInsValString(setstr, locConf['prefix']+"toc");
							colstr = buildInsColumnString(colstr, "Value");
							setstr = buildInsValString(setstr, meta.ISRC);
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
						}
					}else{
						// update existing file row, leave segs, fades, vol, etc alone.
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
						if(meta.ISRC && (meta.ISRC.length > 0)){
							// Update the ISRC if that is included in the file's meta data
							query = "UPDATE "+locConf['prefix']+"meta ";
							query += "SET Value = "+libpool.escape(meta.ISRC);
							query += " WHERE ID = "+ID;
							query += " AND Property = 'ISRC'";
							query += " AND Parent = '"+locConf['prefix']+"toc';";
							try{
								result = await asyncQuery(conn, query);
							}catch(err){
								// roll back
								await asyncRollback(conn);
								conn.release();
								pass.status = -1;
								return pass;
							}
							if(!result.affectedRows){
								// no rows updated... try inserting a new row
								let insert = "INSERT INTO "+locConf['prefix']+"meta "
								let colstr = "";
								let setstr = "";
								colstr = buildInsColumnString(colstr, "ID");
								setstr = buildInsValString(setstr, ID);
								colstr = buildInsColumnString(colstr, "Property");
								setstr = buildInsValString(setstr, "ISRC");
								colstr = buildInsColumnString(colstr, "Parent");
								setstr = buildInsValString(setstr, locConf['prefix']+"toc");
								colstr = buildInsColumnString(colstr, "Value");
								setstr = buildInsValString(setstr, meta.ISRC);
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
							}
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
				let catID = parseInt(params.catid);
				if(catID){
					result = await addToCatID(conn, ID, catID);
					if(!result){
						await asyncRollback(conn);
						conn.release();
						pass.status = -1;
						return pass;
					}
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
	let fpath = "";
	let tailIdx = request.path.search(dirs[2]+"/");
	let tailLen = request.path.length-tailIdx-(dirs[2].length)-1;
	if((tailIdx < 0) || (tailLen > 0)){
		fpath = request.path.substring(tailIdx + dirs[2].length+1);
		fpath = decodeURIComponent(fpath);
		if(request.session.loggedin != true){
			response.status(401);
			response.end();
			return;
		}
		getFileMeta(fpath).then(result => {
			if(result && Object.keys(result).length){
				if(result.Type === "file"){
					let file = result;
					result = {Name: file.Name};
					delete file.Name;
					result.Duration = file.Duration;
					delete file.Duration;
					result.Type = file.Type;
					delete file.Type;
					result.file = file;
				}
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
	let fpath = "";
	let tailIdx = request.path.search(dirs[2]+"/");
	let tailLen = request.path.length-tailIdx-(dirs[2].length)-1;
	if((tailIdx < 0) || (tailLen > 0)){
		fpath = request.path.substring(tailIdx + dirs[2].length+1);
		fpath = decodeURIComponent(fpath);
		if((request.session.permission != "admin") &&  (request.session.permission != "manage")){
			response.status(401);
			response.end();
			return;
		}
		importFileIntoLibrary(fpath, params).then(result => {
			if(result){
				response.status(200); 
				response.json(result);
				// result.status=-3 file copy failed, -2 bad file, -1 error, 0 skipped, 1 new/added, 2 cat update existing, 3 replaced existing, 4 fixed existing
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

function sqlInitLineInterp(line, last, donecb){
	// params is inherited from processFileLines function via this.params
	line = line.replaceAll("[prefix]", this.params.prefix);
	line = line.replaceAll("[!loc-id]", "[loc-id]");
	line = line.replaceAll("[!loc-ids]", "[loc-ids]");
	line = line.replaceAll("[!prefix]", "[prefix]");
	line = line.replaceAll("[!thisID]", "[thisID]");
	line = line.replaceAll("\\n", "\n");
	asyncQuery(this.params.connection, line).then(result => {
		donecb();
	}).catch(function(err){
		this.params.err = err;	// so we can check for errors down the line
		donecb(false);	// finishes, but doesn't note error down the line.
	}.bind({params: this.params}));
}

async function db_initialize(conn, params){
	let versionStr = "";
	let iniFile = "";
	let result = false;
	
	try{
		result = await asyncQuery(conn, "USE "+params.database+";");
		result = await asyncQuery(conn, "SELECT Value FROM "+params.prefix+"info WHERE Property = 'Version'");
		versionStr = result[0].Value;
	}catch(err){
		versionStr = "";
	}
	if(versionStr && versionStr.length){
		// this is an upgrade to an existing database
		iniFile += supportDir + "/" + params.type + versionStr + ".dbi";
	}else{
		// create new database if one with the given name doesn't already exist
		try{
			result = await asyncQuery(conn, "CREATE DATABASE IF NOT EXISTS "+params.database+";");
			iniFile += supportDir + "/" + params.type + ".dbi";
		}catch(err){
			return {error: err};
		}
		// use the new db
		try{
			result = await asyncQuery(conn, "USE "+params.database+";");
		}catch(err){
			return {error: err};
		}
	}
	
	params.connection = conn; // this is how we pass the db-connection to the sqlInitLineInterp callback
	try{
		result = await processFileLines(iniFile, {}, params, sqlInitLineInterp);
	}catch(err) {
		result = err;
	}
	if(result != true){
		if(versionStr && versionStr.length && (result.errno == -2)) // trap missing file... no further upgardes.
			// if we can't open the file, maybe we are already up to date to the latest version.
			// return current version
			return {version: versionStr, oldVersion: versionStr};
		return {error: status};
	}else if(params.err)
		return {error: params.err};
		
	if(versionStr && versionStr.length){
		// re-enter for another go-around so we upgrade
		// all they way to the latest version.
		return db_initialize(conn, params);
	}
	// get the new version number string
	try{
		result = await asyncQuery(conn, "SELECT Value FROM "+params.prefix+"info WHERE Property = 'Version'");
		return {version: result[0].Value, oldVersion: "0.0"};
	}catch(err){
		return {error: err};
	}
}

function handleDbInit(request, response, params){
	if(request.session.permission != "admin"){
		response.status(401);
		response.end();
		return;
	}
	if(supportDir && supportDir.length){
		// setup singleton db connection: the db connection pool, and given db might not exist yet.
		if(params.type && params.host && params.user && params.password && params.database && (hasWhiteSpace(params.database) == false) && params.prefix){
			if(params.type == "mysql"){
				var conn = mysql.createConnection({
					host: params.host,
					port: params.port,
					user: params.user,
					password: params.password,
					dateStrings: true
				});
				conn.connect(function(err) {
					if(err){
						response.status(500);
						response.send(err);
						response.end();
					}else{
						conn.beginTransaction(function (err, result) {
							if(err){
								response.status(500);
								response.send(err);
								response.end();
							}else{
								db_initialize(conn, params).then(result => {
									if(result.version){
										conn.commit(function(){
											conn.end();
										});
										response.status(200); 
										response.json(result);
										response.end();
									}else{
										conn.rollback(function(){
											conn.end();
											//Failure
										});
										response.status(400);
										response.send(result.error);
										response.end();
									}
								});
							}
						});
					}
				});
			}else{
				response.status(400);
				response.end("unsupported database server type");
			}
		}else{
			response.status(400);
			response.end("missing or bad parameter");
		}
	}else{
		response.status(400);
		response.end("Support director path not configured");
	}
}

function getPrefix(request, response, params){
	if(params.path && params.path.length){
		let fpath = params.path;
		let result = getFilePrefixPoint(fpath);
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

function getTmpMediaURL(request, response, params){
	if(params.path && params.path.length){
		let fpath = tmpDir+params.path;
		let result = getFilePrefixPoint(fpath);
		if(result){
			fpath = encodeURI("file://"+result.prefix+"//"+result.path);
			response.status(200); 
			response.send(fpath);
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

async function dbFileSync(connection, mark){
	let num = 0;
	let msgtmr = false;
	let statistics = {running: true, remaining: 0, missing: 0, updated: 0, lost: 0, found: 0, error: 0};
	let msg = {dbsync: statistics};
	sse.postSSEvent(false, JSON.stringify(msg));
	let results = false;
	try{
		results = await asyncQuery(connection, "SELECT ID, Hash, Path, Prefix, URL, Mount, Missing FROM "+locConf['prefix']+"file ORDER BY ID;");
	}catch(err){
		console.log("Failed to get file list for dbSync."); 
		console.log(err); 
		return err;
	}
	if(results && Array.isArray(results) && results.length){
		// create timer to send 5 second progress messages 
		msgtmr = setInterval(() => {
				let msg = {dbsync: statistics};
				sse.postSSEvent(false, JSON.stringify(msg));
			}, 5000);
		let properties = false;
		num = results.length;
		statistics.remaining = num;
		for(let i = 0; i < num; i++){
			if(!dbSyncRunning)
				// abort requested
				break;
			properties = results[i];
			let stat = await resolveFileFromProperties(properties);
			if(stat){
				if(stat == 1){
					// updated
					statistics.updated++
				}else if(stat == 2){
					// lost
					statistics.lost++;
					if(!mark)
						stat = 0; // do not mark missing files
				}else if(stat == 3){
					// found
					statistics.found++
				}
				if(stat){
					// update record in library
					let query = "UPDATE "+locConf['prefix']+"file ";
					query += "SET Hash = "+libpool.escape(properties.Hash)+" ";
					query += ", Path = "+libpool.escape(properties.Path)+" ";
					query += ", Prefix = "+libpool.escape(properties.Prefix)+" ";
					query += ", URL = "+libpool.escape(properties.URL)+" ";
					query += ", Mount = "+libpool.escape(properties.Mount)+" ";
					query += ", Missing = "+properties.Missing+" ";
					query += "WHERE ID = "+properties.ID+";";
					try{
						await asyncQuery(connection, query);
					}catch(err){
						statistics.error++;
					}
				}
			}
			if(properties.Missing){
				// lost, or not found
				statistics.missing++;
			}
			statistics.remaining--;
		}
		// stop progress timer/messages
		clearInterval(msgtmr);
		// send final message
		statistics.running = false;
		let msg = {dbsync: statistics};
		sse.postSSEvent(false, JSON.stringify(msg));
	}
}

function startDbFileSync(request, response, params){
	if(request.session.permission != "admin"){
		response.status(401);
		response.end();
		return;
	}
	if(!dbSyncRunning){
		libpool.getConnection((err, connection) => {
			if(err){
				response.status(500);
				response.send(err.code);
				response.end();
			}else{
				let mark = false;
				if(params.mark)
					mark = true;
				dbSyncRunning = dbFileSync(connection, mark).then(() => {
					// done running: clear the dbSyncRunning variable
					dbSyncRunning = false;
					connection.release();
				});
				response.status(200);
				response.end();
			}
		});
	}else{
		// already running
		response.status(400);
		response.end("Already running");
	}
}

function abortDbFileSync(request, response){
	if(request.session.permission != "admin"){
		response.status(401);
		response.end();
		return;
	}
	if(dbSyncRunning){
		// clear the dbSyncRunning variable... task will stop on next loop iteration
		dbSyncRunning = false;
		response.status(200);
		response.end();
	}else{
		// alrady stopped
		response.status(400);
		response.end("Already stopped");
	}
}

function sleep(ms) {
	return new Promise((resolve) => {
		setTimeout(resolve, ms);
	});
}

async function crawlDirectory(connection, fpath, pace, add, passStat){
	let files = false;
	let finfo = false;
	let statistics = false;
	let msgtmr = false; 
	const fsPromises = fs.promises;
	if(passStat)
		statistics = passStat;
	else
		statistics = {running: true, curPath: "", checked: 0, found: 0, error: 0};
	try{
		files = await fsPromises.readdir(fpath);
	}catch(err){
		if(passStat == false){
			// send final message
			statistics.running = false;
			let msg = {dbsearch: statistics};
			sse.postSSEvent(false, JSON.stringify(msg));
		}
		return;
	}
	if(passStat == false){
		let msg = {dbsearch: statistics};
		sse.postSSEvent(false, JSON.stringify(msg));
		// we are the root directory.  Start status report timer
		msgtmr = setInterval(() => {
				let msg = {dbsearch: statistics};
				sse.postSSEvent(false, JSON.stringify(msg));
			}, pace * 5 * 1000);
	}
	// loop through dir entries
	for(let i=0; i<files.length; i++){
		if(!dbSearchRunning)
				// abort requested
				break;
		// wait pace seconds
		await sleep(pace * 1000);
		let file = files[i];
		let thisFile = path.join(fpath, file);
		try{
			finfo = await fsPromises.stat(thisFile);
		}catch (err){
			// skip any entry that creates an error
			continue;
		}
		if(finfo.isDirectory()){
			// recurse up down directory tree.  We pass statistics so they can be updates and so recusive calls 
			// know not to start a status  message time... it's already running
			await crawlDirectory(connection, thisFile, pace, add, statistics);
		}else{
			// check file for matching hash and missing status in library
			statistics.curPath = thisFile;
			statistics.checked++;
			let fhash = await getFileHash(thisFile);
			if(fhash && fhash.length){
				let matches = false;
				try{
					matches = await asyncQuery(connection, "SELECT ID FROM "+locConf['prefix']+"file WHERE Hash = "+libpool.escape(fhash)+" AND Missing > 0 ORDER BY ID;");
				}catch(err){
					console.log(err); 
					return err;
				}
				if(matches && Array.isArray(matches) && matches.length){
					let pre = getFilePrefixPoint(thisFile);
					let newURL = url.pathToFileURL(thisFile).href;
					// Old OSX: Mount -> Prefix + mountName (first dir in path) 
					// if fpath=/some/path, mountName = /
					// if fpath=some/path, mountName = Some, Mount = /Volumes/Some
					let Mount = "/";
					if(pre.prefix.length){
						let idx = pre.path.indexOf("/");
						if(idx > 0){
							let mountName = pre.path.substring(0, idx);
							mountName = mountName[0].toUpperCase() + mountName.substr(1);
							Mount = "/Volumes/" + mountName;
						}
					}
					let query = "UPDATE "+locConf['prefix']+"file ";
					query += "SET Path = "+libpool.escape(pre.path)+" ";
					query += ", Prefix = "+libpool.escape(pre.prefix)+" ";
					query += ", URL = "+libpool.escape(newURL)+" ";
					query += ", Mount = "+libpool.escape(Mount)+" ";
					query += ", Missing = 0 ";
					query += "WHERE ID = "+matches[0].ID+";";
					try{
						let update = await asyncQuery(connection, query);
						if(update.changedRows)
							statistics.found++;
					}catch(err){
						statistics.error++;
						console.log(err);
					}
				}else if(add){ 
					// not missing... see if it's new and add it if it is 
					let stat = await importFileIntoLibrary(thisFile, {}, true); // no params defauls to skip duplicate mode
					if(stat.status == 1)
						statistics.found++;
				}
			}
		}
	}
	if(msgtmr){
		// stop progress timer/messages
		clearInterval(msgtmr);
		
		// send final message
		statistics.running = false;
		statistics.curPath = "";
		let msg = {dbsearch: statistics};
		sse.postSSEvent(false, JSON.stringify(msg));
	}
}

function startDbFileSearch(request, response, params){
	if(request.session.permission != "admin"){
		response.status(401);
		response.end();
		return;
	}
	if(!dbSearchRunning){
		libpool.getConnection((err, connection) => {
			if(err){
				response.status(500);
				response.send(err.code);
				response.end();
			}else{
				let pace = 1.0;
				let add = false;
				if(params.add)
					add = true;
				if(params.pace && params.pace.length){
					pace = parseFloat(params.pace);
					if(pace == 0.0){
						response.status(400);
						response.end("pace can not be zero.");
						return;
					}
				}
				let fpath = mediaDir;
				if(params.path && params.path.length)
					fpath = params.path;
				if(fpath.length == 0){
					response.status(400);
					response.end("no default mediaDir path setting, or specific path set");
				}
				// add trailing slash if not present
				if(fpath.substr(-1) != '/')
					fpath += '/';
				dbSearchRunning = crawlDirectory(connection, fpath, pace, add, false).then(() => {
					// done running: clear the dbSearchRunning variable
					dbSearchRunning = false;
					connection.release();
				});
				response.status(200);
				response.end();
			}
		});
	}else{
		// already running
		response.status(400);
		response.end("Already running");
	}
}

function abortDbFileSearch(request, response){
	if(request.session.permission != "admin"){
		response.status(401);
		response.end();
		return;
	}
	if(dbSearchRunning){
		// clear the dbSearchRunning variable... task will stop on next loop iteration
		dbSearchRunning = false;
		response.status(200);
		response.end();
	}else{
		// alrady stopped
		response.status(400);
		response.end("Already stopped");
	}
}

module.exports = {
	configure: function (config) {
		supportDir = "/opt/audiorack/support";
		mediaDir = "";
		tmpDir = "";
		if(process.platform == "darwin")
			prefix_list = ["/Volumes/","/private/var/automount/Network/","/Network/"];		// OSX prefix list
		else
			prefix_list = ["/mnt/","/media/*/","/run/user/*/gvfs/*share="];	// all other POSIX prefix list
		fileSettings = config['files'];
		if(fileSettings){
			let prx_list = fileSettings['prefixes'];
			if(prx_list && Array.isArray(prx_list) && prx_list.length)
				prefix_list = prx_list;
			if(fileSettings['tmpMediaDir'] && fileSettings['tmpMediaDir'].length){
				tmpDir = fileSettings['tmpMediaDir'];
				// add trailing slash if not present
				if(tmpDir.substr(-1) != '/')
					tmpDir += '/';
			}
			if(fileSettings['mediaDir'] && fileSettings['mediaDir'].length){
				mediaDir = fileSettings['mediaDir'];
			}
			if(fileSettings['supportDir'] && fileSettings['supportDir'].length){
				supportDir = fileSettings['supportDir'];
				// add trailing slash if not present
				if(supportDir.substr(-1) != '/')
					supportDir += '/';
			}
		}
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
						port: libc['port'],
						password: libc['password'],
						database: libc['database'],
						dateStrings: true
					});
				}
			}
			console.log('Lib Configuration done.');
		}
		if(libpool)
			getLibraryFingerprint();
		return true;
	},
	
	getLibraryLocations: async function () {
		let result;
		let conn = await asyncGetDBConnection();
		if(!conn){
			return [];
		}
		try{
			result = await asyncQuery(conn, "SELECT Name FROM "+locConf['prefix']+"locations ORDER BY Name;");
		}catch(err){
			conn.release();
			return [];
		}
		conn.release();
		return result;
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
		if(dirs[2] == 'get'){
			getFrom(request, response, params, dirs);	// /get/table/{ID/}?column=value1&...
		}else if(dirs[2] == 'set'){
			setIn(request, response, params, dirs);	// /set/table/{ID/}?column=value1&... If ID is excluded, a new row is created
		}else if(dirs[2] == 'delete'){
			deleteID(request, response, params, dirs);	// delete/table/ID?reassign=id&... will reassign related items that contain 
																		// this property as specified, or fail if not.
																		// remove=1 will attempt to delete an associated file.
		}else if(dirs[2] == 'logs'){
			getLogs(request, response, params);			// /logs{?location=loc-name}{&datetime=YYYY-MM-DD-HH:MM}{&column=value1&...} 
																	// Range is from specified date/time to start of that day
																	//	today assumed if no date specified
																	// ?added=0 assumed (played only) if added not specified
																	// /logs alone returns list of location names that have log entries
		}else if(dirs[2] == 'sched'){
			getSched(request, response, params);			// /sched{?location=loc-name}{&fill=1}{&date=YYYY-MM-DD} 
																		// today assumed if no date specified
																		// fill = 0 assumed if not specified
																		// /sched alone returns list of location names that have schedules entries
		}else if(dirs[2] == 'browse'){
			searchFor(request, response, params, dirs);	// /browse{?type=resultType}{&match=name}{&other-type1=value1&...}
																		// /browse alone returns a list of types, less the "item" type
		}else if(dirs[2] == 'item'){		// item/ID{?resolve=(1,0)}		
													// gets full item info, If resolve is true (default 0), file item paths and 
													// missing will be re-writen making use of local prefix search settings.
													// Use separate get queries for last played/history and schedule entries
			getItem(request, response, params, dirs);
		}else if(dirs[2] == 'hash'){		// hash/ID
			getHashForFileID(response, dirs);
		}else if(dirs[2] == 'pldurcalc'){				// pldurcalc/ID -> returns the calculated duration from the actual list in seconds
																	// and updates this duration in the item's properties
			calcLPLDuration(request, response, dirs);
		}else if(dirs[2] == 'download'){
			getDownload(request, response, params, dirs);	// /download/ID	triggers a download of the file associated with the file type item
																	//			?save=1 to save the file instead of showing contents in page
																	//			?export=[fpl(default),fplmedia,cue] convert playlist to type, 
																	//			&save=1 is assumed and &offset=sec is optional to add sec 
																	//			to start times of items in the list.
																	//			?export=json to download a json file for any type, with schedule and 
																	//			history (last 100) for all locations, or as specified by params accepted by
																	//			item/ID above. Again, &save=1 is assumed.
		}else if(dirs[2] == 'query'){
			executeQuery(request, response, params, dirs);	// /query/ID?select=[selval1, selval2,..]&prompt=[promptval1, promptval2,..]
																			//		runs the specified query number, replacing macros [select(...)],
																			//		and [prompt(...)] text with the parameter array values in sequence as 
																			//		they occur. And replacing [loc-id] with it's single parameter.  And
																			//		finally, replacing [prefix] with the database table refix setting value.
																			//		It is the clients job to ensure that matching parameters for macros are set.
		}else if(dirs[2] == 'info'){
			getFileInfo(request, response, params, dirs);	// /info/filename{/possible/sub/directory/filename}
																			// NOTE: file name is within the temporary media directory.
		}else if(dirs[2] == 'import'){
			importFile(request, response, params, dirs);	// /import/filename{/possible/sub/directory/filename}{?catid=id-of-cat}
																		//						{&dup=[skip,catset,replace(delete old),update(if missing)]}
																		//						{&id=[itemID to replace}
																		// note: skip assumed if dup is not specified
																		// note: if id is set, type of new and origin must match.  For files, only 
																		// 		file properties and duration are changed, with the new file replacing
																		//			the old at a new default location, or specified:
																		//						{mdir=[dirName]} where files setting mediDir-dirName is used.
		}else if(dirs[2] == 'getprefix'){
			getPrefix(request, response, params);			// /getprefix?path=the/path/to/find/prefix/of/if/any
		}else if(dirs[2] == 'tmpmediaurl'){
			getTmpMediaURL(request, response, params);	// /tmpmediaurl?path=/file/path/in/tmpMediaDir
		}else if(dirs[2] == 'dbinit'){
			handleDbInit(request, response, params);		// initdb?type=mysql&host=thehostadr&user=dbuser&password=thepassword&database=dbname&prefix=tableprefix (ar_ is typical)
																		// port=port-number is optional
		}else if(dirs[2] == 'dbsync'){
			startDbFileSync(request, response, params);		// /dbsync?mark=1 starts a dbSync process running in the background
																// if mark is true (nonzero), files that can't be found will be marked as missing
																// otherwise fixed or found files will be updated, and lost files ignored. 
		}else if(dirs[2] == 'synchalt'){
			abortDbFileSync(request, response);		// stops a dbSync process running in the background
		}else if(dirs[2] == 'crawl'){
			startDbFileSearch(request, response, params);		// /crawl?path=the/path/to/crawl&pace=0.5{&add=1}
																				// pace is in seconds, and defaults to 1.0
																				// if no path is specified, the mediaDir setting path is used.
																				// add=1 specifies that found audio items are to be added to 
																				// the library if they are not already present based on hash code.
		}else if(dirs[2] == 'crawlhalt'){
			abortDbFileSearch(request, response);		// stops a dbFileSearch process running in the background
		}else{
			response.status(400);
			response.end();
		}
	}
};

