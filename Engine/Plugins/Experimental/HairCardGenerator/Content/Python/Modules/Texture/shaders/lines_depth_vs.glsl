// Copyright Epic Games, Inc. All Rights Reserved.
#version 450

in vec2 in_position;
in vec4 in_info;
in vec3 in_tan;

out vec2 v_line_pos_cm;
out vec3 v_line_tan_cm;
out float v_line_width_cm;
out float v_line_depth_cm;
out float v_line_seed;
out float v_line_u_coord;

void main()
{
    v_line_pos_cm = in_position;
    v_line_tan_cm = in_tan;
    v_line_width_cm = in_info.x;
    v_line_depth_cm = in_info.y;
    v_line_seed = in_info.z;
    v_line_u_coord = in_info.w;
}
