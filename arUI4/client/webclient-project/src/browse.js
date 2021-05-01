import * as React from "react";
import { useMediaQuery } from '@material-ui/core';
import dataProvider from './dataProvider';
import { 
	List,
	Filter,
	SimpleForm,
	SimpleList,
	Datagrid, 
	TextField, 
	TextInput, 
	SelectInput,
	EditButton,
	Edit,
	Create
} from 'react-admin';

const BrowseTitle = ({ record }) => {
	return <span>Item {record ? `"${record.id}"` : 'Create New'}</span>;
};
const PostFilter = (props) => (
	<Filter {...props}>
		<TextInput label="Match" source="q" alwaysOn />
		<SelectInput label="Type" source="type" alwaysOn initialValue="artist" choices={[
			{ id: 'artist', name: 'Artist' },
			{ id: 'album', name: 'Album' },
			{ id: 'title', name: 'Title' },
			{ id: 'category', name: 'Category' },
			{ id: 'playlist', name: 'Playlist' },
			{ id: 'task', name: 'Task' },
			{ id: 'comment', name: 'Comment' },
			{ id: 'missing', name: 'Missing' },
			{ id: 'rested', name: 'Rested' },
			{ id: 'added', name: 'Added' }
		]} />
		<TextInput/>
	</Filter>
);

export const BrowseList = ({ permissions, ...props }) => {
	const isSmall = useMediaQuery(theme => theme.breakpoints.down('sm'));
	return (
		<List exporter={false} filters={<PostFilter />} {...props}>
			{isSmall ? (
				<SimpleList
					primaryText={record => record.Label}
					secondaryText={record => record.Duration}
				/>
			) : (
				<Datagrid rowClick="edit">
					<TextField source="Label" label="Name" />
					<TextField source="Duration" label="Duration" />
				</Datagrid>
			)}
		</List>
	);
};

export const BrowseEdit = props => {
	return (
		<Edit title={<BrowseTitle />} {...props}>
			<SimpleForm>
				<TextInput disabled label="Username" source="id" />
				<SelectInput source="permission" choices={[
					{ id: 'admin', name: 'Administrator (All)' },
					{ id: 'manager', name: 'Manager (Studio, Library, Traffic)' },
					{ id: 'production', name: 'Production (Library, Traffic)' },
					{ id: 'programming', name: 'Programming (Studio, Library)' },
					{ id: 'traffic', name: 'Traffic' },
					{ id: 'library', name: 'Library' },
					{ id: 'studio', name: 'Studio' },
				]} />
			</SimpleForm>
		</Edit>
	);
};

export const BrowseCreate = props => {
	return (
		<Create title={<BrowseTitle />} {...props}>
			<SimpleForm >
				<TextInput label="Username" source="id" />
				<SelectInput source="permission" initialValue="manager" choices={[
					{ id: 'admin', name: 'Administrator (All)' },
					{ id: 'manager', name: 'Manager (Studio, Library, Traffic)' },
					{ id: 'production', name: 'Production (Library, Traffic)' },
					{ id: 'programming', name: 'Programming (Studio, Library)' },
					{ id: 'traffic', name: 'Traffic' },
					{ id: 'library', name: 'Library' },
					{ id: 'studio', name: 'Studio' },
				]} />
			</SimpleForm>
		</Create>
	);
};
