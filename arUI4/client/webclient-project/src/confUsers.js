import * as React from "react";
import { useMediaQuery } from "@material-ui/core";
import { 
	Filter,
	List,
	Datagrid,
	TextField,
	ReferenceField,
	EditButton,
	Edit,
	Create,
	SimpleForm,
	SimpleList,
	ReferenceInput,
	SelectInput,
	TextInput,
} from "react-admin";

const ConfUsersTitle = ({ record }) => {
	return <span>Post {record ? `"${record.id}"` : ''}</span>;
};

export const ConfUsersList = ({ permissions, ...props }) => {
	const isSmall = useMediaQuery(theme => theme.breakpoints.down('sm'));
	return (
		<List {...props}>
			{isSmall ? (
				<SimpleList
					primaryText={record => record.id}
					secondaryText={record => record.permission}
				/>
			) : (
				<Datagrid rowClick="toggleSelection">
					<TextField source="id" sortable={false} label="Name" />
					<TextField source="permission" sortable={false} />
					{permissions === 'admin' && <EditButton />}
				</Datagrid>
			)}
		</List>
	);
};

/*
export const ConfUsersEdit = ({ permissions, ...props }) => (
	<Edit title={<confUsersTitle />} {...props}>
		<SimpleForm>
			<TextInput disabled source="id" />
			<ReferenceInput source="userId" reference="users">
				<SelectInput optionText="name" />
			</ReferenceInput>
			<TextInput source="title" />
			<TextInput multiline source="body" />
		</SimpleForm>
	</Edit>
);
*/
