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

#if defined( _WIN32 ) && defined( _MSC_VER )
#define DEADZONE_CLIENT_API_CALL __cdecl
#define DEADZONE_CLIENT_API_EXPORT __declspec( dllexport )
#else
#define DEADZONE_CLIENT_API_CALL
#define DEADZONE_CLIENT_API_EXPORT
#endif

#define DEADZONE_CLIENT_API_VERSION_1 1u
#define DEADZONE_CLIENT_API_VERSION DEADZONE_CLIENT_API_VERSION_1
#define DEADZONE_CLIENT_QUERY_ENTRY "DeadZone_ClientQuery"

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

/* Filled by the client module. The engine ignores fields beyond size. */
typedef struct deadzone_client_api_s
{
	uint32_t size;
	uint32_t version;
	void *context;
	deadzone_client_shutdown_fn shutdown;
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
