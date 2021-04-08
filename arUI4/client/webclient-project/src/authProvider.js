var auth = {
	conf: {},
	
	// called when the user attempts to log in
	login: ({ username, password }) => {
		const request = new Request('auth', {
			method: 'POST',
			body: JSON.stringify({ username, password }),
			headers: new Headers({ 'Content-Type': 'application/json' }),
		});
		return fetch(request).then(response => {
			if((response.status < 200) || (response.status >= 300)){
				throw new Error(response.statusText);
			}
			return response.json();
		}).then(obj => {
			auth.conf = obj;
			return Promise.resolve();
		});
	},
	// called when the user clicks on the logout button
	logout: () => {
		return fetch('/unauth').then(response => {
			auth.conf = {};
			return Promise.resolve();
		}).catch(err => {
			auth.conf = {};
			return Promise.resolve();
		});
	},
	// called when the API returns an error
	checkError: ({ status }) => {
		if(status === 401 || status === 403) {
			auth.conf = {};
			return Promise.reject();
		}
		return Promise.resolve();
	},
	// called when the user navigates to a new location, to check for authentication
	checkAuth: () => {
		if(auth.conf.username){
			return Promise.resolve();
		}else{
			return Promise.reject();
		}
	},
	// called when the user navigates to a new location, to check for permissions / roles
	getPermissions: () => {
		if(auth.conf.permission){
			return Promise.resolve(auth.conf.permission);
		}else{
			return Promise.reject();
		}
	},
	
	getIdentity: () => {
		if(auth.conf.username){
			return Promise.resolve({fullName: auth.conf.username});
		}else{
			return Promise.reject();
		}
	},
	
};

export default auth;
