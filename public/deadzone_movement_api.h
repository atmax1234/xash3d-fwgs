/*
deadzone_movement_api.h - shared DeadZone native movement ABI records
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

#ifndef DEADZONE_MOVEMENT_API_H
#define DEADZONE_MOVEMENT_API_H

#include <stdint.h>

#define DEADZONE_INPUT_SAMPLE_VERSION 1u
#define DEADZONE_MOVE_INTENT_VERSION 1u
#define DEADZONE_PLAYER_STATE_VERSION 1u
#define DEADZONE_PLAYER_TRACE_REQUEST_VERSION 1u
#define DEADZONE_PLAYER_TRACE_RESULT_VERSION 1u

#define DEADZONE_PLAYER_STATE_GROUNDED       (1u << 0)
#define DEADZONE_PLAYER_STATE_HIT_WORLD      (1u << 1)
#define DEADZONE_PLAYER_TRACE_START_SOLID    (1u << 0)
#define DEADZONE_PLAYER_TRACE_ALL_SOLID      (1u << 1)

/* Engine-collected local input. Borrowed only during the client callback. */
typedef struct deadzone_input_sample_s
{
	uint32_t size;
	uint32_t version;
	uint32_t player_id;
	uint32_t sequence;
	float forward_axis;
	float side_axis;
	float view_yaw_degrees;
	float frame_seconds;
} deadzone_input_sample_t;

/* Client-authored intent. This is not authoritative player state. */
typedef struct deadzone_move_intent_s
{
	uint32_t size;
	uint32_t version;
	uint32_t player_id;
	uint32_t sequence;
	float forward_axis;
	float side_axis;
	float view_yaw_degrees;
} deadzone_move_intent_t;

/* Server-authored state copied by the engine after each simulation call. */
typedef struct deadzone_player_state_s
{
	uint32_t size;
	uint32_t version;
	uint32_t player_id;
	uint32_t tick;
	float origin[3];
	float velocity[3];
	float view_angles[3];
	float eye_height;
	uint32_t flags;
} deadzone_player_state_t;

/* Server request to the engine-owned world collision implementation. */
typedef struct deadzone_player_trace_request_s
{
	uint32_t size;
	uint32_t version;
	uint32_t player_id;
	float start[3];
	float end[3];
} deadzone_player_trace_request_t;

/* Engine-authored trace result copied before the callback returns. */
typedef struct deadzone_player_trace_result_s
{
	uint32_t size;
	uint32_t version;
	float fraction;
	float end[3];
	float plane_normal[3];
	uint32_t flags;
} deadzone_player_trace_result_t;

#endif /* DEADZONE_MOVEMENT_API_H */
