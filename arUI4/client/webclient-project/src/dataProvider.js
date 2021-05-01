import { fetchUtils } from 'react-admin';
const httpClient = fetchUtils.fetchJson;
const apiUrl = '';

var dataProv = {
	locName: "",
	
	delParams: {},
	
	getList: (resource, params) => {
		let query = {};
		let dirs = resource.split('/');
		if(dirs[0] == "config"){	// /config
			dirs[0] = 'getconf';
		}else if(dirs[1] && (dirs[0] == "library")){
			if(dirs[1] == "browse"){
				query = {type: 'artist'};
			}else if(dirs[1] == "logs"){
				params.location = dataProv.locName;
			}else if(dirs[1] == "sched"){
				params.location = dataProv.locName;
			}else{ // /library/table
				dirs[0] = 'library/get';
			}
		}else{
			// bad apiFuction
			return new Promise((resolve, reject) => { reject({status: 400, message: "Bad Request: invalid API function"}) });
		}
		
		// note: filter and sort are ignored by config API
		if(params.filter){
			if(params.filter.q){	// convert q to API match format for browse
				let match = "%"+params.filter.q+"%";
				delete params.filter.q;
				query = {...query, match: match};
			}
			// column1: value1, ...
			query = {...query, ...params.filter};
		}else
			query = {};
		if(params.pagination){
			// my API range:[first, last]
			const { page, perPage } = params.pagination;
			let range = [(page - 1) * perPage, page * perPage - 1];
			query = { ...query, range: range};
		}
		if(params.sort){
			// my API sortBy: sort columns, comma separated list, prepended with "-" for desending order
			const { field, order } = params.sort;
			let sortBy = field;
			if(order == 'DESC')
				sortBy = '-'+sortBy;
			query = { ...query, sortBy: sortBy};
		}
		
		let url = apiUrl + dirs.join('/');
		return httpClient(url, {
			method: 'POST',
			body: JSON.stringify(query)
		}).then(({ headers, json }) => ({
			data: json,
			total: parseInt(headers.get('content-range').split('/').pop(), 10)
		}));
	},

	getOne: (resource, params) => {
		let dirs = resource.split('/');
		let respar = {};
		if(dirs[1] && (dirs[0] == "config"))	// /config
			dirs[0] = 'getconf';
		else if(dirs[1] && dirs[0] == "library"){
			if(dirs[1] == "item")
				dirs[0] = 'library/item';
			else if(dirs[1] == "resolve"){
				dirs[0] = 'library/item';
				respar = { resolve: 1 };
			}else
				dirs[0] = 'library/get';
		}else{
			// bad apiFuction
			return new Promise((resolve, reject) => { reject({status: 400, message: "Bad Request: invalid API function"}) });
		}
		dirs.push(params.id);
		let url = apiUrl + dirs.join('/');
		return httpClient(url, {
			method: 'POST',
			body: JSON.stringify(respar)
		}).then(({ headers, json }) => ({
			data: json[0]
		}));
	},

	getMany: (resource, params) => {
		let dirs = resource.split('/');
		let respar = {};
		if(dirs[1] && (dirs[0] == "config"))	// /config
			dirs[0] = 'getconf';
		else if(dirs[1] && dirs[0] == "library"){
			if(dirs[1] == "item")
				dirs[0] = 'library/item';
			else if(dirs[1] == "resolve"){
				dirs[0] = 'library/item';
				respar = { resolve: 1 };
			}else
				dirs[0] = 'library/get';
		}else{
			// bad apiFuction
			return new Promise((resolve, reject) => { reject({status: 400, message: "Bad Request: invalid API function"}) });
		}
		let promises = params.ids.map( (item) => {
			let url = apiUrl + dirs.join('/') + '/' + item;
			return httpClient(url, {
				method: 'POST',
				body: JSON.stringify(respar)
			}).then(({ headers, json }) => (json[0]));
		});
		
		return Promise.all(promises).then(({results}) => ({ data: results }));
	},

	getManyReference: (resource, params) => {
		let filter = {...params.filter};
		filter[params.target] = params.id;
		let passParam = { 
			pagination: params.pagination, 
			sort: params.sort,
			filter: filter
		}
		return dataProv.getList(resource, passParam);
	},

	update: (resource, params) => {
		let dirs = resource.split('/');
		if(dirs[1] && (dirs[0] == "config")){	// /config
			dirs[0] = 'setconf';
		}else if(dirs[1] && (dirs[0] == "library")){
			dirs[0] = 'library/set';
		}else{
			// bad apiFuction
			return new Promise((resolve, reject) => { reject({status: 400, message: "Bad Request: invalid API function"}) });
		}
		delete params.data.id;
		
		dirs.push(params.id);
		let url = apiUrl + dirs.join('/');
		return httpClient(url, {
			method: 'POST',
			body: JSON.stringify(params.data)
		}).then(({ headers, json }) => ({
			data: { id:params.id, ...params.data }
		}));
	},

	updateMany: (resource, params) => {
		let dirs = resource.split('/');
		if(dirs[1] && (dirs[0] == "config")){	// /config
			dirs[0] = 'setconf';
		}else if(dirs[1] && (dirs[0] == "library")){
			dirs[0] = 'library/set';
		}else{
			// bad apiFuction
			return new Promise((resolve, reject) => { reject({status: 400, message: "Bad Request: invalid API function"}) });
		}
		delete params.data.id;
		
		let promises = params.ids.map( (item) => {
			let url = apiUrl + dirs.join('/') + '/' + item;
			return httpClient(url, {
				method: 'POST',
				body: JSON.stringify(params.data)
			}).then(() => (item));
		});
		
		return Promise.all(promises).then(({results}) => ({ data: results }));
	},

	create: (resource, params) => {
		let dirs = resource.split('/');
		if(dirs[1] && (dirs[0] == "config")){	// /config
			// Note: This is the same as update... the API creates if the object doesn't exist
			dirs[0] = 'setconf';
			dirs.push(params.data.id); // an id MUST be specified in the data for the new entry/object
			let theID = params.data.id;
			delete params.data.id;	// make sure no id is specified, to prevent an update of existing record.
			let url = apiUrl + dirs.join('/');
			return httpClient(url, {
				method: 'POST',
				body: JSON.stringify(params.data)
			}).then(({ headers, json }) => ({
				data: { id: theID, ...params.data }
			}));
		}else if(dirs[1] && (dirs[0] == "library")){
			// This is NOT that same as the update API.  No ID is specified and a new row is created.
			dirs[0] = 'library/set';
			delete params.data.id;	// make sure no id is specified, to prevent an update of existing record.
			let url = apiUrl + dirs.join('/');
			return httpClient(url, {
				method: 'POST',
				body: JSON.stringify(params.data)
			}).then(({ headers, json }) => ({
				// this is for config only, where an id is always specified.
				data: { id: json.insertId, ...params.data }
			}));
		}else{
			// bad apiFuction
			return new Promise((resolve, reject) => { reject({status: 400, message: "Bad Request: invalid API function"}) });
		}
	},

	delete: (resource, params) => {
		// must call setDelParams() just before this to pass extra paramteres, like reassign, delete (file), etc.
		let dirs = resource.split('/');
		if(dirs[1] && (dirs[0] == "config")){	// /config
			dirs[0] = 'delconf';
		}else if(dirs[1] && (dirs[0] == "library")){
			dirs[0] = 'library/deleteID';
		}else{
			// bad apiFuction
			return new Promise((resolve, reject) => { reject({status: 400, message: "Bad Request: invalid API function"}) });
		}
		dirs.push(params.id);
		let url = apiUrl + dirs.join('/');
		return httpClient(url, {
			method: 'POST',
			body: JSON.stringify(dataProv.delParams)
		}).then(() => {
			dataProv.delParams = {};
			return { data: { id: params.id, ...params.previousData } };
		});
	},

	deleteMany: (resource, params) => {
		// must call setDelParams() just before this to pass extra paramteres, like reassign, delete (file), etc.
		let dirs = resource.split('/');
		if(dirs[1] && (dirs[0] == "config")){	// /config
			dirs[0] = 'delconf';
		}else if(dirs[1] && (dirs[0] == "library")){
			dirs[0] = 'library/deleteID';
		}else{
			// bad apiFuction
			return new Promise((resolve, reject) => { reject({status: 400, message: "Bad Request: invalid API function"}) });
		}

		let promises = params.ids.map( (item) => {
			let url = apiUrl + dirs.join('/') + '/' + item;
			return httpClient(url, {
				method: 'POST',
				body: JSON.stringify(dataProv.delParams)
			}).then(() => (item));
		});
		
		return Promise.all(promises).then((results) => { 
			dataProv.delParams = {};
			return { data: results }; 
		});		
	},
	
	genPasswordHash: (resource, params) => {
		return httpClient("/genpass", {
			method: 'POST',
			body: JSON.stringify({password: params.password})
		}).then(({ json }) => ({
			password: json.hash,
			salt: json.salt
		}));
	},
	
	// custom function to set the default automation location ID used by API queries
	setLoc: (locName) => { dataProv.locName = locName; },
	
	// custom function to set extra parameters for deleting library items, such as 
	setDelParams: (paramObj) => { dataProv.delParams = paramObj }
};

export default dataProv;
