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

const HttpTitle = ({ record }) => {
	return <span>Http {record ? `"${record.id}"` : 'Create New'}</span>;
};

export const HttpList = ({ permissions, ...props }) => {
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

export const HttpEdit = props => (
	<Edit title={<HttpTitle />} {...props}>
		<SimpleForm>
			<TextInput disabled label="Property" source="id" />
			<TextInput label="Value" source="value" />
		</SimpleForm>
	</Edit>
);

export const HttpCreate = props => (
	<Create title={<HttpTitle />} {...props}>
		<SimpleForm>
			<TextInput label="Property" source="id" />
			<TextInput label="Value" source="value" />
		</SimpleForm>
	</Create>
);
