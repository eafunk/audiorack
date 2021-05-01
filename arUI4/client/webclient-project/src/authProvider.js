var auth = {
	cred: {},
	
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
			auth.cred = obj;
			return Promise.resolve();
		});
	},
	
	// called when the user clicks on the logout button
	logout: () => {
		return fetch('/unauth').then(response => {
			auth.cred = {};
			return Promise.resolve();
		}).catch(err => {
			auth.cred = {};
			return Promise.resolve();
		});
	},
	
	// called when the API returns an error
	checkError: ({ status }) => {
	if(status === 401 || status === 403) {
			auth.cred = {};
			return Promise.reject();
		}
		return Promise.resolve();
	},
	
	// called when the user navigates to a new location, to check for authentication
	checkAuth: () => {
		if(auth.cred.username){
			return Promise.resolve();
		}else{
			// check with the uiserver, to allow login servival after javascript app reload
			return auth.checkWho().then(() => {
				return Promise.resolve();
			}).catch(() => {
				return Promise.reject();
			});
		}
	},
	
	// called when the user navigates to a new location, to check for permissions / roles
	getPermissions: () => {
		if(auth.cred.permission){
			return Promise.resolve(auth.cred.permission);
		}else{
			// check with the uiserver, to allow login servival after javascript app reload
			return auth.checkWho().then(() => {
				return Promise.resolve(auth.cred.permission);
			}).catch(() => {
				return Promise.reject();
			});
		}
	},
	
	getIdentity: () => {
		if(auth.cred.username){
			return Promise.resolve({fullName: auth.cred.username});
		}else{
			// check with the uiserver, to allow login servival after javascript app reload
			return auth.checkWho().then(() => {
				return Promise.resolve({fullName: auth.cred.username});
			}).catch(() => {
				return Promise.reject();
			});
		}
	},
	
	checkWho: () => {
		return fetch('who').then(response => {
			if((response.status < 200) || (response.status >= 300)){
				auth.cred = {};
				return Promise.reject();
			}
			return response.json();
		}).then(obj => {
			auth.cred = obj;
			return Promise.resolve();
		}).catch(() => {
			auth.cred = {};
			return Promise.reject();
		});
	}

};

export default auth;
