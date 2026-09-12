/*
sv_deadzone.c - DeadZone-native server module lifecycle
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
#include "server.h"
#include "client.h"
#include "library.h"
#include "pm_local.h"
#include "deadzone_engine_api.h"

typedef struct deadzone_server_state_s
{
	void *library;
	deadzone_engine_api_t engine_api;
	deadzone_server_api_t server_api;
	qboolean map_active;
	qboolean player_active;
	deadzone_player_spawn_t player_spawn;
	deadzone_player_state_t player_state;
} deadzone_server_state_t;

static deadzone_server_state_t g_deadzone_server;
static qboolean g_deadzone_reported_player_hull;

STATIC_ASSERT( offsetof( deadzone_engine_api_t, context ) == 8, "unexpected DeadZone engine API layout" );
STATIC_ASSERT( offsetof( deadzone_engine_api_t, log_message ) == 8 + sizeof( void * ), "unexpected DeadZone engine API layout" );
STATIC_ASSERT( offsetof( deadzone_engine_api_t, trace_player ) == 8 + ( 2 * sizeof( void * )), "unexpected DeadZone engine API v4 layout" );
STATIC_ASSERT( sizeof( deadzone_engine_api_t ) == 8 + ( 3 * sizeof( void * )), "unexpected DeadZone engine API v4 size" );
STATIC_ASSERT( offsetof( deadzone_server_api_t, shutdown ) == 8 + sizeof( void * ), "unexpected DeadZone server API layout" );
STATIC_ASSERT( offsetof( deadzone_server_api_t, map_loaded ) == 8 + ( 2 * sizeof( void * )), "unexpected DeadZone server API v2 layout" );
STATIC_ASSERT( offsetof( deadzone_server_api_t, spawn_player ) == 8 + ( 4 * sizeof( void * )), "unexpected DeadZone server API v3 layout" );
STATIC_ASSERT( offsetof( deadzone_server_api_t, simulate_player ) == 8 + ( 6 * sizeof( void * )), "unexpected DeadZone server API v4 layout" );
STATIC_ASSERT( sizeof( deadzone_server_api_t ) == 8 + ( 7 * sizeof( void * )), "unexpected DeadZone server API v4 size" );
STATIC_ASSERT( offsetof( deadzone_map_info_t, name ) == 8, "unexpected DeadZone map info layout" );
STATIC_ASSERT( offsetof( deadzone_map_info_t, mins ) == 20 + sizeof( void * ), "unexpected DeadZone map info layout" );
STATIC_ASSERT( offsetof( deadzone_map_info_t, plane_count ) == 44 + sizeof( void * ), "unexpected DeadZone map info layout" );
STATIC_ASSERT( offsetof( deadzone_player_spawn_t, player_id ) == 8, "unexpected DeadZone player spawn layout" );
STATIC_ASSERT( offsetof( deadzone_player_spawn_t, origin ) == 12, "unexpected DeadZone player spawn layout" );
STATIC_ASSERT( sizeof( deadzone_player_spawn_t ) == 40, "unexpected DeadZone player spawn size" );
STATIC_ASSERT( sizeof( deadzone_move_intent_t ) == 28, "unexpected DeadZone move intent size" );
STATIC_ASSERT( sizeof( deadzone_player_state_t ) == 60, "unexpected DeadZone player state size" );
STATIC_ASSERT( sizeof( deadzone_player_trace_request_t ) == 36, "unexpected DeadZone trace request size" );
STATIC_ASSERT( sizeof( deadzone_player_trace_result_t ) == 40, "unexpected DeadZone trace result size" );

static qboolean SV_DeadZoneValidateSpawn( const deadzone_player_spawn_t *spawn,
	const deadzone_map_info_t *map_info )
{
	int axis;

	if( !spawn || spawn->size < sizeof( *spawn ) ||
		spawn->version != DEADZONE_PLAYER_SPAWN_VERSION || spawn->player_id == 0 ||
		IS_NAN( spawn->eye_height ) || spawn->eye_height <= 0.0f )
	{
		return false;
	}

	for( axis = 0; axis < 3; axis++ )
	{
		if( IS_NAN( spawn->origin[axis] ) || IS_NAN( spawn->view_angles[axis] ) ||
			spawn->view_angles[axis] < -3600.0f || spawn->view_angles[axis] > 3600.0f ||
			spawn->origin[axis] <= map_info->mins[axis] ||
			spawn->origin[axis] >= map_info->maxs[axis] )
		{
			return false;
		}
	}

	return spawn->origin[2] + spawn->eye_height < map_info->maxs[2];
}

static qboolean SV_DeadZoneValidatePlayerState( const deadzone_player_state_t *state )
{
	int axis;

	if( !state || state->size < sizeof( *state ) ||
		state->version != DEADZONE_PLAYER_STATE_VERSION ||
		state->player_id != g_deadzone_server.player_spawn.player_id ||
		state->tick < g_deadzone_server.player_state.tick ||
		IS_NAN( state->eye_height ) || state->eye_height <= 0.0f ||
		state->eye_height > 128.0f ||
		( state->flags & ~( DEADZONE_PLAYER_STATE_GROUNDED |
		DEADZONE_PLAYER_STATE_HIT_WORLD )))
	{
		return false;
	}

	for( axis = 0; axis < 3; axis++ )
	{
		if( IS_NAN( state->origin[axis] ) || IS_NAN( state->velocity[axis] ) ||
			IS_NAN( state->view_angles[axis] ) ||
			state->origin[axis] <= sv.worldmodel->mins[axis] ||
			state->origin[axis] >= sv.worldmodel->maxs[axis] ||
			state->velocity[axis] < -4096.0f || state->velocity[axis] > 4096.0f ||
			state->view_angles[axis] < -3600.0f || state->view_angles[axis] > 3600.0f )
		{
			return false;
		}
	}

	return state->origin[2] + state->eye_height < sv.worldmodel->maxs[2];
}

static deadzone_result_t DEADZONE_API_CALL SV_DeadZoneTracePlayer( void *context,
	const deadzone_player_trace_request_t *request,
	deadzone_player_trace_result_t *result )
{
	hull_t *hull;
	pmtrace_t trace;
	vec3_t start;
	vec3_t end;
	int axis;

	(void)context;

	if( !request || !result || request->size < sizeof( *request ) ||
		request->version != DEADZONE_PLAYER_TRACE_REQUEST_VERSION ||
		request->player_id != g_deadzone_server.player_spawn.player_id ||
		result->size < sizeof( *result ) || !g_deadzone_server.map_active ||
		!g_deadzone_server.player_active || !sv.worldmodel )
	{
		return DEADZONE_RESULT_INVALID_ARGUMENT;
	}

	for( axis = 0; axis < 3; axis++ )
	{
		if( IS_NAN( request->start[axis] ) || IS_NAN( request->end[axis] ) ||
			request->start[axis] < -131072.0f || request->start[axis] > 131072.0f ||
			request->end[axis] < -131072.0f || request->end[axis] > 131072.0f )
		{
			return DEADZONE_RESULT_INVALID_ARGUMENT;
		}
	}

	hull = &sv.worldmodel->hulls[1];
	if( !hull->planes || ( !hull->clipnodes16 && !hull->clipnodes32 ))
	{
		if( !g_deadzone_reported_player_hull )
		{
			Con_Printf( S_ERROR "DeadZone native collision: BSP player hull is unavailable\n" );
			g_deadzone_reported_player_hull = true;
		}
		return DEADZONE_RESULT_INVALID_TABLE;
	}

	VectorCopy( request->start, start );
	VectorCopy( request->end, end );
	PM_InitPMTrace( &trace, end );
	PM_RecursiveHullCheck( hull, hull->firstclipnode, 0.0f, 1.0f,
		start, end, &trace );
	if( !g_deadzone_reported_player_hull )
	{
		Con_Printf( "DeadZone native collision: BSP player hull active (start contents %d, fraction %.3f, startsolid %u, allsolid %u)\n",
			PM_HullPointContents( hull, hull->firstclipnode, start ), trace.fraction,
			trace.startsolid, trace.allsolid );
		g_deadzone_reported_player_hull = true;
	}

	memset( result, 0, sizeof( *result ));
	result->size = sizeof( *result );
	result->version = DEADZONE_PLAYER_TRACE_RESULT_VERSION;
	result->fraction = trace.fraction;
	VectorCopy( trace.endpos, result->end );
	if( trace.fraction < 1.0f )
		VectorCopy( trace.plane.normal, result->plane_normal );
	if( trace.startsolid )
		result->flags |= DEADZONE_PLAYER_TRACE_START_SOLID;
	if( trace.allsolid )
		result->flags |= DEADZONE_PLAYER_TRACE_ALL_SOLID;
	return DEADZONE_RESULT_SUCCESS;
}

static void DEADZONE_API_CALL SV_DeadZoneLogMessage( void *context,
	deadzone_log_level_t level, const char *message, uint32_t message_length )
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

	if( level == DEADZONE_LOG_WARNING )
		prefix = S_WARN;
	else if( level == DEADZONE_LOG_ERROR )
		prefix = S_ERROR;

	Con_Printf( "%sDeadZone server: %s\n", prefix, text );
}

qboolean SV_IsDeadZoneServerLoaded( void )
{
	return g_deadzone_server.library != NULL;
}

qboolean SV_LoadDeadZoneServer( const char *name, uint32_t requested_version )
{
	deadzone_server_query_fn query;
	deadzone_result_t result;

	if( SV_IsDeadZoneServerLoaded() )
		return true;

	if( requested_version < DEADZONE_SERVER_API_VERSION_1 ||
		requested_version > DEADZONE_SERVER_API_VERSION )
	{
		COM_PushLibraryError( "unsupported DeadZone server API version in gameinfo.txt" );
		Con_Printf( S_ERROR "DeadZone native loader: configured API version %u is unsupported (engine supports %u)\n",
			requested_version, DEADZONE_SERVER_API_VERSION );
		return false;
	}

	memset( &g_deadzone_server, 0, sizeof( g_deadzone_server ));
	g_deadzone_server.library = COM_LoadLibrary( name, false, false );
	if( !g_deadzone_server.library )
		return false;

	query = (deadzone_server_query_fn)COM_GetProcAddress(
		g_deadzone_server.library, DEADZONE_SERVER_QUERY_ENTRY );
	if( !query )
	{
		COM_PushLibraryError( "missing DeadZone_ServerQuery export" );
		Con_Printf( S_ERROR "DeadZone native loader: module is missing %s\n", DEADZONE_SERVER_QUERY_ENTRY );
		COM_FreeLibrary( g_deadzone_server.library );
		memset( &g_deadzone_server, 0, sizeof( g_deadzone_server ));
		return false;
	}

	g_deadzone_server.engine_api.size = sizeof( g_deadzone_server.engine_api );
	g_deadzone_server.engine_api.version = requested_version;
	g_deadzone_server.engine_api.context = NULL;
	g_deadzone_server.engine_api.log_message = SV_DeadZoneLogMessage;
	g_deadzone_server.engine_api.trace_player = SV_DeadZoneTracePlayer;

	g_deadzone_server.server_api.size = sizeof( g_deadzone_server.server_api );
	result = query( requested_version, &g_deadzone_server.engine_api,
		&g_deadzone_server.server_api );

	if( result != DEADZONE_RESULT_SUCCESS )
	{
		COM_PushLibraryError( "DeadZone server module rejected the native API request" );
		Con_Printf( S_ERROR "DeadZone native loader: %s rejected API version %u (result %d)\n",
			name, requested_version, result );
		COM_FreeLibrary( g_deadzone_server.library );
		memset( &g_deadzone_server, 0, sizeof( g_deadzone_server ));
		return false;
	}

	size_t required_size = offsetof( deadzone_server_api_t, map_loaded );
	if( requested_version >= DEADZONE_SERVER_API_VERSION_4 )
		required_size = sizeof( deadzone_server_api_t );
	else if( requested_version >= DEADZONE_SERVER_API_VERSION_3 )
		required_size = offsetof( deadzone_server_api_t, simulate_player );
	else if( requested_version >= DEADZONE_SERVER_API_VERSION_2 )
		required_size = offsetof( deadzone_server_api_t, spawn_player );

	if( g_deadzone_server.server_api.size < required_size ||
		g_deadzone_server.server_api.version != requested_version ||
		!g_deadzone_server.server_api.shutdown ||
		( requested_version >= DEADZONE_SERVER_API_VERSION_2 &&
		( !g_deadzone_server.server_api.map_loaded || !g_deadzone_server.server_api.map_unloaded )) ||
		( requested_version >= DEADZONE_SERVER_API_VERSION_3 &&
		( !g_deadzone_server.server_api.spawn_player || !g_deadzone_server.server_api.despawn_player )) ||
		( requested_version >= DEADZONE_SERVER_API_VERSION_4 &&
		!g_deadzone_server.server_api.simulate_player ))
	{
		if( g_deadzone_server.server_api.size >= offsetof( deadzone_server_api_t, map_loaded ) &&
			g_deadzone_server.server_api.shutdown )
		{
			g_deadzone_server.server_api.shutdown( g_deadzone_server.server_api.context );
		}

		COM_PushLibraryError( "DeadZone server module returned an invalid API table" );
		Con_Printf( S_ERROR "DeadZone native loader: module returned an invalid API table\n" );
		COM_FreeLibrary( g_deadzone_server.library );
		memset( &g_deadzone_server, 0, sizeof( g_deadzone_server ));
		return false;
	}

	Cvar_FullSet( "host_gameloaded", "1", FCVAR_READ_ONLY );
	// The legacy server DLL path normally establishes the standard player
	// hull dimensions through SV_InitClientMove. Native DeadZone does not load
	// that interface, so initialize the shared hull definitions here before a
	// world model is loaded and its authored clip hulls are set up.
	Pmove_Init();
	Con_Printf( "DeadZone native loader: accepted server API version %u from %s\n",
		requested_version, name );
	return true;
}

qboolean SV_DeadZoneMapLoaded( const char *mapname )
{
	deadzone_map_info_t map_info;
	deadzone_player_spawn_t spawn;
	deadzone_result_t result;

	if( !SV_IsDeadZoneServerLoaded() ||
		g_deadzone_server.server_api.version < DEADZONE_SERVER_API_VERSION_2 ||
		!sv.worldmodel )
	{
		Con_Printf( S_ERROR "DeadZone native loader: server API does not support map lifecycle\n" );
		return false;
	}

	memset( &map_info, 0, sizeof( map_info ));
	map_info.size = sizeof( map_info );
	map_info.version = DEADZONE_MAP_INFO_VERSION;
	map_info.name = mapname;
	map_info.name_length = Q_strlen( mapname );
	map_info.bsp_version = world.version;
	map_info.checksum = sv.worldmapCRC;
	VectorCopy( sv.worldmodel->mins, map_info.mins );
	VectorCopy( sv.worldmodel->maxs, map_info.maxs );
	map_info.plane_count = sv.worldmodel->numplanes;
	map_info.surface_count = sv.worldmodel->numsurfaces;
	map_info.leaf_count = sv.worldmodel->numleafs;

	result = g_deadzone_server.server_api.map_loaded(
		g_deadzone_server.server_api.context, &map_info );
	if( result != DEADZONE_RESULT_SUCCESS )
	{
		Con_Printf( S_ERROR "DeadZone native loader: server module rejected map %s (result %d)\n",
			mapname, result );
		return false;
	}

	g_deadzone_server.map_active = true;

	if( g_deadzone_server.server_api.version >= DEADZONE_SERVER_API_VERSION_3 )
	{
		memset( &spawn, 0, sizeof( spawn ));
		spawn.size = sizeof( spawn );
		result = g_deadzone_server.server_api.spawn_player(
			g_deadzone_server.server_api.context, &spawn );
		if( result != DEADZONE_RESULT_SUCCESS || !SV_DeadZoneValidateSpawn( &spawn, &map_info ))
		{
			Con_Printf( S_ERROR "DeadZone native loader: server module returned an invalid player spawn (result %d)\n",
				result );
			g_deadzone_server.server_api.map_unloaded( g_deadzone_server.server_api.context );
			g_deadzone_server.map_active = false;
			return false;
		}

		g_deadzone_server.player_spawn = spawn;
		memset( &g_deadzone_server.player_state, 0,
			sizeof( g_deadzone_server.player_state ));
		g_deadzone_server.player_state.size = sizeof( g_deadzone_server.player_state );
		g_deadzone_server.player_state.version = DEADZONE_PLAYER_STATE_VERSION;
		g_deadzone_server.player_state.player_id = spawn.player_id;
		VectorCopy( spawn.origin, g_deadzone_server.player_state.origin );
		VectorCopy( spawn.view_angles, g_deadzone_server.player_state.view_angles );
		g_deadzone_server.player_state.eye_height = spawn.eye_height;
		g_deadzone_server.player_state.flags = DEADZONE_PLAYER_STATE_GROUNDED;
		g_deadzone_server.player_active = true;
		Con_Printf( "DeadZone native loader: authoritative player %u accepted at (%.1f %.1f %.1f)\n",
			spawn.player_id, spawn.origin[0], spawn.origin[1], spawn.origin[2] );
	}

	Con_Printf( "DeadZone native loader: map %s accepted (BSP%u, CRC %u, %u planes, %u surfaces, %u leafs)\n",
		mapname, map_info.bsp_version, map_info.checksum, map_info.plane_count,
		map_info.surface_count, map_info.leaf_count );
	return true;
}

qboolean SV_DeadZoneGetPlayerSpawn( deadzone_player_spawn_t *spawn )
{
	if( !spawn || !g_deadzone_server.player_active )
		return false;

	*spawn = g_deadzone_server.player_spawn;
	return true;
}

qboolean SV_DeadZoneGetPlayerState( deadzone_player_state_t *state )
{
	if( !state || !g_deadzone_server.player_active )
		return false;

	*state = g_deadzone_server.player_state;
	return true;
}

qboolean SV_DeadZoneSimulatePlayer( const deadzone_move_intent_t *intent,
	float frame_seconds, deadzone_player_state_t *state )
{
	deadzone_player_state_t simulated;
	deadzone_result_t result;

	if( !intent || !state || !g_deadzone_server.player_active ||
		g_deadzone_server.server_api.version < DEADZONE_SERVER_API_VERSION_4 ||
		!g_deadzone_server.server_api.simulate_player )
	{
		return false;
	}

	memset( &simulated, 0, sizeof( simulated ));
	simulated.size = sizeof( simulated );
	result = g_deadzone_server.server_api.simulate_player(
		g_deadzone_server.server_api.context, intent, frame_seconds, &simulated );
	if( result != DEADZONE_RESULT_SUCCESS ||
		!SV_DeadZoneValidatePlayerState( &simulated ))
	{
		Con_Printf( S_ERROR "DeadZone native loader: server movement frame failed validation (result %d)\n",
			result );
		return false;
	}

	g_deadzone_server.player_state = simulated;
	*state = simulated;
	return true;
}

void SV_DeadZoneMapUnloaded( void )
{
	if( !SV_IsDeadZoneServerLoaded() || !g_deadzone_server.map_active )
		return;

	if( !Host_IsDedicated() )
		CL_DeadZoneStopLocalMap();

	if( g_deadzone_server.player_active )
	{
		g_deadzone_server.server_api.despawn_player(
			g_deadzone_server.server_api.context,
			g_deadzone_server.player_spawn.player_id );
		g_deadzone_server.player_active = false;
		memset( &g_deadzone_server.player_spawn, 0,
			sizeof( g_deadzone_server.player_spawn ));
		memset( &g_deadzone_server.player_state, 0,
			sizeof( g_deadzone_server.player_state ));
	}

	g_deadzone_server.server_api.map_unloaded( g_deadzone_server.server_api.context );
	g_deadzone_server.map_active = false;
	Con_Printf( "DeadZone native loader: map unload complete\n" );
}

void SV_UnloadDeadZoneServer( void )
{
	if( !SV_IsDeadZoneServerLoaded() )
		return;

	SV_DeadZoneMapUnloaded();
	g_deadzone_server.server_api.shutdown( g_deadzone_server.server_api.context );
	Con_Printf( "DeadZone native loader: server API shutdown complete\n" );
	Cvar_FullSet( "host_gameloaded", "0", FCVAR_READ_ONLY );
	COM_FreeLibrary( g_deadzone_server.library );
	memset( &g_deadzone_server, 0, sizeof( g_deadzone_server ));
	g_deadzone_reported_player_hull = false;
}
