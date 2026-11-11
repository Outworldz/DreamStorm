/**
 * @file class1\environment\waterV.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

uniform mat4 modelview_matrix;
uniform mat3 normal_matrix;
uniform mat4 modelview_projection_matrix;

in vec3 position;


void calcAtmospherics(vec3 inPositionEye);

uniform vec2 waveDir1;
uniform vec2 waveDir2;
uniform float time;
uniform vec3 eyeVec;
uniform float waterHeight;
uniform vec3 lightDir;

// <FS:Outworldz> Sea waves: four Gerstner waves, the same as the OpenSim PhysX
// engine's, so boats bob on the crests drawn here. All zero on flat water.
uniform vec4 waveK;      // wave number per wave
uniform vec4 waveAmp;    // amplitude
uniform vec4 waveQA;     // sideways pull, Gerstner Q times amplitude
uniform vec4 waveDirX;   // travel direction
uniform vec4 waveDirY;
uniform vec4 wavePhase;  // phase at the agent region's origin, now
// </FS:Outworldz>

out vec4 refCoord;
out vec4 littleWave;
out vec4 view;
out vec3 vary_position;
out vec3 vary_light_dir;
out vec3 vary_tangent;
out vec3 vary_normal;
out vec2 vary_fragcoord;

float wave(vec2 v, float t, float f, vec2 d, float s)
{
   return (dot(d, v)*f + t*s)*f;
}

// <FS:Outworldz> Sea waves: how far the surface point resting at p moves,
// and the surface normal and tangent there. fade scales the sea down far away.
vec3 seaWaves(vec2 p, float fade, out vec3 n, out vec3 t)
{
    vec4 theta = waveK * (waveDirX * p.x + waveDirY * p.y) + wavePhase;
    vec4 c = cos(theta);
    vec4 s = sin(theta);
    vec4 amp = waveAmp * fade;
    vec4 qa = waveQA * fade;

    vec3 move = vec3(dot(waveDirX, -qa * s), dot(waveDirY, -qa * s), dot(amp, c));

    // Slopes along x and y; the normal is their cross product.
    vec4 qc = waveK * qa * c;
    vec4 ks = waveK * amp * s;
    vec3 along_x = vec3(1.0 - dot(qc, waveDirX * waveDirX), -dot(qc, waveDirX * waveDirY), -dot(ks, waveDirX));
    vec3 along_y = vec3(-dot(qc, waveDirX * waveDirY), 1.0 - dot(qc, waveDirY * waveDirY), -dot(ks, waveDirY));
    n = normalize(cross(along_x, along_y));
    t = normalize(along_x);
    return move;
}
// </FS:Outworldz>

void main()
{
    // <FS:Outworldz> Sea waves: move the vertex with the sea, fading out far away.
    vec3 sea_normal;
    vec3 sea_tangent;
    float sea_fade = 1.0 - smoothstep(384.0, 768.0, length(position.xy - eyeVec.xy));
    vec3 sea_position = position + seaWaves(position.xy, sea_fade, sea_normal, sea_tangent);
    // </FS:Outworldz>

    //transform vertex
    vec4 pos = vec4(sea_position, 1.0); // <FS:Outworldz/> was position.xyz
    mat4 modelViewProj = modelview_projection_matrix;

    vary_position = (modelview_matrix * pos).xyz;
    vary_light_dir = normal_matrix * lightDir;
    vary_normal = normal_matrix * sea_normal;   // <FS:Outworldz/> was vec3(0, 0, 1)
    vary_tangent = normal_matrix * sea_tangent; // <FS:Outworldz/> was vec3(1, 0, 0)

    vec4 oPosition;

    //get view vector
    vec3 oEyeVec;
    oEyeVec.xyz = pos.xyz-eyeVec;

    float d = length(oEyeVec.xy);
    float ld = min(d, 2560.0);

    pos.xy = eyeVec.xy + oEyeVec.xy/d*ld;
    view.xyz = oEyeVec;

    d = clamp(ld/1536.0-0.5, 0.0, 1.0);
    d *= d;

    oPosition = vec4(sea_position, 1.0); // <FS:Outworldz/> was position
//  oPosition.z = mix(oPosition.z, max(eyeVec.z*0.75, 0.0), d); // SL-11589 remove "U" shaped horizon

    oPosition = modelViewProj * oPosition;

    refCoord.xyz = oPosition.xyz + vec3(0,0,0.2);

    //get wave position parameter (create sweeping horizontal waves)
    vec3 v = pos.xyz;
    v.x += (cos(v.x*0.08/*+time*0.01*/)+sin(v.y*0.02))*6.0;

    //push position for further horizon effect.
    pos.xyz = oEyeVec.xyz*(waterHeight/oEyeVec.z);
    pos.w = 1.0;
    pos = modelview_matrix*pos;

    calcAtmospherics(pos.xyz);

    //pass wave parameters to pixel shader
    vec2 bigWave =  (v.xy) * vec2(0.04,0.04)  + waveDir1 * time * 0.055;
    //get two normal map (detail map) texture coordinates
    littleWave.xy = (v.xy) * vec2(0.45, 0.9)   + waveDir2 * time * 0.13;
    littleWave.zw = (v.xy) * vec2(0.1, 0.2) + waveDir1 * time * 0.1;
    view.w = bigWave.y;
    refCoord.w = bigWave.x;

    gl_Position = oPosition;
}
