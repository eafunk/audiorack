import * as React from "react";
import { useMediaQuery } from '@material-ui/core';
import { 
	List,
	SimpleForm,
	SimpleList,
	Datagrid, 
	TextField, 
	TextInput, 
	EditButton,
	Edit,
	Create
} from 'react-admin';

const FileTitle = ({ record }) => {
	return <span>File {record ? `"${record.id}"` : 'Create New'}</span>;
};

export const FileList = ({ permissions, ...props }) => {
	const isSmall = useMediaQuery(theme => theme.breakpoints.down('sm'));
	return (
		<List exporter={false} {...props}>
			{isSmall ? (
				<SimpleList
					primaryText={record => record.id}
					secondaryText={record => permissions === 'admin' ? '${record.value}' : null}
				/>
			) : (
				<Datagrid rowClick="edit">
					<TextField source="id" label="Property" sortable={false}/>
					{permissions === 'admin' && <TextField source="value" label="Value" sortable={false}/>}
					{permissions === 'admin' && <EditButton />}
				</Datagrid>
			)}
		</List>
	);
};

export const FileEdit = props => (
	<Edit title={<FileTitle />} {...props}>
		<SimpleForm>
			<TextInput disabled label="Property" source="id" />
			<TextInput label="Value" source="value" />
		</SimpleForm>
	</Edit>
);

export const FileCreate = props => (
	<Create title={<FileTitle />} {...props}>
		<SimpleForm>
			<TextInput label="Property" source="id" />
			<TextInput label="Value" source="value" />
		</SimpleForm>
	</Create>
);
