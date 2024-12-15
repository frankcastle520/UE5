// Copyright Epic Games, Inc. All Rights Reserved.
#version 450

uniform sampler2D u_coverage_tx;
uniform sampler2D u_wtan_tx;

in noperspective vec2 v_uv;
in noperspective vec3 v_tan;
// in noperspective vec3 v_bitan;
in noperspective vec3 v_norm;

out vec4 frag_color;

void main()
{
    // alpha test coverage texture
    if ( texture(u_coverage_tx, v_uv).a < 0.001 )
        discard;

    vec3 wtan = texture(u_wtan_tx, v_uv).xyz;

    vec3 lh_norm = -v_norm;
    vec3 bitan = cross(lh_norm,v_tan);

    // Project world-space tangent into interpolated tangent-space
    // TODO: This is not the recommended method from MikkTSpace docs, compare with UE approach for normal maps
    vec3 ltan = -vec3(dot(wtan,normalize(v_tan)), dot(wtan,normalize(bitan)), dot(wtan,normalize(lh_norm)));

    // tangent values need to be re-mapped from [-1,1] to [0,1]
    frag_color = vec4(0.5*normalize(ltan) + 0.5, 1.0);
}
