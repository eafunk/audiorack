// in src/App.js
import * as React from "react";
import jsonServerProvider from 'ra-data-json-server';
import { Admin, Resource } from 'react-admin';
import { UserList } from './users';
import { PostList, PostEdit, PostCreate } from './posts';

import library from './library';
import authProvider from './authProvider';

import PostIcon from '@material-ui/icons/Book';
import UserIcon from '@material-ui/icons/Group';
import LibraryMusicIcon from '@material-ui/icons/LibraryMusic';
import SettingsIcon from '@material-ui/icons/Settings';
import MicIcon from '@material-ui/icons/Mic';

const dataProvider = jsonServerProvider('https://jsonplaceholder.typicode.com');

const App = () => (
	<Admin authProvider={authProvider} dataProvider={dataProvider}>
		<Resource name="library" list={library} icon={LibraryMusicIcon} />
		<Resource name="posts" list={PostList} edit={PostEdit} create={PostCreate} icon={MicIcon} />
		<Resource name="users" list={UserList} icon={SettingsIcon} />
	</Admin>
);

export default App;

