class watchableValue {
	constructor(value) {
		this.value = value;
		this.prior = null;
		this.cblist = [];
	}
	
	getValue() { return this.value; }
	
	getPrior() { return this.prior; }

	setValue(value, forceCallbacks) {
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

/***** Utilitu functions *****/

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

/***** Click event handlers *****/

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
		alert("login failed: "+err);
		return err;
	}
	if(response.ok)
		startupContent();
	else
		alert("login failed: try a different username and/or password");
	return false;
}

function closeInfo(event){
	let el = document.getElementById("infopane");
	el.style.width = "0px";
//!! handle stuff
}

function locMenuChange(event){
	let element = event.currentTarget;
	locName.setValue(element.value);
}

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

function browseCellClick(event){	// this is only the headers cells, for sorting
	if(browseSort === event.target.textContent)
		browseSort = "-"+event.target.textContent; // toggle: make decending
	else
		browseSort = event.target.textContent;
	browseQuery();
}

function editItem(evt, ref){
	evt.preventDefault();
	evt.stopPropagation();
	let i = evt.target.getAttribute("data-index");
	// use ref to determine what variable the index is in reference to.
	if(ref === "browse"){
		showItem(browseData[i], ref, true);
	}else if(ref.indexOf("conf") == 0){
		showItem(confData[ref][i], ref, true);
	}
}

function clickConfType(evt, type) {
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
		reloadConfSection(panel, type);
	}
}

/***** Variable change callbacks *****/

function testLog(value){
	console.log(value);
}

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
			return;
		}
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

