// Copyright Epic Games, Inc. All Rights Reserved.
#version 450

in noperspective vec3 g_tan;
in noperspective float g_seed;
in noperspective float g_ucoord;
in noperspective float g_depth;

in noperspective vec2 g_uv;
in noperspective vec2 g_line_extents;

out vec4 frag_depth_color;
out vec4 frag_ucoord_color;
out vec4 frag_seed_color;
out vec3 frag_tan_vec;

void main()
{
    frag_depth_color = vec4(g_depth, g_depth, g_depth, 1.0);
    frag_ucoord_color = vec4(g_ucoord, g_ucoord, g_ucoord, 1.0);
    frag_seed_color = vec4(g_seed, g_ucoord, g_ucoord, 1.0);
    frag_tan_vec = g_tan;
}
