import * as React from "react";
import { useMediaQuery } from '@material-ui/core';
import { 
	List,
	SimpleForm,
	SimpleList,
	Datagrid, 
	TextField, 
	NumberField,
	TextInput, 
	NumberInput,
	EditButton,
	Edit,
	Create
} from 'react-admin';

const StudioTitle = ({ record }) => {
	return <span>Studio {record ? `"${record.id}"` : 'Create New'}</span>;
};

export const StudioList = ({ permissions, ...props }) => {
	const isSmall = useMediaQuery(theme => theme.breakpoints.down('sm'));
	return (
		<List exporter={false} {...props}>
			{isSmall ? (
				<SimpleList
					primaryText={record => record.id}
					secondaryText={record => permissions === 'admin' ? '${record.host}:${record.port}' : null}
				/>
			) : (
				<Datagrid rowClick="edit">
					<TextField source="id" label="Name" sortable={false}/>
					{permissions === 'admin' && <TextField source="host" label="Host Address" sortable={false}/>}
					{permissions === 'admin' && <NumberField source="port" label="Port Number" sortable={false}/>}
					{permissions === 'admin' && <EditButton />}
				</Datagrid>
			)}
		</List>
	);
};

export const StudioEdit = props => (
	<Edit title={<StudioTitle />} {...props}>
		<SimpleForm>
			<TextInput disabled label="Name" source="id" />
			<TextInput label="Host Address" source="host" />
			<NumberInput label="Port Number" source="port" />
			<TextInput label="Run Command" source="run" />
			<NumberInput label="Max. Connections" source="maxpool" />
			<NumberInput label="Min. Connections" source="minpool" />
		</SimpleForm>
	</Edit>
);

export const StudioCreate = props => (
	<Create title={<StudioTitle />} {...props}>
		<SimpleForm>
			<TextInput label="Name" source="id" />
			<TextInput label="Host Address" source="host" defaultValue={"localhost"} />
			<NumberInput label="Port Number" source="port" defaultValue={9550}/>
			<TextInput label="Run Command" source="run" />
			<NumberInput label="Max. Connections" source="maxpool" defaultValue={5} />
			<NumberInput label="Min. Connections" source="minpool" defaultValue={2} />
		</SimpleForm>
	</Create>
);
