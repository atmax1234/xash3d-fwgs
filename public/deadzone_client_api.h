/*
deadzone_client_api.h - versioned DeadZone engine/client module ABI
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

#ifndef DEADZONE_CLIENT_API_H
#define DEADZONE_CLIENT_API_H

#include <stddef.h>
#include <stdint.h>
#include "deadzone_movement_api.h"

#if defined( _WIN32 ) && defined( _MSC_VER )
#define DEADZONE_CLIENT_API_CALL __cdecl
#define DEADZONE_CLIENT_API_EXPORT __declspec( dllexport )
#else
#define DEADZONE_CLIENT_API_CALL
#define DEADZONE_CLIENT_API_EXPORT
#endif

#define DEADZONE_CLIENT_API_VERSION_1 1u
#define DEADZONE_CLIENT_API_VERSION_2 2u
#define DEADZONE_CLIENT_API_VERSION_3 3u
#define DEADZONE_CLIENT_API_VERSION DEADZONE_CLIENT_API_VERSION_3
#define DEADZONE_CLIENT_QUERY_ENTRY "DeadZone_ClientQuery"
#define DEADZONE_CAMERA_INPUT_VERSION 1u
#define DEADZONE_CAMERA_STATE_VERSION 1u

typedef int32_t deadzone_client_result_t;

#define DEADZONE_CLIENT_RESULT_SUCCESS              ((deadzone_client_result_t)0)
#define DEADZONE_CLIENT_RESULT_INVALID_ARGUMENT     ((deadzone_client_result_t)-1)
#define DEADZONE_CLIENT_RESULT_UNSUPPORTED_VERSION  ((deadzone_client_result_t)-2)
#define DEADZONE_CLIENT_RESULT_INVALID_TABLE        ((deadzone_client_result_t)-3)

typedef int32_t deadzone_client_log_level_t;

#define DEADZONE_CLIENT_LOG_INFO     ((deadzone_client_log_level_t)0)
#define DEADZONE_CLIENT_LOG_WARNING  ((deadzone_client_log_level_t)1)
#define DEADZONE_CLIENT_LOG_ERROR    ((deadzone_client_log_level_t)2)

typedef void ( DEADZONE_CLIENT_API_CALL *deadzone_client_log_message_fn )(
	void *context, deadzone_client_log_level_t level, const char *message,
	uint32_t message_length );

/* Owned by the engine and valid until the client shutdown callback returns. */
typedef struct deadzone_client_engine_api_s
{
	uint32_t size;
	uint32_t version;
	void *context;
	deadzone_client_log_message_fn log_message;
} deadzone_client_engine_api_t;

typedef void ( DEADZONE_CLIENT_API_CALL *deadzone_client_shutdown_fn )( void *context );

/* Borrowed server-authoritative pose. Valid only during camera_frame. */
typedef struct deadzone_camera_input_s
{
	uint32_t size;
	uint32_t version;
	uint32_t player_id;
	float player_origin[3];
	float player_view_angles[3];
	float eye_height;
	float frame_time;
} deadzone_camera_input_t;

/* Filled by the client module and copied by the engine before return. */
typedef struct deadzone_camera_state_s
{
	uint32_t size;
	uint32_t version;
	uint32_t player_id;
	float view_origin[3];
	float view_angles[3];
	float horizontal_fov_degrees;
} deadzone_camera_state_t;

typedef deadzone_client_result_t ( DEADZONE_CLIENT_API_CALL *deadzone_client_camera_frame_fn )(
	void *context, const deadzone_camera_input_t *input,
	deadzone_camera_state_t *camera );
typedef deadzone_client_result_t ( DEADZONE_CLIENT_API_CALL *deadzone_client_input_frame_fn )(
	void *context, const deadzone_input_sample_t *input,
	deadzone_move_intent_t *intent );

/* Filled by the client module. The engine ignores fields beyond size. */
typedef struct deadzone_client_api_s
{
	uint32_t size;
	uint32_t version;
	void *context;
	deadzone_client_shutdown_fn shutdown;
	/* API v2 fields begin here. */
	deadzone_client_camera_frame_fn camera_frame;
	/* API v3 field begins here. */
	deadzone_client_input_frame_fn input_frame;
} deadzone_client_api_t;

typedef deadzone_client_result_t ( DEADZONE_CLIENT_API_CALL *deadzone_client_query_fn )(
	uint32_t requested_version, const deadzone_client_engine_api_t *engine_api,
	deadzone_client_api_t *client_api );

#if defined( __cplusplus )
extern "C"
{
#endif

DEADZONE_CLIENT_API_EXPORT deadzone_client_result_t DEADZONE_CLIENT_API_CALL
DeadZone_ClientQuery( uint32_t requested_version,
	const deadzone_client_engine_api_t *engine_api,
	deadzone_client_api_t *client_api );

#if defined( __cplusplus )
}
#endif

#endif /* DEADZONE_CLIENT_API_H */
