// Copyright Epic Games, Inc. All Rights Reserved.
#version 450

in vec2 in_position;
in vec4 in_info;

out vec2 v_line_pos_cm;
out float v_line_width_cm;
out float v_line_depth_cm;

void main()
{
    v_line_pos_cm = in_position;
    v_line_width_cm = in_info.x;
    v_line_depth_cm = in_info.y;
}
