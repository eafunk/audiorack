// in src/App.js
import * as React from "react";
//import jsonServerProvider from 'ra-data-json-server';
//const dataProvider = jsonServerProvider('https://jsonplaceholder.typicode.com');
import dataProvider from './dataProvider';
import authProvider from './authProvider';
import { Admin, Resource, AppBar, Layout, ReferenceInput, SelectInput, EditGuesser, ListGuesser } from 'react-admin';
import { UserList, UserEdit, UserCreate } from './users';
import { FileList, FileEdit, FileCreate } from './files';
import { HttpList, HttpEdit, HttpCreate } from './http';
import { LibraryList, LibraryEdit, LibraryCreate } from './library';
import { StudioList, StudioEdit, StudioCreate } from './studios';
import { BrowseList, BrowseEdit, BrowseCreate } from './browse';
import { ConfUsersList } from './confUsers';
import Typography from '@material-ui/core/Typography';
import { makeStyles } from '@material-ui/core/styles';

import LibraryMusicIcon from '@material-ui/icons/LibraryMusic';
import SettingsIcon from '@material-ui/icons/Settings';
import MicIcon from '@material-ui/icons/Mic';

const useStyles = makeStyles({
	title: {
		flex: 1,
		textOverflow: 'ellipsis',
		whiteSpace: 'nowrap',
		overflow: 'hidden',
	},
	spacer: {
		flex: 1,
	},
});

const MyAppBar = props => {
	const classes = useStyles();
	return (
		<AppBar {...props}>
			<Typography
				variant="h6"
				color="inherit"
				className={classes.title}
				id="react-admin-title"
			/>
			AudioRack4
			<span className={classes.spacer} />
		</AppBar>
	);
};

const MyLayout = (props) => <Layout {...props} appBar={MyAppBar} />;

const App = () => (
	<Admin layout={MyLayout} authProvider={authProvider} dataProvider={dataProvider} disableTelemetry>
		{permissions => [
			permissions === 'admin'
			?	<Resource name="config/files" 
					list={FileList} 
					edit={permissions === 'admin' ? FileEdit : null}
					create={permissions === 'admin' ? FileCreate : null} 
					options={{ label: 'file settings' }} 
					icon={SettingsIcon} 
				/>
			: null,
			
			permissions === 'admin'
			?	<Resource name="config/users" 
					list={UserList} 
					edit={permissions === 'admin' ? UserEdit : null}
					create={permissions === 'admin' ? UserCreate : null} 
					options={{ label: 'user settings' }} 
					icon={SettingsIcon} 
				/>
			: null,
			
			permissions === 'admin'
			?	<Resource name="config/http" 
					list={HttpList} 
					edit={permissions === 'admin' ? HttpEdit : null}
					create={permissions === 'admin' ? HttpCreate : null} 
					options={{ label: 'http settings' }} 
					icon={SettingsIcon} 
				/>
			: null,

			permissions === 'admin'
			?	<Resource name="config/library" 
					list={LibraryList} 
					edit={permissions === 'admin' ? LibraryEdit : null}
					create={permissions === 'admin' ? LibraryCreate : null} 
					options={{ label: 'library settings' }} 
					icon={SettingsIcon} 
				/>
			: null,
			
			permissions === 'admin'
			?	<Resource name="config/studios" 
					list={StudioList} 
					edit={permissions === 'admin' ? StudioEdit : null}
					create={permissions === 'admin' ? StudioCreate : null} 
					options={{ label: 'studio settings' }} 
					icon={SettingsIcon} 
				/>
			: null,
			
			<Resource name="library/browse" 
				list={BrowseList} 
				options={{ label: 'Library' }} 
				icon={LibraryMusicIcon} 
			/>,
			
			// hidden resources
			<Resource name="library/get/locations" />
		]}
	</Admin>
);

export default App;

