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
var cred = new watchableValue(false);
var locName = new watchableValue(false);
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
var itemProps = false;
var catListCache = new watchableValue(false);
var locListCache = new watchableValue(false);
var artListCache = new watchableValue(false);
var albListCache = new watchableValue(false);

/***** Utility functions *****/

function quoteattr(s){
	if(s){
		return new Option(s).innerHTML;
/*
		 preserveCR = preserveCR ? '&#13;' : '\n';
		 return ('' + s) // Forces the conversion to string.
			.replace(/&/g, '&amp;') // This MUST be the 1st replacement.
			.replace(/'/g, '&apos;') // The 4 other predefined entities, required.
			.replace(/"/g, '&quot;')
			.replace(/</g, '&lt;')
			.replace(/>/g, '&gt;')
			.replace(/\r\n/g, preserveCR) // Must be before the next replacement.
			.replace(/[\r\n]/g, preserveCR);
*/
	}else
		return "";
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

function timeFormat(timesec){
	if(isNaN(timesec))
		return "";
	else{
		let negative = false;
		if(timesec < 0){
			timesec = -timesec;
			negative = true;
		}
		let hrs = Math.floor(timesec / 3600);
		let rem = timesec - hrs * 3600;
		let mins = Math.floor(rem / 60);
		rem = rem - mins * 60;
		let secs = Math.floor(rem);
		let frac = Math.floor((rem - secs) * 10);
		let result = "";
		if(negative)
			result = "-";
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
		result += "." + frac;
		return result;
	}
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

/***** fetch functions from http API *****/

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
	return false;
}

async function loadElement(url, element){
	let resp = await fetchContent(url);
	if(resp instanceof Response){
		if(resp.ok){
			element.innerHTML = await resp.text();
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
			alert("Got an error fetching categories from server.\n"+resp.status);
		}
	}else{
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
			alert("Got an error fetching artists from server.\n"+resp.status);
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
			alert("Got an error fetching albums from server.\n"+resp.status);
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
				alert("Got an error fetching category name from server.\n"+resp.status);
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
		}else{
			alert("Got an error fetching categories from server.\n"+resp.status);
		}
	}else{
		alert("Failed to fetch categories from the server.");
	}
}

/***** Navigation  Functions *****/

function dropClick(evt){
	let id = evt.currentTarget.getAttribute("data-childdiv");
	let content = document.getElementById(id);
	if(content.style.display === "block"){
		content.style.display = "none";
	}else{
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

function toggleShowSearchList(evt){ ; 
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
	//		<div>		List contents el - generate a div for each liste entry inside, with data-id is set 
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
		let inner = "<option val='' onClick='getLocList()'>Reload List</option>";
		for(let i=0; i < value.length; i++)
			inner += "<option val="+value[i].Name+">"+value[i].Name+"</option>";
		element.innerHTML = inner;
	}
}

function fileCatMenuRefresh(value){
	// this function updates the location selection menu list
	// when the locListCache variable changes
	let element = document.getElementById("selloc");
	if(element && value){
		let inner = "<option val='' onClick='getLocList()'>Reload List</option>";
		for(let i=0; i < value.length; i++)
			inner += "<option val="+value[i].Name+">"+value[i].Name+"</option>";
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
	browseQuery();
}

/***** HTML manipulation functions *****/

function includeScript(file) {
	let script  = document.createElement('script');
	script.src  = file;
	script.type = 'text/javascript';
	script.defer = true;
	document.getElementsByTagName('head').item(0).appendChild(script);
}

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
	if(pass !== undefined)
		studioName.setValue(pass);
	if((id === "browse") && !browseData)
		browseQuery();
	if(id === "libmanage")
		loadLocationMgtTbl();
	if((id === "files") && (filesPath === false))
		loadFilesTbl();
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

function genPopulateTableFromArray(list, el, colMap, rowClick, headClick, sortVar, actions, haction, fieldTypes, colWidth){
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
						inner = fieldTypes[cols[j]](val, list[i]);
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
				cell.innerHTML = newac;
			}else
				cell.innerHTML = "";
		}
	}
	// Add the newely created table to the specified <div>
	el.innerHTML = "";
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
				inner = fieldTypes[cols[i]](val);
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
				inner = inner.replaceAll("$i", i);
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
			cell.innerHTML = newac;
		}else
			cell.innerHTML = "";
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
			alert("Got an error fetching live list from server.\n"+resp.status);
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
	el.style.width = "400px";
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
				else
					alert("Item has been updated.");
			}else{
				alert("Got an error saving data to server.\n"+resp.status);
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
				alert("Got an error saving data to server.\n"+resp.status);
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
		entry = {Property: "Path", Value: entry};
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
						alert("Got an error saving data to server.\n"+resp.status);
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
					alert("Got an error saving task to server.\n"+resp.status);
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
	let els = document.getElementById("playlistitemform").elements;
//!! handle new item too
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
						alert("Got an error saving data to server.\n"+resp.status);
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
					alert("Got an error saving category to server.\n"+resp.status);
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
						alert("Got an error saving data to server.\n"+resp.status);
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
					alert("Got an error saving custom properties to server.\n"+resp.status);
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
						alert("Got an error saving data to server.\n"+resp.status);
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
					alert("Got an error saving rest location to server.\n"+resp.status);
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
						alert("Got an error saving data to server.\n"+resp.status);
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
								Fill: (cols[5].firstElementChild.checked?1:0),
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
						alert("Got an error retreaving location ID from server.\n"+resp.status);
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
					alert("Got an error saving rest location to server.\n"+resp.status);
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
				alert("Got an error saving data to server.\n"+resp.status);
			}
		}else{
			alert("Failed to save data to the server.");
		}
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
					alert("Got an error deleteing item from server.\n"+resp.status);
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
				alert("Got an error retreaving file from server.\n"+resp.status);
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
						Fill: "<input type='checkbox' name='fill' $ifvalchk>",
						Priority: itemPrioRender};
	insertTableRow(newRow, div, 1, {Day: "Day", Date: "Date", Month: "Month", Hour: "Hour", Minute: "Minute", Fill: "Fill", Priority: "Priority", RID: false}, false, actions, fields);
}

