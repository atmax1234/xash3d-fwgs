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
	deadzone_camera_input_t camera_input;
	qboolean local_map_active;
} deadzone_client_state_t;

static deadzone_client_state_t g_deadzone_client;

STATIC_ASSERT( offsetof( deadzone_client_engine_api_t, context ) == 8, "unexpected DeadZone client engine API layout" );
STATIC_ASSERT( offsetof( deadzone_client_engine_api_t, log_message ) == 8 + sizeof( void * ), "unexpected DeadZone client engine API layout" );
STATIC_ASSERT( sizeof( deadzone_client_engine_api_t ) == 8 + ( 2 * sizeof( void * )), "unexpected DeadZone client engine API size" );
STATIC_ASSERT( offsetof( deadzone_client_api_t, shutdown ) == 8 + sizeof( void * ), "unexpected DeadZone client API layout" );
STATIC_ASSERT( offsetof( deadzone_client_api_t, camera_frame ) == 8 + ( 2 * sizeof( void * )), "unexpected DeadZone client API v2 layout" );
STATIC_ASSERT( sizeof( deadzone_client_api_t ) == 8 + ( 3 * sizeof( void * )), "unexpected DeadZone client API v2 size" );
STATIC_ASSERT( offsetof( deadzone_camera_input_t, player_id ) == 8, "unexpected DeadZone camera input layout" );
STATIC_ASSERT( sizeof( deadzone_camera_input_t ) == 44, "unexpected DeadZone camera input size" );
STATIC_ASSERT( offsetof( deadzone_camera_state_t, player_id ) == 8, "unexpected DeadZone camera state layout" );
STATIC_ASSERT( sizeof( deadzone_camera_state_t ) == 40, "unexpected DeadZone camera state size" );

static qboolean CL_DeadZoneValidateCamera( const deadzone_camera_state_t *camera )
{
	int axis;

	if( !camera || camera->size < sizeof( *camera ) ||
		camera->version != DEADZONE_CAMERA_STATE_VERSION ||
		camera->player_id != g_deadzone_client.camera_input.player_id ||
		IS_NAN( camera->horizontal_fov_degrees ) ||
		camera->horizontal_fov_degrees < 10.0f ||
		camera->horizontal_fov_degrees > 150.0f )
	{
		return false;
	}

	for( axis = 0; axis < 3; axis++ )
	{
		if( IS_NAN( camera->view_origin[axis] ) || IS_NAN( camera->view_angles[axis] ) ||
			camera->view_origin[axis] < -131072.0f || camera->view_origin[axis] > 131072.0f ||
			camera->view_angles[axis] < -3600.0f || camera->view_angles[axis] > 3600.0f )
		{
			return false;
		}
	}

	return true;
}

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

	size_t required_size = requested_version >= DEADZONE_CLIENT_API_VERSION_2 ?
		sizeof( deadzone_client_api_t ) : offsetof( deadzone_client_api_t, camera_frame );

	if( g_deadzone_client.client_api.size < required_size ||
		g_deadzone_client.client_api.version != requested_version ||
		!g_deadzone_client.client_api.shutdown ||
		( requested_version >= DEADZONE_CLIENT_API_VERSION_2 &&
		!g_deadzone_client.client_api.camera_frame ))
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

	cls.mempool = Mem_AllocPool( "DeadZone Client Static Pool" );
	clgame.mempool = Mem_AllocPool( "DeadZone Client Presentation Pool" );
	clgame.maxRemapInfos = 0;
	clgame.maxEntities = 2;
	cl.maxclients = 1;
	CL_InitEdicts( cl.maxclients );
	R_InitRenderAPI();

	Cvar_FullSet( "host_clientloaded", "1", FCVAR_READ_ONLY );
	Con_Printf( "DeadZone native client loader: accepted client API version %u from %s\n",
		requested_version, name );
	return true;
}

