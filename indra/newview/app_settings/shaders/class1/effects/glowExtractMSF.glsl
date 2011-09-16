/** 
 * @file glowExtractF.glsl
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
 


#extension GL_ARB_texture_rectangle : enable
#extension GL_ARB_texture_multisample : enable

uniform sampler2DMS diffuseMap;
uniform float minLuminance;
uniform float maxExtractAlpha;
uniform vec3 lumWeights;
uniform vec3 warmthWeights;
uniform float warmthAmount;

VARYING vec2 vary_texcoord0;

void main()
{
	ivec2 itc = ivec2(vary_texcoord0.xy);
	vec4 fcol = vec4(0,0,0,0);

	for (int i = 0; i < samples; i++)
	{
		vec4 col = texelFetch(diffuseMap, itc, i);	

		/// CALCULATING LUMINANCE (Using NTSC lum weights)
		/// http://en.wikipedia.org/wiki/Luma_%28video%29
		float lum = smoothstep(minLuminance, minLuminance+1.0, dot(col.rgb, lumWeights ) );
		float warmth = smoothstep(minLuminance, minLuminance+1.0, max(col.r * warmthWeights.r, max(col.g * warmthWeights.g, col.b * warmthWeights.b)) ); 
	
		fcol += vec4(col.rgb, max(col.a, mix(lum, warmth, warmthAmount) * maxExtractAlpha));
	}

	gl_FragColor = fcol/samples;
}
