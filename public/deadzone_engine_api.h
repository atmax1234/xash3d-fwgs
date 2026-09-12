/*
deadzone_engine_api.h - versioned DeadZone engine/server module ABI
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

#ifndef DEADZONE_ENGINE_API_H
#define DEADZONE_ENGINE_API_H

#include <stddef.h>
#include <stdint.h>

#if defined( _WIN32 ) && defined( _MSC_VER )
#define DEADZONE_API_CALL __cdecl
#define DEADZONE_API_EXPORT __declspec( dllexport )
#else
#define DEADZONE_API_CALL
#define DEADZONE_API_EXPORT
#endif

#define DEADZONE_SERVER_API_VERSION_1 1u
#define DEADZONE_SERVER_API_VERSION_2 2u
#define DEADZONE_SERVER_API_VERSION_3 3u
#define DEADZONE_SERVER_API_VERSION DEADZONE_SERVER_API_VERSION_3
#define DEADZONE_MAP_INFO_VERSION 1u
#define DEADZONE_PLAYER_SPAWN_VERSION 1u
#define DEADZONE_SERVER_QUERY_ENTRY "DeadZone_ServerQuery"

typedef int32_t deadzone_result_t;

#define DEADZONE_RESULT_SUCCESS              ((deadzone_result_t)0)
#define DEADZONE_RESULT_INVALID_ARGUMENT     ((deadzone_result_t)-1)
#define DEADZONE_RESULT_UNSUPPORTED_VERSION  ((deadzone_result_t)-2)
#define DEADZONE_RESULT_INVALID_TABLE        ((deadzone_result_t)-3)

typedef int32_t deadzone_log_level_t;

#define DEADZONE_LOG_INFO     ((deadzone_log_level_t)0)
#define DEADZONE_LOG_WARNING  ((deadzone_log_level_t)1)
#define DEADZONE_LOG_ERROR    ((deadzone_log_level_t)2)

typedef void ( DEADZONE_API_CALL *deadzone_log_message_fn )( void *context,
	deadzone_log_level_t level, const char *message, uint32_t message_length );

/* Owned by the engine and valid until the module shutdown callback returns. */
typedef struct deadzone_engine_api_s
{
	uint32_t size;
	uint32_t version;
	void *context;
	deadzone_log_message_fn log_message;
} deadzone_engine_api_t;

typedef void ( DEADZONE_API_CALL *deadzone_server_shutdown_fn )( void *context );

/* Borrowed map metadata. The strings and table are valid only during the callback. */
typedef struct deadzone_map_info_s
{
	uint32_t size;
	uint32_t version;
	const char *name;
	uint32_t name_length;
	uint32_t bsp_version;
	uint32_t checksum;
	float mins[3];
	float maxs[3];
	uint32_t plane_count;
	uint32_t surface_count;
	uint32_t leaf_count;
} deadzone_map_info_t;

typedef deadzone_result_t ( DEADZONE_API_CALL *deadzone_server_map_loaded_fn )(
	void *context, const deadzone_map_info_t *map_info );
typedef void ( DEADZONE_API_CALL *deadzone_server_map_unloaded_fn )( void *context );

/*
Server-authored primary-player state for the local spawn/camera proof. The
engine validates the returned pose against the loaded world before exposing it
to presentation. Movement and replication intentionally arrive in later APIs.
*/
typedef struct deadzone_player_spawn_s
{
	uint32_t size;
	uint32_t version;
	uint32_t player_id;
	float origin[3];
	float view_angles[3];
	float eye_height;
} deadzone_player_spawn_t;

typedef deadzone_result_t ( DEADZONE_API_CALL *deadzone_server_spawn_player_fn )(
	void *context, deadzone_player_spawn_t *spawn );
typedef void ( DEADZONE_API_CALL *deadzone_server_despawn_player_fn )(
	void *context, uint32_t player_id );

/* Filled by the server module. The engine ignores fields beyond size. */
typedef struct deadzone_server_api_s
{
	uint32_t size;
	uint32_t version;
	void *context;
	deadzone_server_shutdown_fn shutdown;
	/* API v2 fields begin here. */
	deadzone_server_map_loaded_fn map_loaded;
	deadzone_server_map_unloaded_fn map_unloaded;
	/* API v3 fields begin here. */
	deadzone_server_spawn_player_fn spawn_player;
	deadzone_server_despawn_player_fn despawn_player;
} deadzone_server_api_t;

typedef deadzone_result_t ( DEADZONE_API_CALL *deadzone_server_query_fn )(
	uint32_t requested_version, const deadzone_engine_api_t *engine_api,
	deadzone_server_api_t *server_api );

#if defined( __cplusplus )
extern "C"
{
#endif

DEADZONE_API_EXPORT deadzone_result_t DEADZONE_API_CALL DeadZone_ServerQuery(
	uint32_t requested_version, const deadzone_engine_api_t *engine_api,
	deadzone_server_api_t *server_api );

#if defined( __cplusplus )
}
#endif

#endif /* DEADZONE_ENGINE_API_H */