function itemChangeArtist(evt){
	// close search-list menu
	let el = document.getElementById("itemArtistBtn");
	toggleShowSearchList({target: el});
	el.innerText = evt.target.innerText;
	let id = evt.target.getAttribute("data-id");
	el.setAttribute("data-id", id);
}

function itemChangeAlbum(evt){
	// close search-list menu
	let el = document.getElementById("itemAlbumBtn");
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
						msg += "\n"+resp.status;
					alert(msg);
				}
				evt.target.disabled = false;
			}else{
				alert("Failed to get uploaded file name back from the server.");
				evt.target.disabled = false;
			}
		}else{
			alert("Got an error posting data to server.\n"+resp.status);
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
console.log(target);
console.log(ctl);
console.log(nom);
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
			alert("Got an error fetching hash from server.\n"+resp.status);
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
					buildSearchList(div, catListCache.getValue());
				else if(itemProps.Type === "artist")
					buildSearchList(div, artListCache.getValue());
				else if(itemProps.Type === "album")
					buildSearchList(div, albListCache.getValue());
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
				alert("Got an error fetching data from server.\n"+resp.status);
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
		let inner = "<form id='genitemform'> ID: "+itemProps.ID+"<br>";
		
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
		if(itemProps.canEdit)
			inner += "<p><button id='savegenbut' name='submit' onclick='saveItemGeneral(event)'>Save General Properties</button>";
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
			var tomor = new Date(Date.now() + 86400000);
			let dd = String(tomor.getDate()).padStart(2, '0');
			let mm = String(tomor.getMonth() + 1).padStart(2, '0');
			let yyyy = tomor.getFullYear();
			tomor = yyyy + '-' + mm + '-' + dd;
			histdateVal = tomor;
		}
		if(itemProps.history){
			let inner = "<form id='histitemform'>";
			inner += "Before date: <input type='date' id='histdatesel' name='histdate' value='"+histdateVal+"' onchange='refreshItemHistory()'></input>";
			inner += "<input type='number' id='histlimsel' name='histlimit' onchange='refreshItemHistory()' value='"+histlimitVal+"' max='210' min='10' step='50'></input>"
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
			el.innerHTML = inner + "</form>";
			let div = document.getElementById("itemschedlist");
			let fields = {	Day: itemWDOMRender, // Weekday of Month
								Date: itemDateRender, 
								Month: itemMonRender,
								Hour: itemHrRender,
								Minute: "<input type='number' min='0' max='59' name='min' value='$val' data-rid='$RID'></input>", // number
								Fill: "<input type='checkbox' name='fill' $ifvalchk>",
								Priority: itemPrioRender};
			let colWidth = {action:"18px", Fill:"20px"};
			genPopulateTableFromArray(itemProps.sched, div, {Day: "Day", Date: "Date", Month: "Month", Hour: "Hour", Minute: "Minute", Fill: "Fill", Priority: "Priority", RID: false}, false, false, false, actions, haction, fields, colWidth);
		}else{
			el.innerHTML = inner + "</form>";
			let div = document.getElementById("itemschedlist");
			div.innerHTML = "Please select a Library Location for which to get the schedule";
		}
	}else if(type == "file"){
		let inner = "";
		if(parseInt(itemProps.file.Missing))
			inner += "<strong><center>File MISSING</center></strong>";
		else
			inner += "<strong><center>File Good</center></strong>";
		inner += `<table class="tableleftj" stype="overflow-wrap: break-word;">`;
		
		inner += "<tr><td width='15%'>Artist</td><td>";
		if(itemProps.canEdit){
			inner += `<button class="editbutton" id='itemArtistBtn' data-id="`;
			inner += itemProps.file.ArtistID + `" onclick="toggleShowSearchList(event)">`;
			inner += quoteattr(itemProps.file.Artist) + `</button>
								<div class="search-list">
									<button class="editbutton" onclick="refreshItemArtists(event)">Refresh List</button><br>
									<input id="itemArtistText" type="text" onkeyup="filterSearchList(event)" data-div="itemArtistBtn" data-removecb="unlistAlreadyInnerText" placeholder="Enter Search..."></input>
									<div id="itemArtistList"></div>
								</div>`;
		}else{
			inner += quoteattr(itemProps.file.Artist);
		}
		inner += "</td>";

		inner += "<tr><td>Album</td><td>";
		if(itemProps.canEdit){
			inner += `<button class="editbutton" id='itemAlbumBtn' data-id="`;
			inner += itemProps.file.AlbumID + `" onclick="toggleShowSearchList(event)">`;
			inner += quoteattr(itemProps.file.Album) + `</button>
								<div class="search-list">
									<button class="editbutton" onclick="refreshItemAlbums(event)">Refresh List</button><br>
									<input id="itemAlbumText" type="text" onkeyup="filterSearchList(event)" data-div="itemAlbumBtn" data-removecb="unlistAlreadyInnerText" placeholder="Enter Search..."></input>
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
		inner += "<tr><td>Volume</td><td>";

		inner += "<input type='range' name='Volume' min='-60' max='20' oninput='itemVolChange(this)' value='"+vol+"'>";
		inner += "<div style='float:right;'>"+vol+" dB</div>";
		inner += "</td></table></form>";

		inner += `<table class="tableleftj" stype="overflow-wrap: break-word;"><form id='fileitemform'>`;
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
			inner += `<form id="replaceform" enctype="multipart/form-data">
							Replace <input type="file" id="replaceinput" class="editbutton" name="filestoupload" onchange="itemReplace(event)">
						</form>`;
			inner += "<p><button id='savefilebut' onclick='saveItemFile(event)'>Save File Properties</button>";

		}
		el.innerHTML = inner;
		let div = document.getElementById("itemArtistList");
		if(div && artListCache.getValue())
			buildSearchList(div, artListCache.getValue(), itemChangeArtist);
		div = document.getElementById("itemAlbumList");
		if(div && albListCache.getValue())
			buildSearchList(div, albListCache.getValue(), itemChangeAlbum);
			
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
		let inner = "<form id='playlistitemform'>";
		inner += "Name: <input type='text' name='Name'";
		inner += " value='"+quoteattr(itemProps.Name)+"'";
		if(itemProps.canEdit)
			inner += "></input><br>";
		else
			inner += " readonly></input><br>";
			
		inner += "Duration: <input type='text' name='Duration'";
		inner += " value='"+timeFormat(itemProps.Duration)+"'";
		
		if(itemProps.Type === "task")		// only tasks have editable durations
			inner += "></input><br>";
		else
			inner += " readonly></input><br>";
		
		inner += "Tag: <input type='text' name='Tag'";
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
		if(itemProps.canEdit)
			inner += "<button id='savegenbut' name='submit' onclick='saveItemPlaylist(event)'>Save Playlist Properties</button>";
		el.innerHTML = inner + "</form>";
	}
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
		wk = parseInt((wk-1) / 7)+1; // 1 thru 5
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
	inner += "<option value='1' "+(wk===1?"selected":"")+">1st</option>";
	inner += "<option value='2' "+(wk===2?"selected":"")+">2nd</option>";
	inner += "<option value='3' "+(wk===3?"selected":"")+">3rd</option>";
	inner += "<option value='4' "+(wk===4?"selected":"")+">4th</option>";
	inner += "<option value='5' "+(wk===5?"selected":"")+">5th</option>";
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
	panel.style.width = "400px";
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
	let inner = "";
	if(itemProps.Type === "file")
		inner += "<audio controls id='itemcueplayer' width='100%'><source id='itemcuesource' src='library/download/"+itemProps.ID+"''>Your browser does not support the audio tag.</audio>";

	inner += `<button class="accordion" id="genbut" onclick="selectAccordType(event, reloadItemSection, 'general')">General</button>
	<div class="accpanel">
	</div>`;
	inner += `<button class="accordion" onclick="selectAccordType(event, reloadItemSection, '`
	inner += itemProps.Type+`')">`;
	inner += itemProps.Type;
	inner += `</button>
	<div class="accpanel">
	</div>
	<button class="accordion" onclick="selectAccordType(event, reloadItemSection, 'categories')">Categories</button>
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
	if(itemProps.canEdit){
		inner += "<p><button id='delitembut' onclick='itemDelete(event)'>Delete Item</button>";
		if(itemProps.Type === "file"){
			inner += " Delete file too: <input type='checkbox' id='delfiletoo' checked></input>";
			inner += `<p><button id='expitembut' onclick='itemExport(event)'>Download</button>
						<select id='downloadtype'>
							<option value='file' $ifvalsel>Audio File</option>
							<option value='json' $ifvalsel>jSON File</option>
						</select>`;
		}else if(itemProps.Type === "playlist")
			inner += `<p><button id='expitembut' onclick='itemExport(event)'>Download</button>
						<select id='downloadtype'>
							<option value='fpl' $ifvalsel>Audiorack FPL file</option>
							<option value='fplmedia' $ifvalsel>Audiorack FPL file with media folder</option>
							<option value='cue' $ifvalsel>Cuesheet file</option>
							<option value='json' $ifvalsel>jSON File</option>
						</select><br>
						Add offset (seconds) to times: <input type='text' id='itemtimeoffset' size='5' value='0'></input>`;
		else
			inner += `<p><button id='expitembut' onclick='itemExport(event)'>Download</button> jSON File`;
		inner += `<a id="filedltarget" style="display: none"></a>`;
	}
	container.innerHTML = inner;
	panel.style.width = "400px";
	let el = document.getElementById("showinfobtn");
	if(el)
		el.style.display = "none";
	let genbut = document.getElementById("genbut");
	selectAccordType({target: genbut}, reloadItemSection, 'general')
}

async function itemFetchProps(id){
	let loc = locName.getValue();
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
		alert("Got an error fetching data from server.\n"+response.status);
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
		alert("Got an error fetching metadata from server.\n"+response.status);
		return false;
	}
}

async function showItem(props, canEdit, noShow){
	if(props.tocID){
		// query API for item properties
		let response;
		let data = false;
		data = await itemFetchProps(props.tocID);
		if(data){
			itemProps = data;
			itemProps.canEdit = canEdit;
			if(noShow)
				return;
			let el = document.getElementById("infopane");
			let da = document.getElementById("infodata");
			showTocItem(el, da);
		}
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
			el.style.width = "400px"
			el = document.getElementById("showinfobtn");
			if(el)
				el.style.display = "none";
		}else{
			// new item
			if(["artist", "album", "category"].includes(type)){
				// create a new property
				data = {ID: 0, Name: "new "+type, Type: type, meta: []};
				itemProps = data;
				itemProps.canEdit = canEdit;
				if(noShow)
					return;
				showPropItem(el, da);
				itemProps.Name = ""; // change name to make it save the default name
				el.style.width = "400px"
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
				el.style.width = "400px"
				el = document.getElementById("showinfobtn");
				if(el)
					el.style.display = "none";
			}
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
				alert("Got an error from server.\n"+resp.status);
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
				alert("Got an error from server.\n"+resp.status);
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
				alert("Got an error from server.\n"+resp.status);
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
			if(item.type === "radio"){
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
					alert("Got an error fetching data from server.\n"+resp.status);
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
				alert("Got an error from server.\n"+resp.status);
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
				alert("Got an error fetching data from server.\n"+resp.status);
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
			alert("Got an error fetching data from server.\n"+resp.status);
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
		alert("response code from server.\n"+response.status);
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
			alert("Got an error fetching data from server.\n"+resp.status);
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
		actions = `<button class="editbutton" onclick="updateConf(event, '`+type+`')">Update</button>
						<button class="editbutton" onclick="delConf(event, '`+type+`')">-</button>`;
		haction = `<button class="editbutton" onclick="newConf(event, '`+type+`')">+</button>`;
		fields = {id: "<input type='text' size='8' name='id' value='$val'/>", 
					host: "<input type='text' name='host' value='$val'></input>",
					port: "<input type='text' size='2' name='port' value='$val'></input>",
					run: "<input type='text' name='run' value='$val'></input>",
					minpool: "<input type='number' min='1' max='8' name='minpool' value='$val'></input>",
					maxpool: "<input type='number' min='1' max='8' name='maxpool' value='$val'></input>"};
		colWidth.minpool = "65px";
		colWidth.maxpool = "65px";
		colWidth.port = "65px";
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
			alert("Got an error fetching data from server.\n"+resp.status);
		else
			alert("Failed to fetch data from the server.");  
	});
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
				alert("response code from server.\n"+response.status);
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
		props.minpool = 2;
		props.maxpool = 5;
	}else if(type === "confusers"){
		props.password = "";
		props.permission = "";
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
		alert("Bad status code from server.\n"+response.status);
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
	let i = event.originalTarget.parentElement.rowIndex-1; // -1 due to header row
	let record = browseTypeList[i];
	browseType.setValue(record.qtype);
}

function browseRowClick(event){
	let i = event.originalTarget.parentElement.rowIndex-1; // -1 due to header row
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
					genPopulateTableFromArray(data, document.getElementById("bres"), {id: false, qtype: false, tocID: false}, browseRowClick, browseCellClick, browseSort, actions, hactions, false, colWidth);
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
	for(let i = 0; i < parts.length; i++){
		if(i)
			inner += " / <button class='editbutton' data-index='"+i+"' onclick='refreshFiles(event)'>"+parts[i]+"</button>";
	}
	el.innerHTML = inner;
}

async function refreshFiles(evt){
	if(evt){
		evt.preventDefault();
		let i = evt.target.getAttribute("data-index");
		let parts = filesPath.split("/");
		parts = parts.slice(0, i+1);
		filesPath = parts.join("/");
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
			let name = row.childNodes[1].firstChild.nodeValue;	// name column
			// directory row
			filesPath += "/" + name;
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
			let hidden = {isDir: "Select", id: "Name", created: false, modified: false, size: "Size (kiB)"};
			let haction = `<button class="editbutton" onclick="selectAllFiles(event)">Select All</button>
								<button class="editbutton" onclick="unselectAllFiles(event)">Unselect All</button>`;
			let fields = {id: "<input type='hidden' name='Name' data-id='$id' data-index='$i'></input>$val",
								isDir: "$iftrue<i class='fa fa-folder-open' aria-hidden='true'>$iftrue$iffalse<input type='checkbox'/>$iffalse"};
			let colWidth = {action:"100px", size:"90px", isDir:"40px"};
			genPopulateTableFromArray(list, el, hidden, fileRowClick, false, false, false, haction, fields, colWidth);
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

function studioChangeCallback(value){
	// clear existing VU meters
	let busvu = document.getElementById('busvu');
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
}

function studioHandleNotice(data){
console.log(data);
}

function studioVuUpdate(data){
	if(!data)
		return;
	// update mix bus meters
	let canv;
	let busvu = document.getElementById('busvu');
	if(data[0]){
		if(!busvu.firstChild){
			// first data for new vu session... create view
			for(let i = 0; i < data[0].pk.length; i++){
				canv = document.createElement("canvas");
				canv.setAttribute('id', 'bus'+i);
				canv.setAttribute('width',10);
				canv.setAttribute('height',128);
				vumeter(canv, {
					"boxCount": 32,
					"boxCountRed": 9,
					"boxCountYellow": 7,
					"boxGapFraction": 0.25,
					"max": 255,
				});
				busvu.appendChild(canv);
			}
		}
		// update meter displays with new values
		let id;
		for(let c = 0; c < data[0].pk.length; c++){
			id = "ch"+c;
			canv = document.getElementById('bus'+c);
			if(canv){
				canv.setAttribute('data-avr', data[0].avr[c]);
				canv.setAttribute('data-pk', data[0].pk[c]);
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
	let data = JSON.stringify(val);
	let el = document.getElementById("ssemsg"); 
	el.innerHTML ="Server Msg: "+data;
	
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
			etype.setValue(event.data);
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

function startupContent(){
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
				// change file import visibility based on ligin status
				let el = document.getElementById('fileimportbox');
				getLocList();
				getCatList();
			});
		}
	});
}

window.onload = function(){
	locName.registerCallback(locMenuTrack);
	locName.registerCallback(refreshItemLocationDep);
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
}
