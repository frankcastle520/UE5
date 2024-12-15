// Copyright Epic Games, Inc. All Rights Reserved.
#version 450

uniform vec2 u_aa_radius_px;
uniform vec2 u_card_size_cm;
uniform vec2 u_viewport_size_px;

in noperspective vec2 g_uv;
in noperspective vec2 g_line_extents;

out vec4 frag_color;

$include <include/units.glsli>

void main()
{
    RenderUnits units = {u_card_size_cm, u_viewport_size_px};
    vec2 aa_radius_cm = compute_antialias_params_cm(u_aa_radius_px, units);

    // We render a quad that is fattened by r, giving total width of the line to be w+r.
    // g_line_extents.x holds original width w, and g_uv.x in [-(w+r), w+r].
    // Far edge   : (w+r)/w  =  1+(w/r)
    // Close edge : (w-r)/w  =  1-(w/r)
    // This way the smoothing is centered around 'w'.

    // Offset from 1.0 (-/+) of smooth step start/end
    vec2 ss_offset = aa_radius_cm / g_line_extents;
    // Squeeze-factor (asymmetric squeeze of smoothstep) move start offset so that it's always 0 if ss_offset < 1.0
    float ss_sqf = min(1.0/max(ss_offset.x, 0.001), 1.0);

    // Approximate the height of erf (gaussian convolution) using piecewise linear approximation (x [0,0.5], 0.5x+0.25 [0.5,1.5], 1.0 [1.5,Inf])
    float width_aa_ratio = g_line_extents.x / max(aa_radius_cm.x,0.001);
    float ss_scf = min(min(width_aa_ratio, 0.5*width_aa_ratio + 0.25), 1.0);

    float au = ss_scf * (1.0 - smoothstep( 1.0 - ss_sqf*ss_offset.x, 1.0 + ss_offset.x, abs(g_uv.x / g_line_extents.x) ));
    float av = 1.0 - smoothstep( 1.0, 1.0 + ss_offset.y, abs(g_uv.y / g_line_extents.y) );
    
    frag_color = vec4(1.0,1.0,1.0, min(av, au));
}