function genPopulateTableFromArray(list, el, hideCols, rowClick, headClick, sortVar, actions, haction, fieldTypes){
	if(!list && !haction){
		el.innerHTML = "";
		return;
	}
	let cols = [];
	for(let i = 0; i < list.length; i++){
		for(let j in list[i]){
			if(cols.indexOf(j) === -1) {
				// Push all keys to the array
				if(!hideCols || !hideCols.includes(j))
					cols.push(j);
			}
		}
	}
	// Create a table element
	let table = document.createElement("table");
	table.className = "tableleftj";
	// Create table row tr element of a table
	let tr = table.insertRow(-1);
	for(let i = 0; i < cols.length; i++) {
		// Create the table header th element
		let theader = document.createElement("th");
		if(headClick){
			theader.className = "tselcell clickable";
			theader.onclick = headClick;
		}else
			theader.className = "tselcell";
		theader.innerHTML = cols[i];
		if(sortVar){
			//add sort direction icon
			if(cols[i] === sortVar)
				theader.innerHTML = "<i class='fa fa-sort-asc' aria-hidden='true'></i>" + theader.innerHTML;
			if(("-"+cols[i]) === sortVar)
				theader.innerHTML = "<i class='fa fa-sort-desc' aria-hidden='true'></i>" + theader.innerHTML;
		}
		// Append columnName to the table row
		tr.appendChild(theader);
	}
	if(actions || haction){
		// add heading/column for actions
		let theader = document.createElement("th");
		theader.className = "tselcell"; // non clickable
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
		trow.className = "tselrow";
		for(let j = 0; j < cols.length; j++){
			let cell = trow.insertCell(-1);
			// Inserting the cell at particular place
			let inner;
			if(fieldTypes && fieldTypes[cols[j]]){
				inner = fieldTypes[cols[j]];
				inner = inner.replaceAll("$val", list[i][cols[j]]);
				if(list[i].id)
					inner = inner.replaceAll("$id", list[i].id);
				else
					inner = inner.replaceAll("$id", "");
				if(inner.indexOf("$ifvalsel") > -1){
					let val = list[i][cols[j]];
					let parts = inner.split("$ifvalsel");
					for(let n = 0; n < parts.length; n++){
						if(parts[n].indexOf("'"+val+"'") > -1){
							parts[n] += "selected";
						}
					}
					inner = parts.join("");
				}
			}else if(cols[j] === "Duration")
				inner = timeFormat(list[i][cols[j]]);
			else
				inner = list[i][cols[j]];
			cell.innerHTML = inner;
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

/***** Item show/edit/delete functions *****/

async function showItem(props, ref, canEdit){
	if(props.tocID){
		// query API for item properties
		let response;
		let data = false;
		try{
			response = await fetch("library/item/"+props.tocID);
		}catch(err){
			alert("Got an error fetching data from server.\n"+err);
			return;
		}
		if(response.ok)
			data = await response.json();
		else{
			alert("Got an error fetching data from server.\n"+response.status);
			return;
		}
		
		let el = document.getElementById("infopane");
		let da = document.getElementById("infodata");
		let test = JSON.stringify(data);
		da.innerHTML = test;

		el.style.width = "300px"
	}else if(props.qtype && props.id){
		// use the properties already passed in props
		let el = document.getElementById("infopane");
		let da = document.getElementById("infodata");

		let test = JSON.stringify(props);
		da.innerHTML = test;

		el.style.width = "300px"
	}else if(ref.indexOf("conf") == 0){
		// use the properties already passed in props
		let el = document.getElementById("infopane");
		let da = document.getElementById("infodata");

		let test = JSON.stringify(props);
		da.innerHTML = test;

		el.style.width = "300px"
	}
}

function saveItem(props, canEdit){
	
}

function deleteItem(props, canEdit){
	
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
		alert("Got an error sending data to server.\n"+err);
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
				let hidden = ['id'];
				let actions = `<button class="editbutton" onclick="updateLoc(event)">Update</button>`;
				let haction = `<button class="editbutton" onclick="newLoc(event)">+</button>`;
				let fields = {Name: "<input type='text' name='Name' data-id='$id' value='$val' ></input>"};
				genPopulateTableFromArray(list, el, hidden, false, false, false, actions, haction, fields);
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
		let hidden = ['id'];
		let actions = `<button class="editbutton" onclick="updateLoc(event)">Update</button>
							<button class="editbutton" onclick="delLoc(event)">-</button>`;
		let haction = `<button class="editbutton" onclick="newLoc(event)">+</button>`;
		let fields = {Name: "<input type='text' name='Name' data-id='$id' value='$val' ></input>"};
		genPopulateTableFromArray(data, el, hidden, false, false, false, actions, haction, fields);
	}
}

/***** Settings specific functions *****/

function loadConfigTypeTable(el, type){
	let data = confData[type];
	let actions;
	let haction = false;
	let fields;
	let hidden = false;
	if(type === "confusers"){
		hidden = ["salt"];
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
	}else if(type === "confstudios"){
		actions = `<button class="editbutton" onclick="updateConf(event, '`+type+`')">Update</button>
						<button class="editbutton" onclick="delConf(event, '`+type+`')">-</button>`;
		haction = `<button class="editbutton" onclick="newConf(event, '`+type+`')">+</button>`;
		fields = {id: "<input type='text' name='id' value='$val'/>", 
					host: "<input type='text' name='host' value='$val'></input>",
					port: "<input type='text' name='port' value='$val'></input>",
					run: "<input type='text' name='run' value='$val'></input>",
					minpool: "<input type='number' min='1' max='8' name='minpool' value='$val'></input>",
					maxpool: "<input type='number' min='1' max='8' name='maxpool' value='$val'></input>"};
	}else{
		actions = `<button class="editbutton" onclick="updateConf(event, '`+type+`')">Update</button>`;
		fields = {id: "<input type='hidden' name='id' value='$val'/>$val", value: "<input type='text' name='value' value='$val'></input>"};
	}
	genPopulateTableFromArray(data, el, hidden, false, false, false, actions, haction, fields);
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
				alert("Got an error sending data to server.\n"+err);
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
		alert("Got an error sending data to server.\n"+err);
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
		alert("Got an error from the server.\n"+err);
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
					genPopulateTableFromArray(data, document.getElementById("btype"), ["qtype"], browseTypeRowClick);
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
						if(data.length && !data[0].tocID && data[0].id){
							// $i will be replaced by the row index number by the genPopulateTableFromArray() function
							actions = `<button class="editbutton" data-index="$i" onclick="editItem(event, 'browse')">Edit</button>`;
							hactions = `<button class="editbutton" >+</button>`;
						}else if(data.length && ['title', 'playlist', 'task'].includes(data[0].qtype)){
							hactions = `<button class="editbutton" >+</button>`;
						}
					}
					genPopulateTableFromArray(data, document.getElementById("bres"), ["id", "qtype", "tocID"], browseRowClick, browseCellClick, browseSort, actions, hactions);
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
console.log("ssesetup");
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
			});
		}
	});
}

window.onload = function(){
	locName.registerCallback(locMenuTrack);
	studioName.registerCallback(studioChangeCallback);
	cred.registerCallback(hideUnauthDiv);
	cred.registerCallback(sseSetup);
	browseType.registerCallback(browseTypeRowSelUpdate);
	sseMsgObj.registerCallback(sseMsgObjShow);
	startupContent();
}
