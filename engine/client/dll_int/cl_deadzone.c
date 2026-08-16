/*
cl_deadzone.c - DeadZone-native client module lifecycle
Copyright (C) 2026 DeadZone contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.
*/

#include "common.h"
#include "client.h"
#include "library.h"
#include "deadzone_client_api.h"

typedef struct deadzone_client_state_s
{
	void *library;
	deadzone_client_engine_api_t engine_api;
	deadzone_client_api_t client_api;
} deadzone_client_state_t;

static deadzone_client_state_t g_deadzone_client;

STATIC_ASSERT( offsetof( deadzone_client_engine_api_t, context ) == 8, "unexpected DeadZone client engine API layout" );
STATIC_ASSERT( offsetof( deadzone_client_engine_api_t, log_message ) == 8 + sizeof( void * ), "unexpected DeadZone client engine API layout" );
STATIC_ASSERT( sizeof( deadzone_client_engine_api_t ) == 8 + ( 2 * sizeof( void * )), "unexpected DeadZone client engine API size" );
STATIC_ASSERT( offsetof( deadzone_client_api_t, shutdown ) == 8 + sizeof( void * ), "unexpected DeadZone client API layout" );
STATIC_ASSERT( sizeof( deadzone_client_api_t ) == 8 + ( 2 * sizeof( void * )), "unexpected DeadZone client API size" );

static void DEADZONE_CLIENT_API_CALL CL_DeadZoneLogMessage( void *context,
	deadzone_client_log_level_t level, const char *message, uint32_t message_length )
{
	char text[1024];
	size_t copy_length;
	const char *prefix = "";

	(void)context;

	if( !message )
		return;

	copy_length = message_length;
	if( copy_length >= sizeof( text ))
		copy_length = sizeof( text ) - 1;

	memcpy( text, message, copy_length );
	text[copy_length] = '\0';

	if( level == DEADZONE_CLIENT_LOG_WARNING )
		prefix = S_WARN;
	else if( level == DEADZONE_CLIENT_LOG_ERROR )
		prefix = S_ERROR;

	Con_Printf( "%sDeadZone client: %s\n", prefix, text );
}

qboolean CL_IsDeadZoneClientLoaded( void )
{
	return g_deadzone_client.library != NULL;
}

qboolean CL_LoadDeadZoneClient( const char *name, uint32_t requested_version )
{
	deadzone_client_query_fn query;
	deadzone_client_result_t result;

	if( CL_IsDeadZoneClientLoaded() )
		return true;

	if( requested_version < DEADZONE_CLIENT_API_VERSION_1 ||
		requested_version > DEADZONE_CLIENT_API_VERSION )
	{
		COM_PushLibraryError( "unsupported DeadZone client API version in gameinfo.txt" );
		Con_Printf( S_ERROR "DeadZone native client loader: configured API version %u is unsupported (engine supports %u)\n",
			requested_version, DEADZONE_CLIENT_API_VERSION );
		return false;
	}

	memset( &g_deadzone_client, 0, sizeof( g_deadzone_client ));
	g_deadzone_client.library = COM_LoadLibrary( name, false, false );
	if( !g_deadzone_client.library )
	{
		Con_Printf( S_ERROR "DeadZone native client loader: failed to load %s\n", name );
		return false;
	}

	query = (deadzone_client_query_fn)COM_GetProcAddress(
		g_deadzone_client.library, DEADZONE_CLIENT_QUERY_ENTRY );
	if( !query )
	{
		COM_PushLibraryError( "missing DeadZone_ClientQuery export" );
		Con_Printf( S_ERROR "DeadZone native client loader: module is missing %s\n",
			DEADZONE_CLIENT_QUERY_ENTRY );
		COM_FreeLibrary( g_deadzone_client.library );
		memset( &g_deadzone_client, 0, sizeof( g_deadzone_client ));
		return false;
	}

	g_deadzone_client.engine_api.size = sizeof( g_deadzone_client.engine_api );
	g_deadzone_client.engine_api.version = requested_version;
	g_deadzone_client.engine_api.context = NULL;
	g_deadzone_client.engine_api.log_message = CL_DeadZoneLogMessage;

	g_deadzone_client.client_api.size = sizeof( g_deadzone_client.client_api );
	result = query( requested_version, &g_deadzone_client.engine_api,
		&g_deadzone_client.client_api );

	if( result != DEADZONE_CLIENT_RESULT_SUCCESS )
	{
		COM_PushLibraryError( "DeadZone client module rejected the native API request" );
		Con_Printf( S_ERROR "DeadZone native client loader: %s rejected API version %u (result %d)\n",
			name, requested_version, result );
		COM_FreeLibrary( g_deadzone_client.library );
		memset( &g_deadzone_client, 0, sizeof( g_deadzone_client ));
		return false;
	}

	if( g_deadzone_client.client_api.size < sizeof( g_deadzone_client.client_api ) ||
		g_deadzone_client.client_api.version != requested_version ||
		!g_deadzone_client.client_api.shutdown )
	{
		if( g_deadzone_client.client_api.size >= sizeof( deadzone_client_api_t ) &&
			g_deadzone_client.client_api.shutdown )
		{
			g_deadzone_client.client_api.shutdown( g_deadzone_client.client_api.context );
		}

		COM_PushLibraryError( "DeadZone client module returned an invalid API table" );
		Con_Printf( S_ERROR "DeadZone native client loader: module returned an invalid API table\n" );
		COM_FreeLibrary( g_deadzone_client.library );
		memset( &g_deadzone_client, 0, sizeof( g_deadzone_client ));
		return false;
	}

	Cvar_FullSet( "host_clientloaded", "1", FCVAR_READ_ONLY );
	Con_Printf( "DeadZone native client loader: accepted client API version %u from %s\n",
		requested_version, name );
	return true;
}

void CL_UnloadDeadZoneClient( void )
{
	if( !CL_IsDeadZoneClientLoaded() )
		return;

	g_deadzone_client.client_api.shutdown( g_deadzone_client.client_api.context );
	Con_Printf( "DeadZone native client loader: client API shutdown complete\n" );
	Cvar_FullSet( "host_clientloaded", "0", FCVAR_READ_ONLY );
	COM_FreeLibrary( g_deadzone_client.library );
	memset( &g_deadzone_client, 0, sizeof( g_deadzone_client ));
}