qboolean CL_DeadZoneStartLocalMap( const char *mapname, model_t *worldmodel,
	const uint32_t player_id, const vec3_t origin, const vec3_t view_angles,
	const float eye_height )
{
	deadzone_camera_state_t camera;

	if( !CL_IsDeadZoneClientLoaded() ||
		g_deadzone_client.client_api.version < DEADZONE_CLIENT_API_VERSION_2 ||
		!mapname || !worldmodel || !player_id || !origin || !view_angles ||
		IS_NAN( eye_height ) || eye_height <= 0.0f )
	{
		return false;
	}

	memset( &g_deadzone_client.camera_input, 0,
		sizeof( g_deadzone_client.camera_input ));
	g_deadzone_client.camera_input.size = sizeof( g_deadzone_client.camera_input );
	g_deadzone_client.camera_input.version = DEADZONE_CAMERA_INPUT_VERSION;
	g_deadzone_client.camera_input.player_id = player_id;
	VectorCopy( origin, g_deadzone_client.camera_input.player_origin );
	VectorCopy( view_angles, g_deadzone_client.camera_input.player_view_angles );
	g_deadzone_client.camera_input.eye_height = eye_height;

	memset( &camera, 0, sizeof( camera ));
	camera.size = sizeof( camera );
	if( g_deadzone_client.client_api.camera_frame(
		g_deadzone_client.client_api.context,
		&g_deadzone_client.camera_input, &camera ) != DEADZONE_CLIENT_RESULT_SUCCESS ||
		!CL_DeadZoneValidateCamera( &camera ))
	{
		Con_Printf( S_ERROR "DeadZone native client: rejected initial camera state\n" );
		memset( &g_deadzone_client.camera_input, 0,
			sizeof( g_deadzone_client.camera_input ));
		return false;
	}

	Q_strncpy( clgame.mapname, mapname, sizeof( clgame.mapname ));
	cl.worldmodel = worldmodel;
	cl.models[1] = worldmodel;
	cl.nummodels = 2;
	cl.playernum = 0;
	cl.viewentity = 1;
	clgame.movevars.zmax = 4096.0f;
	clgame.movevars.wateralpha = 1.0f;
	cl.video_prepped = true;
	cl.audio_prepped = false;
	cl.background = false;
	CL_ClearWorld();
	ref.dllFuncs.R_NewMap();
	Mod_LoadDetailTextures( cl.worldmodel );

	cls.disable_screen = 0.0f;
	cls.signon = 0;
	cls.state = ca_active;
	Key_SetKeyDest( key_game );
	g_deadzone_client.local_map_active = true;

	Con_Printf( "DeadZone native client: local camera entered maps/%s.bsp for authoritative player %u\n",
		mapname, player_id );
	return true;
}

qboolean CL_DeadZoneGetCamera( vec3_t view_origin, vec3_t view_angles,
	float *horizontal_fov_degrees )
{
	deadzone_camera_state_t camera;
	deadzone_client_result_t result;

	if( !g_deadzone_client.local_map_active || !view_origin || !view_angles ||
		!horizontal_fov_degrees )
	{
		return false;
	}

	g_deadzone_client.camera_input.frame_time = host.frametime;
	memset( &camera, 0, sizeof( camera ));
	camera.size = sizeof( camera );
	result = g_deadzone_client.client_api.camera_frame(
		g_deadzone_client.client_api.context,
		&g_deadzone_client.camera_input, &camera );
	if( result != DEADZONE_CLIENT_RESULT_SUCCESS || !CL_DeadZoneValidateCamera( &camera ))
	{
		Con_Printf( S_ERROR "DeadZone native client: camera frame failed validation (result %d)\n",
			result );
		return false;
	}

	VectorCopy( camera.view_origin, view_origin );
	VectorCopy( camera.view_angles, view_angles );
	*horizontal_fov_degrees = camera.horizontal_fov_degrees;
	return true;
}

qboolean CL_IsDeadZoneLocalMapActive( void )
{
	return g_deadzone_client.local_map_active;
}

void CL_DeadZoneStopLocalMap( void )
{
	if( !g_deadzone_client.local_map_active )
		return;

	g_deadzone_client.local_map_active = false;
	cl.video_prepped = false;
	cl.audio_prepped = false;
	cl.worldmodel = NULL;
	cl.models[1] = NULL;
	cl.nummodels = 0;
	cls.signon = 0;
	cls.state = ca_disconnected;
	memset( &g_deadzone_client.camera_input, 0,
		sizeof( g_deadzone_client.camera_input ));
	Con_Printf( "DeadZone native client: local camera left the map\n" );
}

void CL_UnloadDeadZoneClient( void )
{
	if( !CL_IsDeadZoneClientLoaded() )
		return;

	CL_DeadZoneStopLocalMap();
	if( clgame.entities )
		CL_FreeEdicts();
	Mem_FreePool( &cls.mempool );
	Mem_FreePool( &clgame.mempool );
	memset( &clgame, 0, sizeof( clgame ));

	g_deadzone_client.client_api.shutdown( g_deadzone_client.client_api.context );
	Con_Printf( "DeadZone native client loader: client API shutdown complete\n" );
	Cvar_FullSet( "host_clientloaded", "0", FCVAR_READ_ONLY );
	COM_FreeLibrary( g_deadzone_client.library );
	memset( &g_deadzone_client, 0, sizeof( g_deadzone_client ));
}
