// Copyright Epic Games, Inc. All Rights Reserved.
#version 450

// Orthographic projection matrix
uniform mat4 u_projection;
// Card size in physical units
uniform vec2 u_card_size_cm;

in vec2 in_pos;
in vec3 in_norm;
in vec3 in_tan;

out noperspective vec2 v_uv;
out noperspective vec3 v_tan;
// out noperspective vec3 v_bitan;
out noperspective vec3 v_norm;

void main()
{
    v_uv = in_pos / u_card_size_cm;
    v_tan = in_tan;
    v_norm = in_norm;

    gl_Position = u_projection * vec4(in_pos, 0.0, 1.0);
}
