import * as React from "react";
import { useMediaQuery } from '@material-ui/core';
import dataProvider from './dataProvider';
import { 
	List,
	SimpleForm,
	SimpleList,
	Datagrid, 
	TextField, 
	TextInput, 
	PasswordInput,
	SelectInput,
	EditButton,
	Edit,
	Create
} from 'react-admin';

const UserTitle = ({ record }) => {
	return <span>User {record ? `"${record.id}"` : 'Create New'}</span>;
};

export const UserList = ({ permissions, ...props }) => {
	const isSmall = useMediaQuery(theme => theme.breakpoints.down('sm'));
	return (
		<List exporter={false} {...props}>
			{isSmall ? (
				<SimpleList
					primaryText={record => record.id}
					secondaryText={record => permissions === 'admin' ? record.permission : null}
				/>
			) : (
				<Datagrid rowClick="edit">
					<TextField source="id" label="Username" sortable={false}/>
					{permissions === 'admin' && <TextField source="permission" label="Permission" sortable={false}/>}
					{permissions === 'admin' && <EditButton />}
				</Datagrid>
			)}
		</List>
	);
};

export const UserEdit = props => {
	function transform(data) {
		if(data.newpassword){
			// request password change
			return dataProvider.genPasswordHash('', { password: data.newpassword })
				.then(( resp ) => {
					data.salt = resp.salt;
					data.password = resp.password;
					delete data.newpassword;
					return data;
				});
		}else{
			delete data.password;
			delete data.salt;
			delete data.newpassword;
		}
		return data;
	};

	return (
		<Edit title={<UserTitle />} transform={transform} {...props}>
			<SimpleForm>
				<TextInput disabled label="Username" source="id" />
				<PasswordInput label="Change Password to" source="newpassword" />
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

export const UserCreate = props => {
	function transform(data) {
		if(data.newpassword){
			// request password change
			return dataProvider.genPasswordHash('', { password: data.newpassword })
				.then(( resp ) => {
					data.salt = resp.salt;
					data.password = resp.password;
					delete data.newpassword;
					return data;
				});
		}else{
			delete data.password;
			delete data.salt;
			delete data.newpassword;
		}
		return data;
	};
	
	function validateUserCreation(values) {
		const errors = {};
		if(!values.id) 
			errors.id = 'Username is required';
		if(!values.newpassword)
			errors.newpassword = 'Password is required';
		return errors
	};
	
	return (
		<Create title={<UserTitle />} transform={transform} {...props}>
			<SimpleForm  validate={validateUserCreation} >
				<TextInput label="Username" source="id" />
				<PasswordInput label="Password" source="newpassword" />
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
