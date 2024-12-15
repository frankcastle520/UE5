// Copyright Epic Games, Inc. All Rights Reserved.
#version 450

layout (lines_adjacency) in;
layout (triangle_strip, max_vertices = 4) out;

// Amount to increase width
// TODO: Should probably remove this as it should always be 1
uniform float u_dilation;
// Card size in physical units
uniform vec2 u_card_size_cm;
// Texture output size in pxels
uniform vec2 u_viewport_size_px;
// Anti-aliasing amount pixels
uniform vec2 u_aa_radius_px;
// Orthographic projection matrix
uniform mat4 u_projection;

in vec2 v_line_pos_cm[];
in float v_line_width_cm[];
in float v_line_depth_cm[];

out noperspective vec2 g_line_extents;
out noperspective vec2 g_uv;

$include <include/line_funcs_gs.glsli>
$include <include/lines_cov/attributes_gs.glsli>

void main()
{
    gs_emit_vertices();
}