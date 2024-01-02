/***** Watchable Variable Class *****/

class watchableValue {
	constructor(value) {
		this.value = value;
		this.prior = null;
		this.cblist = [];
	}
	
	getValue() { return this.value; }
	
	getPrior() { return this.prior; }

	setValue(value, forceCallbacks){
		if(forceCallbacks || (this.value !== value)){	// does not look inside complex object values
			this.prior = this.value;
			this.value = value;
			this.cblist.forEach(function(item){
				item(value);
			});
		}
	}
	
	registerCallback(callback){ 
		let index = this.cblist.indexOf(callback);
		if(index == -1)
			this.cblist.push(callback); 
	}
	
	removeCallback(callback){
		let index = this.cblist.indexOf(callback);
		if(index > -1)
			delete this.cblist[index];
	}
}

/***** Global Variables *****/

includeScript("vumeter.js");

var infoWidth = "450px";
var cred = new watchableValue(false);
var locName = new watchableValue(false);
var locationID;
var studioName = new watchableValue("");
var sseData = {};
var browseData;
var mngLocList;
var confData = {};
var browseSort = "Label";
var browseType = new watchableValue("");
var browseTypeList = false;
var filesPath = false;
var filesList;
var stashList;
var itemProps = false;
var flatPlist = false;
var curDrag = null;
var stSaveSetTimer = null;
var stSaveConTimer = null;
var catListCache = new watchableValue(false);
var locListCache = new watchableValue(false);
var artListCache = new watchableValue(false);
var albListCache = new watchableValue(false);
var mediaListCache = new watchableValue(false);

/***** Utility functions *****/

function setStash(list){
	localStorage.setItem("stash", JSON.stringify(list));
}

function getStash(){
	let result;
	let list = localStorage.getItem("stash");
	if(list)
		result = JSON.parse(list);
	if(!result)
		result = [];
	return result; 
}

function setStorageLoc(locID){
	localStorage.setItem("location", locID);
}

function getStorageLoc(){
	return localStorage.getItem("location");
}

function setStorageMidiControl(name){
	localStorage.setItem("midiControl", name);
}

function getStorageMidiControl(){
	return localStorage.getItem("midiControl");
}

function quoteattr(s){
	if(s){
		let result = new Option(s).innerHTML;
		return result.replace(/'/g, '&apos;').replace(/"/g, '&quot;'); // handle single/double quotes too.
	}else
		return "";
}

function dateToISOLocal(date){
	let offsetMs = date.getTimezoneOffset() * 60 * 1000;
	let msLocal =  date.getTime() - offsetMs;
	let dateLocal = new Date(msLocal);
	let iso = dateLocal.toISOString();
	let isoLocal = iso.slice(0, 19);
	return isoLocal;
}

function unixTimeToDateStr(ts){
	if(ts){
		let millis = ts * 1000;
		let dateObj = new Date(millis);
		const options = { year: 'numeric', month: 'long', day: 'numeric' };
		return dateObj.toLocaleDateString(undefined, options);
	}else
		return "";
}

function unixTimeToTimeStr(ts){
	if(ts){
		let millis = ts * 1000;
		let dateObj = new Date(millis);
		return dateObj.toLocaleTimeString();
	}else
		return "";
}

function timeParse(str){
	// this does not handle a negative sign
	if(str.length){
		let sec = 0.0;
		let parts = str.split(':');
		for(let i = 0; i<parts.length; i++){
			sec = sec * 60;
			sec += parseFloat(parts[i]);
		}
		return sec;
	}else
		return 0;
}

function timeFormat(timesec, noDP){
	timesec = parseFloat(timesec);
	if(isNaN(timesec))
		return "";
	else{
		let negative = false;
		if(timesec < 0){
			timesec = -timesec;
			negative = true;
		}
		let days = Math.floor(timesec / 86400);
		let rem = timesec - days * 86400;
		let hrs = Math.floor(rem / 3600);
		rem = rem - hrs * 3600;
		let mins = Math.floor(rem / 60);
		rem = rem - mins * 60;
		let secs = Math.floor(rem);
		let frac = Math.floor((rem - secs) * 10);
		let result = "";
		if(negative)
			result = "-";
		if(days){
			result += days;
			if(hrs < 10)
				result += ":0";
			else
				result += ":";
		}
		if(hrs){
			result += hrs;
			if(mins < 10)
				result += ":0";
			else
				result += ":";
		}
		result += mins;
		if(secs < 10)
			result += ":0";
		else
			result += ":";
			result += secs;
		if(!noDP)
			result += "." + frac;
		return result;
	}
}

function timeFormatNoDP(timesec){
	return timeFormat(timesec, 1);
}

function updateListDuration(plprops){
	// plprops is a flat array of objects.
	let duration = 0.0;
	let last = 0.0;
	let offset = 0.0;
	let calDur = 0.0;
	let next = 0.0;
	if(plprops && plprops.length){
		// use Duration, Offset, SegIn, SegOut and FadeOut properties
		for(let i=0; i<plprops.length; i++){
			// possibly readjust the start time
			if(plprops[i].Offset)
				duration = parseFloat(plprops[i].Offset); // override time to last item with start of this one
				
			last = duration;
			// and add the next items offset or this item's duration for the end time
			if(((i+1) < plprops.length) && (plprops[i+1].Offset)){
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
		}
	}
	return duration;
}

function findPropObjInArray(array, name, value){
	if(array && array.length){
		for(let i=0; i<array.length; i++){
			let obj = array[i];
			let prop = obj[name];
			if(prop && (prop === value))
				return obj;
		}
	}
	return null;
}

function flatClone(src){
	let target = {};
	let keys = Object.keys(src);
	for(let i=0; i<keys.length; i++ ){
		let key = keys[i];
		let val = src[key];
		target[key] = val;
	}
	return target;
}

function linToDBtext(lin){
	if(lin <= 0.00001)
		return "Mute";
	else{
		let db = Math.round(20.0 * Math.log10(lin));
		return db + "dB";
	}
}

function faderToLin(val){
	val = Math.pow(val, 4);
	if(val <= 0.00001)
		val = "0.0";
	if(val > 5.1)
		val = 5.1;
	return val;
}

function linToFader(lin){
	val = Math.pow(lin, 0.25);
	if(val < 0.056)
		val = "0.0";
	if(val > 1.5)
		val = 1.5;
	return val;
}

/***** fetch functions from http API *****/

function includeScript(file) {
	let script  = document.createElement('script');
	script.src  = file;
	script.type = 'text/javascript';
	script.defer = true;
	document.getElementsByTagName('head').item(0).appendChild(script);
}

async function fetchContent(url, options){
	let response;
	try{
		response = await fetch(url, options);
	}catch(err){
		return err;
	}
	if(response.status == 401){
		let wres;
		try{
			wres = await fetch("who");
		}catch(err){
			return err;
		}
		if(wres.status == 401)
			// not logged in... show login page
			logOut();
	}else if(response.ok)
		return response;
	else{
		let msg = await response.text();
		if(msg && msg.length){
			let obj = {statusText: msg, status: response.status, ok: response.ok};
			return obj;
		}
	}
	return false;
}

async function loadElement(url, element, refdes){
	// NOTE: optional refdes STRING value is used to replace all occurances of $refdes in loaded content.
	let resp = await fetchContent(url);
	if(resp instanceof Response){
		if(resp.ok){
			let html = await resp.text();
			if((refdes != undefined) && (refdes.length))
				html = html.replaceAll("$refdes", refdes);
			element.innerHTML = html;
			return false;
		}
	}
	return resp;
}

async function checkLogin(){
	let response;
	try{
		response = await fetch("who");
	}catch(err){
		return err;
	}
	if(response.status == 401){
		return false;
	}else if(response.ok)
		return await response.json();
	return false;
}

async function logOut(){
	cred.setValue(false);
	try{
		await fetch("unauth");
	}catch(err){
		return err;
	}
	// reload nav panel, with change is nav options for being logged out.
	let err = await loadElement("nav", document.getElementById("navtab"));
	if(err)
		return err;
	document.getElementById('studioAdmin').innerHTML = "";
	showTabElement(document.getElementById('navlogin'), 'login');
	return false;
}

async function getCatList(){
	let resp;
	resp = await fetchContent("library/get/category?sortBy=Name");
	if(resp){
		if(resp.ok){
			let list = await resp.json();
			catListCache.setValue(list, true);
		}else{
			alert("Got an error fetching categories from server.\n"+resp.statusText);
		}
	}else if(cred.getValue()){
		alert("Failed to fetch categories from the server.");
	}
}

async function getArtistList(){
	let resp;
	resp = await fetchContent("library/get/artist?sortBy=Name");
	if(resp){
		if(resp.ok){
			let list = await resp.json();
			artListCache.setValue(list, true);
		}else{
			alert("Got an error fetching artists from server.\n"+resp.statusText);
		}
	}else{
		alert("Failed to fetch artists from the server.");
	}
}

async function getAlbumList(){
	let resp;
	resp = await fetchContent("library/get/album?sortBy=Name");
	if(resp){
		if(resp.ok){
			let list = await resp.json();
			albListCache.setValue(list, true);
		}else{
			alert("Got an error fetching albums from server.\n"+resp.statusText);
		}
	}else{
		alert("Failed to fetch albums from the server.");
	}
}

async function getNameForCat(catID){
	let resp;
	if(catID){
		resp = await fetchContent("library/get/category?id="+catID);
		if(resp){
			if(resp.ok){
				let result = await resp.json();
				if(result.length)
					return result[0].Name;
			}else{
				alert("Got an error fetching category name from server.\n"+resp.statusText);
				return "unknown";
			}
		}
		alert("Failed to fetch  category name from the server.");
		return "unknown";
	}else
		return "unknown";
}

async function getLocList(){
	let resp;
	resp = await fetchContent("library/get/locations?sortBy=Name");
	if(resp){
		if(resp.ok){
			let list = await resp.json();
			locListCache.setValue(list, true);
			locationID = getStorageLoc();
			let name = false;
			for(let i = 0; i<list.length; i++){
				if(list[i].id == locationID){
					name = list[i].Name;
					break;
				}
			}
			if(name)
				locName.setValue(name);
		}else{
			alert("Got an error fetching location list from server.\n"+resp.statusText);
		}
	}else if(cred.getValue()){
		alert("Failed to fetch location list from the server.");
	}
}

async function getMediaLocs(){
let resp;
	resp = await fetchContent("getconf/files");
	if(resp){
		if(resp.ok){
			let list = await resp.json();
			let newList = [];
			for(let i = 0; i<list.length; i++){
				if(list[i].id.indexOf("mediaDir-") == 0)
					newList.push(list[i].id.slice(9));
			}
			mediaListCache.setValue(newList, true);
		}else if(cred.getValue()){
			alert("Got an error fetching media location list from server.\n"+resp.statusText);
		}
	}else if(cred.getValue()){
		alert("Failed to fetch media location list from the server.");
	}
}

/***** Navigation  Functions *****/

async function dropClick(evt){
	let id = evt.currentTarget.getAttribute("data-childdiv");
	let cb = evt.currentTarget.getAttribute("data-showcb");
	let content = document.getElementById(id);
	if(content.style.display === "block")
		content.style.display = "none";
	else{
		if(cb)
			await window[cb](evt);	// call the update callback function prior to showing
		content.style.display = "block";
	}
}

async function loginSubmit(event){
	event.preventDefault();
	let response;
	let formData = new FormData(event.currentTarget);
	let plainFormData = Object.fromEntries(formData.entries());
	try{
		response = await fetch("auth", {
			method: 'POST',
			body: JSON.stringify(plainFormData),
			headers: {
				"Content-Type": "application/json",
				"Accept": "application/json"
			}
		});
	}catch(err){
		alert("Caught an error, login failed: "+err);
		return err;
	}
	if(response.ok)
		startupContent();
	else
		alert("login failed: try a different username and/or password");
	return false;
}

function locMenuChange(event){
	let element = event.currentTarget;
	locName.setValue(element.value);
	if(element.selectedOptions && element.selectedOptions.length){
		locationID = element.selectedOptions[0].getAttribute("data-id");
		setStorageLoc(locationID);
	}
}

function clickAccordType(evt, cb, param){
	let target = evt.target;
	/* Toggle between adding and removing the "active" class,
	to highlight the button that controls the panel */
	if(target.classList.contains("active"))
		target.classList.remove("active");
	else
		target.classList.add("active");
	/* Toggle between hiding and showing the active panel */
	let panel = target.nextElementSibling;
	if(panel.style.display === "flex") {
		panel.style.display = "none";
	}else{
		panel.style.display = "flex";
		cb(panel, param);
	}
}

function selectAccordType(evt, cb, param){
	// Open target, close syblings
	let target = evt.target;
	/* Toggle between adding and removing the "active" class,
	to highlight the button that controls the panel */
	if(target.classList.contains("active"))	// already selected
		return;
	target.classList.add("active");
	/* start showing the active panel */
	let panel = target.nextElementSibling;
	panel.style.display = "flex";
	// close all syblings, except us
	let els = target.parentNode.getElementsByClassName("accordion");
	for(let i = 0; i < els.length; i++){
		let sib = els[i];
		if(sib == target) // This is us... skip
			continue;
		if(sib.classList.contains("active")){
			sib.classList.remove("active");
			let sibpanel = sib.nextElementSibling;
			if(sibpanel.style.display === "flex")
				sibpanel.style.display = "none";
		}
	}
	cb(panel, param);
}

function selectTabType(evt, cb, param){
	// Open target, close syblings
	let target = evt.target;
	/* Toggle between adding and removing the "active" class,
	to highlight the button that controls the panel */
	if(target.classList.contains("active"))	// already selected
		return;
	target.classList.add("active");
	/* start showing the active panel */
	let panelName = target.getAttribute("data-id");
	let panel = false;
	if(panelName)
		panel = document.getElementById(panelName);
	if(!panel)
		panel = target.nextElementSibling;
	panel.style.display = "flex";
	// close all syblings, except us
	let els = target.parentNode.getElementsByClassName("tab");
	for(let i = 0; i < els.length; i++){
		let sib = els[i];
		if(sib == target) // This is us... skip
			continue;
		if(sib.classList.contains("active")){
			sib.classList.remove("active");
			panelName = sib.getAttribute("data-id");
			let sibpanel = false;
			if(panelName)
				sibpanel = document.getElementById(panelName);
			if(!sibpanel)
				sibpanel = sib.nextElementSibling;
			if(sibpanel.style.display === "flex")
				sibpanel.style.display = "none";
		}
	}
	cb(panel, param);
}

function toggleShowSearchList(evt){ 
	// Expected html structure:
	// <button>			the target (onClick)
	// <div>			Toggle display (visibility)
	//		<input>	Search field
	//		<div>		List contents
	if(evt.preventDefault)
		evt.preventDefault();
	let target = evt.target;
	let div = target.nextElementSibling;
	let search = div.getElementsByTagName("input");
	if(search.length){
		search = search[0];
		filterSearchList({target: search});
	}
	if(div.style.display === "block")
		div.style.display = "none";
	else
		div.style.display = "block";
} 

function filterSearchList(evt){ 
	// Expected html structure:
	// <button>			
	// <div>			
	//		<input>	Search field - the target (onKeyup)
	//		<div>		List contents
	let target = evt.target;
	let removecb = target.getAttribute("data-removecb");
	let cbdiv = target.getAttribute("data-div");
	let search = target.value.toUpperCase();
	let contdiv = target.nextElementSibling;
	let els = contdiv.getElementsByTagName("*");
	for(let i = 0; i < els.length; i++){
		let txtValue = els[i].textContent || els[i].innerText;
		if(txtValue.toUpperCase().indexOf(search) > -1){
			// remove any in matches based on remove callback, if set
			if(removecb && (window[removecb](cbdiv, txtValue)))
				els[i].style.display = "none";
			else
				els[i].style.display = "";
		}else
			els[i].style.display = "none";
	}
} 

function buildSearchList(el, list, cb){
	// Expected html structure:
	// <button>			
	// <div>			
	//		<input>	Search field - the target (onKeyup)
	//		<div>		List contents (el) - generate a div for each liste entry inside, with data-id is set 
	//					to id property, element innerText is set to Name property. cb is the selection callabck.
	el.innerHTML = "";
	if(list){
		for(let i = 0; i < list.length; i++){
			let entry = document.createElement('div');
			entry.textContent = list[i].Name;
			if(list[i].id)
				entry.setAttribute("data-id", list[i].id);
			if(cb)
				entry.onclick = cb;
			entry.classList.add("search-list-entry");
			el.appendChild(entry);
		}
	}
} 

function liveSearchClick(evt){
	// Expected html structure:
	// <form>
	// 	<input type="text" size="30" onkeyup="liveSearchList(event, this.value, queryFunction, "passstr")">
	// 	<div>
	//			<div>entry1</div>		target of event
	//		</div>
	// </form>
	evt.preventDefault;
	let target = evt.target;
	let parentdiv = target.parentNode;
	let input = parentdiv.parentNode.firstElementChild;
	input.value = target.innerText;
	liveSearchList({target: input}, ""); // close list
}

async function liveSearchList(evt, str, queryfn, pass){
	// Expected html structure:
	// <form>
	// 	<input type="text" size="30" onblur="liveSearchList(event, '')" onkeyup="liveSearchList(event, this.value, queryFunction, "passstr")">
	// 	<div>live results</div>
	// </form>
	if(evt.preventDefault)
		evt.preventDefault();
	let target = evt.target;
	let div = target.nextElementSibling;
	if(str.length){
		let result = await queryfn(str, pass);
		if(result && result.length){
			div.innerHTML="";
			for(let i = 0; i < result.length; i++){
				let entry = document.createElement('div');
				entry.textContent = result[i];
				entry.onclick = liveSearchClick;
				entry.onmousedown = function (event){event.preventDefault();};
				entry.classList.add("search-list-entry");
				div.appendChild(entry);
			}
			div.style.border="1px solid #A5ACB2";
			return;
		}
	}
	div.innerHTML="";
	div.style.border="0px";
} 

/***** Variable change callbacks *****/

function hideUnauthDiv(value){
	let el;
	if(value){
		if(value.permission === "admin"){
			el = document.getElementById("dbinit");
			el.style.display = "block";
			el = document.getElementById("dbsyncdiv");
			el.style.display = "block";
			el = document.getElementById("dbcrawldiv");
			el.style.display = "block";
			el = document.getElementById("fileimportbox");
			el.style.display = "block";
			return;
		}
		el = document.getElementById("fileimportbox");
		if(['manager', 'production', 'programming', 'library'].includes(value.permission))
			el.style.display = "block";
		else
			el.style.display = "none";
	}else{
		el = document.getElementById("fileimportbox");
		el.style.display = "none";
	}
	el = document.getElementById("dbinit");
	el.style.display = "none";
	el = document.getElementById("dbsyncdiv");
	el.style.display = "none";
	el = document.getElementById("dbcrawldiv");
	el.style.display = "none";

}

function locMenuTrack(value){
	// this function updates the location selection menu
	// when the locName variable changes
	let element = document.getElementById("selloc");
	if(element){
		if(element.value != value){
			element.value = value;
		}
	}
}

function locMenuRefresh(value){
	// this function updates the location selection menu list
	// when the locListCache variable changes
	let element = document.getElementById("selloc");
	if(element && value){
		let inner = "<option value='' onClick='getLocList()'>Reload List</option>";
		for(let i=0; i < value.length; i++)
			inner += "<option value='"+value[i].Name+"' data-id='"+value[i].id+"'>"+value[i].Name+"</option>";
		element.innerHTML = inner;
	}
	element = document.getElementById("stConfDbLoc");
	if(element && value){
		let inner = "";
		for(let i=0; i < value.length; i++)
			inner += "<option value='"+value[i].id+"' data-id='"+value[i].id+"'>"+value[i].id+" "+value[i].Name+"</option>";
		element.innerHTML = inner;
	}
}

function mediaMenuRefresh(value){
	// this function updates the media location selection menu list
	// when the locListCache variable changes
	let element = document.getElementById("impMediaDestList");
	if(element && value){
		let inner = "<option value='' onClick='getMediaLocs()'>Reload List</option>";
		inner += "<option value='' selected>Default</option>";
		for(let i=0; i < value.length; i++)
			inner += "<option value='"+value[i]+"'>"+value[i]+"</option>";
		element.innerHTML = inner;
	}
}

function fileReplaceDestRefresh(value){
	// this function updates the media location selection menu list
	// when the locListCache variable changes
	let element = document.getElementById("filereplacedest");
	if(element && value){
		let inner = "<option value='' onClick='getMediaLocs()'>Reload List</option>";
		inner += "<option value='' selected>Default</option>";
		for(let i=0; i < value.length; i++)
			inner += "<option value='"+value[i]+"'>"+value[i]+"</option>";
		element.innerHTML = inner;
	}
}

function browseTypeRowSelUpdate(value){
	// Get all elements with class="tselcell" and remove the class "active" from btype div
	let par = document.getElementById("btype");
	let els = par.getElementsByClassName("tselrow");
	for(let i = 0; i < els.length; i++){
		els[i].className = els[i].className.replace(" active", "");
	}
	// and activate the selected element
	if(browseTypeList){
		for(let i=0; i<browseTypeList.length; i++){
			if(browseTypeList[i].qtype === value){
				els[i].className += " active";
				break;
			}
		}
	}
	browseSort = "Label";	// Eric request: always default to Label asending when new type is clicked/changed
	browseQuery();
}

/***** HTML manipulation functions *****/

function showTab(event, id, pass){
	// pass is the studio name, if set.
	let element = event.currentTarget;
	showTabElement(element, id, pass)
}

function showTabElement(el, id, pass){
	// pass is the studio name, if set.
	let i, tabcontent, tablinks;

	// Get all elements with class="tabcontent" and hide them
	tabcontent = document.getElementsByClassName("tabcontent");
	for(i = 0; i < tabcontent.length; i++){
		tabcontent[i].style.display = "none";
	}

	// Get all elements with class="tablinks" and remove the class "active"
	tablinks = document.getElementsByClassName("tabitem");
	for(i = 0; i < tablinks.length; i++){
		tablinks[i].className = tablinks[i].className.replace(" active", "");
	}

	// Display the content
	document.getElementById(id).style.display = "flex";
	// And add an "active" class to the link that opened the tab
	el.className += " active";
	let parent = el.parentNode
	// Make submenu active if child is active
	if(parent){
		let parID = parent.id;
		for(i = 0; i < tablinks.length; i++){
			if(tablinks[i].getAttribute("data-childdiv") === parID)
				tablinks[i].className += " active";
		}
	}
	if(pass !== undefined){
		studioName.setValue(pass);
		setLibUsingStudio();
	}
	if((id === "browse") && !browseData)
		browseQuery();
	if(id === "libmanage")
		loadLocationMgtTbl();
	if((id === "files") && (filesPath === false))
		loadFilesTbl();
	if(id === "query")
		libQueryRefreshList();
} 

function getRowInputProps(row){
	let props = {};
	var cells = row.getElementsByTagName("td");
	// set name/values for all "input" elements found in the row
	for(let i = 0; i < cells.length; i++){
		let els = cells[i].getElementsByTagName("input");
		for(let j = 0; j < els.length; j++){
			if(els[j].name){
				props[els[j].name] = els[j].value;
				let dataid = els[j].getAttribute("data-id");
				if(dataid)
					props.id = dataid;
				if(els[j].name === "password"){
					els[j].value = "";
				}
				if(els[j].name === "startup"){
					if(els[j].checked)
						props[els[j].name] = true;
					else
						props[els[j].name] = false;
				}
			}
		}
		els = cells[i].getElementsByTagName("select");
		for(let j = 0; j < els.length; j++){
			if(els[j].name)
				props[els[j].name] = els[j].value;
		}
	}
	return props;
}
function getSchedItemMinute(list, idx){
	if(list && list.length>idx){
		return list[idx].mapHour * 60 + list[idx].Minute;
	}
	return -1;	// no more items scheduled
}

function schedCellColor(cell, entry){
	let colorStr = "#";

	if(entry.Date)
		colorStr += "F0";
	else if(entry.dbDay)
		colorStr += "C0";
	else
		colorStr += "80";
	
	if(entry.Month)
		colorStr += "D0";
	else
		colorStr += "80";
		
	if(entry.dbHour > -1)
		colorStr += "D0";
	else
		colorStr += "80";
	cell.style.backgroundColor = colorStr;
}

function genPopulateSchedLegend(el){
	// Create a table element
	let table = document.createElement("table");
	table.className = "tablecenterj";
	// Create table row tr element of a table for header
	let tr = table.insertRow(-1);
	let theader = document.createElement("th");
	theader.width = 100;
	theader.innerHTML = " ";
	tr.appendChild(theader);
	theader = document.createElement("th");
	theader.width = 100;
	theader.innerHTML = "Date";
	tr.appendChild(theader);
	theader = document.createElement("th");
	theader.width = 100;
	theader.innerHTML = "Weekday";
	tr.appendChild(theader);
	theader = document.createElement("th");
	theader.width = 100;
	theader.innerHTML = "Every Day";
	tr.appendChild(theader);
	
	tr = table.insertRow(-1);
	let cell = tr.insertCell(-1);
	cell.innerText = "Hour & Month"
	
	cell = tr.insertCell(-1);
	schedCellColor(cell, {dbHour:1, Month:1, Date:1});
	cell.innerText = " ";

	cell = tr.insertCell(-1);
	schedCellColor(cell, {dbHour:1, Month:1, dbDay:1});
	cell.innerText = " ";
	
	cell = tr.insertCell(-1);
	schedCellColor(cell, {dbHour:1, Month:1});
	cell.innerText = " ";
	
	tr = table.insertRow(-1);
	cell = tr.insertCell(-1);
	cell.innerText = "Hourly & Month"
	
	cell = tr.insertCell(-1);
	schedCellColor(cell, {dbHour:-1, Month:1, Date:1});
	cell.innerText = " ";

	cell = tr.insertCell(-1);
	schedCellColor(cell, {dbHour:-1, Month:1, dbDay:1});
	cell.innerText = " ";
	
	cell = tr.insertCell(-1);
	schedCellColor(cell, {dbHour:-1, Month:1});
	cell.innerText = " ";
	
	tr = table.insertRow(-1);
	cell = tr.insertCell(-1);
	cell.innerText = "Hour & Monthly"
	
	cell = tr.insertCell(-1);
	schedCellColor(cell, {dbHour:1, Month:0, Date:1});
	cell.innerText = " ";

	cell = tr.insertCell(-1);
	schedCellColor(cell, {dbHour:1, Month:0, dbDay:1});
	cell.innerText = " ";
	
	cell = tr.insertCell(-1);
	schedCellColor(cell, {dbHour:1, Month:0});
	cell.innerText = " ";
	
	tr = table.insertRow(-1);
	cell = tr.insertCell(-1);
	cell.innerText = "Hourly & Monthly"
	
	cell = tr.insertCell(-1);
	schedCellColor(cell, {dbHour:-1, Month:0, Date:1});
	cell.innerText = " ";

	cell = tr.insertCell(-1);
	schedCellColor(cell, {dbHour:-1, Month:0, dbDay:1});
	cell.innerText = " ";
	
	cell = tr.insertCell(-1);
	schedCellColor(cell, {dbHour:-1});
	cell.innerText = " ";
	
	el.innerHTML = "Insert items may be re-ordered to get them as close to their target times as possible.";
	el.appendChild(table);
}

function genPopulateSchedTable(insert, fill, el, cellClick){
	if(!insert && !fill){
		el.innerHTML = "";
		return;
	}
	// Create a grid div element
	let table = document.createElement("div");
	table.className = "schedGrid";
	// Create 24 cells as the column header
	let theader;
	for(let i = 0; i <= 11; i++) {
		theader = document.createElement("div");
		if(i==11){
			theader.innerHTML = "Fill Item";
		}else{
			theader.innerHTML = "";
		}
		table.appendChild(theader);
	}
	
	theader = document.createElement("div");
	theader.innerHTML = "Time";
	table.appendChild(theader);
	for(let i = 0; i <= 10; i++) {
		theader = document.createElement("div");
		if(i==0){
			theader.innerHTML = "Insert Item";
		}else{
			theader.innerHTML = "";
		}
		table.appendChild(theader);
	}
	theader = document.createElement("div");
	theader.innerHTML = "Target";
	table.appendChild(theader);
	theader = document.createElement("div");
	theader.innerHTML = "";
	table.appendChild(theader);
	let fillIdx = 0;
	let instIdx = 0;
	let nextFillMin = 0;
	let nextInstMin = 0;
	let nextFillPrio = -1;
	let lastInst = 0;
	let min = -1;
	nextFillMin = getSchedItemMinute(fill, fillIdx);
	nextFillPrio = -1;
	nextInstMin = getSchedItemMinute(insert, instIdx);
	// Adding a row to the table for every minute of the day
	for(let i = 0; i < 1440; i++){
		// fill items cols
		if(i == nextFillMin){
			let prio = fill[fillIdx].Priority;
			let por = fill[fillIdx].Fill;	// priority override minues
			cell = document.createElement("div");
			cell.style.border = "1px solid #000000";
			// set column & span
			cell.style.gridColumn = String(12-prio)+" / span "+String(prio+1);
			cell.innerText = fill[fillIdx].Label;
			if(fill[fillIdx].ItemID > 0){
				cell.setAttribute("data-id", fill[fillIdx].ItemID);
				cell.onclick = cellClick;
			}
			schedCellColor(cell, fill[fillIdx]);
			// next
			while(nextFillMin == i){
				// ignore lessor-priority fills at the same time
				fillIdx++;
				nextFillMin = getSchedItemMinute(fill, fillIdx);
				if(nextFillMin > -1)
					nextFillPrio = fill[fillIdx].Priority;
				else
					nextFillPrio = -1;
			}
			// set row & span.
			if(nextFillMin > -1){
				if((prio > nextFillPrio) && ((por+i) > nextFillMin))
					cell.style.gridRow = String(i+2)+" / span "+String(por);
				else
					cell.style.gridRow = String(i+2)+" / span "+String(nextFillMin - i);
			}else
				cell.style.gridRow = String(i+2)+" / span "+String(1440 - i);
			table.appendChild(cell);
		}
		// time col
		cell = document.createElement("div");
		cell.style.gridColumn = "13 / span 1";
		cell.style.gridRow = String(i+2)+" / span 1";
		cell.innerHTML = timeFormat(i, true);
		table.appendChild(cell);

		// insert items cols
		if((nextInstMin >= 0) && (i == nextInstMin)){
			lastInst = nextInstMin;
			let prio = insert[instIdx].Priority;
			let target;
			cell = document.createElement("div");
			cell.style.border = "1px solid #000000";
			// set column & span
			cell.style.gridColumn = "14 / span "+String(prio+1);
			cell.innerText = insert[instIdx].Label;
			if(insert[instIdx].ItemID > 0){
				cell.setAttribute("data-id", insert[instIdx].ItemID);
				cell.onclick = cellClick;
			}
			schedCellColor(cell, insert[instIdx]);
			// handle offset from target due to previous item's duration
			let dur = Math.ceil(insert[instIdx].Duration);
			if(!dur)
				dur = 1;
			// set row span.
			cell.style.gridRow = String(i+2)+" / span "+String(dur);
			table.appendChild(cell);

			target = document.createElement("div");
			target.innerText = timeFormat((insert[instIdx].mapHour * 60) + insert[instIdx].Minute, true);
			target.style.gridRow = String(i+2)+" / span "+String(dur);
			target.style.gridColumn = "25 / span 1";
			table.appendChild(target);

			// next
			instIdx++;
			nextInstMin = getSchedItemMinute(insert, instIdx);
			if((nextInstMin >= 0) && (nextInstMin < (lastInst + dur))){
				// pushed ahead, no gap
				nextInstMin = lastInst + dur;
			}
		}
	}
	// Add the newely created table to the specified <div>
	el.innerHTML = "";
	el.appendChild(table);
} 

function genPopulateTableFromArray(list, el, colMap, rowClick, headClick, sortVar, actions, haction, fieldTypes, colWidth, showCount){
	if(!list && !haction){
		el.innerHTML = "";
		return;
	}
	let cols = [];
	// Make sure the header has at least the colMap columns, even if the list is empty
	if(colMap){
		for(let k in colMap){
			if(colMap[k] && cols.indexOf(k) === -1)
				// Push all keys to the array
				cols.push(k);
		}
	}
	// get columns from list data set, check agains colMap for inclusion or rename
	for(let i = 0; i < list.length; i++){
		if(typeof list[i] === 'object'){
			for(let j in list[i]){
				if(cols.indexOf(j) === -1) {
					// Push all keys to the array
					if(!colMap || (colMap[j] !== false))
						cols.push(j);
				}
			}
		}
	}
	// Create a table element
	let table = document.createElement("table");
	table.className = "tableleftj";
	// Create table row tr element of a table
	let tr = table.insertRow(-1);
	for(let i = 0; i < cols.length; i++) {
		// Create the table header th elements
		let theader = document.createElement("th");
		if(headClick){
			theader.className = "tselcell clickable";
			theader.onclick = headClick;
		}else
			theader.className = "tselcell";
		let rename = false;
		if(colMap)
			rename = colMap[cols[i]];
		if(rename)
			theader.innerHTML = rename;
		else if(cols.length > i)
			theader.innerHTML = cols[i];
		else
			theader.innerHTML = "";
		if(sortVar){
			//add sort direction icon
			if(cols[i] === sortVar)
				theader.innerHTML = "<i class='fa fa-sort-asc' aria-hidden='true'></i>" + theader.innerHTML;
			if(("-"+cols[i]) === sortVar)
				theader.innerHTML = "<i class='fa fa-sort-desc' aria-hidden='true'></i>" + theader.innerHTML;
		}
		let width = "";
		if(colWidth)
			width = colWidth[cols[i]]; // example "25px" or "30%"
		if(width && width.length)
			theader.style.width = width;
		// Append columnName to the table row
		tr.appendChild(theader);
	}
	if(actions || haction){
		// add heading/column for actions
		let theader = document.createElement("th");
		theader.className = "tselcell"; // non clickable
		let width = "";
		if(colWidth)
			width = colWidth.action; // example "25px" or "30%"
		if(width && width.length)
			theader.style.width = width;
		if(haction){
			theader.innerHTML = haction;
		}else{
			theader.innerHTML = "";
		}
		tr.appendChild(theader);
	}
	// Adding the data to the table
	for(let i = 0; i < list.length; i++){
		// Create a new row
		trow = table.insertRow(-1);
		if(rowClick)
			trow.onclick = rowClick;
		if(cols.length)
			trow.className = "tselrow";
		if(cols.length == 0){
			// just a list, not a list of objects
			let cell = trow.insertCell(-1);
			cell.innerHTML = list[i];
		}else{
			for(let j = 0; j < cols.length; j++){
				let cell = trow.insertCell(-1);
				// Inserting the cell at particular place
				let inner;
				if(fieldTypes && fieldTypes[cols[j]]){
					let val = list[i][cols[j]];
					if(fieldTypes[cols[j]] instanceof Function){
						// fieldType is a function... have the function process the value
						inner = fieldTypes[cols[j]](val, list[i], i);
					}else{
						inner = fieldTypes[cols[j]];
						if(list[i][cols[j]] == true)
							inner = inner.replaceAll("$ifvalchk", "checked");
						else
							inner = inner.replaceAll("$ifvalchk", "");
						if(inner.indexOf("$iftrue") > -1){
							let parts = inner.split("$iftrue");
							for(let n = 0; n < parts.length; n++){
								if(n % 2 == 1){
									if(val !== true)
										parts[n] = "";
								}
							}
							inner = parts.join("");
						}
						if(inner.indexOf("$iffalse") > -1){
							let parts = inner.split("$iffalse");
							for(let n = 0; n < parts.length; n++){
								if(n % 2 == 1){
									if(val !== false)
										parts[n] = "";
								}
							}
							inner = parts.join("");
						}
						if(inner.indexOf("$ifvalsel") > -1){
							let parts = inner.split("$ifvalsel");
							for(let n = 0; n < parts.length; n++){
								if(parts[n].indexOf("'"+val+"'") > -1){
									parts[n] += "selected";
								}
							}
							inner = parts.join("");
						}
						inner = inner.replaceAll("$attval", quoteattr(val));
						inner = inner.replaceAll("$val", val);
						if(list[i].id)
							inner = inner.replaceAll("$id", list[i].id);
						else
							inner = inner.replaceAll("$id", "");
						if(list[i].ID)
							inner = inner.replaceAll("$ID", list[i].ID);
						else
							inner = inner.replaceAll("$ID", "");
						if(list[i].RID)
							inner = inner.replaceAll("$RID", list[i].RID);
						else
							inner = inner.replaceAll("$RID", "");
						inner = inner.replaceAll("$i", i);
					}
				}else if(cols[j] === "Duration")	// special case for displaying duration
					inner = timeFormat(list[i][cols[j]]);
				else
					inner = list[i][cols[j]];
				cell.innerHTML = inner;
			}
		}
		if(actions || haction){
			let cell = trow.insertCell(-1);
			if(actions){
				let newac = actions.replaceAll("$i", i);
				if(newac.indexOf("$") > -1){
					let parts = newac.split("$");	// function to call
					for(let n = 0; n < parts.length; n++){
						if(n % 2 == 1)
							parts[n] = window[parts[n]](list[i]);
					}
					newac = parts.join("");
				}
				cell.innerHTML = newac;
			}else
				cell.innerHTML = "";
		}
	}
	// Add the newely created table to the specified <div>
	el.innerHTML = "";
	if(showCount){
		let sc = document.createTextNode(list.length + " Items"); 
		el.appendChild(sc);
	}
	el.appendChild(table);
} 

function insertTableRow(kvcols, el, idx, colMap, rowClick, actions, fieldTypes){
	// kvrow object should match the layout of the list entries used to create the table 
	// with the above genPopulateTableFromArray() function
	let cols = [];
	Object.keys(kvcols).forEach(function(key){
		if(cols.indexOf(key) === -1) {
			// Push all keys to the array
			if(!colMap || (colMap[key] !== false))
				cols.push(key);
		}
	});
	// get a table element
	let table = el.getElementsByTagName("table");
	if(table.length == 0)
		return;	// no table in element!
	table = table[0]; // should be only one table in el.
	
	// Create table row tr element of a table
	let trow = table.insertRow(idx);
	// Create a new row
	if(rowClick)
		trow.onclick = rowClick;
	trow.className = "tselrow";
	for(let i = 0; i < cols.length; i++){
		let cell = trow.insertCell(-1);
		// Inserting the cell at particular place
		let inner;
		if(fieldTypes && fieldTypes[cols[i]]){
			let val = kvcols[cols[i]];
			if(fieldTypes[cols[i]] instanceof Function){
				// fieldType is a function... have the function process the value
				inner = fieldTypes[cols[i]](val, kvcols, idx);
			}else{
				inner = fieldTypes[cols[i]];
				if(kvcols[cols[i]] == true)
					inner = inner.replaceAll("$ifvalchk", "checked");
				else
					inner = inner.replaceAll("$ifvalchk", "");
				if(inner.indexOf("$iftrue") > -1){
					let parts = inner.split("$iftrue");
					for(let n = 0; n < parts.length; n++){
						if(n % 2 == 1){
							if(val != true)
								parts[n] = "";
						}
					}
					inner = parts.join("");
				}
				if(inner.indexOf("$iffalse") > -1){
					let parts = inner.split("$iffalse");
					for(let n = 0; n < parts.length; n++){
						if(n % 2 == 1){
							if(val != false)
								parts[n] = "";
						}
					}
					inner = parts.join("");
				}
				if(inner.indexOf("$ifvalsel") > -1){
					let parts = inner.split("$ifvalsel");
					for(let n = 0; n < parts.length; n++){
						if(parts[n].indexOf("'"+val+"'") > -1){
							parts[n] += "selected";
						}
					}
					inner = parts.join("");
				}
				inner = inner.replaceAll("$attval", quoteattr(val));
				inner = inner.replaceAll("$val", val);
				if(kvcols.id)
					inner = inner.replaceAll("$id", kvcols.id);
				else
					inner = inner.replaceAll("$id", "");
				if(kvcols.ID)
					inner = inner.replaceAll("$ID", kvcols.ID);
				else
					inner = inner.replaceAll("$ID", "");
				if(kvcols.RID)
					inner = inner.replaceAll("$RID", kvcols.RID);
				else
					inner = inner.replaceAll("$RID", "");
				inner = inner.replaceAll("$i", idx);
			}
		}else if(cols[i] === "Duration")	// special case for displaying duration
			inner = timeFormat(kvcols[cols[i]]);
		else
			inner = kvcols[cols[i]];
		cell.innerHTML = inner;
	}
	if(actions){
		let cell = trow.insertCell(-1);
		if(actions){
			let newac = actions.replaceAll("$i", idx);
			if(newac.indexOf("$") > -1){
				let parts = newac.split("$");	// function to call
				for(let n = 0; n < parts.length; n++){
					if(n % 2 == 1)
						parts[n] = window[parts[n]](list[i]);
				}
				newac = parts.join("");
			}
			cell.innerHTML = newac;
		}else
			cell.innerHTML = "";
	}
}


function setDragableInnerHTML(item, li, format){
	// format: li contents set to format with all $key$ replaced with corrisponding key's value
	// or $key->funcName$ replaced with the value returned from the named function with the key's value passed in.
	let inner = format;
	if(inner.indexOf("$") > -1){
		let parts = inner.split("$");
		let key;
		for(let n = 0; n < parts.length; n++){
			if(item){
				if(n % 2 == 1){
					let key = parts[n];
					let obj = item;
					if(key.indexOf("/") > -1){	// handle sub-keys "parent-key/child-key/.."
						let sub = inner.split("/");
						for(let i = 0; i < sub.length; i++)
							obj = obj[sub[i]];
					}
					if(key.indexOf("->") > -1){	// handle value conversion function "key->functionName"
						let sub = key.split("->");
						let fnName = sub[1];
						key = sub[0];
						parts[n] = window[fnName](obj[key]);
					}else
						parts[n] = quoteattr(obj[key]);
					if(!parts[n])
						parts[n] = "";
				}
			}
		}
		inner = parts.join("");
	}
	li.innerHTML = inner;
}

function appendDragableItem(dragable, dropcb, item, idx, ul, format, urlkey){
	let li = document.createElement("li");
	li.setAttribute("data-idx", idx);
	setDragableInnerHTML(item, li, format);
	ul.appendChild(li);
	if(dragable)
		setDragItemEvents(li, false, dropcb)
	if(urlkey){
		let url = item[urlkey];
		if(url && url.length)
			li.setAttribute("data-url", url);
	}
	return li;
}

function genDragableListFromObjectArray(dragable, dropcb, list, ul, format, urlkey){
	ul.innerHTML = "";
	let idx = 0;
	for(let n = 0; n < list.length; n++){
		let item = list[n];
		appendDragableItem(dragable, dropcb, item, idx, ul, format, urlkey);
		idx++;
	}
}

function setDragItemEvents(i, dragcb, dropcb){
	i.draggable = true;
	i.addEventListener("dragstart", function(evt){
		if(dragcb){
			if(!dragcb(this))
				return;
		}
		curDrag = this;
		let target = evt.target.parentNode;
		let items = target.getElementsByTagName("li");
		for(let it of items){
			if(it != curDrag)
				it.classList.add("hint"); 
		}
	});

	i.addEventListener("dragenter", function(){
		if(dropcb && (this != curDrag) && (this.parentNode === curDrag.parentNode)){
			this.classList.add("active");
		}
	});

	i.addEventListener("dragleave", function(){
		this.classList.remove("active");
	});

	i.addEventListener("dragend", function(evt){
		let target = evt.target.parentNode;
		let items = target.getElementsByTagName("li");
		for(let it of items){
			it.classList.remove("hint");
			it.classList.remove("active");
		}
		curDrag = null;
	});

	i.addEventListener("dragover", function(evt){
		if(dropcb)
			evt.preventDefault();
	});

	i.addEventListener("drop", function(evt){
		evt.preventDefault();
		let target = evt.target.parentNode;
		let items = target.getElementsByTagName("li");
		if(dropcb && (this != curDrag)){
			if(this.parentNode === curDrag.parentNode){
				// move in list
				let currentpos = 0, droppedpos = 0;
				for(let it=0; it<items.length; it++){
					if(curDrag == items[it])
						currentpos = it;
					if(this == items[it])
						droppedpos = it;
				}
				dropcb(this, currentpos, droppedpos);
			}else{
				// drop is from some other place
				let atr = {url: curDrag.getAttribute("data-url"), pnum: curDrag.getAttribute("data-pnum")};
				if(atr && dropcb){
					let droppedpos = 0;
					for(let it=0; it<items.length; it++){
						if(this == items[it])
							droppedpos = it;
					}
					dropcb(this, false, droppedpos, atr);
				}
			}
		}
		curDrag = null;
	});
}

/***** Stash functions *****/

function stashGetSelected(){
	let list = [];
	let sl = document.getElementById("stashlist");
	let els = sl.querySelectorAll('input[type=checkbox]:checked');
	if(els && els.length){
		for(let i=0; i<els.length; i++){
			let item = els[i].parentElement.parentElement;
			let idx = item.getAttribute("data-idx");
			list.push(stashList[idx]);
		}
	}
	return list;
}

function updateStashDuration(){
	let timeList = stashGetSelected();
	if(!timeList.length)
		timeList = stashList;
	el = document.getElementById("stashdur");
	if(el)
		el.innerText = timeFormat(updateListDuration(timeList));
}

function loadStashRecallOnLoad(){
	stashList = getStash();
	if(!stashList)
		stashList = [];
	let el = document.getElementById("stashlist");
	let format = `<span style='float: left;'><input type='checkbox' onchange='updateStashDuration()'></input></span>
						<span style='float: right;'>$Duration->timeFormat$ <button class="editbutton" onclick="stashItemInfo(event)">i</button></span>
						<div style='clear:both;'></div>
						<span style='float: left;'>$Name$</span><span style='float: right;'>$StashType$</span><div style='clear:both;'></div>
						<span style='float: left;'>$Artist$</span>
						<span style='float: right;'>Note:<input type=text onblur="stashNoteChange(event)" value="$note$"></text></span><div style='clear:both;'></div>`;
	genDragableListFromObjectArray(true, moveItemInStash, stashList, el, format);
	updateStashDuration();
}

function stashNoteChange(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let target = evt.target;
	let val = target.value;
	let item = target.parentElement.parentElement;
	let idx = item.getAttribute("data-idx");
	item = stashList[idx];
	item.note = val;
	setStash(stashList);
}

function flattenItemForStash(item){
	let forStash = {
		Type: item.Type,
		Name: item.Name,
		Duration: item.Duration
	};
	if(item.ID)
		forStash.ID = item.ID;
	if(item.tmpfile)
		forStash.tmpfile = item.tmpfile;
	if(item.URL)
		forStash.URL = item.URL;
	if(item.Artist)
		forStash.Artist = item.Artist;
	if(item.Album)
		forStash.Album = item.Album;
	if(item.Prefix)
		forStash.Prefix = item.Prefix;
	if(item.Path)
		forStash.Path = item.Path;
	if(item.Hash)
		forStash.Hash = item.Hash;
	if((item.Type === "file") && item.file){
		// flatten out file properties
		forStash.Artist = item.file.Artist;
		forStash.Album = item.file.Album;
		forStash.Prefix = item.file.Prefix;
		forStash.Path = item.file.Path;
		forStash.Hash = item.file.Hash;
	}
	return forStash;
}

function appendItemToStash(item){
	// item format from Item view
	item = flattenItemForStash(item);
	if(item.Type === "file"){
		if(item.ID)
			item.StashType = "libfile";
		else
			item.StashType = "tmpfile";
	}else if(item.Type === "playlist"){
		if(item.ID)
			item.StashType = "libpl";
		else if(item.tmpfile)
			item.StashType = "fpl";
		else{
			// invalid...
			alert("Item "+item.Name+" can't be added to the stash:\nIt must have either an library ID or a file path.\nTry saving the item first.");
			return;
		}
	}else if(item.URL && item.URL.length){
		// handle URL (studio interface)
		let type = item.URL.split("://")[0];
		if(type === "file")
			item.StashType = "tmpfile";
		else
			item.StashType = type;
	}else
		item.StashType = item.Type;
	stashList.push(item);
	let index = stashList.length -1;
	setStash(stashList);
	let el = document.getElementById("stashlist");
	let format = `<span style='float: left;'><input type='checkbox'></input></span>
						<span style='float: right;'>$Duration->timeFormat$ <button class="editbutton" onclick="stashItemInfo(event)">i</button></span>
						<div style='clear:both;'></div>
						<span style='float: left;'>$Name$</span><span style='float: right;'>$StashType$</span><div style='clear:both;'></div>
						<span style='float: left;'>$Artist$</span>
						<span style='float: right;'>Note:<input type=text onblur="stashNoteChange(event)" value="$note$"></text></span><div style='clear:both;'></div>`;
	appendDragableItem(true, moveItemInStash, item, index, el, format)
	updateStashDuration();
}

function moveItemInStash(obj, fromIdx, toIdx, param){
	if(fromIdx === false)
		return; // stash doesn't accept drops from other places
	let items = obj.parentElement.childNodes;
	if(fromIdx < toIdx){
		obj.parentNode.insertBefore(curDrag, obj.nextSibling);
	}else{
		obj.parentNode.insertBefore(curDrag, obj);
	}
	stashList.splice(toIdx, 0, stashList.splice(fromIdx, 1)[0]);
	setStash(stashList);
	updateStashDuration();
	for(let it=0; it<items.length; it++)
		// re-number data-idx
		items[it].setAttribute("data-idx", it);
}

function stashDeleteSelected(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let list = document.getElementById("stashlist");
	let els = list.querySelectorAll('input[type=checkbox]:checked');
	if(els && els.length){
		for(let i=0; i<els.length; i++){
			let item = els[i].parentElement.parentElement;
			let idx = item.getAttribute("data-idx");
			stashList.splice(idx, 1);
			list.removeChild(item);
			items = list.childNodes;
			for(let it=0; it<items.length; it++)
				// re-number data-idx
				items[it].setAttribute("data-idx", it);
		}
		setStash(stashList);
		updateStashDuration();
	}
}

function stashItemInfo(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let target = evt.target;
	let item = target.parentElement.parentElement;
	let idx = item.getAttribute("data-idx");
	item = stashList[idx];
	let credentials = cred.getValue();
	if(credentials && ['admin', 'manager', 'library'].includes(credentials.permission))
		showItem(item, true);
	else
		showItem(item, false);
}

function stashUnselectAll(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let list = document.getElementById("stashlist");
	let els = list.querySelectorAll('input[type=checkbox]:checked');
	if(els){
		for(let i=0; i<els.length; i++)
			els[i].checked = false;
		updateStashDuration();
	}
}

function stashSelectAll(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let list = document.getElementById("stashlist");
	let els = list.querySelectorAll('input[type=checkbox]:not(:checked)');
	if(els){
		for(let i=0; i<els.length; i++)
			els[i].checked = true;
		updateStashDuration();
	}
}

async function stashToQueue(evt){
	if(evt)
		evt.preventDefault();
	let list = document.getElementById("stashlist");
	let els = list.querySelectorAll('input[type=checkbox]:checked');
	if(els){
		let items = [];
		for(let i=0; i<els.length; i++){
			let item = els[i].parentElement.parentElement;
			let idx = item.getAttribute("data-idx");
			item = stashList[idx];
			if(item.tmpfile && item.tmpfile.length){
				// create a URL for temp files
				let data = false;
				let resp = await fetchContent("library/tmpmediaurl", {
					method: 'POST',
					body: JSON.stringify({path: item.tmpfile.substring(1)}), // path leading '/' trimmed off
					headers: {
						"Content-Type": "application/json",
						"Accept": "application/json"
					}
				});
				if(resp){
					if(!resp.ok){
						alert("Got an error retreaving file info from server.\n"+resp.status);
						return;
					}
					data = await resp.text();
				}else{
					alert("Failed to retreaving file info from server.");
					return;
				}
				if(data)
					item.URL = data;
				else
					continue;
			}
			items.push(item);
		}
		appendItemsToQueue(items);
	}
}

/***** Item show/edit/delete functions *****/

async function getMetaPropsForParent(str, parent){
	if(!["artist", "album", "category"].includes(parent))
		parent = "toc";
	let qry = "library/get/meta?Parent="+parent+"&distinct=Property&Property=%25"+str+"%25";
	let resp = await fetchContent(qry);
	if(resp){
		if(resp.ok){
			let list = await resp.json();
			for(let i = 0; i < list.length; i++)
				list[i] = list[i].Property;
			return list;
		}else{
			alert("Got an error fetching live list from server.\n"+resp.statusText);
		}
	}else{
		alert("Failed to fetch live list from the server.");
	}
	return false;
}

function showInfo(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let el = document.getElementById("showinfobtn");
	el.style.display = "none";
	el = document.getElementById("infopane");
	el.style.width = infoWidth;
}

function closeInfo(event){
	let el = document.getElementById("infopane");
	el.style.width = "0px";
	// stop audio, if present
	let audio = el.getElementsByTagName("audio");
	if(audio && audio.length){
		// should be only one
		audio[0].pause();
	}
	if(itemProps){
		el = document.getElementById("showinfobtn");
		el.style.display = "block";
	}
}

async function saveItemGeneral(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let newName = "";
	let els = document.getElementById("genitemform").elements;
	let obj ={};
	for(let i = 0 ; i < els.length ; i++){
		let item = els.item(i);
		let was = itemProps[item.name];
		let is = item.value;
		if(item.name == "Duration"){
			is = timeParse(is);
			if(Math.floor(is * 10) == Math.floor(was * 10))
				is = was;
		}
		if(was === undefined) was = "";
		if(was === null) was = "";
		if(was != is){
			obj[item.name] = is;
			if(item.name == "Name")
				newName = is;
		}
	}
	delete obj["submit"];
	if(Object.keys(obj).length){	// only save if there are changes
		let api = "library/set/toc";
		if(itemProps.ID)
			api += "/"+itemProps.ID;
		else
			obj.Type = itemProps.Type;
		let resp = await fetchContent(api, {
				method: 'POST',
				body: JSON.stringify(obj),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp){
			if(resp.ok){
				let reload = await resp.json();
				if(reload.insertId)
					showItem({tocID: reload.insertId}, itemProps.canEdit, false);
				if(!reload.affectedRows)
					alert("Failed to update or get ID of new item.\n");
				else{
					alert("Item has been updated.");
					showItem({tocID: itemProps.ID}, itemProps.canEdit, true);
					let label = document.getElementById("itemName");
					if(newName.length)
						label.innerText = newName;
				}
			}else{
				alert("Got an error saving data to server.\n"+resp.statusText);
			}
		}else{
			alert("Failed to save data to the server.");
		}
	}
}

async function saveItemFile(evt){
	evt.preventDefault();
	evt.stopPropagation();
	
	let el;
	let id;
	let file = itemProps.file;
	let obj ={};
	el = document.getElementById("itemArtistBtn");
	id = el.getAttribute("data-id");
	if(id && (id != file.ArtistID))
		obj.Artist = id;
	el = document.getElementById("itemAlbumBtn");
	id = el.getAttribute("data-id");
	if(id && (id != file.AlbumID))
		obj.Album = id;
		
	el = document.getElementById("fileitemhash");
	if(el.innerText != file.Hash)
		obj.Hash = el.innerText;
	let els = document.getElementById("fileitemform").elements;
	for(let i = 0 ; i < els.length ; i++){
		let item = els.item(i);
		let was = file[item.name];
		if(was === undefined) was = "";
		if(was === null) was = "";
		let is = item.value;
		let itemName = item.name;
		if(["Intro", "SegIn"].includes(item.name)){
			// handle time format to seconds
			is = timeParse(item.value);
			if(Math.floor(is * 10) == Math.floor(was * 10)) // changed within 0.1 seconds?
				is = was;
		}else if(item.name === "Volume"){
				// convert db level to scalar
				is = parseFloat(item.value);
				is = Math.pow(10, (is / 20.0));
				if(Math.floor(is * 1000) == Math.floor(was * 1000)) // changed withing 3 decimal places?
					is = was;
		}else if(item.name === "SegOut"){
			// handle segout and fade checkbox
			el = document.getElementById("itemFade");
			is = timeParse(item.value);
			if(el.checked){
				// change property of itnterest from SegOut to Fade
				itemName = "FadeOut";
				was = file[itemName];
			}
			if(Math.floor(is * 10) == Math.floor(was * 10)) // changed within 0.1 seconds?
				is = was;
		}else if(item.name == "fade"){
			// ignore this item... it 's handled by SegOut
			continue;
		}
		if(was != is){
			obj[itemName] = is;
		}
	}
	if(Object.keys(obj).length){	// only save if there are changes
		let api = "library/set/file";
		if(itemProps.ID)
			api += "/"+itemProps.ID;
		let resp = await fetchContent(api, {
				method: 'POST',
				body: JSON.stringify(obj),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp){
			if(resp.ok){
				let reload = await itemFetchProps(itemProps.ID);
				if(reload){
					if(itemProps.canEdit)
						reload.canEdit = true;
					itemProps = reload;
					reloadItemSection(el, "file");
					alert("Item has been updated.");
				}
			}else{
				alert("Got an error saving data to server.\n"+resp.statusText);
			}
		}else{
			alert("Failed to save data to the server.");
		}
	}
}

async function saveItemTask(evt){
	evt.preventDefault();
	evt.stopPropagation();
	if(!itemProps.ID)
		alert("You must save the item (general settings) before trying to change it's task properties.");
	let el = document.getElementById("tasktypesel");
	let sub = el.value;
	let props = itemProps.task; // this contain entries populated prior to UI changes
	let updated = false;
	let entry;
	let match;
	let newProps = [];	// this will contain entries populated from the UI
	if(sub === "Category"){
		entry = {Property: "Subtype", Value: "Pick"};
		match = findPropObjInArray(props, "Property", "Subtype");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
		
		el = document.getElementById("taskCatBtn");
		entry = el.getAttribute("data-id");
		entry = {Property: "Category", Value: entry};
		match = findPropObjInArray(props, "Property", "Category");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
		
		entry = {Property: "Mode", Value: "Weighted"};
		match = findPropObjInArray(props, "Property", "Mode");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
		
		el = document.forms.taskwieghtby;
		el = el.querySelector('input[name=mode]:checked');
		entry = {Property: "Supress", Value: el.value};
		match = findPropObjInArray(props, "Property", "Supress");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
		
		el = document.getElementById("taskinc");
		entry = el.value;
		if(entry.length){
			entry = {Property: "db_include_loc", Value: entry};
			match = findPropObjInArray(props, "Property", "db_include_loc");
			if(match && match.RID)
				entry.RID = match.RID;
			newProps.push(entry);
		}
	}else if(sub === "Query"){
		entry = {Property: "Subtype", Value: "Pick"};
		match = findPropObjInArray(props, "Property", "Subtype");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
		
		el = document.getElementById("taskquery");
		entry = el.value;
		entry = {Property: "Query", Value: entry};
		match = findPropObjInArray(props, "Property", "Query");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
		
		el = document.forms.taskmode;
		el = el.querySelector('input[name=mode]:checked');
		entry = {Property: "Mode", Value: el.value};
		match = findPropObjInArray(props, "Property", "Mode");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
	}else if(sub === "Directory"){
		entry = {Property: "Subtype", Value: "Pick"};
		match = findPropObjInArray(props, "Property", "Subtype");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);

		el = document.getElementById("taskprefix");
		entry = el.innerText;
		entry = {Property: "Prefix", Value: entry};
		match = findPropObjInArray(props, "Property", "Prefix");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);

		el = document.getElementById("taskpath");
		entry = el.value;
		entry = {Property: "Path", Value: entry};
		match = findPropObjInArray(props, "Property", "Path");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);

		el = document.getElementById("taskfolder");
		entry = el.value;
		entry = {Property: "Folder", Value: entry};
		match = findPropObjInArray(props, "Property", "Folder");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
		
		el = document.getElementById("tasksegctl");
		entry = el.value;
		entry = {Property: "def_segout", Value: entry};
		match = findPropObjInArray(props, "Property", "def_segout");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
		
		el = document.getElementById("taskmod");
		if(el.checked){
			el = document.getElementById("tasknomod");
			if(el.checked)
				entry = {Property: "NoModLimit", Value: 1};
			else
				entry = {Property: "NoModLimit", Value: 0};
			match = findPropObjInArray(props, "Property", "NoModLimit");
			if(match && match.RID)
				entry.RID = match.RID;
			newProps.push(entry);
			el = document.getElementById("taskmodctl");
			entry = {Property: "Modified", Value: el.value};
		}else{
			entry = {Property: "Modified", Value: 0};
		}
		match = findPropObjInArray(props, "Property", "Modified");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);

		el = document.getElementById("taskselDate");
		if(el.checked)
			entry = {Property: "Date", Value: 1};
		else
			entry = {Property: "Date", Value: 0};
		match = findPropObjInArray(props, "Property", "Date");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
		
		el = document.getElementById("taskselSequencial");
		if(el.checked)
			entry = {Property: "Rerun", Value: 1};
		else
			entry = {Property: "Rerun", Value: 0};
		match = findPropObjInArray(props, "Property", "Rerun");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);

		el = document.getElementById("taskselRerun");
		if(el.checked)
			entry = {Property: "Sequencial", Value: 1};
		else
			entry = {Property: "Sequencial", Value: 0};
		match = findPropObjInArray(props, "Property", "Sequencial");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);

		el = document.getElementById("taskselFirst");
		if(el.checked)
			entry = {Property: "First", Value: 1};
		else
			entry = {Property: "First", Value: 0};
		match = findPropObjInArray(props, "Property", "First");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);

		el = document.getElementById("taskrandlim");
		if(el.checked){
			el = document.getElementById("ranlimctl");
			entry = {Property: "Random", Value: el.value};
		}else
			entry = {Property: "Random", Value: 0};
		match = findPropObjInArray(props, "Property", "Random");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
	}else if(sub === "Command"){
		entry = {Property: "Subtype", Value: "Command"};
		match = findPropObjInArray(props, "Property", "Subtype");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
		
		el = document.getElementById("taskcmd");
		entry = el.value;
		entry = {Property: "Command", Value: entry};
		match = findPropObjInArray(props, "Property", "Command");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
	}else if(sub === "Open"){
		entry = {Property: "Subtype", Value: "Open"};
		match = findPropObjInArray(props, "Property", "Subtype");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
		
		el = document.getElementById("taskopen");
		entry = el.value;
		entry = {Property: "Path", Value: entry};
		match = findPropObjInArray(props, "Property", "Path");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
	}else if(sub === "Execute"){
		entry = {Property: "Subtype", Value: "Execute"};
		match = findPropObjInArray(props, "Property", "Subtype");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
		
		el = document.getElementById("taskexec");
		entry = el.value;
		entry = {Property: "Command", Value: entry};
		match = findPropObjInArray(props, "Property", "Path");
		if(match && match.RID)
			entry.RID = match.RID;
		newProps.push(entry);
	}else{
		return;
	}
	
	let changed = false;
	// remove RID items in itemProps.categories not in the table
	for(let i = 0; i<itemProps.task.length; i++){
		let RID = itemProps.task[i].RID;
		if(RID){
			let j = 0;
			for(j=0; j<newProps.length; j++){
				if(newProps[j].RID){
					if(RID === newProps[j].RID){
						break;
					}
				}
			}
			if(j == newProps.length){
				// delete RID
				let api = "library/delete/task/"+RID;
				let resp = await fetchContent(api);
				if(resp){
					if(!resp.ok){
						alert("Got an error saving data to server.\n"+resp.statusText);
						return;
					}
				}else{
					alert("Failed to save data to the server.");
					return;
				}
				changed = true;
			}
		}
	}
	// add ID items in table that have no RID, or update if values if changed
	for(let i=0; i<newProps.length; i++){
		let RID = newProps[i].RID;
		let prop = newProps[i].Property;
		let val = newProps[i].Value;
		let oldchg = false;
		if(RID){ // check for value change
			for(let j=0; j<itemProps.task.length; j++){
				if(itemProps.task[j].RID == RID){
					if(val !== itemProps.task[j].Value)
						oldchg = true;
					break;
				}
			}
		}
		if(!RID || oldchg){
			if(!prop.length || !val.length)
				continue;
			// add ID
			let api = "library/set/task";
			if(RID)
				api += "/"+RID;
			let data = {Property: prop, Value: val, ID: itemProps.ID};
			let resp = await fetchContent(api, {
				method: 'POST',
				body: JSON.stringify(data),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
			if(resp){
				if(!resp.ok){
					alert("Got an error saving task to server.\n"+resp.statusText);
					return;
				}
			}else{
				alert("Failed to save task to the server.");
				return;
			}
			changed = true;
		}
	}
	if(changed){
		// refresh itemProps.task and reload section display
		let reload = await itemFetchProps(itemProps.ID);
		if(reload){
			if(itemProps.canEdit)
				reload.canEdit = true;
			itemProps = reload;
			reloadItemSection(el, "task");
			alert("Item has been updated.");
		}
	}
}

async function saveItemPlaylist(evt){
	evt.preventDefault();
	evt.stopPropagation();
	if(!itemProps.ID){
		alert("Something is wrong: You should not have been able to get here without first having created the playlist item.");
		return;
	}
	let changed = false;
	let el = document.getElementById("itemplaylist");
	let items = el.children;
	let old = itemProps.playlist;
	// remove rows in itemProps.playlist not in the table
	for(let i = 0; i<old.length; i++){
		for(j=0; j<items.length; j++){
			let idx = items[j].getAttribute("data-idx");	// new rows will have a value of -1 here
			if(idx == i)
				break;	// new list has reference to old row index, maybe not in the same place, but it's still there
		}
		if(j == items.length){
			// not in new list: delete row
			let row = old[i];
			for(let k=0; k<row.length; k++){
				let prop = row[k];
				let rid = prop.RID;
				let api = "library/delete/playlist/"+rid;
				let resp = await fetchContent(api);
				if(resp){
					if(!resp.ok){
						alert("Got an error saving data to server.\n"+resp.statusText);
						return;
					}
				}else{
					alert("Failed to save data to the server.");
					return;
				}
				changed = true;
			}
		}
	}

	// add rows or update rows (having data-idx > -1) rows in table.
	for(i=0; i<items.length; i++){
		let idx = items[i].getAttribute("data-idx");	// new rows will have a value of -1 here
		if(idx == -1){
			// add new
			let propObj = flatPlist[i];
			let keys = Object.keys(propObj);
			for(let k=0; k<keys.length; k++){
				let key = keys[k];
				let val = propObj[key];
				let api = "library/set/playlist";
				let data = {Position: i, Property: key, Value: val, ID: itemProps.ID};
				let resp = await fetchContent(api, {
					method: 'POST',
					body: JSON.stringify(data),
					headers: {
						"Content-Type": "application/json",
						"Accept": "application/json"
					}
				});
				if(resp){
					if(!resp.ok){
						alert("Got an error saving playlist properties to server.\n"+resp.statusText);
						return;
					}
				}else{
					alert("Failed to save playlist properties to the server.");
					return;
				}
				changed = true;
				
			}
		}else if(idx != i){
			// update existing to new playlist index position
			let row = old[idx];
			for(let k=0; k<row.length; k++){
				// change the Position value of each property, using it's RID database row id
				let rid = row[k].RID;
				let api = "library/set/playlist/"+rid;
				let data = {Position: i};
				let resp = await fetchContent(api, {
					method: 'POST',
					body: JSON.stringify(data),
					headers: {
						"Content-Type": "application/json",
						"Accept": "application/json"
					}
				});
				if(resp){
					if(!resp.ok){
						alert("Got an error saving playlist properties to server.\n"+resp.statusText);
						return;
					}
				}else{
					alert("Failed to save playlist properties to the server.");
					return;
				}
				changed = true;
			}
		}
	}
	if(changed){
		// update duration
		let api = "library/pldurcalc/"+itemProps.ID;
		let resp = await fetchContent(api);
		if(resp){
			if(!resp.ok){
				alert("Got an error updating playlist duration data to server.\n"+resp.statusText);
				return;
			}
		}else{
			alert("Failed to update playlist duration data to the server.");
			return;
		}
		// refresh itemProps.playlist and reload section display
		let reload = await itemFetchProps(itemProps.ID);
		if(reload){
			if(itemProps.canEdit)
				reload.canEdit = true;
			itemProps = reload;
			reloadItemSection(el, "playlist");
			alert("Item has been updated.");
		}
	}
}

async function saveItemCat(evt){
	let changed = false;
	evt.preventDefault();
	evt.stopPropagation();
	let el = document.getElementById("catitemform");
	el = el.parentNode;
	// get a table element
	let table = el.getElementsByTagName("table");
	if(table.length == 0)
		return;	// no table in element!
	if(!itemProps.ID)
		alert("You must save the item (general settings) before trying to change it's categories.");
	table = table[0]; // should be only one table in el.
	let rows = table.firstElementChild.childNodes;
	// remove RID items in itemProps.categories not in the table
	for(let i = 0; i<itemProps.categories.length; i++){
		if(itemProps.categories[i].RID){
			let j = 0;
			for(j=1; j<rows.length; j++){ // skip header
				let div = rows[j].childNodes[0].firstElementChild;
				let RID = div.getAttribute("data-rid");
				if(RID == itemProps.categories[i].RID){
					break;
				}
			}
			if(j == rows.length){
				// delete RID
				let api = "library/delete/category_item/"+itemProps.categories[i].RID;
				let resp = await fetchContent(api);
				if(resp){
					if(!resp.ok){
						alert("Got an error saving data to server.\n"+resp.statusText);
						return;
					}
				}else{
					alert("Failed to save data to the server.");
					return;
				}
				changed = true;
			}
		}
	}
	// add ID items in table that have no RID
	for(let j=1; j<rows.length; j++){	// again, we skip the header row
		let div = rows[j].childNodes[0].firstElementChild;
		let RID = div.getAttribute("data-rid");
		let ID = div.getAttribute("data-id");
		if(!RID && ID){
			// add ID
			let api = "library/set/category_item";
			let resp = await fetchContent(api, {
				method: 'POST',
				body: JSON.stringify({Category: ID, Item: itemProps.ID}),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
			if(resp){
				if(!resp.ok){
					alert("Got an error saving category to server.\n"+resp.statusText);
					return;
				}
			}else{
				alert("Failed to save categoy to the server.");
				return;
			}
			changed = true;
		}
	}
	if(changed){
		// refresh itemProps.categories and reload section display
		let reload = await itemFetchProps(itemProps.ID);
		if(reload){
			if(itemProps.canEdit)
				reload.canEdit = true;
			itemProps = reload;
			reloadItemSection(el, "categories");
			alert("Item has been updated.");
		}
	}
}

async function saveItemCust(evt){
	let changed = false;
	evt.preventDefault();
	evt.stopPropagation();
	let el = document.getElementById("custitemform");
	el = el.parentNode;
	// get a table element
	let table = el.getElementsByTagName("table");
	if(table.length == 0)
		return;	// no table in element!
	if(!itemProps.ID)
		alert("You must save the item (general settings) before trying to change it's custom properties.");
	table = table[0]; // should be only one table in el.
	let rows = table.firstElementChild.childNodes;
	// remove RID items in itemProps.categories not in the table
	for(let i = 0; i<itemProps.meta.length; i++){
		if(itemProps.meta[i].RID){
			let j = 0;
			for(j=1; j<rows.length; j++){ // skip header
				let div = rows[j].childNodes[0].firstElementChild;
				let RID = div.getAttribute("data-rid");
				if(RID == itemProps.meta[i].RID){
					break;
				}
			}
			if(j == rows.length){
				// delete RID
				let api = "library/delete/meta/"+itemProps.meta[i].RID;
				let resp = await fetchContent(api);
				if(resp){
					if(!resp.ok){
						alert("Got an error saving data to server.\n"+resp.statusText);
						return;
					}
				}else{
					alert("Failed to save data to the server.");
					return;
				}
				changed = true;
			}
		}
	}
	// add ID items in table that have no RID, or update if values if changed
	let parent = itemProps.Type;
	if(!["artist", "album", "category"].includes(parent))
			parent = "toc";
	for(let i=1; i<rows.length; i++){	// again, we skip the header row
		let cols = rows[i].childNodes;
		let propel = cols[0].firstElementChild;
		let prop = propel.value;
		let RID = propel.getAttribute("data-rid");
		let val = cols[1].firstElementChild.value;
		let oldchg = false;
		if(RID){ // check for value change
			for(let j=0; j<itemProps.meta.length; j++){
				if(itemProps.meta[j].RID == RID){
					if(val !== itemProps.meta[j].Value)
						oldchg = true;
					else if(prop !== itemProps.meta[j].Property)
						oldchg = true;
					break;
				}
			}
		}
		if(!RID || oldchg){
			if(!prop.length || !val.length)
				continue;
			// add ID
			let api = "library/set/meta";
			if(RID)
				api += "/"+RID;
			let data = {Parent: parent, Property: prop, Value: val, ID: itemProps.ID};
			let resp = await fetchContent(api, {
				method: 'POST',
				body: JSON.stringify(data),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
			if(resp){
				if(!resp.ok){
					alert("Got an error saving custom properties to server.\n"+resp.statusText);
					return;
				}
			}else{
				alert("Failed to save custom properties to the server.");
				return;
			}
			changed = true;
		}
	}
	if(changed){
		// refresh itemProps.categories and reload section display
		let reload;
		if(parent !== "toc"){
			reload = await itemPropsMeta(itemProps.ID, parent);
			if(reload){
				itemProps.meta = reload;
				reloadItemSection(el, "custom");
				alert("Item has been updated.");
			}
		}else{
			reload = await itemFetchProps(itemProps.ID, parent);
			if(reload){
				if(itemProps.canEdit)
					reload.canEdit = true;
				itemProps = reload;
				reloadItemSection(el, "custom");
				alert("Item has been updated.");
			}
		}
	}
}

async function saveItemRest(evt){
	let changed = false;
	evt.preventDefault();
	evt.stopPropagation();
	let el = document.getElementById("restitemform");
	el = el.parentNode;
	// get a table element
	let table = el.getElementsByTagName("table");
	if(table.length == 0)
		return;	// no table in element!
	if(!itemProps.ID)
		alert("You must save the item (general settings) before trying to change it's rest locations.");
	table = table[0]; // should be only one table in el.
	let rows = table.firstElementChild.childNodes;
	// remove RID items in itemProps.categories not in the table
	for(let i = 0; i<itemProps.rest.length; i++){
		if(itemProps.rest[i].RID){
			let j = 0;
			for(j=1; j<rows.length; j++){ // skip header
				let div = rows[j].childNodes[0].firstElementChild;
				let RID = div.getAttribute("data-rid");
				if(RID == itemProps.rest[i].RID){
					break;
				}
			}
			if(j == rows.length){
				// delete RID
				let api = "library/delete/rest/"+itemProps.rest[i].RID;
				let resp = await fetchContent(api);
				if(resp){
					if(!resp.ok){
						alert("Got an error saving data to server.\n"+resp.statusText);
						return;
					}
				}else{
					alert("Failed to save data to the server.");
					return;
				}
				changed = true;
			}
		}
	}
	// add ID items in table that have no RID
	for(let j=1; j<rows.length; j++){	// again, we skip the header row
		let div = rows[j].childNodes[0].firstElementChild;
		let RID = div.getAttribute("data-rid");
		let ID = div.getAttribute("data-id");
		if(!RID && ID){
			// add ID
			let api = "library/set/rest";
			let resp = await fetchContent(api, {
				method: 'POST',
				body: JSON.stringify({Location: ID, Item: itemProps.ID}),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
			if(resp){
				if(!resp.ok){
					alert("Got an error saving rest location to server.\n"+resp.statusText);
					return;
				}
			}else{
				alert("Failed to save rest location to the server.");
				return;
			}
			changed = true;
		}
	}
	if(changed){
		// refresh itemProps.categories and reload section display
		let reload = await itemFetchProps(itemProps.ID);
		if(reload){
			if(itemProps.canEdit)
				reload.canEdit = true;
			itemProps = reload;
			reloadItemSection(el, "rest");
			alert("Item has been updated.");
		}
	}
}

async function saveItemSched(evt){
	let changed = false;
	evt.preventDefault();
	evt.stopPropagation();
	let el = document.getElementById("scheditemform");
	el = el.parentNode;
	// get a table element
	let table = el.getElementsByTagName("table");
	if(table.length == 0)
		return;	// no table in element!
	if(!itemProps.ID)
		alert("You must save the item (general settings) before trying to change it's rest locations.");
	table = table[0]; // should be only one table in el.
	let rows = table.firstElementChild.childNodes;
	// remove RID items in itemProps.categories not in the table
	for(let i = 0; i<itemProps.sched.length; i++){
		if(itemProps.sched[i].RID){
			let j = 0;
			for(j=1; j<rows.length; j++){ // skip header
				let div = rows[j].childNodes[4].firstElementChild;
				let RID = div.getAttribute("data-rid");
				if(RID == itemProps.sched[i].RID){
					break;
				}
			}
			if(j == rows.length){
				// delete RID
				let api = "library/delete/schedule/"+itemProps.sched[i].RID;
				let resp = await fetchContent(api);
				if(resp){
					if(!resp.ok){
						alert("Got an error saving data to server.\n"+resp.statusText);
						return;
					}
				}else{
					alert("Failed to save data to the server.");
					return;
				}
				changed = true;
			}
		}
	}
	// add ID items in table that have no RID
	let locID = 0;
	for(let j=1; j<rows.length; j++){	// again, we skip the header row
		let cols = rows[j].childNodes;
		let div = cols[4].firstElementChild;
		let RID = div.getAttribute("data-rid");
		let oldchg = false;
		let dayel = cols[0].firstElementChild // day/wk
		let day = parseInt(dayel.value);
		if(day){
			let wk = parseInt(dayel.nextSibling.nextSibling.value);
			day += (wk-1) * 7;
		}
		let postdat = {	Day: day,
								Date: cols[1].firstElementChild.value,
								Month: cols[2].firstElementChild.value,
								Hour: cols[3].firstElementChild.value,
								Minute: cols[4].firstElementChild.value,
								Fill: cols[5].firstElementChild.value,
								Priority: cols[6].firstElementChild.value };
		if(RID){ // check for value changes
			for(let i=0; i<itemProps.sched.length; i++){
				if(itemProps.sched[i].RID == RID){
					let keys = Object.keys(postdat);
					for(let k=0; k<keys.length; k++){
						if(postdat[keys[k]] != itemProps.sched[i][keys[k]]){
							oldchg = true;
							break;
						}
					}
					break;
				}
			}
		}
		if(!RID || oldchg){
			// add ID
			if(locID == 0){
				// get locID for named location
				let api = "library/get/locations";
				let resp = await fetchContent(api, {
					method: 'POST',
					body: JSON.stringify({Name: locName.getValue()}),
					headers: {
						"Content-Type": "application/json",
						"Accept": "application/json"
					}
				});
				if(resp){
					if(!resp.ok){
						alert("Got an error retreaving location ID from server.\n"+resp.statusText);
						return;
					}
					let repdata = await resp.json();
					if(repdata.length && repdata[0].id){
						locID = repdata[0].id;
					}else{
						alert("Received null location ID from server.");
						return;
					}
				}else{
					alert("Failed to retreaving location ID from server.");
					return;
				}
			}
			// add itemID and location back in
			postdat.Item = itemProps.ID;
			postdat.Location = locID;
			let api = "library/set/schedule";
			if(RID)
				api += "/"+RID;
			let resp = await fetchContent(api, {
				method: 'POST',
				body: JSON.stringify(postdat),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
			if(resp){
				if(!resp.ok){
					alert("Got an error saving rest location to server.\n"+resp.statusText);
					return;
				}
			}else{
				alert("Failed to save rest location to the server.");
				return;
			}
			changed = true;
		}
	}
	if(changed){
		// refresh itemProps.categories and reload section display
		let reload = await itemFetchProps(itemProps.ID);
		if(reload){
			if(itemProps.canEdit)
				reload.canEdit = true;
			itemProps = reload;
			reloadItemSection(el, "schedule");
			alert("Item has been updated.");
		}
	}
}

async function saveItemProperties(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let el = document.getElementById("itemPropName");
	let was = itemProps.Name;
	let is = el.value;
	let obj ={};
	if(was === undefined) was = "";
	if(was === null) was = "";
	if(was != is)
		obj.Name = is;
	if(Object.keys(obj).length){	// only save if there are changes
		let api = "library/set/"+itemProps.Type;
		if(itemProps.ID)
			api += "/"+itemProps.ID;
		let resp = await fetchContent(api, {
				method: 'POST',
				body: JSON.stringify(obj),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp){
			if(resp.ok){
				let reload = await resp.json();
				if(reload.insertId){
					el = document.getElementById("itemPropID");
					el.innerText = reload.insertId;
				}
				if(!reload.affectedRows)
					alert("Failed to update or get ID of new item.\n");
				else
					alert(itemProps.Type+" has been updated.");
			}else{
				alert("Got an error saving data to server.\n"+resp.statusText);
			}
		}else{
			alert("Failed to save data to the server.");
		}
	}
}

async function saveEncProperties(evt){
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
	}
	let enstart = false;
	let makepl = false;
	let err = false;
	let form = document.getElementById("enitemform");
	if(!itemProps.canEdit)
		return;
	let ref = itemProps.UID;
	let studio = studioName.getValue();
	if(ref && studio.length){
		let hexStr =  ("00000000" + ref.toString(16)).substr(-8);
		for(let i = 0 ; i < form.length ; i++){
			let el = form[i];
			let itemName = el.name;
			let is = el.value;
			if(el.type == "checkbox"){
				if(el.checked)
					is = 1;
				else
					is = 0;
			}
			
			if(itemName === "MakePLCheck"){
				makepl = is;
				continue;
			}else if(itemName === "Ports"){
				is = el.parentElement.userPortList;
			}else if(itemName === "MakePL" && !makepl){
				is = "";
			}else if(itemName === "StartCheck"){
				enstart = is;
				continue;
			}else if(itemName === "Start"){
				if(enstart){
					// convert to unixtime from local ISO format 
					is = Date.parse(is) / 1000;
				}else
					is = "0";
			}else if(itemName === "submit"){
				continue;
			}else if(!itemName){
				continue;
			}
			
			let was = itemProps[itemName];
			if(was === undefined) was = "";
			if(was === null) was = "";
			
			if(was !== is){
				is = encodeURIComponent(is);
				let resp = await fetchContent("studio/"+studio+"?cmd=setmeta%20"+hexStr+"%20"+itemName+"%20"+is);
				if(resp instanceof Response){
					if(!err && resp.ok){
						let text = await resp.text();
						if(text.search("OK") != 0){
							alert("Error setting "+itemName);
							err = true;
						}
					}else
						err = true;
				}
			}
		}
		if(evt){
			if(!err){
				let resp = await fetchContent("studio/"+studio+"?cmd=initrec%20"+hexStr);
				if(resp instanceof Response){
					if(resp.ok){
						let text = await resp.text();
						if(text.search("OK<br>") == 0){
							alert("encoder/recorder initialized.");
							closeInfo();
							return;
						}
					}
				}
				alert("Failed to initialize encoder/recorder.");
			}else
				alert("Failed to set all properties.");
		}
	}
}

async function reloadEncProperties(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let makepl = false;
	let form = document.getElementById("enitemform");
	if(!itemProps.canEdit)
		return;
	let ref = itemProps.UID;
	let studio = studioName.getValue();
	if(ref && studio.length){
		let hexStr =  ("00000000" + ref.toString(16)).substr(-8);
		// save the pipeline property
		itemProps.Pipeline = document.getElementById("enPipeline").value;
		is = encodeURIComponent(itemProps.Pipeline);
		let resp = await fetchContent("studio/"+studio+"?cmd=setmeta%20"+hexStr+"%20Pipeline%20"+is);
		if(resp instanceof Response){
			if(resp.ok){
				let text = await resp.text();
				if(text.search("OK") != 0){
					alert("Error setting "+itemName);
					return;
				}
			}else
				return;
		}
		// save all the other properties in the form
		saveEncProperties();
	}
	let el = document.getElementById("infopane");
	let da = document.getElementById("infodata");
	showEncoderItem(el, da);
}

async function saveItemInstance(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let els = document.getElementById("institemform");
	let ref = els.getAttribute("data-id");
	let meta = false;
	let studio = studioName.getValue();
	ref = parseInt(ref);
	if(ref)
		meta = studioStateCache.meta[ref];
	if(meta && studio.length){
		let changed = false;
		let hexStr =  ("00000000" + ref.toString(16)).substr(-8);
		els = els.elements;
		for(let i = 0 ; i < els.length ; i++){
			let item = els.item(i);
			let was = meta[item.name];
			if(was === undefined) was = "";
			if(was === null) was = "";
			let is = item.value;
			let itemName = item.name;

			if(["Duration", "SegIn"].includes(item.name)){
				// handle time format to seconds
				is = timeParse(is);
				if(Math.floor(is * 10) == Math.floor(was * 10)) // changed within 0.1 seconds?
					is = was;
			}else if(item.name === "SegOut"){
				// handle segout and fade checkbox
				el = document.getElementById("instItemFade");
				is = timeParse(is);
				if(el.checked){
					// change property of interest from SegOut to Fade
					itemName = "FadeOut";
					was = meta[itemName];
				}
				if(Math.floor(is * 10) == Math.floor(was * 10)) // changed within 0.1 seconds?
					is = was;
			}else if(item.name == "fade"){
				// ignore this item... it 's handled by SegOut
				continue;
			}else if(item.name == "NoLog"){
				if(item.checked)
					is = 1;
				else
					is = 0;
			}
			if(was != is){
				is = encodeURIComponent(is);
				await fetchContent("studio/"+studio+"?cmd=setmeta%20"+hexStr+"%20"+item.name+"%20"+is);
				changed = true;
			}
		}
		if(changed)
			await fetchContent("studio/"+studio+"?cmd=logmeta%20"+hexStr);
	}
}

async function itemDelete(evt){
	evt.preventDefault();
	evt.stopPropagation();
	if(itemProps && itemProps.ID){
		let data = {};
		let reassignID = 0;
		let el = document.getElementById("itemreassignbtn");
		if(el)
			reassignID = el.getAttribute("data-id");
		let type = itemProps.Type;
		if(["playlist", "task", "file"].includes(type))
			type = "toc";
		if((type === "artist") || (type === "album")){
			if(itemProps.ID < 2){
				alert(`You can not delete the Artist or Album that represents "none" (ID #1)`);
				return;
			}
			if(!reassignID){
				alert(`Artist or album items must be reassigned to another when deleted.`);
				return;
			}
		}
		if(reassignID)
			data.reassign = reassignID;
		el = document.getElementById("delfiletoo");
		if(el && el.checked)
			data.remove = 1;
		if(confirm("Are you sure you want to delete "+itemProps.Name+"?")){
			let api = "library/delete/"+type+"/"+itemProps.ID;
			let resp = await fetchContent(api, {
				method: 'POST',
				body: JSON.stringify(data),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
			if(resp){
				if(!resp.ok){
					alert("Got an error deleteing item from server.\n"+resp.statusText);
				}else{
					alert("Item has been deleted.");
					itemProps = false;
					closeInfo();
				}
			}else{
				alert("Failed to delete item from server.");
			}
		}
	}
}

async function itemExport(evt){
	evt.preventDefault();
	evt.stopPropagation();
	if(itemProps && itemProps.ID){
		let expas = "json";
		let offset = 0.0;
		let el = document.getElementById("downloadtype");
		if(el)
			expas = el.value;
		el = document.getElementById("itemtimeoffset");
		if(el)
			offset = parseFloat(el.value);
		let data = {offset: offset, save: 1};
		if(expas !== "file")
			data["export"] = expas;
		let api = "library/download/"+itemProps.ID;
		let resp = await fetchContent(api, {
			method: 'POST',
			body: JSON.stringify(data),
			headers: {
				"Content-Type": "application/json",
				"Accept": "application/json"
			}
		});
		if(resp){
			if(!resp.ok){
				alert("Got an error retreaving file from server.\n"+resp.statusText);
				return;
			}
			let blob = await resp.blob();
			let contentDisposition = resp.headers.get("content-disposition");
			let fileName = contentDisposition.substring(contentDisposition.indexOf("filename=")+9);
			el = document.getElementById("filedltarget");
			el.href = window.URL.createObjectURL(blob);
			el.download = fileName;
			el.click();
		}else{
			alert("Failed to retreaving file from server.");
			return;
		}
	}
}

function itemRemoveRow(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let rowel = evt.target.parentElement.parentElement;
	let tblel = rowel.parentElement.parentElement;
	tblel.deleteRow(rowel.rowIndex);
}

function itemAddCat(evt){
	// close search-list menu
	let el = document.getElementById("addItemCatBtn");
	toggleShowSearchList({target: el});
	// add row
	let newRow = {Name: evt.target.innerText, ID: evt.target.getAttribute("data-id"), RID: 0, Added: false};
	let div = document.getElementById("itemcatlist");
	actions = `<button class="editbutton" onclick="itemRemoveRow(event)">-</button>`;
	let fields = {Name: "<div data-id='$ID' data-rid='$RID'>$val</div>", Added: unixTimeToDateStr};
	insertTableRow(newRow, div, 1, {Name: "Category", Added: "Added", RID: false, ID: false}, false, actions, fields);
}

function itemAddRest(evt){
	// close search-list menu
	let el = document.getElementById("addItemRestBtn");
	toggleShowSearchList({target: el});
	// add row
	let newRow = {Name: evt.target.innerText, ID: evt.target.getAttribute("data-id"), RID: 0, Added: false};
	let div = document.getElementById("itemrestlist");
	actions = `<button class="editbutton" onclick="itemRemoveRow(event)">-</button>`;
	let fields = {Name: "<div data-id='$ID' data-rid='$RID'>$val</div>", Added: unixTimeToDateStr};
	insertTableRow(newRow, div, 1, {Name: "Location", Added: "Added", RID: false, ID: false}, false, actions, fields);
}

function itemAddCust(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let rowel = evt.target.parentElement.parentElement;
	let div = document.getElementById("itemcustlist");
	// add row
	let newRow = {Property: "", Value: "", RID: 0};
	actions = `<button class="editbutton" onclick="itemRemoveRow(event)">-</button>`;
	let fields = { Property: `<input type='text' size='14' data-rid='$RID' value="$attval" onblur="liveSearchList(event, '')" onkeyup="liveSearchList(event, this.value, getMetaPropsForParent, '`+itemProps.Type+`')"></input>
											<div class="liveSearch"></div>`, 
							Value: "<input type='text' size='14' value='$attval'</input>"};
	insertTableRow(newRow, div, 1, {Property: "Property", Value: "Value", RID: false}, false, actions, fields);
}

function itemAddSched(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let rowel = evt.target.parentElement.parentElement;
	let div = document.getElementById("itemschedlist");
	// add row
	let newRow = {Day: "0", Date: "0", Month: "0", Hour: "0", Minute: "0", Fill: false, Priority: "5", RID: ""};
	actions = `<button class="editbutton" onclick="itemRemoveRow(event)">-</button>`;
	let fields = {	Day: itemWDOMRender, // Weekday of Month
						Date: itemDateRender, 
						Month: itemMonRender,
						Hour: itemHrRender,
						Minute: "<input type='number' min='0' max='59' name='min' value='$val' data-rid='$RID'></input>", // number
						Fill: "<input type='number' min='0' max='1440' name='fill' value='$val'></input>",
						Priority: itemPrioRender};
	insertTableRow(newRow, div, 1, {Day: "Day", Date: "Date", Month: "Month", Hour: "Hour", Minute: "Minute", Fill: "Fill", Priority: "Prio", RID: false}, false, actions, fields);
}

function itemChangeArtist(evt){
	// close search-list menu
	let el = document.getElementById("itemArtistBtn");
	let nm = document.getElementById("itemArtistName");
	toggleShowSearchList({target: el});
	nm.innerText = evt.target.innerText;
	let id = evt.target.getAttribute("data-id");
	el.setAttribute("data-id", id);
}

function itemChangeAlbum(evt){
	// close search-list menu
	let el = document.getElementById("itemAlbumBtn");
	let nm = document.getElementById("itemAlbumName");
	toggleShowSearchList({target: el});
	nm.innerText = evt.target.innerText;
	let id = evt.target.getAttribute("data-id");
	el.setAttribute("data-id", id);
}

function itemReplaceSelection(evt){
	// close search-list menu
	let el = document.getElementById("itemreassignbtn");
	toggleShowSearchList({target: el});
	el.innerText = evt.target.innerText;
	let id = evt.target.getAttribute("data-id");
	el.setAttribute("data-id", id);
}

async function itemReplace(evt){
	evt.preventDefault();
	evt.stopPropagation();
	if(!evt.target.files.length)
		return;
	let form = document.getElementById("replaceform");
	let repDest = document.getElementById("filereplacedest");
	let formData = new FormData(form);
	evt.target.value = [];
	evt.target.disabled = true;
	let resp;
	resp = await fetchContent("tmpupload", {
			method: 'POST',
			body: formData
		});
	if(resp){
		if(resp.ok){
			let files = await resp.json();
			if(files.length){
				if(repDest.value && repDest.value.length)
					resp = await fetchContent("library/import/"+files[0].filename+"?mdir="+repDest.value+"&id="+itemProps.ID);
				else
					resp = await fetchContent("library/import/"+files[0].filename+"?id="+itemProps.ID);
				if(resp && resp.ok){
					// refresh itemProps and reload section display
					let reload = await itemFetchProps(itemProps.ID);
					if(reload){
						if(itemProps.canEdit)
							reload.canEdit = true;
						itemProps = reload;
						let el = form.parentNode;
						reloadItemSection(el, "file");
						// reload cue too.
						let audio = document.getElementById("itemcueplayer");
						let source = document.getElementById("itemcuesource");
						source.src = "library/download/"+itemProps.ID;
						audio.load();
						alert("Item has been updated.");
					}else
						alert("Failed to reload item from the server.");
				}else{
					let msg = "Failed to reassign item to file.";
					if(resp && resp.status)
						msg += "\n"+resp.statusText;
					alert(msg);
				}
				evt.target.disabled = false;
			}else{
				alert("Failed to get uploaded file name back from the server.");
				evt.target.disabled = false;
			}
		}else{
			alert("Got an error posting data to server.\n"+resp.statusText);
			evt.target.disabled = false;
		}
	}else{
		alert("Failed to post data tp the server.");
		evt.target.disabled = false;
	}
}

function taskModifiedCheckChange(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let target = evt.target;
	let ctl = document.getElementById("taskmodctl");
	let nom = document.getElementById("tasknomod");
	if(target.checked){
		ctl.disabled = false;
		nom.disabled = false;
	}else{
		ctl.disabled = true;
		nom.disabled = true;
	}
}

function taskRandLimCheckChange(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let target = evt.target;
	let el = document.getElementById("ranlimctl");
	if(target.checked)
		el.disabled = false;
	else
		el.disabled = true;
}

async function itemSetHash(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let target = evt.target;
	let row = target.parentNode.parentNode;
	let hashcol = row.children[1];
	let api = "library/hash/"+itemProps.ID;
	let resp = await fetchContent(api);
	if(resp){
		if(!resp.ok){
			alert("Got an error fetching hash from server.\n"+resp.statusText);
			return;
		}
	}else{
		alert("Failed to fetch hash from the server.");
		return;
	}
	hashcol.innerText = await resp.text();
	target.outerHTML = `<button class="editbutton" onclick="itemRemoveHash(event)">Clear</button>`;
}

async function itemRemoveHash(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let target = evt.target;
	let row = target.parentNode.parentNode;
	let hashcol = row.children[1];
	hashcol.innerText = "";
	target.outerHTML = `<button class="editbutton" onclick="itemSetHash(event)">Set</button>`;
}

function updatePLDuration(){
	let plprops = [];
	
	let list = document.getElementById("itemplaylist");
	let els = list.querySelectorAll('input[type=checkbox]:checked');
	if(els){
		for(let i=0; i<els.length; i++){
			let item = els[i].parentElement.parentElement;
			let idx = item.getAttribute("data-idx");
			plprops.push(flatPlist[idx]);
		}
	}
	if(!plprops.length)
		plprops = flatPlist;
	el = document.getElementById("pldur");
	if(el)
		el.innerText = timeFormat(updateListDuration(plprops));
}

function plSelectAll(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let list = document.getElementById("itemplaylist");
	let els = list.querySelectorAll('input[type=checkbox]:not(:checked)');
	if(els){
		for(let i=0; i<els.length; i++)
			els[i].checked = true;
	}
	updatePLDuration();
}

function plUnselectAll(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let list = document.getElementById("itemplaylist");
	let els = list.querySelectorAll('input[type=checkbox]:checked');
	if(els){
		for(let i=0; i<els.length; i++)
			els[i].checked = false;
	}
	updatePLDuration();
}

function plDelItems(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let target = evt.target;	// the button
	
	let list = document.getElementById("itemplaylist");
	let els = list.querySelectorAll('input[type=checkbox]:checked');
	if(els){
		for(let i=0; i<els.length; i++){
			let el = els[i].parentElement.parentElement;  // the list item
			let idx = Array.prototype.indexOf.call(list.children, el);
			if(idx>-1){
				flatPlist.splice(idx, 1);
				list.removeChild(el);
			}
		}
		updatePLDuration();
	}
}

function plMoveItem(obj, fromIdx, toIdx){
	
	if(fromIdx === false)
		return; // pl doesn't accept drops from other places
	flatPlist.splice(toIdx, 0, flatPlist.splice(fromIdx, 1)[0]);
	let items = obj.parentElement.childNodes;
	if(fromIdx < toIdx)
		obj.parentNode.insertBefore(curDrag, obj.nextSibling);
	else
		obj.parentNode.insertBefore(curDrag, obj);
	updatePLDuration();
}

function plDupItems(evt){
	// flatPlist has current list meta data
	evt.preventDefault();
	evt.stopPropagation();
	let list = document.getElementById("itemplaylist");
	let els = list.querySelectorAll('input[type=checkbox]:checked');
	if(els){
		for(let i=0; i<els.length; i++){
			let el = els[i].parentElement.parentElement // the list item
			let parent = el.parentElement; // the list
			let idx = Array.prototype.indexOf.call(parent.children, el);
			if(idx>-1){
				let rec = flatPlist[idx];
				flatPlist.push(flatClone(rec));
				let clone = el.cloneNode(true);
				clone.setAttribute("data-idx", -1); // new entry, no index in old metaData
				let chk = clone.querySelector('input[type=checkbox]:checked');
				if(chk)
					chk.checked = false;
				setDragItemEvents(clone);
				parent.appendChild(clone);
			}
		}
		updatePLDuration();
	}
}

function itemSendToStash(evt){
	evt.preventDefault();
	evt.stopPropagation();
	appendItemToStash(itemProps);
}

async function itemSendToQueue(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let items = [];
	let item = Object.assign({}, itemProps);
	if(item.tmpfile && item.tmpfile.length){
		// create a URL for temp files
		let data = false;
		let resp = await fetchContent("library/tmpmediaurl", {
				method: 'POST',
				body: JSON.stringify({path: item.tmpfile.substring(1)}), // path leading '/' trimmed off
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp){
			if(!resp.ok){
				alert("Got an error retreaving file info from server.\n"+resp.status);
				return;
			}
			data = await resp.text();
		}else{
			alert("Failed to retreaving file info from server.");
			return;
		}
		if(data)
			item.URL = data;
		else
			return;
	}
	items.push(item);
	appendItemsToQueue(items);
}

async function itemSendToPlayer(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let item = Object.assign({}, itemProps);
	if(item.Type != "file")
		// lot a type loadable into a player
		return;
	if(item.tmpfile && item.tmpfile.length){
		// create a URL for temp files
		let data = false;
		let resp = await fetchContent("library/tmpmediaurl", {
				method: 'POST',
				body: JSON.stringify({path: item.tmpfile.substring(1)}), // path leading '/' trimmed off
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp){
			if(!resp.ok){
				alert("Got an error retreaving file info from server.\n"+resp.status);
				return;
			}
			data = await resp.text();
		}else{
			alert("Failed to retreaving file info from server.");
			return;
		}
		if(data)
			item.URL = data;
		else
			return;
	}
	let URL = item.URL;
	if(item.ID)
		URL = "item:///"+item.ID;
	let studio = studioName.getValue();
	if(studio.length){
		fetchContent("studio/"+studio+"?cmd=load -1 "+URL);
	}
}

function plSendToStash(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let list = document.getElementById("itemplaylist");
	let els = list.querySelectorAll('input[type=checkbox]:checked');
	if(els){
		for(let i=0; i<els.length; i++){
			let el = els[i].parentElement.parentElement // the list item
			let parent = el.parentElement; // the list
			let idx = Array.prototype.indexOf.call(parent.children, el);
			if(idx>-1){
				let rec = flatPlist[idx];
				appendItemToStash(rec);
			}
		}
	}
}

async function plImportStash(evt){
	evt.preventDefault();
	evt.stopPropagation();
	if(!itemProps.ID){
		alert("You must save the item (general settings) before trying to add anything to it's playlist.");
		return;
	}
	let stashlist = stashGetSelected();
	if(stashlist.length && itemProps.canEdit){
		let el = document.getElementById("itemplaylist");
		for(let i=0; i<stashlist.length; i++){
			let item = stashlist[i];
			let props = await getItemData(item);
			if(props){
				item = flattenItemForStash(props);	// this works for the playlist entries as well
				flatPlist.push(item);
				let format = `<span style='float: left;'><input type='checkbox'></input></span><span style='float: right;'>$Duration->timeFormat$</span><div style='clear:both;'></div>
									$Name$<br>
									$Artist$`;
				appendDragableItem(true, plMoveItem, item, -1, el, format); // all new items get a -1 index
			}
		}
		updatePLDuration();
	}
}

async function refreshItemArtists(evt){
	// if evt is not an event, this is a call back from a watch var change, rather than a button press
	if(evt && evt.preventDefault){
		evt.preventDefault();
		evt.stopPropagation();
		await getArtistList();
	}else{
		let div = document.getElementById("itemArtistList");
		if(div){
			buildSearchList(div, artListCache.getValue(), itemChangeArtist);
			let el = document.getElementById("itemArtistText");
			filterSearchList({target: el});
		}
	}
}

async function refreshItemAlbums(evt){
	// if evt is not an event, this is a call back from a watch var change, rather than a button press
	if(evt && evt.preventDefault){
		evt.preventDefault();
		evt.stopPropagation();
		await getAlbumList();
	}else{
		let div = document.getElementById("itemAlbumList");
		if(div){
			buildSearchList(div, albListCache.getValue(), itemChangeAlbum);
			let el = document.getElementById("itemAlbumText");
			filterSearchList({target: el});
		}
	}
}

async function refreshItemReassign(evt){
	// if evt is not an event, this is a call back from a watch var change, rather than a button press
	if(evt && evt.preventDefault){
		evt.preventDefault();
		evt.stopPropagation();
		if(itemProps && itemProps.Type){
			// the following getXXXList functions will trigger callback of this function
			// again, with out the event, running the the outer "else" below.
			if(itemProps.Type === "category")
				await getCatList();
			else if(itemProps.Type === "artist")
				 await getArtistList();
			else if(itemProps.Type === "album")
				await getAlbumList();
		}
	}else{
		if(itemProps && itemProps.Type){
			let div = document.getElementById("itemReassignList");
			if(div){
				if(itemProps.Type === "category")
					buildSearchList(div, catListCache.getValue(), itemReplaceSelection);
				else if(itemProps.Type === "artist")
					buildSearchList(div, artListCache.getValue(), itemReplaceSelection);
				else if(itemProps.Type === "album")
					buildSearchList(div, albListCache.getValue(), itemReplaceSelection);
				else
					return;
				let el = document.getElementById("itemReassignText");
				filterSearchList({target: el});
			}
		}
	}
}

async function refreshTaskCats(evt){
	if(evt && evt.preventDefault){
		evt.preventDefault();
		evt.stopPropagation();
		await getCatList();
	}else{
		let div = document.getElementById("taskCatList");
		if(div){
			buildSearchList(div, catListCache.getValue(), itemTaskCatSelect);
			let el = document.getElementById("taskCatText");
			filterSearchList({target: el});
		}
	}
}

async function itemTaskCatSelect(evt){
	// close search-list menu
	let el = document.getElementById("taskCatBtn");
	toggleShowSearchList({target: el});
	// set button text to selection and save catid
	el.setAttribute("data-id", evt.target.getAttribute("data-id"));
	el.innerText = evt.target.innerText;
}

async function refreshAddItemCats(evt){
	// if evt is not an event, this is a call back from a watch var change, rather than a button press
	if(evt && evt.preventDefault){
		evt.preventDefault();
		evt.stopPropagation();
		await getCatList();
	}else{
		let div = document.getElementById("itemCatAddList");
		if(div){
			buildSearchList(div, catListCache.getValue(), itemAddCat);
			let el = document.getElementById("addItemCatText");
			filterSearchList({target: el});
		}
	}
}

async function refreshAddItemRest(evt){
	// if evt is not an event, this is a call back from a watch var change, rather than a button press
	if(evt && evt.preventDefault){
		evt.preventDefault();
		evt.stopPropagation();
		await getLocList();
	}else{
		let div = document.getElementById("itemRestAddList");
		if(div){
			buildSearchList(div, locListCache.getValue(), itemAddRest);
			let el = document.getElementById("addItemRestText");
			filterSearchList({target: el});
		}
	}
}

async function taskGetPathFrefix(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let el = document.getElementById("taskpath");
	let path = el.value;
	if(path && path.length){
		let api = "library/getprefix";
		let resp = await fetchContent(api, {
				method: 'POST',
				body: JSON.stringify({path: path}),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp){
			if(resp.ok){
				let result = await resp.json();
				if(result && result.path){
					el.value = result.path;
					el = document.getElementById("taskprefix");
					el.innerText = result.prefix;
				}
			}else{
				alert("Got an error fetching data from server.\n"+resp.statusText);
			}
		}else{
			alert("Failed to fetch data from the server.");
		}
	}
}

async function refreshItemLocationDep(val){
	if(locName.getPrior() !== val){
		let div = document.getElementById("itemhistlist");
		if(div && itemProps){
			await showItem({tocID: itemProps.ID}, itemProps.canEdit, true);
			await reloadItemSection(div.parentNode, "history");
		}
		div = document.getElementById("itemschedlist");
		if(div && itemProps){
			await showItem({tocID: itemProps.ID}, itemProps.canEdit, true);
			await reloadItemSection(div.parentNode.parentNode, "schedule");
		}
	}
}

async function refreshItemHistory(){
	let div = document.getElementById("itemhistlist");
	if(div && itemProps){
		await showItem({tocID: itemProps.ID}, itemProps.canEdit, true);
		genPopulateTableFromArray(itemProps.history, div);
	}
}

function unlistAlreadyInnerText(divid, value){
	// returns true to prevent listing of value
	let div = document.getElementById(divid);
	if(!div)
		return false;
	if(div.innerText === value)
		return true;
	return false;
}

function unlistAlreadyInTable(divid, value){
	// returns true to prevent listing of value
	let div = document.getElementById(divid);
	let table = div.getElementsByTagName("table");
	if(!table || !table.length)
		return false;
	table = table[0];	// first should be only table
	let rows = table.rows;
	for(let i = 1 ; i < rows.length; i++){
		let firstcol = rows[i].firstElementChild;
		if(firstcol.innerText === value)
			return true;
	}
	return false;
}

function unlistItemName(divid, value){
	if(value === itemProps.Name)
		return true;
	return false;
}

function itemVolChange(el){
	let sib = el.nextSibling;
	sib.innerText = el.value + " dB";
}

var histlimitVal = 0;
var histdateVal = "";

async function reloadItemSection(el, type){
	if(type == "general"){
		let inner = "<form id='genitemform'>";
		if(itemProps.ID || ((itemProps.Type !== "file") && (itemProps.Type !== "playlist")))
			inner += "ID: "+itemProps.ID+"<br>";
		inner += "Name: <input type='text' size='45' name='Name'";
		inner += " value='"+quoteattr(itemProps.Name)+"'";
		if(itemProps.canEdit)
			inner += "></input><br>";
		else
			inner += " readonly></input><br>";
			
		inner += "Duration: <input type='text' size='10' name='Duration'";
		inner += " value='"+timeFormat(itemProps.Duration)+"'";
		
		if(itemProps.Type === "task")		// only tasks have editable durations
			inner += "></input><br>";
		else
			inner += " readonly></input><br>";
		if(itemProps.ID || ((itemProps.Type !== "file") && (itemProps.Type !== "playlist"))){
			inner += "Tag: <input type='text' size='45' name='Tag'";
			if(itemProps.Tag)
				inner += " value='"+quoteattr(itemProps.Tag)+"'";
			if(itemProps.canEdit)
				inner += "></input><br>";
			else
				inner += " readonly></input><br>";
				
			inner += "Added: ";
			if(itemProps.Added)
				inner += unixTimeToDateStr(itemProps.Added);
			inner +="<br>";
			
			inner += "Script:<br>";
			inner += "<textarea rows='6' cols='58' name='Script'";
			if(itemProps.canEdit)
				inner += ">";
			else
				inner += " readonly>";
			if(itemProps.Script)
				inner += quoteattr(itemProps.Script);
			inner += "</textarea>";
		}
		if(itemProps.canEdit)
			inner += "<p><button id='savegenbut' name='submit' onclick='saveItemGeneral(event)'>Save General Properties</button></p>";
		el.innerHTML = inner + "</form>";
	}else if(type == "categories"){
		let actions = false;
		let haction = false;
		let inner = "<form id='catitemform'><div id='itemcatlist'></div>";
		if(itemProps.canEdit){
			inner += "<p><button id='savecatbut' name='submit' onclick='saveItemCat(event)'>Save Categories</button>";
			actions = `<button class="editbutton" onclick="itemRemoveRow(event)">-</button>`;
			haction = `<button id='addItemCatBtn' class="editbutton" onclick="toggleShowSearchList(event)">+</button>
							<div class="search-list-right">
								<button class="editbutton" onclick="refreshAddItemCats(event)">Refresh List</button><br>
								<input id="addItemCatText" type="text" onkeyup="filterSearchList(event)" data-div="itemcatlist" data-removecb="unlistAlreadyInTable" placeholder="Enter Search..."></input>
								<div id="itemCatAddList"></div>
							</div>`;
		}
		el.innerHTML = inner + "</form>";
		let div = document.getElementById("itemcatlist");
		let fields = {Name: "<div data-id='$ID' data-rid='$RID'>$val</div>", Added: unixTimeToDateStr};
		let colWidth = {action:"18px", Added:"120px"};
		genPopulateTableFromArray(itemProps.categories, div, {Name: "Category", Added: "Added", RID: false, ID: false}, false, false, false, actions, haction, fields, colWidth);
		if(itemProps.canEdit){
			div = document.getElementById("itemCatAddList");
			if(catListCache.getValue())
				buildSearchList(div, catListCache.getValue(), itemAddCat);
		}
	}else if(type == "custom"){
		let actions = false;
		let haction = false;
		let inner = "<form id='custitemform'><div id='itemcustlist'></div>";
		if(itemProps.canEdit){
			inner += "<p><button id='savecustbut' name='submit' onclick='saveItemCust(event)'>Save Custom Properties</button>";
			actions = `<button class="editbutton" onclick="itemRemoveRow(event)">-</button>`;
			haction = `<button class="editbutton" onclick="itemAddCust(event)">+</button>`;
		}
		el.innerHTML = inner + "</form>";
		let div = document.getElementById("itemcustlist");
		let colWidth = {action:"18px"};
		let fields = { Property: `<input type='text' size='14' data-rid='$RID' value="$attval" onblur="liveSearchList(event, '')" onkeyup="liveSearchList(event, this.value, getMetaPropsForParent, '`+itemProps.meta+`')"></input>
											<div class="liveSearch"></div>`, 
							Value: "<input type='text' size='14' value='$attval'</input>"};
		genPopulateTableFromArray(itemProps.meta, div, {Property: "Property", Value: "Value", Parent: false, RID: false, id: false, tocID: false}, false, false, false, actions, haction, fields, colWidth);
	}else if(type == "rest"){
		let actions = false;
		let haction = false;
		let inner = "<form id='restitemform'><div id='itemrestlist'></div>";
		if(itemProps.canEdit){
			inner += "<p><button id='saverestbut' name='submit' onclick='saveItemRest(event)'>Save Rest Locations</button>";
			actions = `<button class="editbutton" onclick="itemRemoveRow(event)">-</button>`;
			haction = `<button id='addItemRestBtn' class="editbutton" onclick="toggleShowSearchList(event)">+</button>
							<div class="search-list-right">
								<button class="editbutton" onclick="refreshAddItemRest(event)">Refresh List</button><br>
								<input id="addItemRestText" type="text" onkeyup="filterSearchList(event)" data-div="itemrestlist" data-removecb="unlistAlreadyInTable" placeholder="Enter Search..."></input>
								<div id="itemRestAddList"></div>
							</div>`;
		}
		el.innerHTML = inner + "</form>";
		let div = document.getElementById("itemrestlist");
		let fields = {Name: "<div data-id='$ID' data-rid='$RID'>$val</div>", Added: unixTimeToDateStr};
		let colWidth = {action:"18px"};
		genPopulateTableFromArray(itemProps.rest, div, {Name: "Location", Added: "Added", RID: false, ID: false}, false, false, false, actions, haction, fields, colWidth);
		if(itemProps.canEdit){
			div = document.getElementById("itemRestAddList");
			if(locListCache.getValue())
				buildSearchList(div, locListCache.getValue(), itemAddRest);
		}
	}else if(type == "history"){
		if(!histlimitVal)
			histlimitVal = 10;
		if(!histdateVal.length){
			let tomor = new Date(Date.now() + 86400000);
			let dd = String(tomor.getDate()).padStart(2, '0');
			let mm = String(tomor.getMonth() + 1).padStart(2, '0');
			let yyyy = tomor.getFullYear();
			tomor = yyyy + '-' + mm + '-' + dd;
			histdateVal = tomor;
		}
		if(itemProps.history){
			let inner = "<form id='histitemform'>";
			inner += "Before date: <input type='date' id='histdatesel' name='histdate' value='"+histdateVal+"' onchange='refreshItemHistory()'></input>";
			inner += "<input type='number' id='histlimsel' name='histlimit' onchange='refreshItemHistory()' value='"+histlimitVal+"' max='310' min='10' step='50'></input>"
			inner += "<div id='itemhistlist'></div>"
			el.innerHTML = inner + "</form>";
			let div = document.getElementById("itemhistlist");
			genPopulateTableFromArray(itemProps.history, div);
		}else{
			let inner = "<form id='histitemform'>";
			inner += "<div id='itemhistlist'></div>"
			el.innerHTML = inner + "</form>";
			let div = document.getElementById("itemhistlist");
			div.innerHTML = "Please select a Library Location for which to get the play history";
		}
	}else if(type == "schedule"){
		let inner = "<form id='scheditemform'><div id='itemschedlist'></div>";
		if(itemProps.sched){
			let actions = false;
			let haction = false;
			if(itemProps.canEdit){
				inner += "<p><button id='saveschedbut' name='submit' onclick='saveItemSched(event)'>Save Schedule</button>";
				actions = `<button class="editbutton" onclick="itemRemoveRow(event)">-</button>`;
				haction = `<button id='addItemSchedBtn' class="editbutton" onclick="itemAddSched(event)">+</button>`;
			}
			el.innerHTML = inner + "</form><br>Set Fill value to zero for insert mode.<br> Fill value is minutes to over-ride lower prioity fill items.<br>Prioity level * indicates supression of lower priority items scheduled for the same time, with Max setting causing fadeout of previous items for exact time airing.";
			let div = document.getElementById("itemschedlist");
			let fields = {	Day: itemWDOMRender, // Weekday of Month
								Date: itemDateRender, 
								Month: itemMonRender,
								Hour: itemHrRender,
								Minute: "<input type='number' min='0' max='59' name='min' value='$val' data-rid='$RID'></input>", // number
								Fill: "<input type='number' min='0' max='1440' name='fill' value='$val' data-rid='$RID'></input>",
								Priority: itemPrioRender};
			let colWidth = {action:"18px"};
			genPopulateTableFromArray(itemProps.sched, div, {Day: "Day", Date: "Date", Month: "Month", Hour: "Hour", Minute: "Minute", Fill: "Fill", Priority: "Prio", RID: false}, false, false, false, actions, haction, fields, colWidth);
		}else{
			el.innerHTML = inner + "</form>";
			let div = document.getElementById("itemschedlist");
			div.innerHTML = "Please select a Library Location for which to get the schedule";
		}
	}else if(type == "file"){
		let inner = "";
		if(itemProps.ID){
			if(parseInt(itemProps.file.Missing))
				inner += "<strong><center>File MISSING</center></strong>";
			else
				inner += "<strong><center>File Good</center></strong>";
		}
		inner += `<table class="tableleftj" stype="overflow-wrap: break-word;">`;
		
		inner += "<tr><td width='15%'>Artist</td><td>";
		if(itemProps.canEdit){
			inner += `<span id='itemArtistName'>`;
			inner += quoteattr(itemProps.file.Artist) +`</span> &nbsp;<button class="editbutton" id='itemArtistBtn' data-id="`;
			inner += itemProps.file.ArtistID + `" onclick="toggleShowSearchList(event)">change</button>
								<div class="search-list">
									<button class="editbutton" onclick="refreshItemArtists(event)">Refresh List</button><br>
									<input id="itemArtistText" type="text" onkeyup="filterSearchList(event)" data-div="itemArtistName" data-removecb="unlistAlreadyInnerText" placeholder="Enter Search..."></input>
									<div id="itemArtistList"></div>
								</div>`;
		}else{
			inner += quoteattr(itemProps.file.Artist);
		}
		inner += "</td>";

		inner += "<tr><td>Album</td><td>";
		if(itemProps.canEdit){
			inner += `<span id='itemAlbumName'>`;
			inner += quoteattr(itemProps.file.Album) +`</span> &nbsp;<button class="editbutton" id='itemAlbumBtn' data-id="`;
			inner += itemProps.file.AlbumID + `" onclick="toggleShowSearchList(event)">change</button>
								<div class="search-list">
									<button class="editbutton" onclick="refreshItemAlbums(event)">Refresh List</button><br>
									<input id="itemAlbumText" type="text" onkeyup="filterSearchList(event)" data-div="itemAlbumName" data-removecb="unlistAlreadyInnerText" placeholder="Enter Search..."></input>
									<div id="itemAlbumList"></div>
								</div>`;
		}else{
			inner += quoteattr(itemProps.file.Album);
		}
		inner += "</td></table>";

		inner += `<form id='fileitemform'><table class="tableleftj" stype="overflow-wrap: break-word;">`;
		inner += "<tr><td width='15%'>Track</td><td>";
		inner += "<input type='text' size='1' name='Track'";
		inner += " value='"+itemProps.file.Track+"'";
		if(itemProps.canEdit)
			inner += "></input>";
		else
			inner += " readonly></input>";
		inner += "</td>";
		if(itemProps.ID){
			inner += "<tr><td width='15%'>Intro</td><td>";
			inner += "<input type='text' size='10' name='Intro'";
			inner += " value='"+timeFormat(itemProps.file.Intro)+"'";
			if(itemProps.canEdit)
				inner += "></input>";
			else
				inner += " readonly></input>";
			inner += "</td>";

			inner += "<tr><td>SegIn</td><td>";
			inner += "<input type='text' size='10' name='SegIn'";
			inner += " value='"+timeFormat(itemProps.file.SegIn)+"'";
			if(itemProps.canEdit)
				inner += "></input>";
			else
				inner += " readonly></input>";
			inner += "</td>";

			inner += "<tr><td>SegOut</td><td>";
			let fade = 0.0;
			let seg = parseFloat(itemProps.file.SegOut);
			if(isNaN(seg))
				seg = 0.0;
			if(itemProps.file.FadeOut)
				fade = parseFloat(itemProps.file.FadeOut);
			if(fade)
				seg = fade;
			inner += "<input type='text' size='10' name='SegOut'";
			inner += " value='"+timeFormat(seg)+"'";
			if(itemProps.canEdit)
				inner += "></input>";
			else
				inner += " readonly></input>";
				
			if(fade)
				inner += " Fade <input type='checkbox' id='itemFade' name='fade' checked";
			else
				inner += " Fade <input type='checkbox' id='itemFade' name='fade'";
			if(!itemProps.canEdit)
				inner += " disabled>";
			else
				inner += ">";
			inner += "</td>";

			inner += "<tr><td>Outcue</td><td>";
			inner += "<input type='text' size='45' name='OutCue'";
			if(itemProps.Tag)
				inner += " value='"+quoteattr(itemProps.file.Outcue)+"'";
			if(itemProps.canEdit)
				inner += "></input>";
			else
				inner += " readonly></input>";
			inner += "</td>";
			
			let vol = parseFloat(itemProps.file.Volume);
			if(!vol)
				vol = 1.0; // zero or empty is unity gain
			vol = Math.round(20.0 * Math.log10(vol));
			inner += "<tr>";
			inner += "<td>Volume</td><td>";

			inner += "<input type='range' name='Volume' min='-60' max='20' oninput='itemVolChange(this)' value='"+vol+"'>";
			inner += "<div style='float:right;'>"+vol+" dB</div>";
			inner += "</td>";
		}
		inner += "</table></form>";

		inner += `<table class="tableleftj" stype="overflow-wrap: break-word;">`;
		inner += "<tr><td width='15%'>Prefix</td><td>";
		inner += quoteattr(itemProps.file.Prefix);
		inner += "</td>";
		inner += "<tr><td>Path</td><td>";
		inner += quoteattr(itemProps.file.Path);
		inner += "</td>";
		inner += "<tr><td>Hash<br>";
		if(itemProps.canEdit){
			if(itemProps.file.Hash && itemProps.file.Hash.length)
				inner += `<button class="editbutton" onclick="itemRemoveHash(event)">Clear</button></td>`;
			else
				inner += `<button class="editbutton" onclick="itemSetHash(event)">Set</button></td>`;
		}
		inner += "</td><td id='fileitemhash'>"+quoteattr(itemProps.file.Hash);
		inner += "</td>";
		inner += "</table>";

		if(itemProps.canEdit){
			inner += `Replace File With: <form id="replaceform" enctype="multipart/form-data">
							<input type="file" id="replaceinput" class="editbutton" name="filestoupload" onchange="itemReplace(event)">
						</form>
						<label for="filereplacedest">To new Media Location:</label>
						<select id="filereplacedest">
							<option value="" onClick="getMediaLocs()">Reload List</option>
							<option value="" selected>Default</option>
						</select>
						<p><button id='savefilebut' onclick='saveItemFile(event)'>Save File Properties</button>`;
		}
		el.innerHTML = inner;
		let div = document.getElementById("itemArtistList");
		if(div && artListCache.getValue())
			buildSearchList(div, artListCache.getValue(), itemChangeArtist);
		div = document.getElementById("itemAlbumList");
		if(div && albListCache.getValue())
			buildSearchList(div, albListCache.getValue(), itemChangeAlbum);
		if(div && mediaListCache.getValue())
			fileReplaceDestRefresh(mediaListCache.getValue());
		
	}else if(type == "task"){
		if(!itemProps.task)
			// create empty array for new task
			itemProps.task = [];
		let task = itemProps.task;
		let obj = {};
		// flaten array into an object
		for(let i=0; i<task.length; i++){
			let prop = task[i].Property;
			let val =task[i].Value;
			obj[prop] = val;
		}
		let inner = `Task Type: <select id='tasktypesel' onchange='itemLoadTaskDiv(event)'`;
		if(itemProps.canEdit)
			inner += ">";
		else
			inner += " disabled>";
		inner += "<option value='Category'";
		if(obj.Subtype === "Pick"){
			if(obj.Category && obj.Category.length){
				inner += " selected";
			}
			inner += `>Category Pick</option>
						<option value='Directory'`;
			if((obj.Folder && obj.Folder.length) || (obj.Path && obj.Path.length)){
				inner += " selected";
			}
			inner += `>Directory Pick</option>
						<option value='Query'`;
			if(obj.Query && obj.Query.length){
				inner += " selected";
			}
		}else{
			inner += `>Category Pick</option>
						<option value='Path'>Directory Pick</option>
						<option value='Query'`;
		}
		inner += `>Custom Query Pick</option>
						<option value='Command'`;
		if(obj.Subtype === "Command"){
			inner += " selected";
		}
		inner += `>AR Server Command</option>
					<option value='Open'`;
		if(obj.Subtype === "Open"){
			inner += " selected";
		}
		inner += `>Run program</option>
					<option value='Execute'`;
		if(obj.Subtype === "Execute"){
			inner += " selected";
		}
		inner += `>Shell command</option>
					</select><br>`;
		inner += "<div id='tasktypediv'></div>";
		if(itemProps.canEdit)
			inner += "<button id='savetaskbut' name='submit' onclick='saveItemTask(event)'>Save Task Properties</button>";
		el.innerHTML = inner;
		itemLoadTaskDiv();
	}else if(type == "playlist"){
		let inner = "<span style='float: left;'>Total Duration:</span><span id='pldur' style='float: right;'>"+timeFormat(itemProps.Duration)+"</span><div style='clear:both;'></div><p>";
		inner += 	`<button onclick="plSelectAll(event)">Selected all</button>
						<button onclick="plUnselectAll(event)">Unselect all</button><br>
						<ul class="ddlist" id="itemplaylist">
						</ul><p>
						<button onclick="plSendToStash(event)">Send selected to Stash</button>`;
		if(itemProps.canEdit)
			inner += `<button onclick="plImportStash(event)">Import selected from stash</button>
			<button onclick="plDupItems(event)">Duplicate selected</button>
			<button onclick="plDelItems(event)">Delete selected</button><p>
			<button id='savegenbut' name='submit' onclick='saveItemPlaylist(event)'>Save Playlist Properties</button>`;
		el.innerHTML = inner;
		el = document.getElementById("itemplaylist");
		flatPlist = flattenPlaylistArray(itemProps.playlist); // note: this is a global var.
		let format = `<span style='float: left;'><input type='checkbox' onclick='updatePLDuration(event)'></input></span><span style='float: right;'>$Duration->timeFormat$</span><div style='clear:both;'></div>
							$Name$<br>
							$Artist$`;
		if(itemProps.canEdit)
			genDragableListFromObjectArray(true, plMoveItem, flatPlist, el, format);
		else
			genDragableListFromObjectArray(false, false, flatPlist, el, format);
	}else if(type == "instance"){
		let meta = false;
		let ref = parseInt(itemProps.UID);
		if(ref)
			meta = studioStateCache.meta[ref];
		if(meta){
			let inner = "<form id='institemform' data-id='"+ref+"'><table class='tableleftj' stype='overflow-wrap: break-word;'>";
			inner += "<tr><td width='15%'>Name</td><td>";
			inner += "<input type='text' size='45' name='Name'";
			inner += " value='"+quoteattr(meta.Name)+"'";
			inner += "></input><br>";
			inner += "</td>";

			inner += "<tr><td width='15%'>Artist</td><td>";
			inner += "<input type='text' size='45' name='Artist'";
			inner += " value='"+quoteattr(meta.Artist)+"'";
			inner += "></input><br>";
			inner += "</td>";
			
			inner += "<tr><td width='15%'>Album</td><td>";
			inner += "<input type='text' size='45' name='Album'";
			inner += " value='"+quoteattr(meta.Album)+"'";
			inner += "></input><br>";
			inner += "</td>";
			
			inner += "<tr><td width='15%'>Duration</td><td>";
			inner += "<input type='text' size='10' name='Duration'";
			inner += " value='"+timeFormat(meta.Duration)+"'";
			inner += "></input>";
			inner += "</td>";
			
			inner += "<tr><td>SegIn</td><td>";
			inner += "<input type='text' size='10' name='SegIn'";
			inner += " value='"+timeFormat(meta.SegIn)+"'";
			inner += "></input>";
			inner += "</td>";
			
			inner += "<tr><td>SegOut</td><td>";
			let fade = 0.0;
			let seg = parseFloat(meta.SegOut);
			if(isNaN(seg))
				seg = 0.0;
			if(meta.FadeOut)
				fade = parseFloat(meta.FadeOut);
			if(fade)
				seg = fade;
			inner += "<input type='text' size='10' name='SegOut'";
			inner += " value='"+timeFormat(seg)+"'";
			inner += "></input>";
			
			if(fade)
				inner += " Fade <input type='checkbox' id='instItemFade' name='fade' checked";
			else
				inner += " Fade <input type='checkbox' id='instItemFade' name='fade'";
			inner += ">";
			inner += "</td>";
			
			inner += "<tr><td>Don't Log</td><td>";
			if(meta.NoLog)
				inner += "<input type='checkbox' name='NoLog' checked";
			else
				inner += "<input type='checkbox' name='NoLog'";
			inner += ">";
			
			inner += "</table></form>";
			inner += "</td>";
			inner += "<p><button id='saveinstbut' name='submit' onclick='saveItemInstance(event)'>Apply Instance Properties</button></p>";
			el.innerHTML = inner + "</form>";
		}
	}
}

function flattenPlaylistArray(playlist){
	let newlist = [];
	for(let i=0; i<playlist.length; i++){
		let entry = playlist[i];
		let flat = {};
		for(let j=0; j<entry.length; j++){
			let prop = entry[j];
			flat[prop.Property] = prop.Value;
		}
		newlist.push(flat);
	}
	// calculate and back fill durration, in case it's missing
	updateListDuration(newlist);
	return newlist;
}

async function itemLoadTaskDiv(evt){
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
	}
	let sel = document.getElementById("tasktypesel");
	let el = document.getElementById("tasktypediv");
	let sub = sel.value;
	let task = itemProps.task;
		
	let obj = {};
	if(task){
		// flaten array into an object
		for(let i=0; i<task.length; i++){
			let prop = task[i].Property;
			let val = task[i].Value;
			obj[prop] = val;
		}
	}
	inner = "";
	if(sub === "Category"){
		let catid = obj.Category;
		let catname = await getNameForCat(catid);
		let mode = obj.Mode;		// Always Weighted, set as such if not
		let repsup = obj.Supress; // Artist, Album, Name, Anything else = by itemID
		let incloc = obj.db_include_loc	// addition location IDs, if any, to include in weighted play history: "id1,id2,...,idn"
		inner += `<br>Category: `
		if(itemProps.canEdit){
			inner += `<div class="dropdown">
							<button id="taskCatBtn" class="editbutton" data-id="`+catid+`" onclick="toggleShowSearchList(event)">`;
			inner += quoteattr(catname);
			inner += `</button>
							<div class="search-list">
								<button class="editbutton" onclick="refreshTaskCats(event)">Refresh List</button><br>
								<input id="taskCatText" type="text" onkeyup="filterSearchList(event)" placeholder="Enter Search..."></input>
								<div id="taskCatList"></div>
							</div>
						</div>`;
		}else{
			inner += quoteattr(catname);
		}
		let def = true;
		if(itemProps.canEdit)
			inner += "<br><form name='taskwieghtby'>";
		else
			inner += "<br><form name='taskwieghtby'><fieldset disabled='disabled'>";
		inner += `<br>Select weighted by last play: <ul style="list-style-type:none; text-align: left">
					<li><input type="radio" name="mode" value="Artist"`;
		if(repsup === "Artist"){
			inner += ` checked="checked"`;
			def = false;
		}
		inner += `>Artist Name
					<li><input type="radio" name="mode" value="Album"`;
		if(repsup === "Album"){
			inner += ` checked="checked"`;
			def = false;
		}
		inner += `>Album Name
					<li><input type="radio" name="mode" value="Name"`;
		if(repsup === "Name"){
			inner += ` checked="checked"`;
			def = false;
		}
		inner += `>Track Title
					<li><input type="radio" name="mode" value="Item"`;
		if(def)
			inner += ` checked="checked"`;
		inner += `>Specific Library Item
					</ul>`;
		if(itemProps.canEdit)
			inner += "</form>";
		else
			inner += "</fieldset></form>";
		
		inner += "Include Locations (comma separated IDs) in play history (Advanced):<br>";
		inner += "<input type='text' size='49' id='taskinc'";
		if(incloc && incloc.length)
			inner += " value='"+quoteattr(incloc)+"'";
		if(itemProps.canEdit)
			inner += "></input><p>";
		else
			inner += " readonly></input><p>";
		el.innerHTML = inner;
		if(itemProps.canEdit){
			let div = document.getElementById("taskCatList");
			if(div && catListCache.getValue())
				buildSearchList(div, catListCache.getValue(), itemTaskCatSelect); 
		}
	}else if(sub === "Query"){
		let query = obj.Query;
		let mode = obj.Mode; // all, weighted, first, random
		inner += "<br>SQL Query:<br><textarea rows='10' cols='58' id='taskquery'";
		if(itemProps.canEdit)
			inner += ">";
		else
			inner += " readonly>";
		if(query && query.length)
			inner += quoteattr(query);
		inner += "</textarea>";
		
		let def = true;
		if(itemProps.canEdit)
			inner += "<form name='taskmode'>";
		else
			inner += "<form name='taskmode'><fieldset disabled='disabled'>";
		inner += `Select result: <ul style="list-style-type:none; text-align: left">
					<li><input type="radio" name="mode" value="all"`;
		if(mode === "all"){
			inner += ` checked="checked"`;
			def = false;
		}
		inner += `>All results
					<li><input type="radio" name="mode" value="weighted"`;
		if(mode === "weighted"){
			inner += ` checked="checked"`;
			def = false;
		}
		inner += `>Weighted, prefering first
					<li><input type="radio" name="mode" value="first"`;
		if(mode === "first"){
			inner += ` checked="checked"`;
			def = false;
		}
		inner += `>First result
					<li><input type="radio" name="mode" value="random"`;
		if(def)
			inner += ` checked="checked"`;
		inner += `>Random result
					</ul>`;
		if(itemProps.canEdit)
			inner += "</form><p>";
		else
			inner += "</fieldset></form><p>";
		el.innerHTML = inner;
	}else if(sub === "Directory"){
		let path = obj.Path;
		let prefix = obj.Prefix;
		let folder = obj.Folder;
		let mod = parseInt(obj.Modified); // hours prior to now
		let nomod = parseInt(obj.NoModLimit);
		let date = parseInt(obj.Date); // true to use yyyy-mm-dd named file with today's date
		let rerun = parseInt(obj.Rerun);
		let seq = parseInt(obj.Sequencial);
		let first = parseInt(obj.First);
		let randlim = parseInt(obj.Random); // not played within the last randlim hours (integer)
		let segout = parseFloat(obj.def_segout);
		
		inner += "<br>Directory prefix: <div id='taskprefix'>";
		inner += quoteattr(prefix);
		inner += "</div><br>";
		inner += "Directory path: ";
		if(itemProps.canEdit)
			inner += `<button id="taskgetpfx" class="editbutton" onclick="taskGetPathFrefix(event)">Extract Prefix</button>`;
		inner += "<br><input type='text' size='49' id='taskpath'";
		if(path && path.length)
			inner += " value='"+quoteattr(path)+"'";
		if(itemProps.canEdit)
			inner += "></input><p>";
		else
			inner += " readonly></input><p>";
		inner += "Folder path (Depricated):<br>";
		inner += "<input type='text' size='49' id='taskfolder'";
		if(folder && folder.length)
			inner += " value='"+quoteattr(folder)+"'";
		if(itemProps.canEdit)
			inner += "></input><p>";
		else
			inner += " readonly></input><p>";
		
		inner += `Segue out to next item <input type='number' id='tasksegctl' min='0' max='30' step='0.1' value='`+segout+`'`;
		if(!itemProps.canEdit)
			inner += ` disabled`;
		inner += `</input> second from end.  Zero to use studio default segue out time.<p>`;
		
		inner += `<input type="checkbox" id="taskmod" onchange="taskModifiedCheckChange(event)"`;
		if(mod)
			inner += ` checked="checked"></input>Filter out files older than  
						<input type='number' id='taskmodctl' min='1' max='8640' name='ranlim' value='`+mod+`'`;
		else
			inner += `></input>File modified date is older than
						<input type='number' id='taskmodctl' min='1' max='8640' name='ranlim' value='1' disabled`;
		if(itemProps.canEdit)
			inner += `></input> hours`;
		else
			inner += ` readonly></input> hours`;
		inner += `<br> &emsp; <input type="checkbox" id="tasknomod"`;
		if(!mod)
			inner += " disabled";
		if(nomod)
			inner += ` checked="checked"></input>`;
		else
			inner += `></input>`;
		inner += "If selection sequence below yields no results,<br> &emsp; repeat without filtering<p>";
		inner += `Select sequence: <ol id="taskselseq" start="1" style="text-align: left">
					<li><input type="checkbox" id="taskselDate"`;
		if(date)
			inner += ` checked="checked"`;
		inner += `></input>Todays date at start of file name: YYYY-MM-DD
					<li><input type="checkbox" id="taskselRerun"`;
		if(rerun)
			inner += ` checked="checked"`;
		inner += `></input>Re-run last played
					<li><input type="checkbox" id="taskselSequencial"`;
		if(seq)
			inner += ` checked="checked"`;
		inner += `></input>Next an alphabetical sequence from last played
					<li><input type="checkbox" id="taskselFirst"`;
		if(first)
			inner += ` checked="checked"`;
		inner += `></input>First in alphabetical sequence
					<li><input type="checkbox" id="taskrandlim" name="Random" onchange="taskRandLimCheckChange(event)"`;
		if(randlim)
			inner += ` checked="checked"></input>Random, not played in last 
						<input type='number' id='ranlimctl' min='1' max='8640' name='ranlim' value='`+randlim+`'`;
		else
			inner += `></input>Random, not played in last 
						<input type='number' id='ranlimctl' min='1' max='8640' name='ranlim' value='1' disabled`;
		if(itemProps.canEdit)
			inner += `></input> hours`;
		else
			inner += ` readonly></input> hours`;
		inner += `</ol>`;

		el.innerHTML = inner;
	}else if(sub === "Command"){
		let command = obj.Command;
		inner += "<br>AR Command:<br><input type='text' size='49' id='taskcmd'";
		if(command && command.length)
			inner += " value='"+quoteattr(command)+"'";
		if(itemProps.canEdit)
			inner += "></input><p>";
		else
			inner += " readonly></input><p>";
	}else if(sub === "Open"){
		let openpath = obj.Path;
		inner += "<br>Open in new session:<br><input type='text' size='49' id='taskopen'";
		if(openpath && openpath.length)
			inner += " value='"+quoteattr(openpath)+"'";
		if(itemProps.canEdit)
			inner += "></input><p>";
		else
			inner += " readonly></input><p>";
			
			
		el.innerHTML = inner;
	}else if(sub === "Execute"){
		let command = obj.Command;
		inner += "<br>Execute:<br><input type='text' size='49' id='taskexec'";
		if(command && command.length)
			inner += " value='"+quoteattr(command)+"'";
		if(itemProps.canEdit)
			inner += "></input><p>";
		else
			inner += " readonly></input><p>";
		el.innerHTML = inner;
	}

}

function itemWDOMRender(val){
	let day = parseInt(val);
	let wk = day;
	if(day){
		day = ((day-1) % 7) + 1; // 1 thru 7
		wk = parseInt((wk-1) / 7) + 1; // 1 thru 6, 1 =  every week, 2..6 = wk 1..5
	}
	let inner = "<select name='day' onchange='itemChkWeekEn(event)'>";
	inner += "<option value='0' "+(day===0?"selected":"")+">Any</option>";
	inner += "<option value='1' "+(day===1?"selected":"")+">Sun</option>";
	inner += "<option value='2' "+(day===2?"selected":"")+">Mon</option>";
	inner += "<option value='3' "+(day===3?"selected":"")+">Tue</option>";
	inner += "<option value='4' "+(day===4?"selected":"")+">Wed</option>";
	inner += "<option value='5' "+(day===5?"selected":"")+">Thu</option>";
	inner += "<option value='6' "+(day===6?"selected":"")+">Fri</option>";
	inner += "<option value='7' "+(day===7?"selected":"")+">Sat</option>";
	inner += "</select><br>";

	inner += "<select name='itemwk' "+(wk?"":"disabled")+">";
	inner += "<option value='1' "+(wk===1?"selected":"")+">Any</option>";
	inner += "<option value='2' "+(wk===2?"selected":"")+">1st</option>";
	inner += "<option value='3' "+(wk===3?"selected":"")+">2nd</option>";
	inner += "<option value='4' "+(wk===4?"selected":"")+">3rd</option>";
	inner += "<option value='5' "+(wk===5?"selected":"")+">4th</option>";
	inner += "<option value='6' "+(wk===6?"selected":"")+">5th</option>";
	inner += "</select>";
	return inner;
}

function itemChkWeekEn(evt){
	let target = evt.target;
	let el = target.nextSibling.nextSibling;
	if(parseInt(target.value))
		el.disabled = false;
	else
		el.disabled = true;
}

function itemDateRender(val){
	val = parseInt(val);
	let inner = "<select name='date'>";
	inner += "<option value='0' "+(val===0?"selected":"")+">Any</option>";
	inner += "<option value='1' "+(val===1?"selected":"")+">1</option>";
	inner += "<option value='2' "+(val===2?"selected":"")+">2</option>";
	inner += "<option value='3' "+(val===3?"selected":"")+">3</option>";
	inner += "<option value='4' "+(val===4?"selected":"")+">4</option>";
	inner += "<option value='5' "+(val===5?"selected":"")+">5</option>";
	inner += "<option value='6' "+(val===6?"selected":"")+">6</option>";
	inner += "<option value='7' "+(val===7?"selected":"")+">7</option>";
	inner += "<option value='8' "+(val===8?"selected":"")+">8</option>";
	inner += "<option value='9' "+(val===9?"selected":"")+">9</option>";
	inner += "<option value='10' "+(val===10?"selected":"")+">10</option>";
	inner += "<option value='11' "+(val===11?"selected":"")+">11</option>";
	inner += "<option value='12' "+(val===12?"selected":"")+">12</option>";
	inner += "<option value='13' "+(val===13?"selected":"")+">13</option>";
	inner += "<option value='14' "+(val===14?"selected":"")+">14</option>";
	inner += "<option value='15' "+(val===15?"selected":"")+">15</option>";
	inner += "<option value='16' "+(val===16?"selected":"")+">16</option>";
	inner += "<option value='17' "+(val===17?"selected":"")+">17</option>";
	inner += "<option value='18' "+(val===18?"selected":"")+">18</option>";
	inner += "<option value='19' "+(val===19?"selected":"")+">19</option>";
	inner += "<option value='20' "+(val===20?"selected":"")+">20</option>";
	inner += "<option value='21' "+(val===21?"selected":"")+">21</option>";
	inner += "<option value='22' "+(val===22?"selected":"")+">22</option>";
	inner += "<option value='23' "+(val===23?"selected":"")+">23</option>";
	inner += "<option value='24' "+(val===24?"selected":"")+">24</option>";
	inner += "<option value='25' "+(val===25?"selected":"")+">25</option>";
	inner += "<option value='26' "+(val===26?"selected":"")+">26</option>";
	inner += "<option value='27' "+(val===27?"selected":"")+">27</option>";
	inner += "<option value='28' "+(val===28?"selected":"")+">28</option>";
	inner += "<option value='29' "+(val===29?"selected":"")+">29</option>";
	inner += "<option value='30' "+(val===30?"selected":"")+">30</option>";
	inner += "<option value='31' "+(val===31?"selected":"")+">31</option>";
	inner += "</select>";
	return inner;
}

function itemMonRender(val){
	val = parseInt(val);
	let inner = "<select name='month'>";
	inner += "<option value='0' "+(val===0?"selected":"")+">Any</option>";
	inner += "<option value='1' "+(val===1?"selected":"")+">Jan</option>";
	inner += "<option value='2' "+(val===2?"selected":"")+">Feb</option>";
	inner += "<option value='3' "+(val===3?"selected":"")+">Mar</option>";
	inner += "<option value='4' "+(val===4?"selected":"")+">Apr</option>";
	inner += "<option value='5' "+(val===5?"selected":"")+">May</option>";
	inner += "<option value='6' "+(val===6?"selected":"")+">Jun</option>";
	inner += "<option value='7' "+(val===7?"selected":"")+">Jul</option>";
	inner += "<option value='8' "+(val===8?"selected":"")+">Aug</option>";
	inner += "<option value='9' "+(val===9?"selected":"")+">Sep</option>";
	inner += "<option value='10' "+(val===10?"selected":"")+">Oct</option>";
	inner += "<option value='11' "+(val===11?"selected":"")+">Nov</option>";
	inner += "<option value='12' "+(val===12?"selected":"")+">Dec</option>";
	inner += "</select>";
	return inner;
}

function itemHrRender(val){
	val = parseInt(val);
	let inner = "<select name='hour'>";
	inner += "<option value='-1' "+(val===-1?"selected":"")+">Every</option>";
	inner += "<option value='0' "+(val===0?"selected":"")+">0</option>";
	inner += "<option value='1' "+(val===1?"selected":"")+">1</option>";
	inner += "<option value='2' "+(val===2?"selected":"")+">2</option>";
	inner += "<option value='3' "+(val===3?"selected":"")+">3</option>";
	inner += "<option value='4' "+(val===4?"selected":"")+">4</option>";
	inner += "<option value='5' "+(val===5?"selected":"")+">5</option>";
	inner += "<option value='6' "+(val===6?"selected":"")+">6</option>";
	inner += "<option value='7' "+(val===7?"selected":"")+">7</option>";
	inner += "<option value='8' "+(val===8?"selected":"")+">8</option>";
	inner += "<option value='9' "+(val===9?"selected":"")+">9</option>";
	inner += "<option value='10' "+(val===10?"selected":"")+">10</option>";
	inner += "<option value='11' "+(val===11?"selected":"")+">11</option>";
	inner += "<option value='12' "+(val===12?"selected":"")+">12</option>";
	inner += "<option value='13' "+(val===13?"selected":"")+">13</option>";
	inner += "<option value='14' "+(val===14?"selected":"")+">14</option>";
	inner += "<option value='15' "+(val===15?"selected":"")+">15</option>";
	inner += "<option value='16' "+(val===16?"selected":"")+">16</option>";
	inner += "<option value='17' "+(val===17?"selected":"")+">17</option>";
	inner += "<option value='18' "+(val===18?"selected":"")+">18</option>";
	inner += "<option value='19' "+(val===19?"selected":"")+">19</option>";
	inner += "<option value='20' "+(val===20?"selected":"")+">20</option>";
	inner += "<option value='21' "+(val===21?"selected":"")+">21</option>";
	inner += "<option value='22' "+(val===22?"selected":"")+">22</option>";
	inner += "<option value='23' "+(val===23?"selected":"")+">23</option>";
	inner += "</select>";
	return inner;
}

function itemPrioRender(val){
	val = parseInt(val);
	let inner = "<select name='prio'>";
	inner += "<option value='0' "+(val===0?"selected":"")+">Min</option>";
	inner += "<option value='1' "+(val===1?"selected":"")+">1</option>";
	inner += "<option value='2' "+(val===2?"selected":"")+">2</option>";
	inner += "<option value='3' "+(val===3?"selected":"")+">3</option>";
	inner += "<option value='4' "+(val===4?"selected":"")+">4</option>";
	inner += "<option value='5' "+(val===5?"selected":"")+">5</option>";
	inner += "<option value='6' "+(val===6?"selected":"")+">6</option>";
	inner += "<option value='7' "+(val===7?"selected":"")+">7</option>";
	inner += "<option value='8' "+(val===8?"selected":"")+">8*</option>";
	inner += "<option value='9' "+(val===9?"selected":"")+">9*</option>";
	inner += "<option value='10' "+(val===10?"selected":"")+">Max*</option>";
	inner += "</select>";
	return inner;
}

async function showEncoderItem(panel, container){
	let inner = "<form id='enitemform' style='padding:5px;'> Type: "+itemProps.Type+"<br>";
	inner += "Name: <input type='text' id='enName' size='45' name='Name'";
	inner += " value='"+quoteattr(itemProps.Name)+"'";
	if(itemProps.canEdit){
		inner += "></input>";
	}else
		inner += " readonly></input>";
		
	inner += "<br><input type='checkbox' name='Persistent' id='enpersist' ";
	if(itemProps.Persistent && Number(itemProps.Persistent))
		inner += "checked";
	if(!itemProps.canEdit)
		inner += " disabled";
	inner += "></input> Persistent: keeps running when audio inputs are disconnected.<br>";

	inner += "<br>Auidio Ports:<br><div id='enPorts'></div>";

	inner += "<br><br>Post Tracks from Bus:<select id='enTagBus' name='TagBus'";
	if(!itemProps.canEdit)
		inner += " disabled>";
	else
		inner += ">";
	for(let b=1; b<=studioStateCache.buscnt; b++){
		let name;
		switch(b){
			case 1:
				name = "Monitor"
				break;
			case 2:
				name = "Cue"
				break;
			case 3:
				name = "Main";
				break;
			case 4:
				name = "Alternate";
				break;
			default:
				name = "Bus "+b;
				break;
		}
		if(itemProps.TagBus == b)
			inner += "<option value='"+b+"' selected>"+name+"</option>";
		else
			inner += "<option value='"+b+"'>"+name+"</option>";
	}
	inner += "</select>";
	inner += "<br><input type='checkbox' name='MakePLCheck' id='enmakepl' ";
	if(itemProps.MakePL && itemProps.MakePL.length)
		inner += "checked";
	if(!itemProps.canEdit)
		inner += " disabled";
	inner += "></input>";
	inner += "Save tracks to file: <input type='text' id='enFPL' size='30' name='MakePL'";
	inner += " value='"+quoteattr(itemProps.MakePL)+"'  placeholder='[rec_dir][Name].fpl'";
	if(itemProps.canEdit){
		inner += "></input>";
	}else
		inner += " readonly></input>";
	let value = parseInt(itemProps.Limit);
	if(!value)
		value = 0;
	inner += "<br><br>Run time limit (sec.) <input type='number' min='0' max='240' id='enLimit' name='Limit' value='"+value+"'";
		if(itemProps.canEdit){
		inner += "></input> Zero for no limit<br>";
	}else
		inner += " readonly></input> Zero for no limit<br>";
	
	inner += "<br><br>Start: <input type='checkbox' name='StartCheck' id='enStartChk' ";
	if(Number(itemProps.Start))
		inner += "checked";
	if(!itemProps.canEdit)
		inner += " disabled";
	inner += "></input>";
	inner += " <input id='enStart' type='datetime-local' name='Start' value='";
	let val = Number(itemProps.Start)
	if(val){
		let theDate = new Date(val * 1000);
		inner += dateToISOLocal(theDate)+"'";
	}else{
		let theDate = new Date();	// now
		inner += dateToISOLocal(theDate)+"'";
	}
	if(itemProps.canEdit){
		inner += "></input>";
	}else
		inner += " readonly></input>";  
		
	inner += encPropsFromPipeline(itemProps.Pipeline, itemProps, itemProps.canEdit);
	
	if(itemProps.canEdit)
		inner += "<br><br><button id='saveinitenc' name='submit' onclick='saveEncProperties(event)'>Initialize Encoder/Recorder</button>";

	inner += "</form>";
	
	inner += "<br><br>Raw Gstreamer Pipeline<textarea rows='12' cols='58' id='enPipeline'";
	if(itemProps.canEdit){
		inner += ">";
	}else
		inner += " readonly>";
	inner += quoteattr(itemProps.Pipeline);
	inner += "</textarea>";
	if(itemProps.canEdit)
		inner += "<br><button id='reloadenc' onclick='reloadEncProperties(event)'>Refresh properties</button>";
	container.innerHTML = inner;
	
	// create ports control
	let el = document.getElementById("enPorts");
	if(el){
		let devList = await stGetSourceList();
		stRenderPortsControl(el, devList, itemProps.Ports, itemProps.canEdit);
		let inputHidden = document.createElement("input");
		inputHidden.setAttribute("type", "hidden");
		inputHidden.setAttribute("name", "Ports");
		el.appendChild(inputHidden);
	}

	panel.style.width = infoWidth;
	el = document.getElementById("showinfobtn");
	if(el)
		el.style.display = "none";
	
	if(itemProps.canEdit)
		refreshItemReassign();
}

function encPropsFromPipeline(Pipeline, props, canEdit){
// format: pipeline is parsed, searching for all bracketed content.  For each bracketed content section, a control is generated
// named with the text following the first bracket; defult value set, if any by value following equil sign; and control being either 
// a list if values are comma specified, or a text field if no commas are found.
	let inner = "";
	if(Pipeline.indexOf("[") > -1){
		let parts = Pipeline.split("[");
		parts.shift(); // ignore text prior to first "["
		let ignore = ["rec_dir", "Name", "channels"];
		for(let n = 0; n < parts.length; n++){
			let sub = parts[n].split("]");
			parts[n] = sub[0];
			sub = parts[n].split("=");
			let key = sub[0];
			// ignore certain keys
			if(ignore.includes(key)) 
				continue; // already have a control for this
			ignore.push(key);
			let def = sub[1];
			let val = props[key];
			if(def){
				let list = def.split(",");
				def = list[0];
				if(!val || !val.length)
					val = def;
				list.shift();
				if(list.length){
					// list control
					inner += "<br>"+key+": <select id='en"+key+"' name='"+key+"'>";
					for(let i=0; i<=list.length; i++){
						if(list[i] && list[i].length){
							if(list[i] == val)
								inner += "<option value='"+list[i]+"' selected>"+list[i]+"</option>";
							else
								inner += "<option value='"+list[i]+"'>"+list[i]+"</option>";
						}
					}
					inner += "</select>";
				}else{
					// text control with default value
					inner += "<br>"+key+": <input type='text' id='en"+key+"' size='20' name='"+key+"'";
					inner += " value='"+quoteattr(val)+"'";
					if(canEdit)
						inner += "></input>";
					else
						inner += " readonly></input>";
				}
			}else{
				// text control, no default
				inner += "<br>"+key+": <input type='text' id='en"+key+"' size='15' name='"+key+"'";
				inner += " value='"+quoteattr(props[key])+"'";
				if(canEdit)
					inner += "></input>";
				else
					inner += " readonly></input>";
			}
		}
	}
	return inner;
}

async function showPropItem(panel, container){
	let inner = "<form id='propitemform' style='padding:5px;'> Type: "+itemProps.Type+"<br><div id='itemPropID'>ID: "+itemProps.ID+"</div>";
	inner += "Name: <input type='text' id='itemPropName' size='45' name='Name'";
	inner += " value='"+quoteattr(itemProps.Name)+"'";
	if(itemProps.canEdit){
		inner += "></input><br>";
		inner += "<button id='savepropbut' name='submit' onclick='saveItemProperties(event)'>Save "+itemProps.Type+"</button>";
	}else
		inner += " readonly></input>";
	inner += "</form>";
	inner += `<button class="accordion" id="metabut" onclick="selectAccordType(event, reloadItemSection, 'custom')">Custom</button>
	<div class="accpanel">
	</div>`;
	if(itemProps.canEdit){
		inner += "<button id='delitembut' onclick='itemDelete(event)'>Delete Item</button>";
		inner += ` Reassign items to <button class="editbutton" id="itemreassignbtn" data-id="1" onclick="toggleShowSearchList(event)">`;
		inner += `[None]</button>
							<div class="search-list">
								<button id="itemReassignRefresh" class="editbutton" onclick="refreshItemReassign(event)">Refresh List</button><br>
								<input type="text" id="itemReassignText" data-removecb="unlistItemName" onkeyup="filterSearchList(event)" data-div="itemreassignbtn" placeholder="Enter Search..."></input>
								<div id="itemReassignList"></div>
							</div>`;
	}
	container.innerHTML = inner;
	panel.style.width = infoWidth;
	let el = document.getElementById("showinfobtn");
	if(el)
		el.style.display = "none";
	let genbut = document.getElementById("metabut");
	selectAccordType({target: metabut}, reloadItemSection, 'custom');
	el = document.getElementById("itemReassignRefresh");
	if(itemProps.canEdit)
		refreshItemReassign();
}

async function showTocItem(panel, container){
	let inner = "<center id='itemName'>"+quoteattr(itemProps.Name)+"</center><br>";
	if(itemProps.ID && (itemProps.Type === "file"))
		inner += "<audio controls id='itemcueplayer' width='100%'><source id='itemcuesource' src='library/download/"+itemProps.ID+"''>Your browser does not support the audio tag.</audio>";

	inner += `<button class="accordion" id="genbut" onclick="selectAccordType(event, reloadItemSection, 'general')">General</button>
	<div class="accpanel">
	</div>`;
	inner += `<button class="accordion" onclick="selectAccordType(event, reloadItemSection, '`
	inner += itemProps.Type+`')">`;
	inner += itemProps.Type;
	inner += `</button>
	<div class="accpanel">
	</div>`;
	if(itemProps.ID || ((itemProps.Type !== "file") && (itemProps.Type !== "playlist"))){
		inner += `<button class="accordion" onclick="selectAccordType(event, reloadItemSection, 'categories')">Categories</button>
		<div class="accpanel">
		</div>
		<button class="accordion" onclick="selectAccordType(event, reloadItemSection, 'schedule')">Schedule</button>
		<div id="itemSchedPanel" class="accpanel">
		</div>
		<button class="accordion" onclick="selectAccordType(event, reloadItemSection, 'history')">History</button>
		<div id="itemHistPanel" class="accpanel">
		</div>
		<button class="accordion" onclick="selectAccordType(event, reloadItemSection, 'custom')">Custom</button>
		<div class="accpanel">
		</div>
		<button class="accordion" onclick="selectAccordType(event, reloadItemSection, 'rest')">Rest</button>
		<div class="accpanel">
		</div>`;
	}
	if(itemProps.UID){
		inner += `<button class="accordion" id="instbut" onclick="selectAccordType(event, reloadItemSection, 'instance')">Instance Logging</button>
		<div class="accpanel">
		</div>`;
	}
	if(itemProps.canEdit){
		inner += "<p><button id='delitembut' onclick='itemDelete(event)'>Delete Item</button>";
		if(itemProps.ID && (itemProps.Type === "file")){
			inner += " Delete file too: <input type='checkbox' id='delfiletoo' checked></input>";
			inner += `<p><button id='expitembut' onclick='itemExport(event)'>Download</button>
						<select id='downloadtype'>
							<option value='file' $ifvalsel>Audio File</option>
							<option value='json' $ifvalsel>jSON File</option>
						</select>`;
		}else if(itemProps.ID && (itemProps.Type === "playlist"))
			inner += `<p><button id='expitembut' onclick='itemExport(event)'>Download</button>
						<select id='downloadtype'>
							<option value='fpl' $ifvalsel>Audiorack FPL file</option>
							<option value='fplmedia' $ifvalsel>Audiorack FPL file with media folder</option>
							<option value='cue' $ifvalsel>Cuesheet file</option>
							<option value='json' $ifvalsel>jSON File</option>
						</select><br>
						Add offset (seconds) to times: <input type='text' id='itemtimeoffset' size='5' value='0'></input>`;
		else if(itemProps.ID)
			inner += `<p><button id='expitembut' onclick='itemExport(event)'>Download</button> jSON File`;
		inner += `<a id="filedltarget" style="display: none"></a>`;
	}
	inner += `<p><button onclick='itemSendToStash(event)'>Item to Stash</button><button onclick='itemSendToQueue(event)'>Item to Queue</button>`;
	if(itemProps.Type == "file")
		inner += `<button onclick='itemSendToPlayer(event)'>Item to Player</button>`;

	container.innerHTML = inner;
	panel.style.width = infoWidth;
	let el = document.getElementById("showinfobtn");
	if(el)
		el.style.display = "none";
	let genbut = document.getElementById("genbut");
	selectAccordType({target: genbut}, reloadItemSection, 'general')
}

async function itemFetchProps(id){
	let loc = locName.getValue();
	let response;
	try{
		if(loc && loc.length){
			let ds = document.getElementById("histdatesel");
			let ls = document.getElementById("histlimsel");
			let postdata = {locname: loc};
			if(ds && ds.value){
				postdata.histdate = ds.value;
				histdateVal = ds.value;
			}
			if(ls && ls.value){
				postdata.histlimit = ls.value;
				histlimitVal = ls.value;
			}
			response = await fetch("library/item/"+id, {
				method: 'POST',
				body: JSON.stringify(postdata),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		}else{
			histlimitVal = 0;
			histdateVal = "";
			response = await fetch("library/item/"+id);
		}
	}catch(err){
		alert("Caught an error fetching data from server.\n"+err);
		return;
	}
	if(response.ok){
		return await response.json();
	}else{
		alert("Got an error fetching data from server.\n"+response.statusText);
		return;
	}
}

async function itemPropsMeta(id, parent){
	let response;
	try{
		response = await fetch("library/get/meta?Parent="+parent+"&ID="+id);
	}catch(err){
		alert("Caught an error fetching metadata from server.\n"+err);
		return false;
	}
	if(response.ok){
		let res = await response.json();
		for(let i = 0; i < res.length; i++){
			res[i].RID = res[i].id;
			delete res[i].id;
		}
		return res;
	}else{
		alert("Got an error fetching metadata from server.\n"+response.statusText);
		return false;
	}
}

async function getItemData(props){
	if(props.tocID){
		// query API for item properties
		let response;
		let data = false;
		data = await itemFetchProps(props.tocID);
		if(data)
			return data;
	}else if(props.ID){
		// query API for item properties
		let response;
		let data = false;
		data = await itemFetchProps(props.ID);
		if(data)
			return data;
	}else if(props.tmpfile){
		// query API for tmpfile properties
		let data = false;
		let api = "library/info" + props.tmpfile;
		let resp = await fetchContent(api);
		if(resp){
			if(!resp.ok){
				alert("Got an error retreaving file info from server.\n"+resp.statusText);
				return false;
			}
			data = await resp.json();
			data.tmpfile = props.tmpfile;
		}else{
			alert("Failed to retreaving file info from server.");
			return false;
		}
		if(data)
			return data;
	}
	return false;
}

async function showItem(props, canEdit, noShow){
	if(props.tocID){
		let data = await getItemData(props);
		if(data){
			itemProps = data;
			if(props.UID)
				itemProps.UID = props.UID;
			itemProps.canEdit = canEdit;
			if(noShow)
				return;
			let el = document.getElementById("infopane");
			let da = document.getElementById("infodata");
			showTocItem(el, da);
		}
	}else if(props.ID){
		let data = await getItemData(props);
		if(data){
			itemProps = data;
			if(props.UID)
				itemProps.UID = props.UID;
			itemProps.canEdit = canEdit;
			if(noShow)
				return;
			let el = document.getElementById("infopane");
			let da = document.getElementById("infodata");
			showTocItem(el, da);
		}
	}else if(props.Type == "encoder"){
		// handle encoder/recorder settings
		let el = document.getElementById("infopane");
		let da = document.getElementById("infodata");
		itemProps = props;
		itemProps.canEdit = canEdit;
		if(noShow)
			return;
		showEncoderItem(el, da);
	}else if(props.qtype){
		let type = props.qtype;
		// use the properties already passed in props
		let el = document.getElementById("infopane");
		let da = document.getElementById("infodata");
		if(props.id){
			// edit existing artist, album, or category
			let meta = await itemPropsMeta(props.id, props.qtype);
			if(!meta)
				meta = [];
			data = {ID:props.id, Name: props.Label, Type: props.qtype, meta: meta};
			itemProps = data;
			itemProps.canEdit = canEdit;
			if(noShow)
				return;
			showPropItem(el, da);
			el.style.width = infoWidth;
			el = document.getElementById("showinfobtn");
			if(el)
				el.style.display = "none";
		}else{
			if(["artist", "album", "category"].includes(type)){
				// new item
				// create a new property
				data = {ID: 0, Name: "new "+type, Type: type, meta: []};
				itemProps = data;
				itemProps.canEdit = canEdit;
				if(noShow)
					return;
				showPropItem(el, da);
				itemProps.Name = ""; // change name to make it save the default name
				el.style.width = infoWidth;
				el = document.getElementById("showinfobtn");
				if(el)
					el.style.display = "none";
			}else if(["task", "playlist"].includes(type)){
				// create a new mostly empty item record
				data = { ID: 0, 
							Type: type,
							Name: "new "+type,
							categories: [],
							rest: [],
							meta: []
				};
				data[type] = [];
				
				let loc = locName.getValue();
				if(loc && loc.length){
					data.schedule = [];
					data.history = [];
				}
				itemProps = data;
				itemProps.canEdit = canEdit;
				if(noShow)
					return;
				showTocItem(el, da);
				el.style.width = infoWidth;
				el = document.getElementById("showinfobtn");
				if(el)
					el.style.display = "none";
			}
		}
	}else if(props.tmpfile){
		let data = await getItemData(props);
		if(data){
			itemProps = data;
			itemProps.canEdit = false;
			if(noShow)
				return;
			let el = document.getElementById("infopane");
			let da = document.getElementById("infodata");
			showTocItem(el, da);
		}
	}
}

/***** db management specific functions *****/

async function clickDbSync(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let obj = sseMsgObj.getValue()
	obj = obj.dbsync;
	if(obj && obj.running){
		// currently running
		resp = await fetch("library/synchalt");
		if(resp){
			if(!resp.ok){
				alert("Got an error from server.\n"+resp.statusText);
			}
		}else{
			alert("Failed to post request to the server.");
		}
	}else{
		// currently stopped
		let pass = {};
		let el = document.getElementById("syncmark");
		if(el.checked)
			pass.mark  = 1;
		/* run the actual dbsync process */
		resp = await fetch("library/dbsync", {
				method: 'POST',
				body: JSON.stringify(pass),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp){
			if(!resp.ok){
				alert("Got an error from server.\n"+resp.statusText);
			}
		}else{
			alert("Failed to post request to the server.");
		}
	}
}

async function clickDbCrawl(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let obj = sseMsgObj.getValue()
	obj = obj.dbsearch;
	if(obj && obj.running){
		// currently running
		resp = await fetch("library/crawlhalt");
		if(resp){
			if(!resp.ok){
				alert("Got an error from server.\n"+resp.statusText);
			}
		}else{
			alert("Failed to post request to the server.");
		}
	}else{
		// currently stopped
		let elements = document.getElementById("crawlform").elements;
		let obj ={};
		let pass = {path: "", pace:1.0};
		for(let i = 0 ; i < elements.length ; i++){
			let item = elements.item(i);
			if((item.type === "radio") || (item.type === "checkbox")){
				if(!item.checked)
					continue;
			}
			obj[item.name] = item.value;
		}
		delete obj["submit"];
		if(obj["crawlprop"] === "conf"){
			// use library configuration settings
			let resp = await fetchContent("getconf/files/mediaDir");
			if(resp){
				if(resp.ok){
					elements = await resp.json();
					for(let i = 0 ; i < elements.length ; i++){
						let item = elements[i];
						if(item.id === "mediaDir")
							pass.path = item.value;
					}
				}else{
					alert("Got an error fetching data from server.\n"+resp.statusText);
					return;
				}
			}else{
				alert("Failed to fetch data from the server.");
				return;
			}
		}else{
			// use form setting
			pass.path = obj["path"];
		}
		if(!pass.path.length){
			alert("File path to directory needs to be specified.");
			return;
		}
		if(obj.add)
			pass.add = obj.add;
		/* run the actual dbcrawl process */
		resp = await fetch("library/crawl", {
				method: 'POST',
				body: JSON.stringify(pass),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp){
			if(!resp.ok){
				alert("Got an error from server.\n"+resp.statusText);
			}
		}else{
			alert("Failed to post request to the server.");
		}
	}
}

async function clickDbInit(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let elements = document.getElementById("dbiform").elements;
	let obj ={};
	for(let i = 0 ; i < elements.length ; i++){
		let item = elements.item(i);
		if(item.type === "radio"){
			if(!item.checked)
				continue;
		}
		obj[item.name] = item.value;
	}
	delete obj["submit"];
	if(obj["dbiprop"] === "conf"){
		// use library configuration settings
		let resp = await fetchContent("getconf/library");
		if(resp){
			if(resp.ok){
				elements = await resp.json();
				obj ={};
				for(let i = 0 ; i < elements.length ; i++){
					let item = elements[i];
					obj[item.id] = item.value;
				}
			}else{
				alert("Got an error fetching data from server.\n"+resp.statusText);
				return;
			}
		}else{
			alert("Failed to fetch data from the server.");
			return;
		}
	}else{
		// use form library settings
		delete obj["dbiprop"];
		obj["type"] = "mysql";
	}
	/* run the actual db init/update script */
	resp = await fetchContent("library/dbinit", {
			method: 'POST',
			body: JSON.stringify(obj),
			headers: {
				"Content-Type": "application/json",
				"Accept": "application/json"
			}
		});
	if(resp){
		if(resp.ok){
			let el = document.getElementById("dbimsg");
			el.innerHTML = await resp.text();
		}else{
			alert("Got an error fetching data from server.\n"+resp.statusText);
		}
	}else{
		alert("Failed to fetch data from the server.");
	}
}

async function updateLoc(evt){
	if(evt)
		evt.preventDefault();
	let props = getRowInputProps(evt.target.parentNode.parentNode);
	let id = props.id;
	delete props.id;
	try{
		let uri = "library/set/locations";
		if(id)
			uri += "/"+id;
		response = await fetch(uri, {
			method: 'POST',
			body: JSON.stringify(props),
			headers: {
				"Content-Type": "application/json",
				"Accept": "application/json"
			}
		});
		// reload & error handling
	}catch(err){
		alert("Caught an error sending data to server.\n"+err);
		return err;
	}
	if(!response.ok){
		alert("response code from server.\n"+response.statusText);
		return;
	}
	// reload the table from the database
	loadLocationMgtTbl();
}

function newLoc(evt){
	if(evt)
		evt.preventDefault();
	let props = {id: "", Name: "NewLocationName"};
	mngLocList.push(props);
	loadLocationMgtTbl(mngLocList);
}

async function loadLocationMgtTbl(data){
	let el = document.getElementById("manloctbl");
	if(!data){
		genPopulateTableFromArray(false, el);
		el.innerHTML = "<div class='center'><i class='fa fa-circle-o-notch fa-spin' style='font-size:48px'></i></div>";
		let resp = await fetchContent("library/get/locations");
		if(resp){
			if(resp.ok){
				let list = await resp.json();
				mngLocList = list;
				let hidden = {id: false};
				let actions = `<button class="editbutton" onclick="updateLoc(event)">Update</button>`;
				let haction = `<button class="editbutton" onclick="newLoc(event)">+</button>`;
				let fields = {Name: "<input type='text' name='Name' data-id='$id' value='$val' ></input>"};
				let colWidth = {action:"38px"};
				genPopulateTableFromArray(list, el, hidden, false, false, false, actions, haction, fields, colWidth);
				return;
			}
		}
		// handle failure
		genPopulateTableFromArray(false, el);
		if(resp)
			alert("Got an error fetching data from server.\n"+resp.statusText);
		else
			alert("Failed to fetch data from the server."); 
	}else{
		let hidden = {id: false};
		let actions = `<button class="editbutton" onclick="updateLoc(event)">Update</button>
							<button class="editbutton" onclick="delLoc(event)">-</button>`;
		let haction = `<button class="editbutton" onclick="newLoc(event)">+</button>`;
		let fields = {Name: "<input type='text' name='Name' data-id='$id' value='$val' ></input>"};
		let colWidth = {action:"38px"};
		genPopulateTableFromArray(data, el, hidden, false, false, false, actions, haction, fields, colWidth);
	}
}

/***** Settings specific functions *****/

function loadConfigTypeTable(el, type){
	let data = confData[type];
	let actions;
	let haction = false;
	let fields;
	let hidden = false;
	let colWidth = {action:"40px"};
	if(type === "confusers"){
		hidden = {salt: false};
		actions = `<button class="editbutton" onclick="updateConf(event, '`+type+`')">Update</button>
						<button class="editbutton" onclick="delConf(event, '`+type+`')">-</button>`;
		haction = `<button class="editbutton" onclick="newConf(event, '`+type+`')">+</button>`;
		fields = {id: "<input type='text' name='id' value='$val'/>", 
					password: "<input type='text' name='password' value='' placeholder='New Password'></input>",
					permission: `<select name='permission'>
										<option value='admin' $ifvalsel>Admin</option>
										<option value='manager' $ifvalsel>Manager</option>
										<option value='production' $ifvalsel>Production</option>
										<option value='programming' $ifvalsel>Programming</option>
										<option value='traffic' $ifvalsel>Traffic</option>
										<option value='library' $ifvalsel>Library</option>
										<option value='studio' $ifvalsel>Studio</option>
									</select>`};
		colWidth.action = "60px";
	}else if(type === "confstudios"){
		actions = `<button class="editbutton" onclick="runStudioConf(event)">run</button>
						<button class="editbutton" onclick="updateConf(event, '`+type+`')">Update</button>
						<button class="editbutton" onclick="delConf(event, '`+type+`')">-</button>`;
		haction = `<button class="editbutton" onclick="newConf(event, '`+type+`')">+</button>`;
		fields = {id: "<input type='text' size=10' name='id' value='$val'/>", 
					host: "<input type='text' name='host' value='$val'></input>",
					port: "<input type='text' size='4' name='port' value='$val'></input>",
					run: "<input type='text' name='run' value='$val'></input>",
					startup: "<input type='checkbox' name='startup' $ifvalchk></input>",
					minpool: "<input type='number' min='1' max='8' name='minpool' value='$val'></input>",
					maxpool: "<input type='number' min='1' max='8' name='maxpool' value='$val'></input>"
		};
		colWidth.id = "90px";
		colWidth.minpool = "65px";
		colWidth.maxpool = "65px";
		colWidth.port = "65px";
		colWidth.startup = "55px";
		colWidth.action = "80px";
	}else if(type === "conffiles"){
		actions = `<button class="editbutton" onclick="updateConf(event, '`+type+`')">Update</button> $mediaDirActions$`;
		fields = {id: "<input type='hidden' name='id' value='$val'/>$val", value: "<input type='text' name='value' value='$val'></input>"};
		colWidth.action = "60px";
	}else{
		actions = `<button class="editbutton" onclick="updateConf(event, '`+type+`')">Update</button>`;
		fields = {id: "<input type='hidden' name='id' value='$val'/>$val", value: "<input type='text' name='value' value='$val'></input>"};
	}
	genPopulateTableFromArray(data, el, hidden, false, false, false, actions, haction, fields, colWidth);
}

function reloadConfSection(el, type){
	genPopulateTableFromArray(false, el);
	el.innerHTML = "<div class='center'><i class='fa fa-circle-o-notch fa-spin' style='font-size:48px'></i></div>";
	fetchContent("getconf/"+type).then((resp) => {
		if(resp instanceof Response){
			if(resp.ok){
				resp.json().then((data) => {
					type = "conf"+type;
					confData[type] = data;
					loadConfigTypeTable(el, type);
				});
				return;
			}
		}
		// handle failure
		genPopulateTableFromArray(false, el);
		if(resp)
			alert("Got an error fetching data from server.\n"+resp.statusText);
		else
			alert("Failed to fetch data from the server.");  
	});
}

async function runStudioConf(evt, type){
	evt.preventDefault();
	let cols = evt.target.parentNode.parentNode.childNodes;
	let name = "";
	for(let i=0; i < cols.length; i++){
		if(cols[i].firstChild.name === "id"){
			name = cols[i].firstChild.value;
			break;
		}
	}
	if(name.length){
		let response;
		try{
			response = await fetch("studio/run/"+name);
		}catch(err){
			alert("Caught an error sending request to server.\n"+err);
			return;
		}
		if(!response.ok){
			alert("response code from server.\n"+response.statusText);
			return;
		}
		let msg = await response.text();
		alert(msg);
	}
}

async function updateConf(evt, type){
	if(evt)
		evt.preventDefault();
	let props = getRowInputProps(evt.target.parentNode.parentNode);
	let id = props.id;
	delete props.id;
	type = type.replace("conf", "");
	if(type === "users"){
		if(props.password.length){
			// special handing for user settings (i.e. password hashing, etc)
			let response;
			try{
				response = await fetch("genpass", {
					method: 'POST',
					body: JSON.stringify({password: props.password}),
					headers: {
						"Content-Type": "application/json",
						"Accept": "application/json"
					}
				});
			}catch(err){
				alert("Caught an error sending data to server.\n"+err);
				return;
			}
			if(!response.ok){
				alert("response code from server.\n"+response.statusText);
				return;
			}
			let data = await response.json();
			props.password = data.hash;
			props.salt = data.salt;
		}else{
			delete props.password;
		}
	}
	try{
		response = await fetch("setconf/"+type+"/"+id, {
			method: 'POST',
			body: JSON.stringify(props),
			headers: {
				"Content-Type": "application/json",
				"Accept": "application/json"
			}
		});
	}catch(err){
		alert("Caught an error sending data to server.\n"+err);
		return err;
	}
}

function newConf(evt, type){
	if(evt)
		evt.preventDefault();
	let props = {id: ""};
	if(type === "confstudios"){
		props.host = "";
		props.port = 9550;
		props.run = "";
		props.startup = false;
		props.minpool = 2;
		props.maxpool = 5;
	}else if(type === "confusers"){
		props.password = "";
		props.permission = "";
	}else if(type === "conffiles"){
		let retVal = prompt("Custom media directory lable (i.e. ads): ", "");
		if(retVal && retVal.length){
			props.id = "mediaDir-"+retVal;
			props.value = "";
		}else
			return;
	}else{
		props.value = "";
	}
	confData[type].push(props);
	loadConfigTypeTable(evt.target.parentNode.parentNode.parentNode.parentNode, type)
}


async function delConf(evt, type){
	if(evt)
		evt.preventDefault();
	let props = getRowInputProps(evt.target.parentNode.parentNode);
	let shorttype = type.replace("conf", "");
	let response;
	try{
		response = await fetch("delconf/"+shorttype+"/"+props.id);
	}catch(err){
		alert("Caught an error from the server.\n"+err);
		return err;
	}
	if(!response.ok){
		alert("Bad status code from server.\n"+response.statusText);
		return;
	}
	
	let list = confData[type];
	for(let i = 0; i < list.length; i++){
		if(list[i].id === props.id){
			list.splice(i, 1);
			break;
		}
	}
	loadConfigTypeTable(evt.target.parentNode.parentNode.parentNode.parentNode, type)
}

function mediaDirActions(entry){
	if(entry.id.indexOf("mediaDir-") == 0)
		return `<button class="editbutton" onclick="delConf(event, 'conffiles')">-</button>`
	else if(entry.id.indexOf("mediaDir") == 0)
		return `<button class="editbutton" onclick="newConf(event, 'conffiles')">+</button>`;
	else
		return "";
}

/***** Logs specific functions *****/

var logList = [];

async function refreshLogsLocationDep(val){
	if(locName.getPrior() !== val){
		await loadLogs();
	}
}

function selectAllLogs(evt){
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
	}
	let table = document.getElementById("logres").firstChild.firstChild;
	let els = table.querySelectorAll('input[type=checkbox]:not(:checked)');
	if(els){
		for(let i=0; i<els.length; i++)
			els[i].checked = true;
	}
}

function unselectAllLogs(evt){
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
	}
	let table = document.getElementById("logres").firstChild.firstChild;
	let els = table.querySelectorAll('input[type=checkbox]:checked');
	if(els){
		for(let i=0; i<els.length; i++)
			els[i].checked = false;
	}
}

async function logRowClick(evt){
	let row = evt.target.parentElement;
	if((row.localName == "td") && (row.firstChild.localName !== "input"))
		row = row.parentNode;
	if(row.localName == "tr"){
		let sel = row.childNodes[0].firstChild;	// select column
		let idx = sel.getAttribute("data-idx");
		item = logList[idx];
		if(item.Item){
			item.tocID = item.Item;
			let credentials = cred.getValue();
			if(credentials && ['admin', 'manager', 'library'].includes(credentials.permission))
				showItem(item, true);
			else
				showItem(item, false);
		}
	}
}

async function logsToStash(evt){
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
	}
	let table = document.getElementById("logres").firstChild.firstChild;
	let els = table.querySelectorAll('input[type=checkbox]:checked');
	if(els){
		for(let i=0; i<els.length; i++){
			let div = els[i].parentNode;
			let idx = div.getAttribute("data-idx");
			let item = logList[idx];
			if(item){
				let data = {};
				if(item.Item){
					// get library item properties
					let response;
					try{
						response = await fetch("library/item/"+item.Item);
					}catch(err){
						alert("Caught an error fetching data from server.\n"+err);
						return;
					}
					if(response.ok){
						data = await response.json();
					}else{
						alert("Got an error fetching data from server.\n"+response.statusText);
						return;
					}
				}else{
					// use URL
					data.URL = item.Source;
					data.Name = item.Name;
					data.Album = item.Album;
					data.ARtist = item.Artist;
				}
				appendItemToStash(data);
			}
		}
	}
}

function logNowBtn(){
	let el = document.getElementById("logdatesel");
	let dateObj = new Date();
	el.valueAsDate = new Date(Date.UTC(dateObj.getFullYear(), dateObj.getMonth(), dateObj.getDate()));
	loadLogs();
}

async function loadLogs(evt){
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
	}
	let div = document.getElementById("logres");
	let datesel = document.getElementById("logdatesel");
	let lname = locName.getValue();
	if(!lname || !lname.length)
		div.innerHTML = "Please select a Library Location for which to get the logs.";
	else{
		genPopulateTableFromArray(false, div);
		if(!datesel.value.length){
			// set default date to today
			let today = new Date(Date.now());
			let dd = String(today.getDate()).padStart(2, '0');
			let mm = String(today.getMonth() + 1).padStart(2, '0');
			let yyyy = today.getFullYear();
			today = yyyy + '-' + mm + '-' + dd;
			datesel.value = today;
		}
		let logs;
		let api = "library/logs";
		let resp = await fetchContent(api, {
				method: 'POST',
				body: JSON.stringify({location: lname, datetime: datesel.value+"-23:59"}),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp){
			if(resp.ok){
				let result = await resp.json();
				if(result && result.length){
					let keys = Object.keys(result[0]);
					let colmap = {};
					for(let i=0; i<keys.length; i++)	// hide all fields
						colmap[keys[i]] = false;
					colmap.TimeStr = "Time";	// except for these two
					colmap.Label = "Item";
					let haction = false;
					let actions = false;
					let colWidth = {TimeStr:"60px"};
					let fields = {TimeStr:logTimeView, Label: logItemView};
					logList = result;
					genPopulateTableFromArray(result, div, colmap, logRowClick, false, false, actions, haction, fields, colWidth);
				}
			}else{
				genPopulateTableFromArray(false, div);
				alert("Got an error fetching logs from server.\n"+resp.statusText);
			}
		}else{
			genPopulateTableFromArray(false, div);
			alert("Failed to fetch logs from the server.");
		}
	
	}
}

function logTimeView(val, row, i){
	let inner = "<div data-idx='"+i+"'>";
	if(row.Item || (row.Source && row.Source.length))
		inner += "<input type='checkbox'></input>";
	return inner + "<center>"+val+"</center></div>";
}

function logItemView(val, row){
	let inner = quoteattr(row.Name)+"<br>"+quoteattr(row.Artist)+"<br>"+quoteattr(row.Album); 
	if(document.getElementById("logowner").checked)
		inner += "<br>"+quoteattr(row.Owner);
	if(document.getElementById("logsource").checked)
		inner += "<br>"+quoteattr(row.Source);
	if(document.getElementById("logids").checked)
		inner += "<br>ItemID: "+row.Item+", ArtistID: "+row.ArtistID+", AlbumID: "+row.ArtistID+", OwnerID: "+row.OwnerID+", LogID: "+row.id;
	return inner;
}

/***** Schedule specific functions *****/

var schedInst = false;
var schedFill = false;

async function refreshSchedLocationDep(val){
	if(locName.getPrior() !== val){
		await loadSchedule();
	}
}

async function schedCellClick(evt){
	let sel = evt.target;
	let id = sel.getAttribute("data-id");
	if(id){
		let item = {tocID: id};
		let credentials = cred.getValue();
		if(credentials && ['admin', 'manager', 'library'].includes(credentials.permission))
			showItem(item, true);
		else
			showItem(item, false);
	}
}

async function loadSchedule(evt){
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
	}
	// generate legend
	let div = document.getElementById("schedkey");
	genPopulateSchedLegend(div);
	// generate actual schedule table
	div = document.getElementById("scheddiv");
	let datesel = document.getElementById("scheddatesel");
	let lname = locName.getValue();
	if(!lname || !lname.length)
		div.innerHTML = "Please select a Library Location for which to get the schedule.";
	else{
		genPopulateSchedTable(false, false, div);
		if(!datesel.value.length){
			// set default date to today
			let today = new Date(Date.now());
			let dd = String(today.getDate()).padStart(2, '0');
			let mm = String(today.getMonth() + 1).padStart(2, '0');
			let yyyy = today.getFullYear();
			today = yyyy + '-' + mm + '-' + dd;
			datesel.value = today;
		}
		let api = "library/sched";
		let resp = await fetchContent(api, {
				method: 'POST',
				body: JSON.stringify({location: lname, date: datesel.value, fill: 1}),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp){
			if(resp.ok){
				let result = await resp.json();
				if(result)
					schedFill = result;
			}else{
				genPopulateTableFromArray(false, div);
				alert("Got an error fetching schedule from server.\n"+resp.statusText);
				return;
			}
		}else{
			alert("Failed to fetch schedule from the server.");
			return;
		}
		resp = await fetchContent(api, {
				method: 'POST',
				body: JSON.stringify({location: lname, date: datesel.value, fill: 0}),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp){
			if(resp.ok){
				let result = await resp.json();
				if(result){
					schedInst = result;
					genPopulateSchedTable(schedInst, schedFill, div, schedCellClick);
				}
			}else{
				genPopulateTableFromArray(false, div);
				alert("Got an error fetching schedule from server.\n"+resp.statusText);
			}
		}else{
			genPopulateTableFromArray(false, div);
			alert("Failed to fetch schedule from the server.");
		}

	}
}

/***** Query specific functions *****/

var queryList = false;
var custDiaSelList = [];
var lastCustomList;

async function libQueryRefreshList(evt, keepSQL){
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
	}
	let resp;
	let div = document.getElementById("queryList");
	div.innerHTML = "<div class='center'><i class='fa fa-circle-o-notch fa-spin' style='font-size:48px'></i></div>";
	resp = await fetchContent("library/get/queries?sortBy=Name");
	if(resp){
		if(resp.ok){
			let list = await resp.json();
			queryList = list;
			if(list && list.length){
				let colmap = {SQLText: false, id: false, Name: "Saved Queries"};
				genPopulateTableFromArray(list, div, colmap, queryListRowClick);
				if(!keepSQL){
					let el = document.getElementById("queryName");
					el.setAttribute("data-id", "0");
					el.setAttribute("data-idx", "-1");
					el = document.getElementById("querySQL");
					el.value = "";
				}
			}else
				div.innerText = "No saved queries";
			return;
		}else{
			alert("Got an error fetching categories from server.\n"+resp.statusText);
		}
	}else{
		alert("Failed to fetch categories from the server.");
	}
	div.innerText = "No saved queries";
}

async function queryListRowClick(evt){
	let row = evt.target.parentElement;
	let idx = row.rowIndex-1;
	let name = document.getElementById("queryName");
	let text = document.getElementById("querySQL");
	name.value = quoteattr(queryList[idx].Name);
	name.setAttribute("data-id", queryList[idx].id);
	name.setAttribute("data-idx", idx);
	text.value = queryList[idx].SQLText;
}

function queryNew(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let name = document.getElementById("queryName");
	let text = document.getElementById("querySQL");
	name.value = "New Custom Query";
	name.setAttribute("data-id", "0");
	name.setAttribute("data-idx", "-1");
	text.value = "";
}

async function queryRun(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let el = document.getElementById("queryName");
	let id = el.getAttribute("data-id");
	if(!id)
		return;
	let text = document.getElementById("querySQL");
	query = text.value;
	let inner = buildQueryDialog(query);
	if(inner.length){
		// show prompt/select dialog box to get user input
		let div = document.getElementById("custQueryDiv");
		div.innerHTML = inner;
		let dia = document.getElementById("custQueryDialog");
		dia.style.display = "block";
	}else{
		// run query unmodified
		runFinalQuery(id, null, null);
	}
}

async function querySave(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let name = document.getElementById("queryName");
	let text = document.getElementById("querySQL");
	let idv = name.getAttribute("data-id");
	let id = 0;
	if(idv)
		id = parseInt(idv);
	let idx = name.getAttribute("data-idx");
	let i = -1;
	if(idx)
		i = parseInt(idx);
	let obj = {SQLText: text.value};
	if((i == -1) || (name.value !== queryList[i].Name))
			// save new name for ID
			obj.Name = name.value;
	let api = "library/set/queries";
	if(id)
		api += "/"+id;
	let resp = await fetchContent(api, {
			method: 'POST',
			body: JSON.stringify(obj),
			headers: {
				"Content-Type": "application/json",
				"Accept": "application/json"
			}
		});
	if(resp){
		if(resp.ok){
			let reload = await resp.json();
			if(reload.insertId){
				// new query saved
				for(let j=0; j<queryList.length; j++){
					if(queryList[j].id == reload.insertId){
						i = j;
						break;
					}
				}
				name.setAttribute("data-id", reload.insertId);
				name.setAttribute("data-idx", i);
			}
			if(!reload.affectedRows)
				alert("Failed to update or get ID of new query.\n");
			else{
				alert("Query has been saved.");
				libQueryRefreshList(false, true);
			}
		}else{
			alert("Got an error saving data to server.\n"+resp.statusText);
		}
	}else{
		alert("Failed to save data to the server.");
	}
}

async function queryDelete(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let name = document.getElementById("queryName");
	let text = document.getElementById("querySQL");
	let idv = name.getAttribute("data-id");
	let id = 0;
	if(idv)
		id = parseInt(idv);
	if(id){
		let api = "library/delete/queries/"+id;
		let resp = await fetchContent(api);
		if(resp){
			if(!resp.ok){
				alert("Got an error deleteing query from server.\n"+resp.statusText);
			}else{
				alert("Query has been deleted.");
				name.setAttribute("data-id", 0);
				name.setAttribute("data-idx", -1);
				name.value = "";
				text.value = "";
				libQueryRefreshList();
			}
		}else{
			alert("Failed to delete query from server.");
		}
	}
}

async function getQueryItemInfo(evt){
	let row = evt.target.parentElement;
	if((row.localName == "td") && (row.firstChild.localName !== "button"))
		row = row.parentNode;
	if(row.localName == "tr"){
		let i = row.rowIndex;
		let id = lastCustomList[i-1].ItemID;
		if(id){
			let credentials = cred.getValue();
			if(credentials && ['admin', 'manager', 'library'].includes(credentials.permission))
				showItem({tocID: id}, true);
			else
				showItem({tocID: id}, false);
		}
	}
}

async function queryItemToStash(evt){
	let row = evt.target.parentNode.parentNode.rowIndex;
	let id = lastCustomList[row-1].ItemID;
	if(id){
		// get library item properties
		let response;
		try{
			response = await fetch("library/item/"+id);
		}catch(err){
			alert("Caught an error fetching data from server.\n"+err);
			return;
		}
		if(response.ok){
			data = await response.json();
		}else{
			alert("Got an error fetching data from server.\n"+response.statusText);
			return;
		}
		appendItemToStash(data);
	}
}

async function runFinalQuery(query, promptValues, selectValues){
	let params = {locid: locationID};
	if(promptValues && promptValues.length)
		params.prompt = promptValues;
	if(selectValues && selectValues.length)
		params.select = selectValues;
	let div = document.getElementById("queryResult");
	div.innerHTML = "<div class='center'><i class='fa fa-circle-o-notch fa-spin' style='font-size:48px'></i></div>";
	let api = "library/query/"+query;
	let resp = await fetchContent(api, {
		method: 'POST',
		body: JSON.stringify(params),
		headers: {
			"Content-Type": "application/json",
			"Accept": "application/json"
		}
	});
	if(resp){
		if(!resp.ok){
			alert("Got an error running query from server.\n"+resp.statusText);
			genPopulateTableFromArray(false, div);
			return;
		}
		let repdata = await resp.json();
		lastCustomList = repdata;
		let actions = false;
		if(repdata.length && repdata[0].ItemID)	// we have item IDs: allow get info on items
			actions = `<button class="editbutton" onclick="queryItemToStash(event)">To Stash</button>`;
		let colWidth = {ItemID: "60px", action:"40px"};
		genPopulateTableFromArray(repdata, div, false, getQueryItemInfo, false, false, actions, false, false, colWidth);
	}else{
		alert("Query failed to run from server.");
		genPopulateTableFromArray(false, div);
	}
}

function buildQueryDialog(query){
	let substr = query;
	let inner = "";
	let si = 0;
	let pi = 0;
	custDiaSelList = [];
	while(1){
		let sstart = substr.indexOf("[select(");
		let pstart = substr.indexOf("[prompt(");
		if((sstart > -1) && ((pstart == -1) || (pstart > sstart))){
			// we have a select next
			let subs = substr.substring(sstart);
			sstart = subs.indexOf("(");
			if(sstart == -1)
				break;
			let end = subs.indexOf(")");
			if(end == -1)
				break;
			let prop = subs.substring(sstart+1,end);
			// prop is the prompt text name
			inner += prop + ": "+buildQueryDiaSelect(prop, si)+"<br>"
			si++;
			end = subs.indexOf("]");
			substr = subs.substring(end+1);
		}else if(pstart > -1){
			// we have a prompt next
			let subs = substr.substring(pstart);
			pstart = subs.indexOf("(");
			if(pstart == -1)
				break;
			let end = subs.indexOf(")");
			if(end == -1)
				break;
			let prop = subs.substring(pstart+1,end);
			// prop is the prompt text name
			inner += prop + ": <input type='text' name='prompt-"+pi+"'><br>"
			pi++;
			end = subs.indexOf("]");
			substr = subs.substring(end+1);
		}else
			break;
	}
	return inner;
}

function buildQueryDiaSelect(tableName, idx){
	let inner = 
	`<div class="dropdown">
		<button class="editbutton" name="select-`+idx+`" data-id="0" onclick="toggleShowSearchList(event)">[None]</button>
		<div class="search-list" data-idx="`+idx+`">
			<button class="editbutton" onclick="refreshQueryDiaDropdown(event)">Refresh List</button><br>
			<input type="text" onkeyup="filterSearchList(event)" placeholder="Enter Search..."></input>
			<div></div>
		</div>
	</div>`;
	custDiaSelList.push(tableName);
	return inner;
}

async function custQueryDialogOK(evt){
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
	}
	let dia = document.getElementById("custQueryDialog");
	dia.style.display = "none";
	let div = document.getElementById("custQueryDiv");
	custDiaSelList = [];
	let elements = document.getElementById("custDiaForm").elements;
	let sel = [];
	let prm = [];
	for(let i = 0 ; i < elements.length ; i++){
		let item = elements.item(i);
		if(item.name){
			let val;
			let parts = item.name.split("-");
			if(parts[0] == "select"){
				val = item.getAttribute("data-id");
				sel.push(val);
			}else{
				val = item.value;
				prm.push(val);
			}
		}
	}
	let el = document.getElementById("queryName");
	let id = el.getAttribute("data-id");
	if(id)
		runFinalQuery(id, prm, sel);
}

function custQueryDialogCanc(evt){
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
	}
	let dia = document.getElementById("custQueryDialog");
	dia.style.display = "none";
	custDiaSelList = [];
}

function custDropdownChange(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let ddbut = evt.target.parentNode.parentNode.parentNode.firstElementChild;
	// close search-list menu
	toggleShowSearchList({target: ddbut});
	// set the button values
	ddbut.innerText = evt.target.innerText;
	let id = evt.target.getAttribute("data-id");
	ddbut.setAttribute("data-id", id);
}

async function refreshQueryDiaDropdown(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let target = evt.target;
	let idx = target.parentNode.getAttribute("data-idx");
	idx = parseInt(idx);
	let table = custDiaSelList[idx];
	let resp;
	if(table == "playlist")
		resp = await fetchContent("library/get/toc?Type=playlist&sortBy=Name");
	else if(table == "task")
		resp = await fetchContent("library/get/toc?Type=task&sortBy=Name");
	else
		resp = await fetchContent("library/get/"+table+"?sortBy=Name");
	if(resp){
		if(resp.ok){
			let list = await resp.json();
			// set custDropdownChange as list callback
			let el = target.parentNode.children[3];
			buildSearchList(el, list, custDropdownChange);
		}else{
			alert("Got an error fetching "+table+"s from server.\n"+resp.status);
		}
	}else{
		alert("Failed to fetch "+table+"s from the server.");
	}
}

function downloadCSV(csv, filename) {
	let csvFile;
	let downloadLink;
	
	csvFile = new Blob([csv], {type: "text/csv"});
	downloadLink = document.createElement("a");
	downloadLink.download = filename;
	downloadLink.href = window.URL.createObjectURL(csvFile);
	downloadLink.style.display = "none";
	document.body.appendChild(downloadLink);
	downloadLink.click();
}

function exportTableToCSV(evt, filename) {
	evt.preventDefault();
	evt.stopPropagation();
	let table = document.getElementById("queryResult");
	let csv = [];
	let rows = table.querySelectorAll("tr");
	
	for(let i = 0; i < rows.length; i++) {
		let row = [], cols = rows[i].querySelectorAll("td, th");
		for(let j = 0; j < cols.length; j++)
			row.push(cols[j].innerText);
		csv.push(row.join("\t"));
	}
	downloadCSV(csv.join("\n"), filename);
}

/***** Browse specific functions *****/

function removeRefineElement(event){
	let us = event.target;
	while(us.className !== "closeb")
		us = us.parentElement;
	let par = us.parentElement;
	let key = us.getAttribute("data-key");
	let lm = us.getAttribute("data-match");
	let ma = document.getElementById("bmatch");
	ma.value = lm;
	par.removeChild(us);
	browseType.setValue(key, true);	// this will trigger a query
}

function browseTypeRowClick(event){
	let i = event.currentTarget.rowIndex-1; // -1 due to header row
	let record = browseTypeList[i];
	browseType.setValue(record.qtype, true);
}

function browseRowClick(event){
	let i = event.currentTarget.rowIndex-1; // -1 due to header row
	let record = browseData[i];
	if(record.tocID){
		// get item info
		let credentials = cred.getValue();
		if(credentials && ['admin', 'manager', 'library'].includes(credentials.permission))
			showItem(browseData[i], true);
		else
			showItem(browseData[i], false);
	}else{
		// use selection as added search criteria
		let el = document.getElementById("brefine");
		let ma = document.getElementById("bmatch");
		// make sure this is not already in the refine list
		for(var element of el.childNodes) {
			try{
				let key = element.getAttribute("data-key");
				let val = element.getAttribute("data-value");
				if((key === record.qtype) && (val === record.Label)){
					return;
				}
			}catch{};
		};
		
		let button = document.createElement("button");
		button.className = "closeb";
		button.innerHTML = "<i class='fa fa-times' aria-hidden='true'></i>"+record.qtype+"\n"+record.Label;
		button.setAttribute("data-value", record.Label);
		button.setAttribute("data-key", record.qtype);
		button.setAttribute("data-match", ma.value);
		button.onclick = removeRefineElement;
		el.append(button);
		// clear match field
		ma.value = "";
		browseType.setValue("title"); // this triggers a query via var watch callback, of title type
	}
}

function browseEditItem(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let i = evt.target.getAttribute("data-index");
	showItem(browseData[i], true);
}

function browseNewItem(evt, qtype){
	evt.preventDefault();
	evt.stopPropagation();
	showItem({qtype: qtype}, true);
}

function browseCellClick(event){	// this is only the headers cells, for sorting
	if(event.target.textContent.length){
		if(browseSort === event.target.textContent)
			browseSort = "-"+event.target.textContent; // toggle: make decending
		else
			browseSort = event.target.textContent;
		browseQuery();
	}
}

function buildBrowsePostData(){
	let post = {};
	if(browseType.getValue().length){
		post.type = browseType.getValue();
		el = document.getElementById("bmatch");
		let value = el.value;
		if(value)
			post.match = "%"+value+"%";
		el = document.getElementById("brefine");
		el.childNodes.forEach( (element) => {
			try{
				let key = element.getAttribute("data-key");
				let val = element.getAttribute("data-value");
				if(post[key]){
					if(Array.isArray(post[key]))
						post[key].push(val);
					else
						// make existing value and this new one, and array
						post[key] = [post[key], val];
				}else
					post[key] = val;
			}catch{};
		});
		return post;
	}else
		return false;
}

function browseGetTypeList(){
	fetchContent("library/browse").then((resp) => {
		if(resp instanceof Response){
			if(resp.ok){
				resp.json().then((data) => {
					browseTypeList = data;
					genPopulateTableFromArray(data, document.getElementById("btype"), {qtype: false}, browseTypeRowClick);
				});
			}
		}else{
			// handle failure
			browseTypeList = false;
			if(resp)
				alert("Got an error fetching data from server.\n"+resp);
			else
				alert("Failed to fetch data from the server.");  
		}
	});
}

function browseQuery(evt){
	if(evt)
		evt.preventDefault();
	genPopulateTableFromArray(false, document.getElementById("bres"));
	if(!browseTypeList)
		browseGetTypeList();
	let post = buildBrowsePostData();
	if(!post){
		return;
	}
	if((browseSort === "Duration") || (browseSort === "-Duration")){
		if(["artist", "album", "category", "added", "rest", "comment"].includes(post.type))
			// back to default sort for types without duration
			browseSort = "Label";
	}
	post.sortBy = browseSort;
	let el = document.getElementById("bres");
	el.innerHTML = "<div class='center'><i class='fa fa-circle-o-notch fa-spin' style='font-size:48px'></i></div>";
	
	fetchContent("library/browse", {
		method: 'POST',
		body: JSON.stringify(post),
		headers: {
			"Content-Type": "application/json",
			"Accept": "application/json"
		}
	}).then((resp) => {
		if(resp instanceof Response){
			if(resp.ok){
				resp.json().then((data) => {
					browseData = data;
					let credentials = cred.getValue();
					let actions = false;
					let hactions = false;
					if(credentials && ['admin', 'manager', 'library'].includes(credentials.permission)){
						// include edit ability
						if(data.length && ['artist', 'album', 'category'].includes(post.type))
							// $i will be replaced by the row index number by the genPopulateTableFromArray() function
							actions = `<button class="editbutton" data-index="$i" onclick="browseEditItem(event)">Edit</button>`;
						if(data.length && ['playlist', 'task', 'artist', 'album', 'category'].includes(post.type))
							hactions = `<button class="editbutton" onclick="browseNewItem(event, '`+post.type+`')">+</button>`;
					}
					let colWidth = {action:"25px", Duration:"70px"};
					genPopulateTableFromArray(data, el, {id: false, qtype: false, tocID: false}, browseRowClick, browseCellClick, browseSort, actions, hactions, false, colWidth, true);
				});
				return;
			}
		}
		// handle failure
		genPopulateTableFromArray(false, document.getElementById("bres"));
		if(resp)
			alert("Got an error fetching data from server.\n"+resp);
		else
			alert("Failed to fetch data from the server.");  
	});
}

/***** File functions *****/

async function displayFilesPath(){
	let el = document.getElementById("filefolder");
	let parts = filesPath.split("/");
	let inner = "<button class='editbutton' data-index='0' onclick='refreshFiles(event)'>Temp. Media</button>";
	for(let i = 1; i < parts.length; i++)
		inner += " / <button class='editbutton' data-index='"+i+"' onclick='refreshFiles(event)'>"+parts[i]+"</button>";
	el.innerHTML = inner;
}

async function refreshFiles(evt){
	if(evt){
		evt.preventDefault();
		let i = parseInt(evt.target.getAttribute("data-index"));
		let parts = filesPath.split("/");
		let trimed = parts.slice(0, i+1);
		filesPath = trimed.join("/");
		displayFilesPath();
	}
	loadFilesTbl();
}

async function uploadFiles(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let form = document.getElementById("uploadform");
	let formData = new FormData(form);
	let resp;
	resp = await fetchContent("tmpupload", {
			method: 'POST',
			body: formData
		});
	if(resp){
		if(resp.ok){
			loadFilesTbl();
		}else{
			alert("Got an error posting data to server.\n"+resp.status);
		}
	}else{
		alert("Failed to post data tp the server.");
	}
}

function selectAllFiles(evt){
	if(evt)
		evt.preventDefault();
	let rows = document.getElementById("filelist").firstChild.firstChild.childNodes;
	for(let i=0; i<rows.length; i++){
		let sel = rows[i].childNodes[0].firstChild;	// select column
		if((sel.localName === "input") && !sel.checked)
			sel.checked = true;
	}
}

function unselectAllFiles(evt){
	if(evt)
		evt.preventDefault();
	let rows = document.getElementById("filelist").firstChild.firstChild.childNodes;
	for(let i=0; i<rows.length; i++){
		let sel = rows[i].childNodes[0].firstChild;	// select column
		if((sel.localName === "input") && sel.checked)
			sel.checked = false;
	}
}

function fileRowClick(event){
	let row = event.target.parentElement;
	if((row.localName == "td") && (row.firstChild.localName !== "input"))
		row = row.parentNode;
	if(row.localName == "tr"){
		let sel = row.childNodes[0].firstChild;	// select column
		if(sel.localName === "i"){
			let name = row.childNodes[1].innerText;	// name column
			// directory row
			if(filesPath)
				filesPath += "/" + name;
			else
				filesPath = "/" + name;
			displayFilesPath();
			refreshFiles();
		}else if(sel.localName === "input"){
			// file name
			if(sel.checked)
				sel.checked = false;
			else
				sel.checked = true;
		}
	}
}

async function filesToStash(evt){
	if(evt)
		evt.preventDefault();
	let rows = document.getElementById("filelist").firstChild.firstChild.childNodes;
	for(let i=0; i<rows.length; i++){
		let sel = rows[i].childNodes[0].firstChild;	// select column
		if((sel.localName === "input") && sel.checked){
			let path = filesPath+"/"+rows[i].childNodes[1].innerText;
			// query API for tmpfile properties
			let data = false;
			let api = "library/info" + path;
			let resp = await fetchContent(api);
			if(resp){
				if(!resp.ok){
					alert("Got an error retreaving file info from server.\n"+resp.status);
					return;
				}
				data = await resp.json();
				data.tmpfile = path;
			}else{
				alert("Failed to retreaving file info from server.");
				return;
			}
			if(data){
				itemProps = data;
				data.canEdit = false;
				appendItemToStash(data);
			}
		}
	}
}

async function filesToQueue(evt){
	if(evt)
		evt.preventDefault();
	let rows = document.getElementById("filelist").firstChild.firstChild.childNodes;
	let items = [];
	for(let i=0; i<rows.length; i++){
		let sel = rows[i].childNodes[0].firstChild;	// select column
		if((sel.localName === "input") && sel.checked){
			let path = rows[i].childNodes[1].innerText;
			if(filesPath.length)
				path = filesPath+"/"+path;
			// query API for tmpfileurl properties
			let data = false;
			let resp = await fetchContent("library/tmpmediaurl", {
					method: 'POST',
					body: JSON.stringify({path: path}),
					headers: {
						"Content-Type": "application/json",
						"Accept": "application/json"
					}
				});
			if(resp){
				if(!resp.ok){
					alert("Got an error retreaving file info from server.\n"+resp.status);
					return;
				}
				data = await resp.text();
			}else{
				alert("Failed to retreaving file info from server.");
				return;
			}
			if(data){
				let item = {URL: data, Type: "file"};
				items.push(item);
			}
		}
	}
	appendItemsToQueue(items);
}

async function getFileInfo(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let row = event.target.parentElement.parentElement;
	let path = filesPath+"/"+row.childNodes[1].innerText;
	showItem({tmpfile: path});
}

function setRowAction(obj){
	if(!obj.isDir){
		return `<button class="editbutton" onclick="getFileInfo(event)">i</button>`;
	}else
		return "";
}

async function loadFilesTbl(){
	let el = document.getElementById("filelist");
	genPopulateTableFromArray(false, el); // clear any existing display
	el.innerHTML = "<div class='center'><i class='fa fa-circle-o-notch fa-spin' style='font-size:48px'></i></div>";
	if(filesPath == false){
		filesPath = "";
		displayFilesPath();
	}
	let resp = await fetchContent("tmplist"+filesPath);
	if(resp){
		if(resp.ok){
			let list = await resp.json();
			filesList = list;
			let action = `$setRowAction$`;
			let hidden = {isDir: "Select", id: "Name", created: false, modified: false, size: "Size (kiB)"};
			let haction = `<button class="editbutton" onclick="selectAllFiles(event)">Select All</button>
								<button class="editbutton" onclick="unselectAllFiles(event)">Unselect All</button>`;
			let fields = {id: "<input type='hidden' name='Name' data-id='$id' data-index='$i'></input>$val",
								isDir: "$iftrue<i class='fa fa-folder-open' aria-hidden='true'>$iftrue$iffalse<input type='checkbox'/>$iffalse"};
			let colWidth = {action:"100px", size:"90px", isDir:"40px"};
			genPopulateTableFromArray(list, el, hidden, fileRowClick, false, false, action, haction, fields, colWidth);
			return;
		}
	}
	// handle failure
	genPopulateTableFromArray(false, el);
	if(resp)
		alert("Got an error fetching data from server.\n"+resp.status);
	else
		alert("Failed to fetch data from the server."); 
}

async function refreshFileImportCats(evt){
	if(evt && evt.preventDefault){
		evt.preventDefault();
		evt.stopPropagation();
		await getCatList();
	}else{
		let div = document.getElementById("fileImportCatList");
		if(div){
			buildSearchList(div, catListCache.getValue(), importCatSelect);
			let el = document.getElementById("fileImportCatText");
			filterSearchList({target: el});
		}
	}
}

function clearCatSelect(evt){
	let el = document.getElementById("fileImportCatBtn");
	el.setAttribute("data-id", "0");
	el.innerText = "[None]";
}

async function importCatSelect(evt){
	// close search-list menu
	let el = document.getElementById("fileImportCatBtn");
	toggleShowSearchList({target: el});
	// set button text to selection and save catid
	el.setAttribute("data-id", evt.target.getAttribute("data-id"));
	el.innerText = evt.target.innerText;
}

async function importSelectFiles(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let form = document.getElementById("importform");
	let formData = new FormData(form);
	let el = document.getElementById("fileImportCatBtn");
	formData.set("catid", el.getAttribute("data-id"));
	let plainFormData = Object.fromEntries(formData.entries());
	let rows = document.getElementById("filelist").firstChild.firstChild.childNodes;
	for(let i=0; i<rows.length; i++){
		let sel = rows[i].childNodes[0].firstChild;	// select column
		if((sel.localName === "input") && sel.checked){
			let path = filesPath+"/"+rows[i].childNodes[1].innerText;
			let resp;
			resp = await fetchContent("library/import"+path, {
					method: 'POST',
					body: JSON.stringify(plainFormData),
					headers: {
						"Content-Type": "application/json",
						"Accept": "application/json"
					}
				});
			let stat = "Error";
			let statno = -1;
			if(resp){
				if(resp.ok){
					let result = await resp.json();
					statno = result.status;
				}else{
					alert("Got an error fetching data from server.\n"+resp.status);
				}
			}else{
				alert("Failed to fetch data from the server.");
			}
			if(statno == -3)
				stat = "File move failed";
			else if(statno == -2)
				stat = "Unknown file type";
			else if(statno == 0)
				stat = "Skipped";
			else if(statno == 1)
				stat = "Added";
			else if(statno == 2)
				stat = "Category update";
			else if(statno == 3)
				stat = "Replaced";
			else if(statno == 4)
				stat = "Fixed missing";
			// statno=-3 file copy failed, -2, bad file, -1 error, 0 skipped, 1 new/added, 2 cat update existing, 3 replaced existing, 4 fixed existing
			rows[i].childNodes[3].innerHTML = stat;
		}
	}
}

/***** Studio functions *****/

var busmeters = [];
var studioStateCache = {control: false, meta: {}, queue: [], queueRev: 0, queueSec: 0.0, queueDur: 0.0, logTime: 0, logs: [], live: [], ins: [], outs: [], encoders: {}, autoStat: 0, runStat: 0, chancnt: 0, buscnt: 0, outcnt: 0};
var pTemplate;
var midiAccess;

/***** periodic all-studio-timers update tick and sync *****/
setInterval('updateStudioTimers()', 500);
setInterval('syncStudioMetalist(studioName.getValue())', 60000);

function updateStudioTimers(){
	let sec = studioStateCache.queueSec;
	sec = sec - 0.5;
	if(sec < 0.0)
		sec = 0.0;
	studioStateCache.queueSec = sec;
	let el = document.getElementById("stQNext");
	el.innerText = timeFormat(sec, 1);
	
	sec = studioStateCache.queueDur;
	sec = sec - 0.5;
	if(sec < 0.0)
		sec = 0.0;
	studioStateCache.queueDur = sec;
	el = document.getElementById("stQDur");
	el.innerText = timeFormat(sec, 1) + " total";
	let ins = studioStateCache.ins;
	if(ins && ins.length){
		for(n=0; n<ins.length; n++){
			let p = studioStateCache.ins[n];
			if(p && (parseInt(p.status) & 0x4)){
				let pos = parseFloat(p.pos);
				pos = pos + 0.5;
				p.pos = pos.toString();
			}
			playerTimeUpdate(n);
		}
	}
	let enc = studioStateCache.encoders;
	if(enc && enc.length){
		for(n=0; n<enc.length; n++){
			let p = studioStateCache.encoders[n];
			if(p && (parseInt(p.Status) & 0x4)){
				let uid = p.UID;
				let pos = parseFloat(p.Time);
				pos = pos + 0.5;
				p.Time = pos.toString();
				el = document.getElementById("rTime"+uid);
				if(el)
					el.innerText = timeFormat(pos, 1);
			}
		}
	}
	if(studioStateCache.control)
		studioStateCache.control.tick();
}

function playerTimeUpdate(n){
	let p = studioStateCache.ins[n];
	if(p && (p.pos != undefined) && p.pos.length){
		let pte = document.getElementById("pTime"+n);
		let pde = document.getElementById("pRem"+n);
		if(pte && pde){
			let pos = Math.round(parseFloat(p.pos));
			pte.innerText = timeFormat(pos, 1);
			if(p.dur){
				let dur = Math.round(parseFloat(p.dur));
				if(dur){
					dur = dur - pos;
					if(dur < 0.00)
						dur = 0.0;
					pde.innerText = timeFormat(dur, 1);
				}
			}
		}
	}
}

async function setLibUsingStudio(){
	let settings = studioStateCache.meta[0];
	if(settings){
		let locid = settings.db_loc;
		if(locid){
			let resp;
			resp = await fetchContent("library/get/locations?id="+locid);
			if(resp){
				if(resp.ok){
					let res = await resp.json();
					if(res && res.length)
						locName.setValue(res[0].Name);
				}else{
					alert("Got an error fetching library location name from server.\n"+resp.statusText);
				}
			}else if(cred.getValue()){
				alert("Failed to fetch library location name from the server.");
			}
		}
	}
}

async function studioChangeCallback(value){
	// clear existing VU meters
	let busvu = document.getElementById('studioOutsVU');
	while(busvu.firstChild)
		busvu.removeChild(busvu.firstChild);
	let old = studioName.getPrior();
	if(old){
		eventTypeUnreg(old);
		eventTypeUnreg("vu_"+old);
	}
	eventTypeReg(value, studioHandleNotice);
	// new VU meter canvases will be built once we receive the first VU data event
	eventTypeReg("vu_"+value, studioVuUpdate);
	busmeters = [];
	studioStateCache = {meta: {}, queueRev: -1, queueSec: 0.0, queueDur: 0.0, logTime: 0, logs: [], live: [], ins: [], outs: [], encoders: [], runStat: 0, autoStat: 0, chancnt: 0, buscnt: 0, outcnt: 0};
	await getServerInfo(value);
	await syncStudioMetalist(value);
	await syncStudioStat(value);
	await syncPlayers(value);
	await refreshOutGroups();
	updateControlSurface();
}

async function getServerInfo(studio){
	let resp = await fetchContent("studio/"+studio+"?cmd=info&raw=1");
	if(resp instanceof Response){
		if(resp.ok){
			let data = await resp.text();
			let lines = data.split("\n");
			for(let n = 0; n < lines.length; n++){
				let fields = lines[n].split(" = ");
				let key = fields[0];
				let value = fields[1];
				if((key == "\tmatrix bus count")){
					let part = value.split(" x ");
					studioStateCache.buscnt = parseInt(part[0]);
				}else if((key == "\toutput group count")){
					let part = value.split(" x ");
					studioStateCache.outcnt = parseInt(part[0]);
					studioStateCache.chancnt = parseInt(part[1]);
				}
			}
		}
	}
}

function queueHistoryHasID(itemID){
	if(parseInt(itemID)){
		return `<button class="editbutton" onclick="queueHistoryInfo(event)">i</button>`;
	}
	return "";
}

function calcQueueTimeToNext(data){
	if(data){
		for(let n = (data.length - 1); n >= 0; n--){
			if(data[n].status & 0x0C)	// has played or playing status flag is set
				return data[n].segout;
		}
	}
	return 0.0;
}

function calcQueueTimeToEnd(data){
	let last = data.length - 1;
	if(last >= 0)
		return data[last].total;
	return 0.0;
}

function studioSegNow(evt){
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
	}
	let studio = studioName.getValue();
	if(studio.length)
		fetchContent("studio/"+studio+"?cmd=segnow");
}

function studioAutoMode(evt, type){
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
	}
	let studio = studioName.getValue();
	if(studio.length)
		fetchContent("studio/"+studio+"?cmd=auto"+type);
}

function studioRunStop(evt){
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
	}
	// undo browser state change... we don't want the check box 
	// to actually change is propigated back from the server
	if(evt.target.checked)
		evt.target.checked = false;
	else
		evt.target.checked = true;
	let studio = studioName.getValue();
	if(studio.length){
		if(evt.target.checked)
			fetchContent("studio/"+studio+"?cmd=halt");
		else
			fetchContent("studio/"+studio+"?cmd=run");
	}
}

async function refreshRecorderPanel(){
	let studio = studioName.getValue();
	let el = document.getElementById("stRecorderList");
	if(studio && studio.length){
		let resp = await fetchContent("studio/"+studio+"?cmd=rstat&raw=1");
		if(resp){
			if(resp.ok){
				let list = [];
				let data = await resp.text();
				let lines = data.split("\n");
				for(let n = 1; n < lines.length; n++){
					let fields = lines[n].split("\t");
					let name = fields[6];
					if(name && name.length){
						let uid = parseInt(fields[0], 16);
						list.push({UID: uid, Status: fields[1], Time: fields[2], Limit: fields[3], Gain: fields[5], Name: name});
					}
				}
				studioStateCache.encoders = list;
				let format = 
`<div class="stRecGrid" id="r$UID$">
	<div data-id="$UID$" style="grid-area: head; display: flex; justify-content: space-between; padding: 2px;">
		<div style="width: 17px;">
			<button id="rUnload$UID$" style="height: 13px; width: 13px; font-size: 8px; padding: 0px;" data-id="$UID$"><i class="fa fa-times-circle" aria-hidden="true" onclick="stEncoderAction('unload', event);"></i></button>
		</div>
		<div id ="rName$UID$">$Name$</div>
		<button style="height: 13px; width: 13px; font-size: 8px;" data-id="$UID$" onclick="stEncoderAction('settings', event);">i</button>
	</div>
	<div id="rTime$UID$" style="grid-area: time; font-size: 12px;">$Time->timeFormatNoDP$</div>
	<div id="rRem" style="grid-area: rem; font-size: 12px;"></div>
	<div id="rBus$UID$" style="grid-area: bus;">$Ports$</div>
	<div id="rStatus$UID$" style="grid-area: status;"></div>
	<div style="grid-area: button; padding: 2px;">
		<button class="playerStopBtn" id="rRec$UID$" data-id="$UID$" style="float: left; display: flex;"><i class="fa fa-circle" aria-hidden="true" onclick="stEncoderAction('run', event);"></i></button>
		<button class="playerStopBtn" id="rStop$UID$" data-id="$UID$" style="float: right; display: flex;"><i class="fa fa-stop" aria-hidden="true" onclick="stEncoderAction('stop', event);"></i></button>
	</div>
	<div style="grid-area: fade;">
		<div style="display: flex; flex-flow: row; align-items: center;">
			<input id="rGain$UID$" data-id="$UID$" type="range" min="0" max="1.5" value="$Gain->linToFader$" step="0.01" style=" width: 90%; height: 12px;" 
				oninput="stEncoderVolAction(event);" ontouchstart="this.touching = true;" onmousedown="this.touching = true;" 
				onmouseup="this.touching = false;" ontouchend="this.touching = false;"></input>
			<div id="rdB$UID$" style="width:30px">$Gain->linToDBtext$</div>
		</div>
	</div>
	<div style="grid-area: vu;" id="rVU$UID$"></div>
</div>`;
				genDragableListFromObjectArray(false, false, list, el, format);
				list.forEach(item => { stEncStatus(item.UID, item.Status); } );
				// add button and menu for new encoder/recorder
				format =
`<button id='stNewRecTemplateBtn' class="editbutton" onclick="toggleShowSearchList(event)">+</button>
<div class="search-list-rtemplate">
	<button class="editbutton" onclick="stRefreshRecTemplateList(event)">Refresh List</button><br>
	<input id="stRecTemplateText" type="text" style="width: 120px;" onkeyup="filterSearchList(event)" data-div="stRecTemplateList" placeholder="Enter Search..."></input>
	<div id="stRecTemplateList"></div>
</div>`;
				appendDragableItem(false, false, null, -1, el, format);
			}else{
				alert("Got an error fetching recorder list from server.\n"+resp.statusText);
			}
		}else{
			alert("Failed to fetch recorder list  from the server.");
		}
	}
}

function stEncStatus(ref, statusBits){
/*	rec_uninit			=0L,
	rec_ready			=(1L << 0),
	rec_start			=(1L << 1),
	rec_running			=(1L << 2),		// note: same as status_playing 
	rec_stop			=(1L << 3),
	rec_done			=(1L << 5),		// recorder time limit exceeded
	rec_locked			=(1L << 7),
	rec_err_write		=(1L << 8),		// recorder encoder write error
	rec_err_keepup		=(1L << 9),		// recorder encoder can't keep up with audio
	rec_err_con_fail	=(1L << 10),	// recorder connection failure (stream encoder)
	rec_err_comp		=(1L << 11),	// recorder compression error
	rec_conn			=(1L << 12),	// recorder connecting
	rec_wait			=(1L << 13),	// recorder time record waiting
	rec_err_other		=(1L << 14)		// error number in high 16 bit word
*/
	let st = document.getElementById("rStatus"+ref);
	let rec = document.getElementById("rRec"+ref);
	let stop = document.getElementById("rStop"+ref);
	let ul = document.getElementById("rUnload"+ref);
	let rColor = "DarkGrey";
	let sColor = "DarkGrey";
	if(st){
		if(statusBits & 1){
			if(statusBits & 256)
				st.innerText = "Err: Write fail";
			else if(statusBits & 512)
				st.innerText =  "Err: Overflow";
			else if(statusBits & 1024)
				st.innerText =  "Err: Connection";
			else if(statusBits & 2)
				st.innerText =  "Starting";
			else if(statusBits & 4){
				st.innerText =  "Running";
				rColor = "red";
			}else if(statusBits & 32)
				st.innerText =  "Done";
			else if(statusBits & 4096)
				st.innerText =  "Connecting";
			else if(statusBits & 8192)
				st.innerText =  "Waiting";
			else{
				st.innerText =  "Ready";
				sColor = "yellow";
			}
		}else
			st.innerText =  "Uninitialized";
	}
	if(ul){
		if(statusBits & 128)
			ul.firstElementChild.className = "fa fa-lock";
		else
			ul.firstElementChild.className = "fa fa-times-circle";
	}
	let el = document.getElementById("rGain"+ref);
	if(el){
		if(statusBits & 128)
			el.disabled = true;
		else
			el.disabled = false;
	}
	el = document.getElementById("rRec"+ref);
	if(el){
		if(statusBits & 128)
			el.disabled = true;
		else
			el.disabled = false;
	}
	el = document.getElementById("rStop"+ref);
	if(el){
		if(statusBits & 128)
			el.disabled = true;
		else
			el.disabled = false;
	}
	el = document.getElementById("rUnload"+ref);
	if(el){
		if(statusBits & 128)
			el.disabled = true;
		else
			el.disabled = false;
	}
	if(rec)
		rec.style.backgroundColor = rColor;
	if(stop)
		stop.style.backgroundColor = sColor;
}
function stEncoderVolAction(evt){
	val = parseFloat(evt.target.value);
	let uid = evt.target.getAttribute("data-id");
	if(uid && uid.length){
		uid = parseInt(uid);
		if(uid){
			let studio = studioName.getValue();
			if(studio.length){
				val = faderToLin(val);
				let hexStr =  ("00000000" + uid.toString(16)).substr(-8);
				fetchContent("studio/"+studio+"?cmd=recgain "+hexStr+" "+val);
			}
		}
	}
}

function updateEncoderVolUI(ref, val){
	let vol = parseFloat(val);
	let fader = document.getElementById("rGain"+ref);
	let db = document.getElementById("rdB"+ref);
	if(fader && db){
		db.innerText = linToDBtext(vol);
		if(fader.touching)
			return;	// dont update slider while it is being touched
		fader.value = linToFader(vol);
	}
}

async function stEncoderAction(type, evt){
	let uid = evt.target.parentNode.getAttribute("data-id");
	if(uid && uid.length){
		uid = parseInt(uid);
		if(uid){
			if(type == "settings"){
				// show properties
				let item = studioStateCache.meta[uid];
				if(item){
					// make sure to set the UID, so the panel can update changes
					item.UID = uid;
					// show the item
					showItem(item, !(item.Locked || (item.Status & 1)));
				}
			}else{
				// handle actions
				let studio = studioName.getValue();
				if(studio.length){
					let hexStr =  ("00000000" + uid.toString(16)).substr(-8);
					if(type == "run"){
						fetchContent("studio/"+studio+"?cmd=startrec "+hexStr);
					}else if(type == "stop"){
						fetchContent("studio/"+studio+"?cmd=stoprec "+hexStr);
					}else if(type == "unload"){
						await fetchContent("studio/"+studio+"?cmd=closerec "+hexStr);
						refreshRecorderPanel();
					}
				}
			}
		}
	}
}

function stParseAudioList(data){
	let lines = data.split("\n");
	let devList = [];
	let lastName = "";
	let dev;
	for(let n = 1; n < (lines.length-1); n++){
		let fields = lines[n].split(":");
		let name = fields[0];
		let chan = fields[1];
		if(name && name.length && chan && chan.length){
			if(name != lastName){
				lastName = name;
				if(dev)
					devList.push(dev);
				dev = {name: name, channels: []};
			}
			if(dev)
				dev.channels.push(chan);
		}
	}
	if(dev)
		devList.push(dev);
	return devList; // array of dev objects.  dev object = {name: "theName", channels: array-of-port-names}
}

async function stParsePortList(data){
	let ourName;
	let chans = data.split("&");
	let chList = [];
	for(let n = 0; n < (chans.length); n++){
		let ports = chans[n].split("+");
		let chan = [];
		for(let i = 0; i < (ports.length); i++){
			let fields = ports[i].split(":");
			let name = fields[0];
			if(name == "[ourJackName]"){
				if(!ourName){
					let studio = studioName.getValue();
					if(studio.length){
						let resp = await fetchContent("studio/"+studio+"?cmd=info&raw=1");
						if(resp instanceof Response){
							if(resp.ok){
								let data = await resp.text();
								let lines = data.split("\n");
								for(let n = 0; n < lines.length; n++){
									let fields = lines[n].split(" = ");
									let key = fields[0];
									let value = fields[1];
									if((key == "\tJACK-Audio name"))
										ourName = value;
								}
							}
						}
					}
				}
				name = ourName;
			}
			let port = fields[1];
			if(name && name.length && port && port.length)
				chan.push({device: name, port: port});
		}
		chList.push(chan);
	}
	return chList; // array (channels) of array (connections) of objects. object = {device: "devName", port: "portName"}
}

async function stParseConnectionList(data){
	let ourName;
	let lines = data.split("\n");
	let List = [];
	for(let n = 1; n < (lines.length); n++){
		let stat = lines[n].split("\t");
		if(stat.length != 2)
			continue;
		let ports = stat[1].split(">");
		if(ports.length != 2)
			continue;
		let fields = ports[0].split(":");
		let srcDev = fields[0];
		let srcPort = fields[1];
		fields = ports[1].split(":");
		let destDev = fields[0];
		let destPort = fields[1];
		if(srcDev && srcDev.length && srcPort && srcPort.length && destDev && destDev.length && destPort && destPort.length)
			List.push({origStr: stat[1], isConn: stat[0], srcDev: srcDev, srcPort: srcPort, destDev: destDev, destPort: destPort});
	}
	// sort list
	List.sort(function(a, b){
		let result = a.srcDev.localeCompare(b.srcDev);
		if(!result){
			result = a.srcPort.localeCompare(b.srcPort);
			if(!result){
				result = a.destDev.localeCompare(b.destDev);
				if(!result)
					result = a.destPort.localeCompare(b.destPort);
			}
		}
		return result}
	);
	return List; //  array of (connections) objects. object = {isConn: "Y" or "N", srcDev: "srcDevName", srcPort: "srcPortName", destDev: "destDevName", destPort: "destPortName"}
}

async function stGetSourceList(){
	let resp;
	let studio = studioName.getValue();
	if(studio.length){
		resp = await fetchContent("studio/"+studio+"?cmd=srcports&raw=1");
		if(resp){
			if(resp.ok){
				let data = await resp.text();
				return stParseAudioList(data);
			}else{
				alert("Got an error fetching audio source list from server.\n"+resp.status);
			}
		}else{
			alert("Failed to fetch audio source list from the server.");
		}
	}
}

async function stGetDestinationList(){
	let resp;
	let studio = studioName.getValue();
	if(studio.length){
		resp = await fetchContent("studio/"+studio+"?cmd=dstports&raw=1");
		if(resp){
			if(resp.ok){
				let data = await resp.text();
				return stParseAudioList(data);
			}else{
				alert("Got an error fetching audio source list from server.\n"+resp.status);
			}
		}else{
			alert("Failed to fetch audio source list from the server.");
		}
	}
}

function stPortsAddChan(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let row = evt.target.parentNode.parentNode.parentNode;
	let table = row.parentNode.parentNode;	
	let el = table.parentNode;
	row = table.insertRow(row.rowIndex);
	let cell = row.insertCell(-1);
	cell = row.insertCell(-1);
	let devtable = document.createElement("table");
	devtable.className = "tableleftj";
	row = devtable.insertRow(-1);
	row.insertCell(-1);
	row.insertCell(-1);
	let dcell = row.insertCell(-1);
	dcell.innerHTML = `<button class="editbutton" onclick="stPortsAddPort(event)">+</button>`;
	dcell.width = '18px';
	cell.appendChild(devtable);
	// renumber channel rows
	for(let i = 1; i < (table.rows.length - 1); i++){
		row = table.rows[i];
		cell = row.firstElementChild;
		cell.innerHTML = `<span style='float: left;'>`+i+`</span><span style='float: right;'><button class="editbutton" onclick="stPortsDelChan(event)">-</button></span>`;
	}
}

function stPortsDelChan(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let row = evt.target.parentNode.parentNode.parentNode;
	let table = row.parentNode.parentNode;
	let el = table.parentNode;
	row.remove();
	// renumber channel rows
	for(let i = 1; i < (table.rows.length - 1); i++){
		row = table.rows[i];
		let cell = row.firstElementChild;
		cell.innerHTML = `<span style='float: left;'>`+i+`</span><span style='float: right;'><button class="editbutton" onclick="stPortsDelChan(event)">-</button></span>`;
	}
	stPortsControlValue(el);
}

function stPortsAddPort(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let row = evt.target.parentNode.parentNode;
	let table = row.parentNode;
	let el = table.parentNode.parentNode.parentNode.parentNode.parentNode.parentNode;
	row = table.insertRow(row.rowIndex);
	let cell = row.insertCell(-1);
	let devsel = stCreateDevSel(el.userDevList, null, stPortsDevChange);
	cell.appendChild(devsel);
	cell = row.insertCell(-1);
	cell.appendChild(stCreatePortSel(el.userDevList, devsel.value, null, stPortsPortChange));
	cell = row.insertCell(-1);
	cell.width = '18px';
	cell.innerHTML = `<button class="editbutton" onclick="stPortsDelPort(event)">-</button>`;
	stPortsControlValue(el);
}

function stPortsDelPort(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let row = evt.target.parentNode.parentNode;
	let el = row.parentNode.parentNode.parentNode.parentNode.parentNode.parentNode.parentNode;
	row.remove();
	stPortsControlValue(el);
}

function stPortsDevChange(evt){
	let devName = evt.target.value;
	let row = evt.target.parentNode.parentNode;
	let table = row.parentNode.parentNode.parentNode.parentNode.parentNode.parentNode;
	let el = table.parentNode;
	let cell = row.childNodes[1];
	cell.removeChild(cell.firstElementChild);
	cell.appendChild(stCreatePortSel(el.userDevList, devName, null, stPortsPortChange));
	stPortsControlValue(el);
}

function stPortsPortChange(evt){
	let el = evt.target.parentNode.parentNode.parentNode.parentNode.parentNode.parentNode.parentNode.parentNode.parentNode;
	stPortsControlValue(el);
}

function stConnsSrcDevChange(evt){
	let devName = evt.target.value;
	let row = evt.target.parentNode.parentNode;
	let el = row.parentNode.parentNode.parentNode;
	let cell = evt.target.parentNode;
	cell.removeChild(cell.lastChild);
	cell.appendChild(stCreatePortSel(el.userSrcDevList, devName, null, stConnsPortChange));
	stConnsControlValue(row);
}

function stConnsDestDevChange(evt){
	let devName = evt.target.value;
	let row = evt.target.parentNode.parentNode;
	let el = row.parentNode.parentNode.parentNode;
	let cell = evt.target.parentNode;
	cell.removeChild(cell.lastChild);
	cell.appendChild(stCreatePortSel(el.userDestDevList, devName, null, stConnsPortChange));
	stConnsControlValue(row);
}

function stConnsPortChange(evt){
	let row = evt.target.parentNode.parentNode;
	stConnsControlValue(row);
}

function stCreateDevSel(devList, selected, change){
	let select = document.createElement('select');
	for(let i = 0; i < devList.length; i++){
		let entry = devList[i];
		let opt = document.createElement('option');
		opt.value = entry.name;
		opt.innerHTML = entry.name;
		if(selected == entry.name)
			opt.selected = true;
		select.appendChild(opt);
	}
	select.addEventListener("change", change, false);
	return select;
}

function stCreatePortSel(devList, devName, selected, change){
	let select = document.createElement('select');
	let dev = findPropObjInArray(devList, "name", devName);
	if(dev){
		let chans = dev.channels;
		for(let i = 0; i < chans.length; i++){
			let entry = chans[i];
			let opt = document.createElement('option');
			opt.value = entry;
			opt.innerHTML = entry;
			if(selected == entry)
				opt.selected = true;
			select.appendChild(opt);
		}
	}
	select.addEventListener("change", change, false);
	return select;
}

async function stRenderPortsControl(el, devList, portList, settable, fixedChan){
	el.userDevList = devList;
	el.style.overflowY = "auto";
	let connList = [];
	if(portList){
		el.userPortList = portList;
		connList = await stParsePortList(portList);
	}
	let cols = [, ""];
	// Create a table element
	let table = document.createElement("table");
	table.className = "tableleftj";
	// Create table row tr element of a table
	let tr = table.insertRow(-1);
	// Create the table header th elements
	let theader = document.createElement("th");
	theader.className = "tselcell";
	theader.innerHTML = "Chan.";
	theader.width = "40px";
	tr.appendChild(theader);
	theader = document.createElement("th");
	theader.className = "tselcell";
	theader.innerHTML = "Device & Port";
	tr.appendChild(theader);
	// create channel rows
	let chanCnt = 0;
	if(fixedChan)
		chanCnt = fixedChan;
	else
		chanCnt = connList.length;
	// use connList to build channels
	for(let i = 0; i < chanCnt; i++){
		let trow = table.insertRow(-1);
		let cell = trow.insertCell(-1);
		if(settable && (!fixedChan))
			cell.innerHTML = `<span style='float: left;'>`+(i+1)+`</span><span style='float: right;'><button class="editbutton" onclick="stPortsDelChan(event)">-</button></span>`;
		else
			cell.innerHTML = `<span style='float: left;'>`+(i+1)+`</span>`;
		let con;
		if(connList)
			con = connList[i];
		cell = trow.insertCell(-1);
		let devtable = document.createElement("table");
		devtable.className = "tableleftj";
		devtable.style = "table-layout: auto;";
		let portCnt = 0;
		if(con)
			portCnt = con.length;
		for(let j = 0; j < portCnt; j++){
			let dtrow = devtable.insertRow(-1);
			let dcell = dtrow.insertCell(-1);
			let entry = {device: "", port: ""};
			if(con)
				entry = con[j];
			if(settable){
				let dmenu = stCreateDevSel(devList, entry.device, stPortsDevChange);
				dcell.appendChild(dmenu);
				if(!entry.device)
					entry.device = dmenu.value;
				dcell = dtrow.insertCell(-1);
				dcell.appendChild(stCreatePortSel(devList, entry.device, entry.port, stPortsPortChange));
			}else{
				dcell.innerHTML = entry.device;
				dcell = dtrow.insertCell(-1);
				dcell.innerHTML = entry.port;
			}
			dcell = dtrow.insertCell(-1);
			dcell.width = '18px';
			if(settable)
				dcell.innerHTML = `<button class="editbutton" onclick="stPortsDelPort(event)">-</button>`;
		}
		if(settable){
			let dtrow = devtable.insertRow(-1);
			dtrow.insertCell(-1);
			dtrow.insertCell(-1);
			let dcell = dtrow.insertCell(-1);
			dcell.innerHTML = `<button class="editbutton" onclick="stPortsAddPort(event)">+</button>`;
		}
		cell.appendChild(devtable);
	}
	if(settable && (!fixedChan)){
		// add new channel button row
		trow = table.insertRow(-1);
		let cell = trow.insertCell(-1);
		cell.innerHTML = `<span style='float: right;'><button class="editbutton" onclick="stPortsAddChan(event)">+</button></span>`;
		// one emply columns
		trow.insertCell(-1);
	}
	el.appendChild(table);
	if(connList.length == 0){
		stPortsControlValue(el);
	}
}

function stPortsControlValue(el){
	let ctable = el.querySelector("table:first-of-type");
	let pStr = "";
	let rows = ctable.rows;
	for(let c = 1; c < rows.length; c++){
		let row = rows[c];
		if(row){
			let ptable = row.childNodes[1].firstElementChild;
			if(ptable){
				let prows = ptable.rows;
				if(c > 1)
					pStr += "&";
				for(let p = 0; p < (prows.length); p++){
					let prow = prows[p];
					let dev = prow.childNodes[0].firstElementChild;
					let port = prow.childNodes[1].firstElementChild;
					if(dev && port){
						if(p > 0)
							pStr += "+";
						pStr += dev.value + ":" + port.value;
					}
				}
			}
		}
	}
	if(pStr.length < rows.length)
		pStr = "";
	el.userPortList = pStr;	// save the port list for which this control is based on
}

function stConnsControlValue(el){
	let srcCell = el.childNodes[1];
	let destCell = el.childNodes[2];
	let cStr = "";
	cStr += srcCell.childNodes[0].value + ":" + srcCell.childNodes[1].value;
	cStr += ">";
	cStr += destCell.childNodes[0].value + ":" + destCell.childNodes[1].value;
	el.userPortList = cStr;	// save the port list for which this row is based on
	let update = el.childNodes[3].childNodes[1].firstChild;
	if(el.userPortList != el.userOrigValue){
		// enable apply button
		update.style.display = "inline";
	}else{
		// disable apply button
		update.style.display = "none";
	}
}

async function stRenderJConControl(el, srcDevList, destDevList, connList){
	el.userSrcDevList = srcDevList;
	el.userDestDevList = destDevList;
	el.style.overflowY = "auto";
	if(connList){
		el.userConnList = connList;
		connList = await stParseConnectionList(connList);
	}else
		connList = [];
	// Create a table element
	let table = document.createElement("table");
	table.className = "tableleftj";
	// Create table row tr element of a table
	let tr = table.insertRow(-1);
	// Create the table header th elements
	let theader = document.createElement("th");
	theader.className = "tselcell";
	theader.innerHTML = "Con.";
	theader.width = "30px";
	tr.appendChild(theader);
	theader = document.createElement("th");
	theader.className = "tselcell";
	theader.innerHTML = "Source";
	tr.appendChild(theader);
	theader = document.createElement("th");
	theader.className = "tselcell";
	theader.innerHTML = "Destination";
	tr.appendChild(theader);
	theader = document.createElement("th");
	theader.className = "tselcell";
	theader.innerHTML = `<button class="editbutton" onclick="stJackConnAdd(event)">+</button>`;
	theader.width = "50px";
	tr.appendChild(theader);
	// create channel rows
	let cnt = connList.length;
	// use connList to build channels
	for(let i = 0; i < cnt; i++){
		let con;
		if(connList)
			con = connList[i];
		tr = table.insertRow(-1);
		tr.userOrigValue = con.origStr;
		let cell = tr.insertCell(-1);
		if(con.isConn && parseInt(con.isConn))
			cell.innerHTML = `Y`;
		else
			cell.innerHTML = `N`;
		cell = tr.insertCell(-1);
		let dmenu = stCreateDevSel(srcDevList, con.srcDev, stConnsSrcDevChange);
		cell.appendChild(dmenu);
		if(!con.srcDev)
			con.srcDev = dmenu.value;
		cell.appendChild(stCreatePortSel(srcDevList, con.srcDev, con.srcPort, stConnsPortChange));

		cell = tr.insertCell(-1);
		dmenu = stCreateDevSel(destDevList, con.destDev, stConnsDestDevChange);
		cell.appendChild(dmenu);
		if(!con.destDev)
			con.destDev = dmenu.value;
		cell.appendChild(stCreatePortSel(destDevList, con.destDev, con.destPort, stConnsPortChange));
		
		cell = tr.insertCell(-1);
		cell.innerHTML = `<span style='float: left;'><button class="editbutton" onclick="stJackConnRemove(event)">-</button></span><span style='float: right;'><button class="editbutton" style="display: none;" onclick="stJackConnUpdate(event)">Apply</button></span>`;
		stConnsControlValue(tr);
	}
	el.appendChild(table);
}

async function stJackConnAdd(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let tr = evt.target.parentNode.parentNode;
	let table = tr.parentNode.parentNode;
	let el = table.parentNode;
	let srcDevList = el.userSrcDevList;
	let destDevList = el.userDestDevList;
	
	tr = table.insertRow(1);
	tr.userOrigValue = "";
	let cell = tr.insertCell(-1);
	cell.innerHTML = " ";
	
	cell = tr.insertCell(-1);
	let dmenu = stCreateDevSel(srcDevList, null, stConnsSrcDevChange);
	cell.appendChild(dmenu);
	cell.appendChild(stCreatePortSel(srcDevList, dmenu.value, null, stConnsPortChange));
	
	cell = tr.insertCell(-1);
	dmenu = stCreateDevSel(destDevList, null, stConnsDestDevChange);
	cell.appendChild(dmenu);
	cell.appendChild(stCreatePortSel(destDevList, dmenu.value, null, stConnsPortChange));
	
	cell = tr.insertCell(-1);
	cell.innerHTML = `<span style='float: left;'><button class="editbutton" onclick="stJackConnRemove(event)">-</button></span><span style='float: right;'><button class="editbutton" style="display: inline;" onclick="stJackConnUpdate(event)">Apply</button></span>`;
	stConnsControlValue(tr);
}

function stTriggerConSaveTimer(){
	let studio = studioName.getValue();
	if(studio.length){
		if(stSaveConTimer)
			clearInterval(stSaveConTimer);
		stSaveConTimer = setTimeout(function(studio){
				stSaveConTimer = null;
				fetchContent("studio/"+studio+"?cmd=savejcons");
			}, 6000, studio);
	}
}

async function stJackConnRemove(evt){
	evt.preventDefault();
	evt.stopPropagation();

	let row = evt.target.parentNode.parentNode.parentNode;
	let table = row.parentNode.parentNode;
	if(row.userOrigValue && row.userOrigValue.length){
		let studio = studioName.getValue();
		if(studio.length){
			let resp = await fetchContent("studio/"+studio+"?cmd=jackdisc%20"+row.userOrigValue);
			if(resp instanceof Response){
				if(resp.ok){
					row.remove();
					stTriggerConSaveTimer();
				}else
					alert("Error deleting studio jack audio connection");
			}
		}
	}else
		row.remove();
}

async function stJackConnUpdate(evt){
	evt.preventDefault();
	evt.stopPropagation();

	let row = evt.target.parentNode.parentNode.parentNode;
	let table = row.parentNode.parentNode;
	let studio = studioName.getValue();
	if(studio.length){
		if(row.userOrigValue && row.userOrigValue.length){
			let resp = await fetchContent("studio/"+studio+"?cmd=jackdisc%20"+row.userOrigValue);
			if(resp instanceof Response){
				if(!resp.ok){
					alert("Error deleting studio jack audio connection");
					return;
				}
			}else{
				alert("Error deleting studio jack audio connection");
				return;
			}
		}
		stTriggerConSaveTimer();
		let resp = await fetchContent("studio/"+studio+"?cmd=jackconn%20"+row.userPortList);
		if(resp instanceof Response){
			if(!resp.ok){
				alert("Error creating studio jack audio connection");
				return;
			}
		}else{
			alert("Error creating studio jack audio connection");
			return;
		}
		row.userOrigValue = row.userPortList;
		stConnsControlValue(row);
	}
}

async function refreshStAdminRouts(){
	let studio = studioName.getValue();
	if(studio.length){
		let resp = await fetchContent("studio/"+studio+"?cmd=jconlist&raw=1");
		if(resp instanceof Response){
			if(resp.ok){
				let text = await resp.text();
				let el = document.getElementById("stConfJackConns");
				el.innerHTML = "";
				let srcDevList = await stGetSourceList();
				let destDevList = await stGetDestinationList();
				stRenderJConControl(el, srcDevList, destDevList, text);
			}else{
				alert("Error getting studio jack audio connection list");
				return;
			}
		}
	}
}

async function stRefreshRecTemplateList(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let resp;
	let studio = studioName.getValue();
	if(studio.length){
		resp = await fetchContent("studio/"+studio+"?cmd=rtemplates&raw=1");
		if(resp){
			if(resp.ok){
				let list = [];
				let data = await resp.text();
				let lines = data.split("\n");
				for(let n = 0; n < (lines.length-1); n++){
					let fields = lines[n].split(".");
					let name = fields[0];
					if(name && name.length && fields[1] && fields[1].length)
						list.push({Name: name, id: lines[n]});
				}
				// set custDropdownChange as list callback
				let el = document.getElementById("stRecTemplateList");
				buildSearchList(el, list, stNewRecSelection);
			}else{
				alert("Got an error fetching recorder templates from server.\n"+resp.status);
			}
		}else{
			alert("Failed to fetch recorder templates from the server.");
		}
	}
}

function stNewRecSelection(evt){
	// close search-list menu
	let el = document.getElementById("stNewRecTemplateBtn");
	toggleShowSearchList({target: el});
	let id = evt.target.getAttribute("data-id");
	let studio = studioName.getValue();
	if(studio.length)
		fetchContent("studio/"+studio+"?cmd=newrec "+id);
}

async function stConfset(key, value){
	let studio = studioName.getValue();
	if(studio.length){
		// save the pipeline property
		let resp = await fetchContent("studio/"+studio+"?cmd=set%20"+key+"%20"+value);
		if(resp instanceof Response){
			if(resp.ok){
				let text = await resp.text();
				if(text.search("OK") != 0){
					alert("Error setting "+key+" setting");
					return;
				}
			}else{
				alert("Error setting "+key+" setting");
				return;
			}
		}
	}
	// set timmer to trigger saveset command
	if(stSaveSetTimer)
		clearInterval(stSaveSetTimer);
	stSaveSetTimer = setTimeout(function(studio){
			stSaveSetTimer = null;
			fetchContent("studio/"+studio+"?cmd=saveset");
		}, 6000, studio);
}

function refreshStAdminAuto(){
	let settings = studioStateCache.meta[0];
	let el;
	let val;
	if(settings){
		if(parseInt(settings["auto_startup"])){
			el = document.getElementById("stConfAutoStart1");
			el.checked = true;
		}else{
			el = document.getElementById("stConfAutoStart0");
			el.checked = true;
		}
		val = parseInt(settings["auto_live_flags"]);
		el = document.getElementById("stConfAutoLive1");
		if(val & 0x01)
			el.checked = true;
		else
			el.checked = false;
		el = document.getElementById("stConfAutoLive2");
		if(val & 0x02)
			el.checked = true;
		else
			el.checked = false;
		el = document.getElementById("stConfAutoLive4");
		if(val & 0x04)
			el.checked = true;
		else
			el.checked = false;
		el = document.getElementById("stConfAutoLive8");
		if(val & 0x08)
			el.checked = true;
		else
			el.checked = false;
		el = document.getElementById("stConfAutoLiveTO");
		val = parseInt(settings["auto_live_timeout"]);
		val = val / 60;
		if(!val || (val < 1))
			val = 1;
		el.value = val;
		el = document.getElementById("stConfAutoFillCount");
		val = parseInt(settings["auto_thresh"]);
		if(!val)
			val = 1;
		el.value = val;
		el = document.getElementById("stConfAutoSegTime");
		val = parseInt(settings["def_segout"]);
		if(!val)
			val = 0;
		el.value = val;
		
		el = document.getElementById("stConfAutoSegLevel");
		val = parseFloat(settings["def_seglevel"]);
		if(val)
			val = Math.round(20.0 * Math.log10(val));
		else
			val = "";
		el.value = val;
	}else{
		el = document.getElementById("stConfAutoStart0");
		el.checked = true;
		el = document.getElementById("stConfAutoLive1");
		el.checked = false;
		el = document.getElementById("stConfAutoLive2");
		el.checked = false;
		el = document.getElementById("stConfAutoLive4");
		el.checked = false;
		el = document.getElementById("stConfAutoLive8");
		el.checked = false;
		el = document.getElementById("stConfAutoLiveTO");
		el.value = 30;
		el = document.getElementById("stConfAutoFillCount");
		el.value = 8;
		el = document.getElementById("stConfAutoSegTime");
		el.value = 6;
		el = document.getElementById("stConfAutoSegLevel");
		el.value = "";
	}
}

async function stConfLiveChange(evt){
	let sl = document.getElementById("stConfAutoLive");
	let els = sl.querySelectorAll('input[type=checkbox]:checked');
	if(sl){
		let value = 0;
		for(let i=0; i<els.length; i++)
			value = value + parseInt(els[i].name);
		await stConfset("auto_live_flags", value);
	}
}

async function stConfAutoChange(evt){
	if(evt.originalTarget.value)
		await stConfset("auto_startup", "1");
	else
		await stConfset("auto_startup", "0");
}

async function stConfLiveTOChange(evt){
	let el = document.getElementById("stConfAutoLiveTO");
	let toSec = parseInt(el.value) * 60;
	await stConfset("auto_live_timeout", toSec);
}

async function stConfAutoFillCountChange(evt){
	let val = parseInt(evt.originalTarget.value);
	await stConfset("auto_thresh", val);
}

async function stConfAutoSegTimeChange(evt){
	let val = parseInt(evt.originalTarget.value);
	await stConfset("def_segout", val);
}

async function stConfAutoSegLevelChange(evt){
	let el = document.getElementById("stConfAutoSegLevel");
	let val = parseFloat(el.value);
	if(val){
		if(val > 0.0)
			val = -val;
		val = Math.pow(10, (val / 20.0));	// dB to linear
	}else
		val = 0.0;
	await stConfset("def_seglevel", val);
}

function refreshStAdminMixer(){
	let settings = studioStateCache.meta[0];
	if(settings){
		let bus = parseFloat(settings["def_bus"]);
		let el = document.getElementById("stConfMixDefBus");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Default Player Bus Assignment"));
		el.appendChild(document.createElement("br"));
		for(let b=0; b<studioStateCache.buscnt; b++){
			// update busses
			if(b != 1){ // skip cue channel
				let c = document.createElement("input");
				c.id = "stConfMixDefBus"+b;
				let l = document.createElement('label');
				l.htmlFor = c.id;
				c.setAttribute("type", "checkbox");
				c.setAttribute("data-bus", b);
				switch(b){
					case 0:
						l.appendChild(document.createTextNode("Monitor"));
						break;
					case 2:
						l.appendChild(document.createTextNode("Main"));
						break;
					case 3:
						l.appendChild(document.createTextNode("Alternate"));
						break;
					default:
						l.appendChild(document.createTextNode("Bus " + b));
						break;
				}
				el.appendChild(c);
				el.appendChild(l);
				el.appendChild(document.createElement("br"));
				// update bus values
				if(bus !== false){
					if(c){
						if((1 << b) & bus)
							c.checked = true;
						else
							c.checked = false;
					}
				}
			}
		}
		
		el = document.getElementById("stConfMixRecDir");
		el.value = settings["def_record_dir"];
		
		bus = parseFloat(settings["sys_silence_bus"]);
		el = document.getElementById("stConfMixSilenceBus");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Silence Detection Monitor Bus"));
		el.appendChild(document.createElement("br"));
		for(let b=0; b<studioStateCache.buscnt; b++){
			// update busses
			if(b != 1){ // skip cue channel
				let c = document.createElement("input");
				c.id = "stConfMixSilenceBus"+b;
				let l = document.createElement('label');
				l.htmlFor = c.id;
				c.setAttribute("type", "radio");
				c.setAttribute("name", "stConfMixSilenceBus");
				c.setAttribute("data-bus", b);
				switch(b){
					case 0:
						l.appendChild(document.createTextNode("Monitor"));
						break;
					case 2:
						l.appendChild(document.createTextNode("Main"));
						break;
					case 3:
						l.appendChild(document.createTextNode("Alternate"));
						break;
					default:
						l.appendChild(document.createTextNode("Bus " + b));
						break;
				}
				el.appendChild(c);
				el.appendChild(l);
				el.appendChild(document.createElement("br"));
				// update bus values
				if(bus !== false){
					if(c){
						if(b == bus)
							c.checked = true;
						else
							c.checked = false;
					}
				}
			}
		}
		
		el = document.getElementById("stConfMixSilenceLevel");
		val = parseFloat(settings["sys_silence_thresh"]);
		if(val)
			val = Math.round(20.0 * Math.log10(val));
		else
			val = "";
		el.value = val;
		
		el = document.getElementById("stConfMixSilenceTO");
		val = parseInt(settings["sys_silence_timeout"]);
		if(!val || (val < 0))
			val = 0;
		el.value = val;
	}else{
		let el = document.getElementById("stConfMixDefBus");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Default Player Bus Assignment"));
		el.appendChild(document.createElement("br"));
		for(let b=0; b<studioStateCache.buscnt; b++){
			// update busses
			if(b != 1){ // skip cue channel
				let c = document.createElement("input");
				c.id = "stConfMixDefBus"+b;
				let l = document.createElement('label');
				l.htmlFor = c.id;
				c.setAttribute("type", "checkbox");
				c.setAttribute("data-bus", b);
				c.checked = false;	// not selected
				switch(b){
					case 0:
						l.appendChild(document.createTextNode("Monitor"));
						break;
					case 2:
						l.appendChild(document.createTextNode("Main"));
						break;
					case 3:
						l.appendChild(document.createTextNode("Alternate"));
						break;
					default:
						l.appendChild(document.createTextNode("Bus " + b));
						break;
				}
				el.appendChild(c);
				el.appendChild(l);
				el.appendChild(document.createElement("br"));
			}
		}
		
		el = document.getElementById("stConfMixRecDir");
		el.value = "";
		
		bus = parseFloat(settings["sys_silence_bus"]);
		el = document.getElementById("stConfMixSilenceBus");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Silence Detection Monitor Bus"));
		el.appendChild(document.createElement("br"));
		for(let b=0; b<studioStateCache.buscnt; b++){
			// update busses
			if(b != 1){ // skip cue channel
				let c = document.createElement("input");
				c.id = "stConfMixSilenceBus"+b;
				let l = document.createElement('label');
				l.htmlFor = c.id;
				c.setAttribute("type", "radio");
				c.setAttribute("name", "stConfMixSilenceBus");
				c.setAttribute("data-bus", b);
				switch(b){
					case 0:
						l.appendChild(document.createTextNode("Monitor"));
						break;
					case 2:
						l.appendChild(document.createTextNode("Main"));
						break;
					case 3:
						l.appendChild(document.createTextNode("Alternate"));
						break;
					default:
						l.appendChild(document.createTextNode("Bus " + b));
						break;
				}
				el.appendChild(c);
				el.appendChild(l);
				el.appendChild(document.createElement("br"));
			}
		}
		
		el = document.getElementById("stConfMixSilenceLevel");
		el.value = "";
		
		el = document.getElementById("stConfMixSilenceTO");
		el.value = 0;
	}
}

async function stConfMixDefBusChange(evt){
	let sl = document.getElementById("stConfMixDefBus");
	let els = sl.querySelectorAll('input[type=checkbox]:checked');
	if(sl){
		let value = 0;
		for(let i=0; i<els.length; i++)
			value = value + (1 << parseInt(els[i].getAttribute("data-bus")));
		await stConfset("def_bus", value);
	}
}

async function stConfMixSilenceBusChange(evt){
	let value = evt.originalTarget.getAttribute("data-bus");
	await stConfset("sys_silence_bus", value);
}

async function stConfMixRecDirChange(evt){
	let el = document.getElementById("stConfMixRecDir");
	await stConfset("def_record_dir", el.value);
}

async function stConfMixSilenceLevelChange(evt){
	let el = document.getElementById("stConfMixSilenceLevel");
	let val = parseFloat(el.value);
	if(val){
		if(val > 0.0)
			val = -val;
		val = Math.pow(10, (val / 20.0));	// dB to linear
	}else
		val = 0.0;
	await stConfset("sys_silence_thresh", val);
}

async function stConfMixSilenceTOChange(evt){
	let el = document.getElementById("stConfMixSilenceTO");
	let toSec = parseInt(el.value);
	await stConfset("sys_silence_timeout", toSec);
}

async function refreshStAdminVoIP(){
	let studio = studioName.getValue();
	if(studio)
		await syncStudioStat(studio);
	let settings = studioStateCache.meta[0];
	if(settings){
		let bus = parseFloat(settings["sip_bus"]);
		let el = document.getElementById("stConfSIPBus");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Default SIP Bus Assignment"));
		el.appendChild(document.createElement("br"));
		for(let b=0; b<studioStateCache.buscnt; b++){
			// update busses
			let c = document.createElement("input");
			c.id = "stConfSIPDefBus"+b;
			let l = document.createElement('label');
			l.htmlFor = c.id;
			c.setAttribute("type", "checkbox");
			c.setAttribute("data-bus", b);
			c.checked = false;	// not selected
			switch(b){
				case 0:
					l.appendChild(document.createTextNode("Monitor"));
					break;
				case 1:
					l.appendChild(document.createTextNode("Cue"));
					break;
				case 2:
					l.appendChild(document.createTextNode("Main"));
					break;
				case 3:
					l.appendChild(document.createTextNode("Alternate"));
					break;
				default:
					l.appendChild(document.createTextNode("Bus " + b));
					break;
			}
			el.appendChild(c);
			el.appendChild(l);
			el.appendChild(document.createElement("br"));
			// update bus values
			if(bus !== false){
				if(c){
					if((1 << b) & bus)
						c.checked = true;
					else
						c.checked = false;
				}
			}
		}
		el = document.getElementById("stConfSIPVol");
		val = parseFloat(settings["sip_vol"]);
		if(val)
			val = Math.round(20.0 * Math.log10(val));
		else
			val = 0.0;
		el.value = val;
		
		el = document.getElementById("stConfSIPFeedVol");
		val = parseFloat(settings["sip_feed_vol"]);
		if(val)
			val = Math.round(20.0 * Math.log10(val));
		else
			val = 0.0;
		el.value = val;
		
		el = document.getElementById("stConfSIPCtl");
		val = parseInt(settings["sip_ctl_port"]);
		if(!val || (val < 0))
			val = 0;
		el.value = val;
		
		bus = parseInt(settings["sip_feed_bus"]) & 0x1f;
		el = document.getElementById("stConfSIPFeedBus");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Feed Bus"));
		el.appendChild(document.createElement("br"));
		for(let b=0; b<studioStateCache.buscnt+1; b++){
			// update busses
			let c = document.createElement("input");
			c.id = "SIPFeedBus"+b;
			l = document.createElement('label');
			l.htmlFor = c.id;
			c.setAttribute("type", "radio");
			c.setAttribute("value", b);
			c.setAttribute("name", "SIPFeedBus");
			switch(b){
				case 0:
					l.appendChild(document.createTextNode("None"));
					break;
				case 1:
					l.appendChild(document.createTextNode("Monitor"));
					break;
				case 2:
					l.appendChild(document.createTextNode("Cue"));
					break;
				case 3:
					l.appendChild(document.createTextNode("Main"));
					break;
				case 4:
					l.appendChild(document.createTextNode("Alternate"));
					break;
				default:
					l.appendChild(document.createTextNode("Bus " + b));
					break;
			}
			el.appendChild(c);
			el.appendChild(l);
			el.appendChild(document.createElement("br"));
			// update bus values
			if(bus !== false){
				if(c){
					if(b == bus)
						c.checked = true;
					else
						c.checked = false;
				}
			}
		}
		bus = settings["sip_feed_bus"];
		if(!bus)
			bus = 0;
		bus = BigInt(bus);
		el = document.getElementById("stConfSIPFeedTB");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Feed Cue on"));
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "SIPFeedBusb24";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Cue"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1<<24);
		if((1n << 24n) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(c);
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "SIP_FeedBusb29";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 1"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1<<29);
		if((1n << 29n) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(c);
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "SIPFeedBusb30";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 2"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1<<30);
		if((1n << 30n) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(c);
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "SIPFeedBusb31";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 3"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1n<<31n);
		if((1n << 31n) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(c);
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
	}else{
		let el = document.getElementById("stConfSIPBus");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Default SIP Bus Assignment"));
		el.appendChild(document.createElement("br"));
		for(let b=0; b<studioStateCache.buscnt; b++){
			// update busses
			let c = document.createElement("input");
			c.id = "stConfSIPDefBus"+b;
			let l = document.createElement('label');
			l.htmlFor = c.id;
			c.setAttribute("type", "checkbox");
			c.setAttribute("data-bus", b);
			c.checked = false;	// not selected
			switch(b){
				case 0:
					l.appendChild(document.createTextNode("Monitor"));
					break;
				case 1:
					l.appendChild(document.createTextNode("Cue"));
					break;
				case 2:
					l.appendChild(document.createTextNode("Main"));
					break;
				case 3:
					l.appendChild(document.createTextNode("Alternate"));
					break;
				default:
					l.appendChild(document.createTextNode("Bus " + b));
					break;
			}
			el.appendChild(c);
			el.appendChild(l);
			el.appendChild(document.createElement("br"));
		}
		el = document.getElementById("stConfSIPVol");
		el.value = 0.0;
		
		el = document.getElementById("stConfSIPFeedVol");
		el.value = 0.0;
		
		el = document.getElementById("stConfSIPCtl");
		el.value = 0;
		
		el = document.getElementById("stConfSIPFeedBus");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Feed Bus"));
		el.appendChild(document.createElement("br"));
		for(let b=0; b<studioStateCache.buscnt+1; b++){
			// update busses
			let c = document.createElement("input");
			c.id = "SIPFeedBus"+b;
			l = document.createElement('label');
			l.htmlFor = c.id;
			c.setAttribute("type", "radio");
			c.setAttribute("value", b);
			c.setAttribute("name", "SIPFeedBus");
			switch(b){
				case 0:
					l.appendChild(document.createTextNode("None"));
					c.checked = true;
					break;
				case 1:
					l.appendChild(document.createTextNode("Monitor"));
					break;
				case 2:
					l.appendChild(document.createTextNode("Cue"));
					break;
				case 3:
					l.appendChild(document.createTextNode("Main"));
					break;
				case 4:
					l.appendChild(document.createTextNode("Alternate"));
					break;
				default:
					l.appendChild(document.createTextNode("Bus " + b));
					break;
			}
			el.appendChild(c);
			el.appendChild(l);
			el.appendChild(document.createElement("br"));
		}
		el = document.getElementById("stConfSIPFeedTB");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Feed Cue on"));
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "SIPFeedBusb24";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Cue"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1<<24);
		el.appendChild(c);
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "SIP_FeedBusb29";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 1"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1<<29);
		el.appendChild(c);
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "SIPFeedBusb30";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 2"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1<<30);
		el.appendChild(c);
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "SIPFeedBusb31";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 3"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1n<<31n);
		el.appendChild(c);
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
	}
}

async function stConfSIPBusChange(evt){
 	let sl = document.getElementById("stConfSIPBus");
	let els = sl.querySelectorAll('input[type=checkbox]:checked');
	if(sl){
		let value = 0;
		for(let i=0; i<els.length; i++)
			value = value + (1 << parseInt(els[i].getAttribute("data-bus")));
		await stConfset("sip_bus", value);
	}
}

async function stConfSIPFeedChange(evt){
	let bus = document.querySelector('input[name="SIPFeedBus"]:checked').value;
	bus = BigInt(parseInt(bus));
	let el = document.getElementById("stConfSIPFeedTB");
	let buses = el.querySelectorAll("input[type='checkbox']:checked");
	for(let i = 0; i < buses.length; i++)
		bus += BigInt(parseInt(buses[i].getAttribute("data-idx")));
	await stConfset("sip_feed_bus", bus);
}

async function stConfSIPCtlChange(evt){
	let el = document.getElementById("stConfSIPCtl");
	let value = parseInt(el.value);
	await stConfset("sip_ctl_port", value);
	await new Promise(resolve => setTimeout(resolve, 3000)); // 3 sec
	let studio = studioName.getValue();
	if(studio)
		syncStudioStat(studio);
}

async function stConfSIPVolChange(evt){
	let el = document.getElementById("stConfSIPVol");
	let val = parseFloat(el.value);
	if(val){
		val = Math.pow(10, (val / 20.0));	// dB to linear
	}else
		val = 0.0;
	await stConfset("sip_vol", val);
}

async function stConfSIPFeedVolChange(evt){
	let el = document.getElementById("stConfSIPFeedVol");
	let val = parseFloat(el.value);
	if(val){
		val = Math.pow(10, (val / 20.0));	// dB to linear
	}else
		val = 0.0;
	await stConfset("sip_feed_vol", val);
}

function refreshStAdminLib(){
	let settings = studioStateCache.meta[0];
	let el;
	if(settings){
		el = document.getElementById("stConfDbType");
		el.innerText = settings["db_type"];
		el = document.getElementById("stConfDbName");
		el.innerText = settings["db_name"];
		el = document.getElementById("stConfDbHost");
		el.value = settings["db_server"];
		el = document.getElementById("stConfDbPort");
		el.value = settings["db_port"];
		el = document.getElementById("stConfDbMark");
		if(parseInt(settings["db_mark_missing"]))
			el.checked = true;
		else
			el.checked = false;
		el = document.getElementById("stConfDbLoc");
		el.value = parseInt(settings["db_loc"]);
	}else{
		el = document.getElementById("stConfDbType");
		el.innerText = "Not Set";
		el = document.getElementById("stConfDbName");
		el.innerText = "Not Set";
		el = document.getElementById("stConfDbHost");
		el.value = "Not Set";
		el = document.getElementById("stConfDbPort");
		el.value = "";
		el = document.getElementById("stConfDbMark");
		el.checked = false;
		el = document.getElementById("stConfDbLoc");
		el.value = 0;
	}
}

async function stConfDbCopy(event){
	// get library configuration settings
	let obj = {};
	let resp = await fetchContent("getconf/library");
	if(resp){
		if(resp.ok){
			elements = await resp.json();
			for(let i = 0 ; i < elements.length ; i++){
				let item = elements[i];
				obj[item.id] = item.value;
			}
		}else{
			alert("Got an error fetching library settings from server.\n"+resp.statusText);
			return;
		}
	}else{
		alert("Failed to fetch library settings from the server.");
		return;
	}
	
	// save to arServer settings
	let el = document.getElementById("stConfDbLoc");
	await stConfset("db_loc", el.value);
	el = document.getElementById("stConfDbName");
	el.innerText = obj["database"];
	await stConfset("db_name", obj["database"]);
	await stConfset("db_prefix", obj["prefix"]);
	await stConfset("db_pw", obj["password"]);
	el = document.getElementById("stConfDbHost");
	el.value = obj["host"];
	await stConfset("db_server", obj["host"]);
	el = document.getElementById("stConfDbType");
	el.innerText = obj["type"];
	await stConfset("db_type", obj["type"]);
	await stConfset("db_user", obj["user"]);
	el = document.getElementById("stConfDbPort");
	if(obj["port"])
		el.innerText = obj["port"];
	else
		el.innerText = "";
	await stConfset("db_port", obj["port"]);
}

async function stConfDbApplyHost(event){
	let el = document.getElementById("stConfDbHost");
	let value = el.value;
	await stConfset("db_port", value);
}

async function stConfDbApplyPort(event){
	let el = document.getElementById("stConfDbPort");
	await stConfset("db_server", el.value);
}

async function stConfDbLocChange(event){
	let el = document.getElementById("stConfDbLoc");
	await stConfset("db_loc", el.value);
}

async function stConfDbMarkChange(event){
	let el = document.getElementById("stConfDbMark");
	let value = 0;
	if(el.checked)
		value = 1;
	await stConfset("db_mark_missing", value);
}

async function getInputGroupsMM(studio, name){
	let resp = await fetchContent("studio/"+studio+"?cmd=getmm%20"+name+"&raw=1");
	if(resp){
		if(resp.ok){
			let data = await resp.text();
			let lines = data.split("\n");
			for(let n = 1; n < lines.length; n++){
				let fields = lines[n].split("\t");
				let bus = fields[0];
				let vol = fields[1];
				let ports = fields[2];
				return {mmvol: vol, mmbus: bus, mmports: ports};
			}
		}
	}
	return false;
}

async function refreshInputGroups(){
	let studio = studioName.getValue();
	let el = document.getElementById("stInGrpList");
	if(studio && studio.length){
		let resp = await fetchContent("studio/"+studio+"?cmd=dumpin&raw=1");
		if(resp){
			if(resp.ok){
				let list = [];
				let data = await resp.text();
				let lines = data.split("\n");
				for(let n = 1; n < lines.length; n++){
					let fields = lines[n].split("\t");
					let name = fields[0];
					let bus = fields[1];
					let ctl = fields[2];
					let ports = fields[3];
					if(name.length){
						let mm = await getInputGroupsMM(studio, name);
						if(mm){
							list.push({url: "input:///"+name, Name: name, Bus: bus, Controls: ctl, Ports: ports, ...mm});
							continue;
						}
						list.push({url: "input:///"+name, Name: name, Bus: bus, Controls: ctl, Ports: ports});
					}
				}
				studioStateCache.live = list;
				// update server settings panel here, of any
				refreshStAdminInputs(list);
				// update the studio live source tab
				let format = `$Name$`;
				genDragableListFromObjectArray(true, false, list, el, format, "url");
			}else{
				alert("Got an error fetching input group list from server.\n"+resp.statusText);
			}
		}else{
			alert("Failed to fetch input group list  from the server.");
		}
	}
}

function refreshStAdminInputs(list){
	let el = document.getElementById("stInConfList");
	let colMap = {url: false, Name: "Name", Bus: false, Controls: false, Ports: false, mmports: false, mmvol: false, mmbus: false};
	genPopulateTableFromArray(list, el, colMap, selectStAdminInputItem);
	
	el = document.getElementById("stConfInSettings");
	el.style.display = "none";
}

function stConfInNew(evt){
	// unselect all in live input list
	let par = document.getElementById("stInConfList");
	let els = par.getElementsByClassName("tselrow");
	for(let i = 0; i < els.length; i++){
		els[i].className = els[i].className.replace(" active", "");
	}
	// load new in settings
	selectStAdminInputItem();
}

async function stConfInDelete(evt){
	let nel = document.getElementById("stConfInName");
	let oldname = nel.getAttribute("data-idx");
	if(oldname > -1)
		oldname = studioStateCache.live[oldname].Name;
	else
		oldname = "";
	if(oldname){
		let studio = studioName.getValue();
		if(studio.length){
			let resp = await fetchContent("studio/"+studio+"?cmd=delin "+oldname);
			if(resp && resp.ok){
				refreshInputGroups();
				await fetchContent("studio/"+studio+"?cmd=savein");
			}
		}
	}
}

async function stConfInSave(evt){
	let el = document.getElementById("stConfInMain");
	let buses = el.querySelectorAll("input[type='checkbox']:checked");
	let bus = BigInt(0);
	for(let i = 0; i < buses.length; i++)
		bus += (1n << BigInt(parseInt(buses[i].getAttribute("data-bus"))));
	bus =  ("00000000" + bus.toString(16)).substr(-8);
	let pel = document.getElementById("stConfInPorts");
	stPortsControlValue(pel);
	let ports = pel.userPortList;
	let nel = document.getElementById("stConfInName");
	let newname = nel.value;
	newname = newname.replace(/ /g,"_");
	let oldname = nel.getAttribute("data-idx");
	if(oldname > -1)
		oldname = studioStateCache.live[oldname].Name;
	else
		oldname = "";
	let studio = studioName.getValue();
	if(studio.length){
		let obj = {cmd: "setin "+newname+" "+bus+" 00000011 "+ports, raw: 1};
		let resp = await fetchContent("studio/"+studio, {
				method: 'POST',
				body: JSON.stringify(obj),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp && resp.ok){
			if(oldname && (newname !== oldname))
				// rename: delete old
				resp = await fetchContent("studio/"+studio+"?cmd=delin "+oldname);
		}
		if(resp && resp.ok){
			// feed settings
			pel = document.getElementById("stConfInFeedPorts");
			stPortsControlValue(pel);
			ports = pel.userPortList;
			el = document.getElementById("stConfInFeedBus");
			bus = document.querySelector('input[name="inFeedBus"]:checked').value;
			bus = BigInt(parseInt(bus));
			el = document.getElementById("stConfInFeedTB");
			buses = el.querySelectorAll("input[type='checkbox']:checked");
			for(let i = 0; i < buses.length; i++)
				bus += BigInt(parseInt(buses[i].getAttribute("data-idx")));
			el = document.getElementById("stConfInFeedLevel");
			let val = parseFloat(el.value);
			if(val){
				val = Math.pow(10, (val / 20.0));	// dB to linear
			}else
				val = 1.0;
			obj = {cmd: "setmm "+newname+" "+bus+" "+val+" "+ports, raw: 1};
			resp = await fetchContent("studio/"+studio, {
					method: 'POST',
					body: JSON.stringify(obj),
					headers: {
						"Content-Type": "application/json",
						"Accept": "application/json"
					}
				});
		}
		if(resp && resp.ok){
			refreshInputGroups();
			await fetchContent("studio/"+studio+"?cmd=savein");
			if(resp && resp.ok)
				refreshInputGroups();
		}
	}
}

async function selectStAdminInputItem(evt){
	let entry;
	let i = -1;
	if(evt){
		let par = evt.target.parentElement.parentElement.parentElement;
		i = evt.target.parentElement.rowIndex - 1;
		entry = studioStateCache.live[i];
		// Get all elements with class="tselcell" and remove the class "active" from btype div
		let els = par.getElementsByClassName("tselrow");
		for(let i = 0; i < els.length; i++){
			els[i].className = els[i].className.replace(" active", "");
		}
		// and activate the selected element
		els[i].className += " active";
	}else{
		// create new entry
		entry = {Name: "NewLiveInput", Bus: "00000d", Controls: 0, Ports:"", mmports: "", mmvol: 1.0, mmbus: 0};
	}
	let el = document.getElementById("stConfInSettings");
	// fill in settings for selected
	if(entry){
		el.style.display = "block";
		el = document.getElementById("stConfInName");
		el.value = entry.Name;
		el.setAttribute("data-idx", i);
		el = document.getElementById("stConfInPorts");
		let devList = await stGetSourceList();
		el.innerHTML = "";
		stRenderPortsControl(el, devList, entry.Ports, true, studioStateCache.chancnt);

		let bus = parseInt(entry.Bus, 16);
		el = document.getElementById("stConfInBus");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Bus Assignment"));
		el.appendChild(document.createElement("br"));
		for(let b=0; b<studioStateCache.buscnt; b++){
			// update busses
			if(b != 1){ // skip cue channel
				let c = document.createElement("input");
				c.id = "LiveInBus"+b;
				let l = document.createElement('label');
				l.htmlFor = c.id;
				c.setAttribute("type", "checkbox");
				c.setAttribute("data-bus", b);
				switch(b){
					case 0:
						l.appendChild(document.createTextNode("Monitor"));
						break;
					case 2:
						l.appendChild(document.createTextNode("Main"));
						break;
					case 3:
						l.appendChild(document.createTextNode("Alternate"));
						break;
					default:
						l.appendChild(document.createTextNode("Bus " + b));
						break;
				}
				el.appendChild(c);
				el.appendChild(l);
				el.appendChild(document.createElement("br"));
				// update bus values
				if(bus !== false){
					if(c){
						if((1 << b) & bus)
							c.checked = true;
						else
							c.checked = false;
					}
				}
			}
		}
		
		el = document.getElementById("stConfInTB");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Talkback Assignment"));
		el.appendChild(document.createElement("br"));
		c = document.createElement("input");
		c.id = "LiveInBus"+29;
		let l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 1"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-bus", 29);
		el.appendChild(c);
		if((1<<29) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "LiveInBus"+30;
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 2"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-bus", 30);
		el.appendChild(c);
		if((1<<30) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "LiveInBus"+31;
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 3"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-bus", 31);
		el.appendChild(c);
		if((1<<31) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		el = document.getElementById("stConfInMute");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Mute Group"));
		el.appendChild(document.createElement("br"));
		c = document.createElement("input");
		c.id = "LiveInBus"+25;
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Mute A"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-bus", 25);
		el.appendChild(c);
		if((1<<25) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "LiveInBus"+26;
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Mute B"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-bus", 26);
		el.appendChild(c);
		if((1<<26) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "LiveInBus"+27;
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Mute C"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-bus", 27);
		el.appendChild(c);
		if((1<<27) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		el = document.getElementById("stConfInFeedPorts");
		devList = await stGetDestinationList();
		el.innerHTML = "";
		stRenderPortsControl(el, devList, entry.mmports, true, studioStateCache.chancnt);
		
		bus = parseInt(entry.mmbus) & 0x1f;
		el = document.getElementById("stConfInFeedBus");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Feed Bus"));
		el.appendChild(document.createElement("br"));
		for(let b=0; b<studioStateCache.buscnt+1; b++){
			// update busses
			let c = document.createElement("input");
			c.id = "InFeedBus"+b;
			l = document.createElement('label');
			l.htmlFor = c.id;
			c.setAttribute("type", "radio");
			c.setAttribute("value", b);
			c.setAttribute("name", "inFeedBus");
			switch(b){
				case 0:
					l.appendChild(document.createTextNode("None"));
					break;
				case 1:
					l.appendChild(document.createTextNode("Monitor"));
					break;
				case 2:
					l.appendChild(document.createTextNode("Cue"));
					break;
				case 3:
					l.appendChild(document.createTextNode("Main"));
					break;
				case 4:
					l.appendChild(document.createTextNode("Alternate"));
					break;
				default:
					l.appendChild(document.createTextNode("Bus " + b));
					break;
			}
			el.appendChild(c);
			el.appendChild(l);
			el.appendChild(document.createElement("br"));
			// update bus values
			if(bus !== false){
				if(c){
					if(b == bus)
						c.checked = true;
					else
						c.checked = false;
				}
			}
		}
		
		bus = entry.mmbus;
		if(!bus)
			bus = 0;
		bus = BigInt(bus);
		el = document.getElementById("stConfInFeedTB");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Feed Cue on"));
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "InFeedBusb24";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Cue"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1<<24);
		if((1n << 24n) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(c);
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "InFeedBusb29";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 1"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1<<29);
		if((1n << 29n) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(c);
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "InFeedBusb30";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 2"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1<<30);
		if((1n << 30n) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(c);
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "InFeedBusb31";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 3"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1n<<31n);
		if((1n << 31n) & bus)
			c.checked = true;
		else
			c.checked = false;
		el.appendChild(c);
		el.appendChild(l);
		el.appendChild(document.createElement("br"));
		
		el = document.getElementById("stConfInFeedLevel");
		val = parseFloat(entry.mmvol);
		if(val)
			val = Math.round(20.0 * Math.log10(val));
		else
			val = "";
		el.value = val;
	}
}

function stOutVolAction(evt){
	val = parseFloat(evt.target.value);
	let parent = evt.target.parentNode;
	let output = parent.previousElementSibling.innerText;
	let studio = studioName.getValue();
	if(studio.length){
		// make val scalar
		val = faderToLin(val);
		fetchContent("studio/"+studio+"?cmd=outvol "+output+" "+val);
	}
}

function updateOutVolUI(val, idx){
	let vol = parseFloat(val);
	let el = document.getElementById("stOutIdx"+(idx+1));
	if(el){
		let div = el.children[1];
		let fader = div.children[0];
		let db = div.children[1];
		db.innerText = linToDBtext(vol);
		if(fader.touching)
			return;	// dont update slider while it is being touched
		fader.value = linToFader(vol);
	}
}

function stOutBusAction(evt){
	let bus = evt.target.selectedIndex;
	let parent = evt.target.parentNode.parentNode;
	let output = parent.firstElementChild.innerText;
	let studio = studioName.getValue();
	if(studio.length){
		// make val scalar
		val = faderToLin(val);
		fetchContent("studio/"+studio+"?cmd=outbus "+output+" "+bus);
	}
}

function updateOutBusUI(bus, idx){
	bus = parseFloat(bus);
	let el = document.getElementById("stOutIdx"+(idx+1));
	if(el){
		let div = el.children[2];
		let sel = div.firstElementChild;
		sel.selectedIndex = bus;
	}
}

function genOutBusMenuHTML(bus){
	let html = `<select onchange="stOutBusAction(event)">`;
	for(let b=0; b<studioStateCache.buscnt; b++){
		let name;
		switch(b){
			case 0:
				name = "Mon";
				break;
			case 1:
				name = "Cue";
				break;
			case 2:
				name = "Main";
				break;
			case 3:
				name = "Alt";
				break;
			default:
				name = "Bus " + b;
				break;
		}
		if(bus == b)
			html += "<option value='"+name+"' selected>"+name+"</option>";
		else
			html += "<option value='"+name+"'>"+name+"</option>";
	}
	html += `</select>`;
	return html;
}
 
async function refreshStudioDelays(outlist, index, delay){
	let el = document.getElementById("stDelayList");
	if(outlist){
		let studio = studioName.getValue();
		if(studio && studio.length){
			studioStateCache.outs
			let resp = await fetchContent("studio/"+studio+"?cmd=getdly&raw=1");
			if(resp){
				if(resp.ok){
					let list = [];
					let data = await resp.text();
					let lines = data.split("\n");
					for(let n = 1; n < lines.length; n++){
						let fields = lines[n].split("\t");
						if(fields[0].length){
							let entry = findPropObjInArray(studioStateCache.outs,"Name", fields[0]);
							if(entry){
								entry.Delay = parseFloat(fields[2]);
								if(entry.Delay)
									entry.delaySel = 1;
								else
									entry.delaySel = 0;
							}
						}
					}
				}
			}
		}
		// update outlist to include any existing el check box states or non-zero delays
		let table = el.getElementsByTagName("table");
		if(table.length){
			table = table[0]; // should be only one table in el.console.log(table);
			let rows = table.firstChild.firstChild;
			for(let i = 1; i < rows.length; i++){
				let cols = rows[i].childNodes;
				let name = cols[0].innerText;
				let entry = findPropObjInArray(studioStateCache.outs,"Name", name);
				if(entry){
					if(cols[1].firstChild.checked || parseFloat(cols[2].innerText))
						entry.delaySel = 1;
					else
						entry.delaySel = 1;
				}
			} 
		}
		// populate new table
		let colMap = {url: false, Name: "Name", delaySel: "Enable", Bus: false, Mutes: false, Volume: false, Ports: false, ShowUI: false, Idx: false};
		let fields = {delaySel: "<input type='checkbox' onchange='stDelayCheckAction(event)' $ifvalchk></input>"};
		genPopulateTableFromArray(outlist, el, colMap, false, false, false, false, false, fields, false);
	}else{
		let entry = findPropObjInArray(studioStateCache.outs,"Idx", index+1);
		if(entry){
			entry.Delay = parseFloat(delay);
			if(delay)
				entry.delaySel = 1;
			else
				entry.delaySel = 0;
			// update row in table
			let table = el.getElementsByTagName("table");
			if(table.length){
				table = table[0]; // should be only one table in el.console.log(table);
				let rows = table.firstChild.childNodes;
				for(let i = 1; i < rows.length; i++){
					let cols = rows[i].childNodes;
					let name = cols[0].innerText;
					if(name == entry.Name){
						if(delay){
							cols[1].firstChild.checked = true;
							cols[2].innerText = delay;
						}else{
							cols[1].firstChild.checked = false;
							cols[2].innerText = 0;
						}
						break;
					}
				} 
			}
		}
	}
	// enable/disaqble button based on any delay set
	el = document.getElementById("stDumpBtn");
	let i;
	for(i = 0; i < studioStateCache.outs.length; i++){
		if(studioStateCache.outs[i].Delay){
			el.disabled = false;
			break;
		}
	}
	if(i >= studioStateCache.outs.length)
		el.disabled = true;
}

async function stDumpAction(evt){
	let studio = studioName.getValue();
	if(studio.length)
		await fetchContent("studio/"+studio+"?cmd=dump");
}

async function stDelayCheckAction(evt){
	let target = evt.target;
	let name = target.parentNode.parentNode.firstChild.innerText;
	let delay = 0.0;
	if(target.checked)
		delay = document.getElementById("stDelaySec").value;
	let studio = studioName.getValue();
	if(studio.length){
		let resp = await fetchContent("studio/"+studio+"?cmd=setdly "+name+" "+delay);
		if(!resp || !resp.ok){
			// switch back if failed
			if(target.checked)
				target.checked = false;
			else
				target.checked = false;
		}
		return;
	}
	// enable/disaqble button based on any delay set
	let el = document.getElementById("stDumpBtn");
	let i;
	for(i = 0; i < studioStateCache.outs.length; i++){
		if(studioStateCache.outs[i].Delay){
			el.disabled = false;
			break;
		}
	}
	if(i >= studioStateCache.outs.length)
		el.disabled = true;
}

async function refreshOutGroups(){
	let studio = studioName.getValue();
	let el = document.getElementById("studioOutList");
	if(studio && studio.length){
		let resp = await fetchContent("studio/"+studio+"?cmd=dumpout&raw=1");
		if(resp){
			if(resp.ok){
				let list = [];
				let show = [];
				let data = await resp.text();
				let lines = data.split("\n");
				let cnt = 0;
				for(let n = 1; n < lines.length; n++){
					let fields = lines[n].split("\t");
					if(fields[0].length){
						cnt++;
						let entry = {Idx: cnt, Name: fields[0], Volume: fields[1], Mutes: fields[2], Bus: fields[3], ShowUI: fields[4], Ports: fields[5]};
						list.push(entry);
						if(entry.ShowUI && entry.ShowUI.length && parseInt(entry.ShowUI))
							show.push(entry);
					}
				}
				while(list.length < studioStateCache.outcnt){
					cnt++;
					list.push({url: false, Idx: cnt, Name: false, Bus: false, Mutes: false, Volume: false, ShowUI: false, Ports: false, Delay:false});
				}
				studioStateCache.outs = list;
				// update server settings panel here, of any
				refreshStAdminOuts(list);
				// update the studio output controls
				let format = `<div id="stOutIdx$Idx$" style="display: flex; flex-flow: row; align-items: center;">
					<div style="width:75px;">$Name$</div>
					<div style="display: flex; flex-flow: row; align-items: center;">
						<input type='range' min='0.0' max='1.0' value="$Volume->linToFader$" step='0.01' oninput='stOutVolAction(event)'
						ontouchstart='this.touching = true;' onmousedown='this.touching = true;' 
						onmouseup='this.touching = false;' ontouchend='this.touching = false;' style="width:100px;"></input>
						<div>$Volume->linToDBtext$</div>
					</div>
					<div style="width:75px; text-align: center;">
						$Bus->genOutBusMenuHTML$
					<div></div>`;
				genDragableListFromObjectArray(false, false, show, el, format);
				// update the studio delay controls
				refreshStudioDelays(list);
			}else{
				alert("Got an error fetching output group list from server.\n"+resp.statusText);
			}
		}else{
			alert("Failed to fetch output group list  from the server.");
		}
	}
}
function stAdminOutNameFormat(val){
	if(val)
		return val;
	else
		return "<button onclick='stConfOutNew(event)'>New</button>"
}

function refreshStAdminOuts(list){
	let el = document.getElementById("stOutConfList");
	let colMap = {url: false, Idx: " ", Name: "Name", Bus: false, Mutes: false, Volume: false, ShowUI: false, Ports: false, Delay: false};
	let fields = {Name: stAdminOutNameFormat};
	genPopulateTableFromArray(list, el, colMap, selectStAdminOutItem, false, false, false, false, fields, false);
	
	el = document.getElementById("stConfOutSettings");
	el.style.display = "none";
}

function stConfOutNew(evt){
	// unselect any currently selected outputs and select this one
	let par = document.getElementById("stOutConfList");
	let els = par.getElementsByClassName("tselrow");
	let idx = evt.target.parentNode.parentNode.rowIndex - 1;
	for(let i = 0; i < els.length; i++){
		if(idx == i)
			els[i].className += " active";
		else
			els[i].className = els[i].className.replace(" active", "");
	}
}

async function stConfOutDelete(evt){
	let nel = document.getElementById("stConfOutName");
	let oldname = nel.getAttribute("data-idx");
	if(oldname > -1)
		oldname = studioStateCache.outs[oldname].Name;
	else
		oldname = "";
	if(oldname){
		let studio = studioName.getValue();
		if(studio.length){
			let resp = await fetchContent("studio/"+studio+"?cmd=delout "+oldname);
			if(resp && resp.ok){
				refreshInputGroups();
				await fetchContent("studio/"+studio+"?cmd=saveout");
			}
		}
	}
}

async function stConfOutSave(evt){
	// note: current volume settings will be saved too!
	let el = evt.target.parentElement.parentElement;
	let ctlel = el.querySelector("input[type='radio']:checked");
	let bus = parseInt(ctlel.getAttribute("data-bus"));
	let showui = 0;
	ctlel = document.getElementById("stConfOutVolUI");
	if(ctlel.checked)
		showui = 1;
		
	let pel = document.getElementById("stConfOutPorts");
	stPortsControlValue(pel);
	let ports = pel.userPortList;
	
	el = document.getElementById("OutMuteC");
	let mute = BigInt(parseInt(el.value));

	el = document.getElementById("OutMuteB");
	mute = (256n * mute) + BigInt(parseInt(el.value));
	
	el = document.getElementById("OutMuteA");
	mute = (256n * mute) + BigInt(parseInt(el.value));
	
	el = document.getElementById("OutMuteCue");
	mute = (256n * mute) + BigInt(parseInt(el.value));
	
	mutgain = ("00000000" + mute.toString(16)).substr(-8);

	let nel = document.getElementById("stConfOutName");
	let newname = nel.value;
	newname = newname.replace(/ /g,"_");
	let oldname = nel.getAttribute("data-idx");
	if(oldname > -1)
		oldname = studioStateCache.outs[oldname].Name;
	else
		oldname = "";
	let studio = studioName.getValue();

	if(studio.length){
		let obj = {cmd: "setout "+newname+" "+mutgain+" "+bus+" "+showui+" "+ports, raw: 1};
		let resp = await fetchContent("studio/"+studio, {
				method: 'POST',
				body: JSON.stringify(obj),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp && resp.ok){
			if(oldname && (newname !== oldname))
				// rename: delete old
				resp = await fetchContent("studio/"+studio+"?cmd=delout "+oldname);
		}
		if(resp && resp.ok){
			refreshOutGroups();
			await fetchContent("studio/"+studio+"?cmd=saveout");
		}
	}
}

function stConfOutMuteChange(evt){
	let sib = evt.target.nextSibling;
	sib.innerText = linToDBtext(evt.target.value / 255);
}

async function selectStAdminOutItem(evt){
	let entry;
	let i = -1;
	if(evt.target.nodeName == "TD"){
		let par = evt.target.parentElement.parentElement.parentElement;
		i = evt.target.parentElement.rowIndex - 1;
		entry = studioStateCache.outs[i];
		// Get all elements with class="tselcell" and remove the class "active" from btype div
		let els = par.getElementsByClassName("tselrow");
		for(let i = 0; i < els.length; i++){
			els[i].className = els[i].className.replace(" active", "");
		}
		// and activate the selected element
		els[i].className += " active";
	}else{
		// create new entry
		entry = {Name: "NewOutput", Bus: 0, Mutes: "FFFFFFFF", Ports:""};
	}
	let el = document.getElementById("stConfOutSettings");
	// fill in settings for selected
	if(entry){
		el.style.display = "block";
		el = document.getElementById("stConfOutName");
		el.value = entry.Name;
		el.setAttribute("data-idx", i);
		
		el = document.getElementById("stConfOutVolUI");
		if(parseInt(entry.ShowUI))
			el.checked = true;
		else
			el.checked = false;
		el = document.getElementById("stConfOutPorts");
		let devList = await stGetDestinationList();
		el.innerHTML = "";
		stRenderPortsControl(el, devList, entry.Ports, true, studioStateCache.chancnt);

		let bus = parseInt(entry.Bus, 16);
		el = document.getElementById("stConfOutBus");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Bus Assignment"));
		el.appendChild(document.createElement("br"));
		let fs = document.createElement("fieldset");
		el.appendChild(fs);
		for(let b=0; b<studioStateCache.buscnt; b++){
			// update busses
			let c = document.createElement("input");
			c.id = "OutBusGrp"+b;
			let l = document.createElement('label');
			l.htmlFor = c.id;
			c.setAttribute("type", "radio");
			c.setAttribute("name", "stConfOutBus");
			c.setAttribute("data-bus", b);
			switch(b){
				case 0:
					l.appendChild(document.createTextNode("Monitor"));
					break;
				case 1:
					l.appendChild(document.createTextNode("Cue"));
					break;
				case 2:
					l.appendChild(document.createTextNode("Main"));
					break;
				case 3:
					l.appendChild(document.createTextNode("Alternate"));
					break;
				default:
					l.appendChild(document.createTextNode("Bus " + b));
					break;
			}
			fs.appendChild(c);
			fs.appendChild(l);
			fs.appendChild(document.createElement("br"));
			// update bus values
			if(bus !== false){
				if(c){
					if(b == bus)
						c.checked = true;
					else
						c.checked = false;
				}
			}
		}
		let mute = BigInt(parseInt(entry.Mutes, 16));
		let vol = Number(mute & BigInt(0xFF));
		el = document.getElementById("stConfOutMute");
		el.innerHTML = "";
		el.appendChild(document.createTextNode("Mute Levels"));
		el.appendChild(document.createElement("br"));
		c = document.createElement("input");
		c.id = "OutMuteCue";
		l = document.createElement('label');
		l.style.width = "25px"; 
		l.style.display = "inline-block";
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Cue"));
		c.setAttribute("type", "range");
		c.setAttribute("min", "0");
		c.setAttribute("max", "255");
		c.addEventListener("input", stConfOutMuteChange, false);
		c.setAttribute("value", vol);
		c.setAttribute("data-bus", 0);
		el.appendChild(l);
		el.appendChild(c);
		c = document.createElement('div');
		c.style.width = "45px";
		c.style.display = "inline-block";
		c.innerText = linToDBtext(vol / 255);
		el.appendChild(c);
		el.appendChild(document.createElement("br"));
		
		vol = Number((mute & BigInt(0xFF00)) / BigInt(0x100));
		c = document.createElement("input");
		c.id = "OutMuteA";
		l = document.createElement('label');
		l.style.width = "25px"; 
		l.style.display = "inline-block";
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("A"));
		c.setAttribute("type", "range");
		c.setAttribute("min", "0");
		c.setAttribute("max", "255");
		c.addEventListener("input", stConfOutMuteChange, false);
		c.setAttribute("value", vol);
		c.setAttribute("data-bus", 1);
		el.appendChild(l);
		el.appendChild(c);
		c = document.createElement('div');
		c.style.width = "45px";
		c.style.display = "inline-block";
		c.innerText = linToDBtext(vol / 255);
		el.appendChild(c);
		el.appendChild(document.createElement("br"));
		
		vol = Number((mute & BigInt(0xFF0000)) / BigInt(0x10000));
		c = document.createElement("input");
		c.id = "OutMuteB";
		l = document.createElement('label');
		l.style.width = "25px"; 
		l.style.display = "inline-block";
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("B"));
		c.setAttribute("type", "range");
		c.setAttribute("min", "0");
		c.setAttribute("max", "255");
		c.addEventListener("input", stConfOutMuteChange, false);
		c.setAttribute("value", vol);
		c.setAttribute("data-bus", 2);
		el.appendChild(l);
		el.appendChild(c);
		c = document.createElement('div');
		c.style.width = "45px";
		c.style.display = "inline-block";
		c.innerText = linToDBtext(vol / 255);
		el.appendChild(c);
		el.appendChild(document.createElement("br"));
		
		vol = Number((mute & BigInt(0xFF000000)) / BigInt(0x1000000));
		c = document.createElement("input");
		c.id = "OutMuteC";
		l = document.createElement('label');
		l.style.width = "25px"; 
		l.style.display = "inline-block";
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("C"));
		c.setAttribute("type", "range");
		c.setAttribute("min", "0");
		c.setAttribute("max", "255");
		c.addEventListener("input", stConfOutMuteChange, false);
		c.setAttribute("value", vol);
		c.setAttribute("data-bus", 3);
		el.appendChild(l);
		el.appendChild(c);
		c = document.createElement('div');
		c.style.width = "45px";
		c.style.display = "inline-block";
		c.innerText = linToDBtext(vol / 255);
		el.appendChild(c);
		el.appendChild(document.createElement("br"));
	}
}

async function refreshWallPanel(){
	let post = {"type":"playlist","match":"WALL%","sortBy":"Label"};
	let resp = await fetchContent("library/browse", {
		method: 'POST',
		body: JSON.stringify(post),
		headers: {
			"Content-Type": "application/json",
			"Accept": "application/json"
		}
	});
	
	let el = document.getElementById("selwall");
	if(resp instanceof Response){
		if(resp.ok){
			let data = await resp.json();
			if(el && data){
				let inner = "";
				for(let i=0; i < data.length; i++)
					inner += "<option value='"+data[i].Label+"' data-id='"+data[i].id+"'>"+data[i].Label+"</option>";
				el.innerHTML = inner;
			}
		}
	}
	wallMenuChange({target: el});
}

async function wallMenuChange(evt){
	let id = evt.target.selectedOptions[0].getAttribute("data-id");
	let el = document.getElementById("wallList");
	let props = await itemFetchProps(id);
	if(props){
		let plist = flattenPlaylistArray(props.playlist);
		el.innerHTML = "";
		for(let i = 0; i < plist.length; i++){
			let button = document.createElement("button");
			button.className = "stWallBtn";
			button.innerText = plist[i].Name;
			button.setAttribute("data-url", "item:///"+plist[i].ID);
			button.onclick = wallItemAction;
			el.appendChild(button);
		}
	}
}

async function wallItemAction(evt){
	let url = evt.target.getAttribute("data-url");
	let studio = studioName.getValue();
	if(studio.length){
		await fetchContent("studio/"+studio+"?cmd=playnow%20"+url);
	}
}

function stTBAction(evt, state, tb){
	evt.preventDefault();
	evt.stopPropagation();
	let studio = studioName.getValue();
	if(studio.length){
		if(state){
			evt.target.style.backgroundColor = "Red";
			fetchContent("studio/"+studio+"?cmd=tbon "+tb);
		}else{
			evt.target.style.backgroundColor = "LightGray";
			fetchContent("studio/"+studio+"?cmd=tboff "+tb);
		}
	}
}

async function syncStudioStat(studio){
	let resp = await fetchContent("studio/"+studio+"?cmd=stat&raw=1");
	if(resp instanceof Response){
		if(resp.ok){
			let data = await resp.text();
			let lines = data.split("\n");
			for(let n = 0; n < lines.length; n++){
				let fields = lines[n].split("=");
				let key = fields[0];
				let value = fields[1];
				if(key == "ListRev"){
					value =  parseInt(value);
					if(studioStateCache.queueRev != value){
						// queue (list) has changed... issue list command to handle changes
						syncQueue(studioName.getValue());
						studioStateCache.queueRev = value;
					}
					let el = document.getElementById("stRun");
					// update Queue Run/Halt button
					fields = fields[1].split(" ");
					if(fields[1] == "Running"){
						el.checked = true;
						studioStateCache.runStat = 1;
					}else{
						el.checked = false;
						studioStateCache.runStat = 0;
					}
				}else if(key == "LogTime"){
					value =  parseInt(value);
					if(studioStateCache.logTime != value){
						// log time has changed... issue list command to handle changes
						syncRecentPlays();
						studioStateCache.logTime = value;
					}
				}else if(key == "auto"){
					let idx = value.indexOf(' ');
					
					let fill = "Nothing to fill";
					if(idx > -1){
						fill = value.slice(idx + 1);
						value = value.slice(0, idx);
						if(fill.length == 0)
							fill = "Nothing to fill yet.";
					}
					// update automation buttons and fill text
					let el = document.getElementById("stFillDesc");
					el.innerText = fill;
					if(value == "on"){
						studioStateCache.autoStat = 2;	// auto
						el = document.getElementById("stAuto");
						el.checked = true;
					}else if(value == "live"){
						studioStateCache.autoStat = 1;	// live
						el = document.getElementById("stLive");
						el.checked = true;
					}else{
						studioStateCache.autoStat = 0;	// off
						el = document.getElementById("stOff");
						el.checked = true;
					}
				}else if(key == "sipPhone"){
					let el = document.getElementById("stConfSIPstat");
					if(el)
						el.innerText = value;
				}
			}
			if(studioStateCache.control)
				studioStateCache.control.setAutoStat();
		}
	}
}

async function syncStudioMetalist(studio){
	if(studio && studio.length){
		let resp;
		resp = await fetchContent("studio/"+studio+"?cmd=metalist&raw=1");
		if(resp){
			if(resp.ok){
				let raw = await resp.text();
				let lines = raw.split("\n");
				if(lines.length > 1){
					let refList = [];
					for(let n = 1; n < lines.length; n++){
						if(lines[n].length){
							let fields = lines[n].split("\t");
							if(fields.length > 1){
								let ref = parseInt(fields[0], 16);
								let item = studioStateCache.meta[ref];
								if(!item || (studioStateCache.meta[ref].rev != fields[1]))
									updateMetaItem(studio, ref);
								refList.push(ref);
								
							}
						}
					}
					// Remove from studioStateCache.meta if not in refList
					let keys = Object.keys(studioStateCache.meta);
					for(let n = 1; n < keys.length; n++){
						let ref = keys[n];
						if(refList.some(elem => elem == ref) == false){
							studioDelMeta(ref);
						}
					}
				}
			}else{
				alert("Got an error fetching metalist from studio.\n"+resp.statusText);
			}
		}else if(cred.getValue()){
			alert("Failed to fetch metalist from the studio.");
		}
	}
}

function queueStatusText(stat){
	if(stat & 0x04)
		return"Playing";
	if(stat & 0x10)
		return "Done";
	else if(stat & 0x100)
		return "Running";
	else if(stat & 0x02)
		return "Ready";
	else if(stat & 0x02)
		return "Loading";
	else
		return "";
}

function queueItemInfo(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let target = evt.target;
	let item = target.parentElement.parentElement;
	let ref = item.getAttribute("data-idx");
	item = studioStateCache.meta[ref];
	if(item && item.Type){
		item.UID = ref;
		item.qtype = item.Type;
		let credentials = cred.getValue();
		if(credentials && ['admin', 'manager', 'library'].includes(credentials.permission))
			showItem(item, true);
		else
			showItem(item, false);
	}
}

function playerItemInfo(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let player = evt.target.parentNode.parentElement.getAttribute("data-pnum");
	let ref = refFromPlayerNumber(player);
	if(ref < 0)
		return;
	item = studioStateCache.meta[ref];
	if(item && item.Type){
		item.UID = ref;
		item.qtype = item.Type;
		let credentials = cred.getValue();
		if(credentials && ['admin', 'manager', 'library'].includes(credentials.permission))
			showItem(item, true);
		else
			showItem(item, false);
	}
}

function queueHistoryInfo(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let target = evt.target;
	let logs = studioStateCache.logs;
	let el = target.parentElement.parentElement;
	let logID = el.getAttribute("data-idx");
	for(let i = 0; i<logs.length; i++){
		let entry = logs[i];
		if(entry.logID == logID){
			let credentials = cred.getValue();
			if(credentials && ['admin', 'manager', 'library'].includes(credentials.permission))
				showItem(entry, true);
			else
				showItem(entry, false);
			return;
		}
	}
}

function queueItemCheckAction(evt){
	evt.preventDefault();
	evt.stopPropagation();
	let target = evt.target;
	let item = target.parentElement.parentElement;
	let ref = item.getAttribute("data-idx");
	item = studioStateCache.meta[ref];
	if(item){
		// make persisten accross queue list updates
		if(target.checked)
			item.chkd = "checked";
		else
			item.chkd = "";
	}
}

function queueSelectAll(evt){
	if(evt)
		evt.preventDefault();
	let rows = document.getElementById("stQlist").childNodes;
	for(let i=0; i<rows.length; i++){
		let sel = rows[i].childNodes[0].childNodes[0];	// checkbox
		if(!sel.checked){
			sel.checked = true;
			let item = rows[i];
			let ref = item.getAttribute("data-idx");
			item = studioStateCache.meta[ref];
			if(item){
				// make persisten accross queue list updates
				item.chkd = "checked";
			}
		}
	}
}

function queueUnselectAll(evt){
	if(evt)
		evt.preventDefault();
	let rows = document.getElementById("stQlist").childNodes;
	for(let i=0; i<rows.length; i++){
		let sel = rows[i].childNodes[0].childNodes[0];	// checkbox
		if(sel.checked){
			sel.checked = false;
			let item = rows[i];
			let ref = item.getAttribute("data-idx");
			item = studioStateCache.meta[ref];
			if(item){
				// make persistent accross queue list updates
				item.chkd = "";
			}
		}
	}
}

async function queueDeleteSel(evt){
	if(evt)
		evt.preventDefault();
	let rows = document.getElementById("stQlist").childNodes;
	let studio = studioName.getValue();
	if(studio.length){
		for(let i=0; i<rows.length; i++){
			let sel = rows[i].childNodes[0].childNodes[0];	// checkbox
			if(sel.checked){
				let ref = rows[i].getAttribute("data-idx");
				let item = studioStateCache.meta[ref];
				if(item){
					ref = parseInt(ref, 10);
					let hexStr =  ("00000000" + ref.toString(16)).substr(-8);
					let resp = await fetchContent("studio/"+studio+"?cmd=delete%20"+hexStr);
				}
			}
		}
	}
}

async function appendItemsToQueue(data){
	if(!data || (data.length == 0))
		return;
	let rows = document.getElementById("stQlist").childNodes;
	let studio = studioName.getValue();
	if(studio.length){
		for(let i=0; i<rows.length; i++){
			let sel = rows[i].childNodes[0].childNodes[0];	// checkbox
			if(sel.checked){
				let ref = rows[i].getAttribute("data-idx");
				let item = studioStateCache.meta[ref];
				if(item && (!item.stat || (item.stat != "Playing"))){
					ref = parseInt(ref, 10);
					let hexStr =  ("00000000" + ref.toString(16)).substr(-8);
					for(let n=0; n<data.length; n++){
						let url = "";
						if(data[n].ID){
							url = "item:///"+data[n].ID;
						}else if((data[n].Type === "file") || (data[n].Type === "stop")){
							if(data[n].URL && data[n].URL.length)
								url = data[n].URL;
						}
						if(url.length){
							await fetchContent("studio/"+studio+"?cmd=add%20"+hexStr+"%20"+url);
						}
					}
				}
				return;
			}
		}
		// no queue item selected.  Add to end of the list, forward order
		for(let n=0; n<data.length; n++){
			let url = "";
			if(data[n].ID){
				url = "item:///"+data[n].ID;
			}else if(((data[n].Type === "file") || (data[n].Type === "stop")) && data[n].URL && data[n].URL.length){
				url = data[n].URL;
			}
			if(url.length){
				await fetchContent("studio/"+studio+"?cmd=add%20-1%20"+url);
			}
		}
	}
}

function queueSelToStash(evt){
	if(evt)
		evt.preventDefault();
	let rows = document.getElementById("stQlist").childNodes;
	for(let i=0; i<rows.length; i++){
		let sel = rows[i].childNodes[0].childNodes[0];	// checkbox
		if(sel.checked){
			let ref = rows[i].getAttribute("data-idx");
			let item = studioStateCache.meta[ref];
			if(item)
				appendItemToStash(item);
		}
	}
}

async function breakToQueue(evt){
	if(evt)
		evt.preventDefault();
	let rows = document.getElementById("stQlist").childNodes;
	let studio = studioName.getValue();
	if(studio.length){
		for(let i=0; i<rows.length; i++){
			let sel = rows[i].childNodes[0].childNodes[0];	// checkbox
			if(sel.checked){
				let ref = rows[i].getAttribute("data-idx");
				let item = studioStateCache.meta[ref];
				if(item && (!item.stat || (item.stat != "Playing"))){
					ref = parseInt(ref, 10);
					let hexStr =  ("00000000" + ref.toString(16)).substr(-8);
					await fetchContent("studio/"+studio+"?cmd=add%20"+hexStr+"%20stop:///");
				}
				return;
			}
		}
		// no queue item selected.  Add to end of the list, forward order
		await fetchContent("studio/"+studio+"?cmd=add%20-1%20stop:///");
	}
}

function moveItemInQueue(obj, fromIdx, toIdx, param){
	// check to prevent moving of playing items
	if((fromIdx === false) && param){
		let val = param.pnum;
		if(!val || !val.length){
			// no player number, try url
			val = param.url;
		}
		if(val && val.length){
			let studio = studioName.getValue();
			if(studio.length)
				fetchContent("studio/"+studio+"?cmd=add%20"+toIdx+"%20"+val);
		}
	}else{
		let meta = queueMetaFromIdx(fromIdx);
		if(meta){
			if(meta.stat == "Playing")
				return true; // prevent local drop
		}
		let metato = queueMetaFromIdx(toIdx);
		if(metato){
			if(metato.stat == "Playing")
				return true; // prevent local drop
		}
		let studio = studioName.getValue();
		if(studio.length){
			fetchContent("studio/"+studio+"?cmd=move "+fromIdx+" "+toIdx);
		}
	}
}

function queueMetaFromIdx(index){
	let el = document.getElementById("stQlist");
	let items = el.getElementsByTagName("li");
	let item = items[index];
	if(item){
		let ref = item.getAttribute("data-idx");
		return studioStateCache.meta[ref];
	}
	return undefined;
}

function queueElementFromRef(ref){
	let el = document.getElementById("stQlist");
	let items = el.getElementsByTagName("li");
	for(let n = 0; n < items.length; n++){
		let uid = items[n].getAttribute("data-idx");
		if(uid && (ref == uid))
			return {el: items[n], idx: n};
	}
	return undefined;
}

function playerNumberFromRef(ref){
	for(let n = 0; n < studioStateCache.ins.length; n++){
		let item = studioStateCache.ins[n];
		if(item["meta-UID"] == ref)
			return item.pNum;
	}
	return -1;	// not in a player
}

function refFromPlayerNumber(pNum){
	pNum = parseInt(pNum);
	let item = studioStateCache.ins[pNum];
	let ref = item["meta-UID"];
	if(ref)
		return parseInt(ref);
	return -1;	// player not loaded
}

function queueSetItemcolor(el, meta){
	let color = "DarkGrey";
	if(meta && (typeof meta.stat !== "undefined")){
		if(meta.stat == "Playing")
			color = "green";
		else if(meta.stat == "Ready")
			color = "yellow";
		else
			color = "red";
	}
	el.style.backgroundColor = color;
}

async function syncRecentPlays(evt){
	if(evt)
		evt.preventDefault();
	let studio = studioName.getValue();
	let cnt = document.getElementById("qHistCount");
	let settings = studioStateCache.meta[0];
	if(settings && studio && studio.length){
		let locID = settings["db_loc"];
		if(locID && locID.length){
			locID = parseInt(locID, 10);
			let logs;
			let limit = parseInt(cnt.value);
			let api = "library/logs";
			let resp = await fetchContent(api, {
					method: 'POST',
					body: JSON.stringify({locID: locID, range: "0/"+limit}),
					headers: {
						"Content-Type": "application/json",
						"Accept": "application/json"
					}
				});
			if(resp){
				if(resp.ok){
					let result = await resp.json();
					if(result && result.length){
						result.forEach((obj) => {obj.ID = obj.Item; delete obj.Item; obj.logID = obj.id; delete obj.id});
						studioStateCache.logs = result;
						updateQueueLogDisplay(true);
					}
				}else{
					alert("Got an error fetching logs from server.\n"+resp.statusText);
				}
			}else{
				alert("Failed to fetch logs from the server.");
			}
		}
	}
}

async function syncQueue(studio){
	let resp;
	resp = await fetchContent("studio/"+studio+"?cmd=list&raw=1");
	if(resp){
		if(resp.ok){
			let raw = await resp.text();
			let lines = raw.split("\n");
			let res = [];
			let keys = lines[0].split("\t");
			if(lines.length > 1){
				for(let n = 1; n < lines.length; n++){
					if(lines[n].length){
						let obj = {};
						let fields = lines[n].split("\t");
						let cnt = fields.length;
						if(cnt > keys.length) 
							cnt = keys.length;
						for(let i = 0; i < cnt; i++)
							obj[keys[i]] = fields[i];
						res.push(obj);
					}
				}
			}
			studioStateCache.queue = res;
			// sync cached times
			studioStateCache.queueSec = calcQueueTimeToNext(res);
			studioStateCache.queueDur = calcQueueTimeToEnd(res);
			updateQueueLogDisplay(false);
		}else{
			alert("Got an error fetching list (queue) from studio.\n"+resp.statusText);
		}
	}else if(cred.getValue()){
		alert("Failed to fetch list (queue) from the studio.");
	}
}

function pNumPlusOne(pnum){
	if((pnum != undefined) && (pnum !== ""))
		return pnum + 1;
	return "";
}

function updateQueueLogDisplay(logOnly){
	let res = studioStateCache.queue;
	if(!logOnly){
		let format = `<span style='float: left;'><input type='checkbox' $chkd$ onchange='queueItemCheckAction(event)'></input>$Name$</span>
					<span style='float: right;'>$Duration->timeFormat$ <button class="editbutton" onclick="queueItemInfo(event)">i</button></span>
					<div style='clear:both;'></div>
					<span style='float: left;'>$Artist$</span><span style='float: right;'>$stat$ $pNum->pNumPlusOne$</span><div style='clear:both;'></div>
					<span style='float: left;'>$Album$</span><span style='float: right;'>$Owner$/$Type$</span><div style='clear:both;'></div>`;
		let el = document.getElementById("stQlist");
		el.innerHTML = "";
		for(let n = 0; n < res.length; n++){
			let item = res[n];
			let ref = parseInt(item["meta-UID"], 16);
			let stat = parseInt(item["status"], 10);
			let pNum = parseInt(item["pNum"], 10);
			let meta = studioStateCache.meta[ref];
			if(!meta){
				// create empty meta record... should be filled in later
				meta = {stat: ""};
				studioStateCache.meta[ref] = meta;
			}
			if(pNum > -1)
				meta.pNum = pNum;
			else
				meta.pNum = "";
			meta.stat = queueStatusText(stat);
			let li = appendDragableItem(true, moveItemInQueue, meta, ref, el, format, "URL");
			queueSetItemcolor(li, meta);
		}
	}
	// handle log display too
	let logs = studioStateCache.logs;
	// remove queue items with logIDs set in meta from the log list
	let qel = document.getElementById("stQlist");
	let hel = document.getElementById("stHlist");
	hel.innerHTML = "";
	let format = `<span style='float: left;'>$Name$</span>
					<span style='float: right;'>@$TimeStr$ $ID->queueHistoryHasID$</span>
					<div style='clear:both;'></div>
					<span style='float: left;'>$Artist$</span><span style='float: right;'>$stat$ $pNum->pNumPlusOne$</span><div style='clear:both;'></div>
					<span style='float: left;'>$Album$</span><span style='float: right;'>$Owner$</span><div style='clear:both;'></div>`;
	let items = qel.getElementsByTagName("li");
	for(let i = logs.length-1; i>=0; i--){
		let entry = logs[i];
		let logID = entry.logID;
		for(let n = 0; n < items.length; n++){
			let li = items[n];
			if(li){
				let ref = li.getAttribute("data-idx");
				let meta = studioStateCache.meta[ref];
				if(meta && (meta.logID == entry.logID))
					logID = 0;
			}
		}
		if(logID){
			// display this entry
			let li = appendDragableItem(true, false, entry, logID, hel, format, "Source");
			queueSetItemcolor(li, entry);
		}
	}
}

function studioDelMeta(ref){
	if(studioStateCache.meta[ref]){
		delete studioStateCache.meta[ref];
		let el = queueElementFromRef(ref);
		if(el){
			// this meta record has an element in the queue... trigger an queue update
			syncQueue(studioName.getValue());
		}
		// check recorder list for update
		for(let i=0; i<studioStateCache.encoders.length; i++){
			let item = studioStateCache.encoders[i];
			if(item.UID == ref){
				// in recorder list
				refreshRecorderPanel();
			}
		}
	}
}

function updateQueueElement(el, meta){
	let format = `<span style='float: left;'><input type='checkbox' $chkd$ onchange='queueItemCheckAction(event)'></input>$Name$</span>
		<span style='float: right;'>$Duration->timeFormat$ <button class="editbutton" onclick="queueItemInfo(event)">i</button></span>
		<div style='clear:both;'></div>
		<span style='float: left;'>$Artist$</span><span style='float: right;'>$stat$ $pNum->pNumPlusOne$</span><div style='clear:both;'></div>
		<span style='float: left;'>$Album$</span><span style='float: right;'>$Owner$/$Type$</span><div style='clear:both;'></div>`;
	setDragableInnerHTML(meta, el, format);
	queueSetItemcolor(el, meta);
}

async function updateMetaItem(studio, ref){
	let resp;
	let hexStr =  ("00000000" + ref.toString(16)).substr(-8);
	resp = await fetchContent("studio/"+studio+"?cmd=dumpmeta%20"+hexStr+"&raw=1");
	if(resp){
		if(resp.ok){
			let raw = await resp.text();
			let lines = raw.split("\n");
			let res = {};
			if(lines.length > 1){
				for(let n = 0; n < lines.length; n++){
					if(lines[n].length){
						let fields = lines[n].split("=");
						if(fields[0].length && (fields[1] != undefined)){
							// remove first field and keep any additionals, restoring '=' token
							let prop = fields[0];
							fields.splice(0,1)
							res[prop] = fields.join("=");
						}
					}
				}
			}
			if(Object.keys(res).length){
				let old = studioStateCache.meta[ref];
				let update = false;
				if(!old){
					// new ref
					studioStateCache.meta[ref] = res;
					update = true;
				}
				else if(res["rev"] != old.rev){
					// merge new properties into existing
					studioStateCache.meta[ref] = {...old, ...res};
					update = true;
				}
				if(update){
					// do stuff with updated record
					if(ref == 0){
						// got setting... update recent plays
						syncRecentPlays();
						if(!old || (old.db_loc != studioStateCache.meta[ref].db_loc))
							setLibUsingStudio();
						if(!old || (old.client_players_visible != studioStateCache.meta[ref].client_players_visible))
							syncPlayers(studio);
					}else{
						let qli = queueElementFromRef(ref);
						if(qli){
							// meta item is in queue... update queue list item
							updateQueueElement(qli.el, studioStateCache.meta[ref]);
						}
						let pNum = playerNumberFromRef(ref);
						if(pNum != -1){
							updatePlayerTitle(pNum, res);
							let el = document.getElementById("pType"+pNum); 
							if(res.Type)
								el.innerText = res.Type;
							else
								el.innerText = "";
							el = document.getElementById("pName"+pNum); 
							if(res.Name)
								el.innerText = res.Name;
							else
								el.innerText = "";
						}
					}
				}
			}
		}else{
			alert("Got an error fetching metadata from studio.\n"+resp.statusText);
		}
	}else if(cred.getValue()){
		alert("Failed to fetch metadata from the studio.");
	}
}

function updatePlayerTitle(pNum, res){
	let el = document.getElementById("player" + pNum);
	if(el){
		let str = "Title: ";
		if(res.Name)
			str += res.Name;
		str += "\nArtist: ";
		if(res.Artist)
			str += res.Artist;
		str += "\nAlbum: ";
		if(res.Album)
			str += res.Album;
		el.title = str;
		if(res.URL && res.URL.length)
			el.setAttribute("data-url", res.URL);
	}
}

async function playerFaderAction(obj, pNum){
	// obj is either a float value from 0 to 1.5, fader value
	// with pNum set the the player number, or it is a fader object
	// and pNum is empty
	let player;
	let val;
	if(pNum){
		player = pNum;
		value = obj;
	}else{
		player = obj.id;
		player = parseInt(player.slice(6)); // trim off "pFader" prefix
		val = parseFloat(obj.value);
	}
	let studio = studioName.getValue();
	if(studio.length){
		// make val scalar
		val = faderToLin(val);
		fetchContent("studio/"+studio+"?cmd=vol "+player+" "+val);
	}
}

async function playerBalanceAction(obj, pNum){
	// obj is either a float value from 0 to 1.5, balance value
	// with pNum set the the player number, or it is a balance control object
	// and pNum is empty
	let player;
	let val;
	if(pNum){
		player = pNum;
		value = obj;
	}else{
		player = obj.id;
		player = parseInt(player.slice(4)); // trim off "pBal" prefix
		val = parseFloat(obj.value);
	}
	let studio = studioName.getValue();
	if(studio.length){
		if(val > 1.0)
			val = 1.0;
		if(val < -1.0)
			val = -1.0;
		fetchContent("studio/"+studio+"?cmd=bal "+player+" "+val);
	} 
}

async function playerPosAction(obj, pNum){
	// obj is either a float value from -1.0 to 1.0, position jog value
	// with pNum set the the player number, or it is a position control object
	// and pNum is empty
	let player;
	let val;
	if(pNum){
		player = pNum;
		value = obj;
	}else{
		player = obj.id;
		player = parseInt(player.slice(4)); // trim off "pPos" prefix
		val = parseFloat(obj.value);
	}
	let p = studioStateCache.ins[player];
	if(p){
		if((p.pos != undefined) && p.pos.length){
			let pos = Math.round(parseFloat(p.pos));
			if(p && p.dur){
				let dur = parseFloat(p.dur);
				if(dur){
					let scaled = val;
					if(scaled < 0.0)
						scaled = -scaled;
					scaled = Math.pow(scaled, 3);
					scaled = scaled * dur;
					if(val < 0.0)
						scaled = -scaled;
					scaled = pos + scaled;
					if(scaled > dur)
						scaled = dur;
					if(scaled < 0.0)
						scaled = 0.0;
					let studio = studioName.getValue();
					if(studio.length){
						p.pos = scaled.toString();
						playerTimeUpdate(player);
						fetchContent("studio/"+studio+"?cmd=pos "+player+" "+scaled);
					}
				}
			}
		}
	}
}

async function playerAction(cmd, evt, pNum){
	// evt is the event from the button press, or if
	// evt is false, pNum is the player number to stop
	let player;
	if(evt){
		evt.preventDefault();
		evt.stopPropagation();
		player = evt.target.parentNode.parentElement.parentElement.getAttribute("data-pnum");
	}else
		player = pNum;
	let studio = studioName.getValue();
	if(studio.length)
		fetchContent("studio/"+studio+"?cmd="+cmd+" "+player);
}

function updatePlayerFaderUI(val, pNum){
	let vol = parseFloat(val);
	let el = document.getElementById("pGain"+pNum);
	if(el)
		el.innerText = linToDBtext(vol);
	el = document.getElementById("pFader"+pNum);
	if(el){
		if(el.touching)
			return;	// dont update fader while it is being touched
		val = linToFader(vol);
		el.value = val;
	}
}

function updatePlayerBalanceUI(val, pNum){
	val = parseFloat(val);
	let el = document.getElementById("pBal"+pNum);
	if(el){
		if(el.touching)
			return;	// dont update balance while it is being touched
		if(val < -1.0)
			val = -1.0;
		if(val > 1.0)
			val = 1.0;
		el.value = val;
	}
}

function updatePlayerUI(p){
	let pte = document.getElementById("pTime"+p.pNum);
	let pde = document.getElementById("pRem"+p.pNum);
	let ppe = document.getElementById("pPos"+p.pNum);
	if(pte && pde && ppe){
		if(p && (p.pos != undefined) && p.pos.length){
			let pos = Math.round(parseFloat(p.pos));
			pte.innerText = timeFormat(pos, 1);
			if(p.dur){
				let dur = Math.round(parseFloat(p.dur));
				if(dur){
					dur = dur - pos;
					if(dur < 0.00)
						dur = 0.0;
					pde.innerText = timeFormat(dur, 1);
					ppe.style.display = "block;";
				}else{
					pde.innerText = "";
					ppe.style.display = "none";
				}
			}else
				pde.innerText = "";
		}else{
			pte.innerText = "0:00";
			pde.innerText = "";
		}
	}
	let play = document.getElementById("pPlay"+p.pNum);
	let stop = document.getElementById("pStop"+p.pNum);
	let unload = document.getElementById("pUnload"+p.pNum);
	if(play && stop && unload){
		if(parseInt(p.status) & 0x2){
			play.style.display = "flex";
			stop.style.display = "flex";
			if(parseInt(p.status) & 0x4){
				// is playing
				play.style.background = "Green";
				stop.style.background = "LightGray";
				unload.style.display = "none";
			}else{
				play.style.background = "LightGray";
				stop.style.background = "Yellow";
				unload.style.display = "block";
			}
		}else{
			play.style.display = "none";
			stop.style.display = "none";
			unload.style.display = "block";
		}
	}
	updatePlayerFaderUI(p.vol, p.pNum);
	updatePlayerBalanceUI(p.bal, p.pNum);
	updateCueButton(p.pNum, parseInt(p.bus, 16));
}

function updateCueButton(pNum, bus){
	let cue = document.getElementById("pCue"+pNum);
	if(cue){
		if(2 & bus)
			cue.style.background = "Red";
		else
			cue.style.background = "";
	}
}

async function genPlayerBusMenu(evt){ //pNum, bus, meta){
	// called when bus button is clicked to show menu
	pNum = evt.target.id;
	pNum = parseInt(pNum.slice(7)); // trim off "pBusSel" prefix
	let p = studioStateCache.ins[pNum];
	let ref = refFromPlayerNumber(pNum);
	let meta;
	if(ref > 0)
		meta = studioStateCache.meta[ref];
	if(!p)
		return;
	if(p.bus !== false){
		bus = parseInt(p.bus, 16);
		bus = bus & 0x00ffffff;
	}
	let bdiv;
	let fdiv;
	let mdiv;
	let tbdiv;
	let list = document.getElementById("pBusList"+pNum);
	if(!list)
		return;
	
	let build = false;
	let id = "p"+pNum+"BusSep";
	let el = document.getElementById(id);
	if(!el){
		bdiv = document.createElement("div");
		el = document.createElement("b");
		el.innerText = "Busses";
		el.id = id;
		bdiv.appendChild(el);
		bdiv.appendChild(document.createElement("br"));
		list.appendChild(bdiv);
		
		fdiv = document.createElement("div");
		el = document.createElement("hr");
		fdiv.appendChild(el);
		el = document.createElement("b")
		el.innerText = "Feed 0dB";
		el.id = "pFeedSep"+pNum;
		fdiv.appendChild(el);
		fdiv.appendChild(document.createElement("br"));
		list.appendChild(fdiv);
		
		mdiv = document.createElement("div");
		el = document.createElement("hr");
		mdiv.appendChild(el);
		el = document.createElement("b");
		el.innerText = "Mutes";
		mdiv.appendChild(el);
		mdiv.appendChild(document.createElement("br"));
		let c = document.createElement("input");
		c.id = "p"+pNum+"b25";
		let l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Group A"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 25);
		c.setAttribute("data-pnum", pNum);
		c.addEventListener('change', playerMuteTBAction, false);
		mdiv.appendChild(c);
		mdiv.appendChild(l);
		mdiv.appendChild(document.createElement("br"));
		c = document.createElement("input");
		c.id = "p"+pNum+"b26";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Group B"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 26);
		c.setAttribute("data-pnum", pNum);
		c.addEventListener('change', playerMuteTBAction, false);
		mdiv.appendChild(c);
		mdiv.appendChild(l);
		mdiv.appendChild(document.createElement("br"));
		c = document.createElement("input");
		c.id = "p"+pNum+"b27";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Group C"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 27);
		c.setAttribute("data-pnum", pNum);
		c.addEventListener('change', playerMuteTBAction, false);
		mdiv.appendChild(c);
		mdiv.appendChild(l);
		mdiv.appendChild(document.createElement("br"));
		list.appendChild(mdiv);
		
		tbdiv = document.createElement("div");
		el = document.createElement("hr")
		tbdiv.appendChild(el);
		el = document.createElement("b")
		el.innerText = "Talkback Source";
		tbdiv.appendChild(el);
		tbdiv.appendChild(document.createElement("br"));
		c = document.createElement("input");
		c.id = "p"+pNum+"tb29";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("TB 1"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 29);
		c.setAttribute("data-pnum", pNum);
		c.addEventListener('change', playerMuteTBAction, false);
		tbdiv.appendChild(c);
		tbdiv.appendChild(l);
		tbdiv.appendChild(document.createElement("br"));
		c = document.createElement("input");
		c.id = "p"+pNum+"tb30";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("TB 2"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 30);
		c.setAttribute("data-pnum", pNum);
		c.addEventListener('change', playerMuteTBAction, false);
		tbdiv.appendChild(c);
		tbdiv.appendChild(l);
		tbdiv.appendChild(document.createElement("br"));
		c = document.createElement("input");
		c.id = "p"+pNum+"tb31";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("TB 3"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 31);
		c.setAttribute("data-pnum", pNum);
		c.addEventListener('change', playerMuteTBAction, false);
		tbdiv.appendChild(c);
		tbdiv.appendChild(l);
		tbdiv.appendChild(document.createElement("br"));
		list.appendChild(tbdiv);
	}
	if(fdiv){
		// create a feed volume control
		id = "pfvol"+pNum;
		c = document.createElement("input");
		c.id = id;
		c.setAttribute("type", "range");
		c.setAttribute("min", "0.0");
		c.setAttribute("max", "1.5");
		c.setAttribute("value", "1.0");
		c.setAttribute("step", "0.03");
		c.style.width = "65px";
		c.style.height = "height: 12px";
		c.setAttribute("data-pnum", pNum);
		c.addEventListener('input', playerFeedVolAction, false);
		c.draggable = true;
		c.addEventListener('dragstart', function(event){event.preventDefault(); event.stopPropagation();});
		fdiv.appendChild(c);
		fdiv.appendChild(document.createElement("br"));
		// create a NONE feed entry
		id = "p"+pNum+"f0";
		c = document.createElement("input");
		c.id = id;
		c.name = "fGrpP"+pNum;
		c.checked = true;
		let l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("None"));
		c.setAttribute("type", "radio");
		c.setAttribute("data-idx", "0");
		c.setAttribute("data-pnum", pNum);
		c.addEventListener('change', playerFeedAction, false);
		fdiv.appendChild(c);
		fdiv.appendChild(l);
		fdiv.appendChild(document.createElement("br"));
	}
	let mmbus = 0;
	let mmvol = 0.0;
	if(meta){
		// default to none feed
		mmbus = meta.MixMinusBus;
		if(mmbus)
			mmbus = mmbus & 0x00ffffff;
		if(!mmbus){
			let c = document.getElementById("p"+pNum+"f0");
			c.checked = true;
		}
		mmvol = meta.MixMinusVol;
		let el = document.getElementById("pfvol"+pNum);
		if(el && (mmvol != undefined)){
			// update feedvol control
			mmvol = parseFloat(mmvol);
			mmvol = linToFader(mmvol);
			el.value = mmvol;
			el = document.getElementById("pFeedSep"+pNum);
			el.innerText = "Feed " + linToDBtext(mmvol);
		}
	}
	for(let b=0; b<studioStateCache.buscnt; b++){
		let c;
		// update feed
		id = "p"+pNum+"f"+(b+1);
		c = document.getElementById(id);
		if(fdiv){
			c = document.createElement("input");
			c.id = id;
			c.name = "fGrpP"+pNum;
			let l = document.createElement('label');
			l.htmlFor = c.id;
			c.setAttribute("type", "radio");
			c.setAttribute("data-idx", b+1);
			c.setAttribute("data-pnum", pNum);
			c.addEventListener('change', playerFeedAction, false);
			switch(b){
				case 0:
					l.appendChild(document.createTextNode("Monitor"));
					break;
				case 1:
					l.appendChild(document.createTextNode("Cue"));
					break;
				case 2:
					l.appendChild(document.createTextNode("Main"));
					break;
				case 3:
					l.appendChild(document.createTextNode("Alternate"));
					break;
				default:
					l.appendChild(document.createTextNode("Bus " + b));
					break;
			}
			fdiv.appendChild(c);
			fdiv.appendChild(l);
			fdiv.appendChild(document.createElement("br"));
		}
		// update feed values
		if(meta !== false){
			if(c){
				if(b+1 == mmbus)
					c.checked = true;
			}
		}
		// update busses
		if(b != 1){ // skip cue channel
			id = "p"+pNum+"b"+b;
			c = document.getElementById(id);
			if(bdiv){
				c = document.createElement("input");
				c.id = id;
				let l = document.createElement('label');
				l.htmlFor = c.id;
				c.setAttribute("type", "checkbox");
				c.setAttribute("data-idx", b);
				c.setAttribute("data-pnum", pNum);
				c.addEventListener('change', playerBusCheckAction, false);
				switch(b){
					case 0:
						l.appendChild(document.createTextNode("Monitor"));
						break;
					case 2:
						l.appendChild(document.createTextNode("Main"));
						break;
					case 3:
						l.appendChild(document.createTextNode("Alternate"));
						break;
					default:
						l.appendChild(document.createTextNode("Bus " + b));
						break;
				}
				bdiv.appendChild(c);
				bdiv.appendChild(l);
				bdiv.appendChild(document.createElement("br"));
			}
			// update bus values
			if(bus !== false){
				if(c){
					if((1 << b) & bus)
						c.checked = true;
					else
						c.checked = false;
				}
			}
		}
	}
	if(fdiv){
		let c = document.createElement("b");
		c.innerText = "Feed Cue on";
		fdiv.appendChild(c);
		fdiv.appendChild(document.createElement("br"));
		c = document.createElement("input");
		c.id = "p"+pNum+"b24";
		let l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Cue"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1<<24);
		c.setAttribute("data-pnum", pNum);
		c.addEventListener('change', playerFeedAction, false);
		fdiv.appendChild(c);
		fdiv.appendChild(l);
		fdiv.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "p"+pNum+"b29";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 1"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1<<29);
		c.setAttribute("data-pnum", pNum);
		c.addEventListener('change', playerFeedAction, false);
		fdiv.appendChild(c);
		fdiv.appendChild(l);
		fdiv.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "p"+pNum+"b30";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 2"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1<<30);
		c.setAttribute("data-pnum", pNum);
		c.addEventListener('change', playerFeedAction, false);
		fdiv.appendChild(c);
		fdiv.appendChild(l);
		fdiv.appendChild(document.createElement("br"));
		
		c = document.createElement("input");
		c.id = "p"+pNum+"b31";
		l = document.createElement('label');
		l.htmlFor = c.id;
		l.appendChild(document.createTextNode("Talkback 3"));
		c.setAttribute("type", "checkbox");
		c.setAttribute("data-idx", 1n<<31n);
		c.setAttribute("data-pnum", pNum);
		c.addEventListener('change', playerFeedAction, false);
		fdiv.appendChild(c);
		fdiv.appendChild(l);
		fdiv.appendChild(document.createElement("br"));
	}
	if(meta && (meta.MixMinusBus != undefined)){
		// set feed cue overrides
		mmbus = BigInt(meta.MixMinusBus);
		c = document.getElementById("p"+pNum+"b24");
		if(c){
			if((1n << 24n) & mmbus)
				c.checked = true;
			else
				c.checked = false;
		}
		c = document.getElementById("p"+pNum+"b29");
		if(c){
			if((1n << 29n) & mmbus)
				c.checked = true;
			else
				c.checked = false;
		}
		c = document.getElementById("p"+pNum+"b30");
		if(c){
			if((1n << 30n) & mmbus)
				c.checked = true;
			else
				c.checked = false;
		}
		c = document.getElementById("p"+pNum+"b31");
		if(c){
			if((1n << 31n) & mmbus)
				c.checked = true;
			else
				c.checked = false;
		}
	}

	// handle talkback and mute assignments
	let studio = studioName.getValue();
	if(studio.length){
		let resp;
		resp = await fetchContent("studio/"+studio+"?cmd=showbus "+pNum+"&raw=1");
		if(resp){
			if(resp.ok){
				let raw = await resp.text();
				p.bus = raw; // update bus to include mutes and TBs
				bus = parseInt(raw, 16);
				for(b = 25; b < 28; b++){
					c = document.getElementById("p"+pNum+"b"+b);
					if(c){
						if((1 << b) & bus)
							c.checked = true;
						else
							c.checked = false;
					}
				}
				for(b = 29; b < 32; b++){
					c = document.getElementById("p"+pNum+"tb"+b);
					if(c){
						if((1 << b) & bus)
							c.checked = true;
						else
							c.checked = false;
					}
				}
			}
		}
	}
}

async function playerFeedVolAction(obj, pNum){
	// obj is either a float value from 0 to 1.5, fader value
	// with pNum set the the player number, or it is a fader object
	// and pNum is empty
	let player;
	let val;
	if(pNum){
		player = pNum;
		value = obj;
	}else{
		player = obj.target.id;
		player = parseInt(player.slice(5)); // trim off "pfVol" prefix
		val = parseFloat(obj.target.value);
	}
	let studio = studioName.getValue();
	if(studio.length){
		// make val scalar
		val = faderToLin(val);
		fetchContent("studio/"+studio+"?cmd=mmvol "+player+" "+val);
		let el = document.getElementById("pFeedSep"+player);
		el.innerText = "Feed " + linToDBtext(val);
	}
}

async function playerBusCheckAction(evt){
	let b = evt.target.getAttribute("data-idx");
	let pNum = evt.target.getAttribute("data-pnum");
	let p = studioStateCache.ins[pNum];
	let studio = studioName.getValue();
	if(p && studio.length){
		let bus = parseInt(p.bus, 16);
		bus = bus & 0x00ffffff;
		b = 1 << b;
		if(bus & b)
			bus = bus & ~b;
		else
			bus = bus | b;
		let hexStr =  ("00000000" + bus.toString(16)).substr(-8);
		fetchContent("studio/"+studio+"?cmd=bus "+pNum+" "+hexStr);
	}
}

async function playerMuteTBAction(evt){
	let b = BigInt(evt.target.getAttribute("data-idx"));
	let pNum = evt.target.getAttribute("data-pnum");
	let p = studioStateCache.ins[pNum];
	if(!p)
		return;
	let bus = BigInt(parseInt(p.bus, 16));
	let studio = studioName.getValue();
	if(studio.length){
		b = BigInt(1) << b;
		if(bus & b)
			bus = bus & ~b;
		else
			bus = bus | b;
		let hexStr =  ("00000000" + bus.toString(16)).substr(-8);
		fetchContent("studio/"+studio+"?cmd=mutes "+pNum+" "+hexStr);
	}
}

function playerFeedAction(evt){
	let pNum = evt.target.getAttribute("data-pnum");
	let p = studioStateCache.ins[pNum];
	let ref = refFromPlayerNumber(pNum);
	let meta;
	if(ref > 0)
		meta = studioStateCache.meta[ref];
	if(!meta)
		return;
	if(meta.MixMinusBus == undefined)
		mmbus = BigInt(0);
	else
		mmbus = BigInt(meta.MixMinusBus);
	let b = BigInt(evt.target.getAttribute("data-idx"));
	if(b > 0x00ffffffn){
		// cue override bits
		if(mmbus & b)
			mmbus = mmbus & ~b;
		else
			mmbus = mmbus | b;
	}else{
		mmbus = (mmbus & 0xff000000n) + b;
	}
	let studio = studioName.getValue();
	if(studio.length){
		fetchContent("studio/"+studio+"?cmd=mmbus "+pNum+" "+mmbus);
	}
}

async function playerCueAction(evt){
	if(evt){
		evt.preventDefault();
		let pNum = evt.target.getAttribute("data-idx");
		let p = studioStateCache.ins[pNum];
		let studio = studioName.getValue();
		if(p && studio.length){
			let bus = parseInt(p.bus, 16);
			bus = bus & 0x00ffffff;
			if(bus & 2)
				bus = bus & ~2;
			else
				bus = bus | 2;
			let hexStr =  ("00000000" + bus.toString(16)).substr(-8);
			fetchContent("studio/"+studio+"?cmd=bus "+pNum+" "+hexStr);
		}
	}
}

function playerDropAllow(evt){ 
	let target = evt.target;
	let atr = curDrag.getAttribute("data-url");
	if(atr && atr.length && !target.hasChildNodes())
		evt.preventDefault();	// otherwise the default will prevent drop
}

function playerDragAllow(evt){
	let target = evt.target;
	if(!target.hasChildNodes())
		evt.preventDefault();	// otherwise the default will prevent drag
	curDrag = this;
}

function playerDropHandler(evt){
	evt.preventDefault();
	let target = evt.target;
	let pNum = target.getAttribute("data-pnum");
	let atr = curDrag.getAttribute("data-url");
	if(atr && atr.length && !target.hasChildNodes()){ // player is empty and drag item url is set
		let studio = studioName.getValue();
		if(studio.length){
			fetchContent("studio/"+studio+"?cmd=load "+pNum+" "+atr);
			return true;
		}
	}
	return false;
}

async function syncPlayers(studio){
	// get players list
	let resp;
	resp = await fetchContent("studio/"+studio+"?cmd=pstat&raw=1");
	if(resp){
		if(resp.ok){
			let raw = await resp.text();
			let lines = raw.split("\n");
			lines.pop(); // remove last, blank line
			let keys = lines[0].split("\t");
			let count = lines.length;
			if(count > 1){
				let settings = studioStateCache.meta[0];
				if(settings){
					let vis = settings.client_players_visible;
					if(vis && count > vis)
						count = parseInt(vis)+1;
				}
				let mixer = document.getElementById("mixergrid");
				// check grid size against list size
				while(count-1 < mixer.childElementCount){
					// remove columns
					mixer.removeChild(mixer.lastChild);
				}
				while(count-1 > mixer.childElementCount){
					// add columns
					let p = document.createElement("div");
					p.setAttribute("data-pnum", mixer.childElementCount);
					p.draggable = false;
					p.className = "player";
					p.id = "player" + mixer.childElementCount;
					p.addEventListener("dragstart", playerDragAllow);
					p.addEventListener("dragenter", playerDropAllow);
					p.addEventListener("dragover", playerDropAllow);
					p.addEventListener("dragleave", playerDropAllow);
					p.addEventListener("drop", playerDropHandler);
					mixer.appendChild(p);
				}
				for(let n = 1; n < count; n++){
					let obj = {};
					if(lines[n].length){
						let fields = lines[n].split("\t");
						let cnt = fields.length;
						if(cnt > keys.length) 
							cnt = keys.length;
						for(let i = 0; i < cnt; i++)
							obj[keys[i]] = fields[i];
					}
					let ref = obj["meta-UID"];
					if(ref){
						ref = parseInt(ref, 16);
						obj["meta-UID"] = ref;
					}
					// update queue status if in queue
					let meta = studioStateCache.meta[ref];
					if(meta){
						meta.pNum = n-1;
						meta.stat = queueStatusText(obj.status);
						let el = queueElementFromRef(ref);
						if(el){
							updateQueueElement(el.el, meta);
							// recalc queue time
							let qitem = studioStateCache.queue[el.idx]
							if(qitem){
								obj.seg = parseFloat(obj.seg);
								if(obj.seg)
									qitem.segout = obj.seg;
								else
									qitem.segout = parseFloat(obj.dur);
								qitem.segout = qitem.segout - parseFloat(obj.pos);
								qitem.status = obj.status;
							}
						}
					}
					// check for UI change update
					let prev = studioStateCache.ins[n-1];
					if((!prev && obj.status) || (prev.status != obj.status)){
						let player = document.getElementById("player" + (n-1));
						if(obj.status == 0){
							while(player.hasChildNodes())
								player.removeChild(player.lastChild);
							player.title = "";
							player.draggable = false;
						}else{
							if(!player.hasChildNodes()){
								// copy player template
								let clone = pTemplate.content.cloneNode(true);
								let el = clone.querySelector("#pLabel"); 
								el.setAttribute("id", "pLabel"+(n-1));
								el.setAttribute("data-pnum", n-1);
								el.draggable = true;
								el.innerText = n.toString();
								el = clone.querySelector("#pType"); 
								el.setAttribute("id", "pType"+(n-1));
								if(meta)
									el.innerText = meta.Type;
								else
									el.innerText = "";
								el = clone.querySelector("#pName"); 
								el.setAttribute("id", "pName"+(n-1));
								if(meta)
									el.innerText = meta.Name;
								else
									el.innerText = "";
								el = clone.querySelector("#pTime"); 
								el.setAttribute("id", "pTime"+(n-1));
								el = clone.querySelector("#pRem"); 
								el.setAttribute("id", "pRem"+(n-1));
								el = clone.querySelector("#pPos"); 
								el.setAttribute("id", "pPos"+(n-1));
								el = clone.querySelector("#pVU"); 
								el.setAttribute("id", "pVU"+(n-1));
								el = clone.querySelector("#pBusSel"); 
								el.setAttribute("id", "pBusSel"+(n-1));
								el.setAttribute("data-childdiv", "pBusList"+(n-1));
								el = clone.querySelector("#pBusList"); 
								el.setAttribute("id", "pBusList"+(n-1));
								el = clone.querySelector("#pCue"); 
								el.setAttribute("id", "pCue"+(n-1));
								el.setAttribute("data-idx", n-1);
								el = clone.querySelector("#pBal"); 
								el.setAttribute("id", "pBal"+(n-1));
								el = clone.querySelector("#pFader"); 
								el.setAttribute("id", "pFader"+(n-1));
								el = clone.querySelector("#pGain"); 
								el.setAttribute("id", "pGain"+(n-1));
								el = clone.querySelector("#pPlay"); 
								el.setAttribute("id", "pPlay"+(n-1));
								el = clone.querySelector("#pStop"); 
								el.setAttribute("id", "pStop"+(n-1));
								el = clone.querySelector("#pUnload"); 
								el.setAttribute("id", "pUnload"+(n-1));
								
								player.appendChild(clone);
								player.draggable = true;
								if(meta)
									updatePlayerTitle(n-1, meta);
							}
							updatePlayerUI(obj);
						}
					}
					studioStateCache.ins[n-1] = obj;
				}
				studioStateCache.queueSec = calcQueueTimeToNext(studioStateCache.queue);
			}
		}else{
			alert("Got an error fetching players from studio.\n"+resp.statusText);
		}
	}else if(cred.getValue()){
		alert("Failed to fetch players from the studio.");
	}
}

function studioHandleNotice(data){
	let val = 0;
	let ref = 0;
	let p;
	switch(data.type){
		case "outvol":			// output volume change, ref=output index, val=scalar volume
			val = data.val;	// number
			ref = data.num;
			updateOutVolUI(val, ref);
			break;
		case "invol":			// player volume change, ref=input index, val=scalar volume
			val = data.val;	// number
			ref = data.num;
			if(studioStateCache.control)
				studioStateCache.control.setPVol(val, ref);
			updatePlayerFaderUI(val, ref);
			break;
		case "inbal":			// player balance change, ref=input index, val=scalar balance, zero for center
			val = data.val;	// number
			ref = data.num;
			if(studioStateCache.control)
				studioStateCache.control.setPBal(val, ref);
			updatePlayerBalanceUI(val, ref);
			break;
		case "outbus":			// output bus assignment change, ref=output index, val=bus assignment number
			val = data.val;	// number (bus index)
			ref = data.num;
			updateOutBusUI(val, ref);
			break;
		case "inbus":			// input bus assignment change, ref=input index, val=hex string bus assignment bits
			val = parseInt(data.val, 16);	// hex string
			ref = data.num;
			p = studioStateCache.ins[ref];
			if(p){
				updateCueButton(ref, val);
				p.bus = data.val;
			}
			if(studioStateCache.control)
				studioStateCache.control.setPBus(val, ref);
			break;
		case "instat":			// input status change, ref=input index, val=status number
			val = data.val;	// number
			ref = data.num;
			if(studioStateCache.control)
				studioStateCache.control.setPStat(val, ref);
//!! handle TB status too...
			syncPlayers(studioName.getValue());
			break;
		case "status":			// over-all status change, no ref, no val.  Use "stat" command to get status
			// no ref or value: Change in ListRev, LogTime, automation status trigger this notice. Does not include sip registoration.
			syncStudioStat(studioName.getValue());
			break;
		case "metachg":		// metadata content change, ref=UID number, no val. Use "dumpmeta" command to get new content
			ref = data.uid;
			updateMetaItem(studioName.getValue(), ref);
			break;
		case "rstat":			// recorder/encoder status change, no ref, no val. Use "rstat" command to get status of all recorders
			refreshRecorderPanel();
			break;
		case "recgain":		// recorder/encoder gain change, ref=recorder UID number, val=scalar gain
			val = data.val;	// number
			ref = data.uid;
			updateEncoderVolUI(ref, val);
			break;
		case "inpos":		// input position change, ref=input index, val=position in seconds
			val = data.val;	// number
			ref = data.num;
			p = studioStateCache.ins[ref];
			if(studioStateCache.control)
				studioStateCache.control.setPPos(val, ref);
			if(p && p.status){
				p.pos = val.toString();
				playerTimeUpdate(ref);
			}
			break;
		case "metadel":		// metadata record deleted, ref=UID number, no val.
			ref = data.uid;
			studioDelMeta(ref);
			break;
		case "outdly":			// output delay change, ref=output index, val=delay in seconds, 16 max.
			val = data.val;	// number
			ref = data.num;
			refreshStudioDelays(null, ref, val);
			break;
			
		case "cpu":	// System Processor load, val=realtime JACK load in 0.8 format
			val = data.val;	// number: 255 * (percentage / 100)
			let el = document.getElementById("stCpuLoad");
			if(el)
				el.value = 100.0 * (val / 255.0);
			break;
			
		default:
			// ignore unknown type;
			return;
	}
}

async function updateControlSurface(){
	if(!midiAccess){
		if(navigator.requestMIDIAccess){
			midiAccess = await navigator.requestMIDIAccess({sysex: false});
			if(midiAccess)
				midiAccess.onstatechange = updateControlSurface; // call this function when midi devices change
		}
	}
	if(midiAccess){
		let resp = await fetchContent("control");
		if(resp && resp.ok){
			let list = [];
			let csmodules = await resp.json();
			for(let i = 0; i < csmodules.length; i++)
				csmodules[i] = csmodules[i].substring(0, csmodules[i].lastIndexOf('.')); // remove file extention
			let inputs = midiAccess.inputs.values();
			// inputs is an Iterator
			for(let input = inputs.next(); input && !input.done; input = inputs.next()){
				input = input.value;
				let outputs = midiAccess.outputs.values();
				for(let output = outputs.next(); output && !output.done; output = outputs.next()){
					output = output.value;
					if((output.name === input.name) && (output.manufacturer === input.manufacturer)){
						// found matching output.  See if we have a js module for this device
//						let devname = input.manufacturer + "_" + input.name;
						let devname = input.name;	// pipewire doesn't capture the manufacturer, just use name for matching
						for(let i = 0; i < csmodules.length; i++){
							if(devname.startsWith(csmodules[i])){
								let entry = {name: devname, module: csmodules[i]+".mjs", input: input, output: output};
								list.push(entry);
								break;
							}
						}
					}
				}
			}
			studioStateCache.midiList = list;
			// updates the control surface selection menu list
			// when the locListCache variable changes
			let element = document.getElementById("ctlsurf");
			if(element){
				let inner = "<option value='' >NONE</option>";
				for(let i=0; i < list.length; i++)
					inner += "<option value='"+list[i].name+"'>"+list[i].name+"</option>";
				element.innerHTML = inner;
			}
			let savedname = getStorageMidiControl();
			let i = list.length;
			if(savedname){
				for(i = 0; i < list.length; i++){
					if(list[i].name == savedname){
						selectControlSurface(list[i]);
						break;
					}
				}
			}
			if(i == list.length)
				selectControlSurface(undefined);
		}else{
			alert("Failed to fetch control surface modules from the server.");
		}
	}else{
		console.log("no browser support for midi");
		let element = document.getElementById("ctlsurf");
		if(element)
			element.innerHTML = "<option value='' >No Browser Midi Support</option>";
	}
}

async function stContSurfChange(evt){
	if(evt)
		evt.preventDefault();
	if(studioStateCache.midiList){
		let element = document.getElementById("ctlsurf");
		if(element){
			for(let i=0; i<studioStateCache.midiList.length; i++){
				if(studioStateCache.midiList[i].name == element.value){
					selectControlSurface(studioStateCache.midiList[i]);
					return;
				}
			}
			selectControlSurface(false);
		}
	}
}

async function stConsSend(){
	let studio = studioName.getValue();
	if(studio.length){
		let resp;
		let el = document.getElementById("conCommand");
		let obj = {cmd: el.value, raw: 1};
		resp = await fetchContent("studio/"+studio, {
				method: 'POST',
				body: JSON.stringify(obj),
				headers: {
					"Content-Type": "application/json",
					"Accept": "application/json"
				}
			});
		if(resp){
			if(resp.ok){
				let raw = await resp.text();
				let lines = raw.split("\n");
				el = document.getElementById("stConsRep");
				el.value = raw;
			}
		}
	}
}

async function selectControlSurface(entry){
	// load module
	if(entry){
		let Module = await import('/control/'+entry.module);
		if(entry.name != studioStateCache.midiName){
			if(Module){
				setStorageMidiControl(entry.name);
				studioStateCache.midiName = entry.name;
				studioStateCache.control = Module;
				Module.init(entry.input, entry.output);
			}
		}else
			Module.init();	// already selected, just refresh controls to new studio
	}else{
		if(studioStateCache.midiName !== false){
			studioStateCache.midiName = false;
			studioStateCache.control = false;
			if(entry === false){ // undefined does not changed saved, incase interface comes back
				setStorageMidiControl(false);
			}
		}
	}
	// update list selection
	let element = document.getElementById("ctlsurf");
	if(element){
		if(studioStateCache.midiName)
			element.value = studioStateCache.midiName;
		else
			element.selectedIndex = 0;
	}
}

function reloadStudioSection(el, type){
	if(type == "control"){
		updateControlSurface();
	}else if(type == "wall"){
		refreshWallPanel();
	}else if(type == "ins"){
		refreshInputGroups();
	}else if(type == "outs"){
		refreshOutGroups();
	}else if(type == "mixer"){
		refreshStAdminMixer();
	}else if(type == "voip"){
		refreshStAdminVoIP()
	}else if(type == "automation"){
		refreshStAdminAuto();
	}else if(type == "library"){
		refreshStAdminLib();
	}else if(type == "recorders"){
		refreshRecorderPanel();
	}else if(type == "jconns"){
		refreshStAdminRouts();
	}
}

function studioVuUpdate(data){
	if(!data)
		return;
	// update mix bus meters
	let canv;
	let busvu = document.getElementById('studioOutsVU');
	if(data[0]){
		if(!busvu.firstChild){
			// first data for new vu session... create view
			busmeters = [];
			for(let i = 0; i < data[0].pk.length; i++){
				canv = document.createElement("canvas");
				canv.setAttribute('width',10);
				canv.setAttribute('height',128);
				busvu.appendChild(canv);
				busmeters.push(new vumeter(canv, {
					"boxCount": 32,
					"boxCountRed": 9,
					"boxCountYellow": 7,
					"boxGapFraction": 0.25,
					"max": 255,
				}));
			}
		}
		// update meter displays with new values
		for(let c = 0; c < data[0].pk.length; c++){
			let vu = busmeters[c];
			if(vu)
				vu.vuSetValue(data[0].avr[c], data[0].pk[c]);
		}
		delete data[0];
	}
	// recorder and player levels are expressed in data[uid-numeric].  We need to itterate.
	for(ref in data){
		let pNum = playerNumberFromRef(ref);
		if(pNum != -1){
			let vu = document.getElementById('pVU' + pNum);
			if(vu){
				let els = vu.getElementsByTagName("canvas");
				if(!els.length){
					// first data for new vu session... create view
					vu.vumeters = [];
					for(let c = 0; c < data[ref].pk.length; c++){
						canv = document.createElement("canvas");
						canv.setAttribute('width',8);
						canv.setAttribute('height',128);
						vu.appendChild(canv);
						vu.vumeters.push(new vumeter(canv, {
							"boxCount": 32,
							"boxCountRed": 9,
							"boxCountYellow": 7,
							"boxGapFraction": 0.25,
							"max": 255,
						}));
					}
				}
				let max = 0;
				for(let c = 0; c < data[ref].pk.length; c++){
					if(max < data[ref].avr[c])
						max = data[ref].avr[c];
					let meter = vu.vumeters[c];
					if(meter)
						meter.vuSetValue(data[ref].avr[c], data[ref].pk[c]);
				}
				if(studioStateCache.control)
					studioStateCache.control.setPVU(max, pNum);
			}
		}else{
			// Possible recorder/encoder VU
			let vu = document.getElementById('rVU' + ref);
			if(vu){
				let els = vu.getElementsByTagName("canvas");
				if(!els.length){
					// first data for new vu session... create view
					vu.vumeters = [];
					for(let c = 0; c < data[ref].pk.length; c++){
						canv = document.createElement("canvas");
						canv.setAttribute('class', 'rVUcanvas');
						vu.appendChild(canv);
						vu.vumeters.push(new vumeter(canv, {
							"boxCount": 32,
							"boxCountRed": 9,
							"boxCountYellow": 7,
							"boxGapFraction": 0.25,
							"max": 255,
							"rotate": true
						}));
					}
				}
				let max = 0;
				for(let c = 0; c < data[ref].pk.length; c++){
					if(max < data[ref].avr[c])
						max = data[ref].avr[c];
					let meter = vu.vumeters[c];
					if(meter)
						meter.vuSetValue(data[ref].avr[c], data[ref].pk[c]);
				}
			}
		}
	}
}

/***** Server Side Events/Messages functions *****/

var sseMsgObj = new watchableValue({});
var es;
const sseReconFreqMilliSec = 10000;	// 10 seconds

function sseSetup(credVal){
	// login status changed callback or reconnecting
	if(es){
		es.close();
		es = false;
	}
	if(credVal){
		// logged in
		es = new EventSource('/ssestream'); // event source creation
		es.onopen = function(e) {
			es.addEventListener('msg', sseListener); // listen for 'msg' general messages at a minimum
			addListenersForAllSubscriptions();
			// if a studio is already selected, make the sse session get studio's events
			if(studioName.getValue().length)
				studioChangeCallback(studioName.getValue());
		};
		es.onerror = function(e) {
			es.close();
			es = false;
		};
	}
}

//  periodic check of sse connection for reconnect
setInterval(function() {
	if(!es || (es.readyState !== EventSource.OPEN)){
		if(cred.getValue())
			sseSetup(true);
	}
}, sseReconFreqMilliSec);

function sseMsgObjShow(val){
	let obj = val.dbsync;
	if(obj){
		el = document.getElementById("syncmsg");
		let btn = document.getElementById("syncbutton");
		if(obj.running){
			el.innerHTML = "Running. Remaining:"+obj.remaining+" Missing:"+obj.missing+" Updated:"+obj.updated+" Lost:"+obj.lost+" Found:"+obj.found+" Error:"+obj.error;
			btn.innerHTML = "Stop";
		}else{
			el.innerHTML = "Stopped. Remaining:"+obj.remaining+" Missing:"+obj.missing+" Updated:"+obj.updated+" Lost:"+obj.lost+" Found:"+obj.found+" Error:"+obj.error;
			btn.innerHTML = "Start";
		}
	}
	obj = val.dbsearch;
	if(obj){
		el = document.getElementById("crawlmsg");
		let btn = document.getElementById("crawlbutton");
		if(obj.running){
			el.innerHTML = "Running. Checked:"+obj.checked+" Found:"+obj.found+" Error:"+obj.error+" Current Path:"+obj.curPath;
			btn.innerHTML = "Stop";
		}else{
			el.innerHTML = "Stopped. Checked:"+obj.checked+" Found:"+obj.found+" Error:"+obj.error+" Current Path:"+obj.curPath;
			btn.innerHTML = "Start";
		}
	}
}

/* unregister to receive a event type */ 
function eventTypeUnreg(type){
	fetch("/sserem/"+type).then(response => {
		if(response.status == 200){
			// sucess...
			es.removeEventListener(type, sseListener);
			delete sseData[type];
		}
	});
}

/* register receiving of an event type */
function eventTypeReg(type, cb){
	fetch("/sseadd/"+type).then(response => {
		if(response.status == 200){
			// sucess...
			if(!sseData[type])
				sseData[type] = new watchableValue(false);
			let etype = sseData[type];
			etype.registerCallback(cb);
			addListenersForAllSubscriptions();
		}
	});
}

function sseListener(event) { // event data callback
	let etype;
	let idx = event.type.indexOf("vu_");
	if(idx == 0){
		let vu = buffToUvObj(toArrayBuffer(event.data));
		etype = sseData[event.type];
		if(etype){
			// because vu is an array, not a simple type, a old to new value
			// comparison is not valid for triggering a value change callback.
			// instead, we unconditionaly force a callback of all interested watchers.
			etype.setValue(vu, true); 
		}
	}else if(event.type === "msg"){
		let obj = JSON.parse(event.data);
		let cur = sseMsgObj.getValue();
		sseMsgObj.setValue({...cur, ...obj}, true);
	}else{
		etype = sseData[event.type];
		if(etype){
			let obj = JSON.parse(event.data);
			etype.setValue(obj, true);
		}
	}
};

/* add a handler for all evens the server has us listed to get 
		for this login session */ 
function addListenersForAllSubscriptions(){
	fetch("/sseget").then(response => {
		if((response.status >= 200) && (response.status < 400)){
			// sucess...
			response.json().then(data => {
				data.forEach(entry => {
					es.removeEventListener(entry, sseListener); // to prevent duplicate calls ifalready registered
					es.addEventListener(entry, sseListener);
				});
			});
		}
	});
}

/* VU meter data conversion */
function toArrayBuffer(hexStr){
	let buffer = new ArrayBuffer(Math.ceil(hexStr.length / 2));
	let view = new Uint8Array(buffer);
	for(let i = 0; i < view.byteLength; i++)
		view[i] = parseInt(hexStr.substr(i*2, 2), 16);
	return buffer;
}

function buffToUvObj(buffer){
	// vu object -> id: {pk: [], avr: []}
	let result = {};
	let dataView = new DataView(buffer);
	let offset = 1;
	recCnt = dataView.getUint8(0);
	for(let i = 0; i < recCnt; i++){
		let uid = dataView.getUint32(offset);
		offset += 4;
		let ccnt = dataView.getUint8(offset);
		offset++;
		let pk = [];
		let avr = [];
		for(let c = 0; c < ccnt; c++){
			pk.push(dataView.getUint8(offset));
			offset++;
			avr.push(dataView.getUint8(offset));
			offset++
		}
		result[uid] = {pk: pk, avr: avr};
	}
	return result;
}

/**** startup and load functions *****/

async function startupContent(){
	loadElement("nav", document.getElementById("navtab")).then((err) => {
		if(!err){
			checkLogin().then((res)=> {
				if(res){
					cred.setValue(res);
					showTabElement(document.getElementById('navabout'), 'about');
				}else{
					cred.setValue(false);
					showTabElement(document.getElementById('navlogin'), 'login');
				}
				// change file import visibility based on login status
				let el = document.getElementById('fileimportbox');
				getLocList();
				getCatList();
				getMediaLocs();
			});
		}
	});
	await loadElement("stadmin", document.getElementById("studioAdmin"));
	let el = document.getElementById("conCommand");
	if(el){
		el.addEventListener("keyup", function(event) {
			if(event.key === "Enter"){
				stConsSend();
			}
		});
	}
}

document.onclick = function(event){
	if(!document.getElementById("infopane").contains(event.target)){
		if(event.target.parentNode)
			closeInfo(event);
	}
};
        
window.onload = function(){
	locName.registerCallback(locMenuTrack);
	locName.registerCallback(refreshLogsLocationDep);
	locName.registerCallback(refreshSchedLocationDep);
	locName.registerCallback(refreshItemLocationDep);
	mediaListCache.registerCallback(mediaMenuRefresh);
	mediaListCache.registerCallback(fileReplaceDestRefresh);
	locListCache.registerCallback(locMenuRefresh);
	locListCache.registerCallback(refreshAddItemRest);
	catListCache.registerCallback(refreshAddItemCats);
	catListCache.registerCallback(refreshFileImportCats);
	catListCache.registerCallback(refreshTaskCats);
	artListCache.registerCallback(refreshItemArtists);
	albListCache.registerCallback(refreshItemAlbums);
	catListCache.registerCallback(refreshItemReassign);
	artListCache.registerCallback(refreshItemReassign);
	albListCache.registerCallback(refreshItemReassign);
	studioName.registerCallback(studioChangeCallback);
	cred.registerCallback(hideUnauthDiv);
	cred.registerCallback(sseSetup);
	browseType.registerCallback(browseTypeRowSelUpdate);
	sseMsgObj.registerCallback(sseMsgObjShow);
	startupContent();
	loadStashRecallOnLoad();
	pTemplate = document.querySelector("#playerTemplate");
}
