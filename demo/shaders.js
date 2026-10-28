// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's.
export const VERSION = '#version 410 core\n';

// kQuadVertex, source/Shaders.cpp
export const QUAD_VERTEX = `
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
`;

// kStrokeVertex, source/Shaders.cpp
export const STROKE_VERTEX = `
layout( location = 0 ) in vec4 Ends;   //x0 y0 x1 y1, canvas units
layout( location = 1 ) in vec2 Radii;  //r0 r1
layout( location = 2 ) in vec4 Ink;    //straight rgb, alpha
uniform vec2 Canvas;                   //(aspect, 1)
uniform float PixelSize;               //canvas units per pixel
out vec2 place;
flat out vec4 ends;
flat out vec2 radii;
flat out vec4 ink;
void main()
{
	vec2 p0   = Ends.xy;
	vec2 p1   = Ends.zw;
	vec2 d    = p1 - p0;
	float len = length( d );
	vec2 dir  = len > 1e-9 ? d / len : vec2( 1.0, 0.0 );
	vec2 nrm  = vec2( -dir.y, dir.x );
	//The capsule's bounds, and a pixel and a half for the ramp.
	float r      = max( Radii.x, Radii.y ) + 1.5 * PixelSize;
	int corner   = gl_VertexID;
	float along  = ( corner == 0 || corner == 2 ) ? -r : len + r;
	float across = corner < 2 ? -r : r;
	vec2 p       = p0 + dir * along + nrm * across;
	place        = p;
	ends         = Ends;
	radii        = Radii;
	ink          = Ink;
	gl_Position  = vec4( p.x / ( 0.5 * Canvas.x ), p.y / 0.5, 0.0, 1.0 );
}
`;

// kStrokeFragment, source/Shaders.cpp
export const STROKE_FRAGMENT = `
in vec2 place;
flat in vec4 ends;
flat in vec2 radii;
flat in vec4 ink;
uniform float PixelSize;
out vec4 fragColour;
void main()
{
	//Distance to the capsule whose radius runs from r0 to r1 along it.
	vec2 pa  = place - ends.xy;
	vec2 ba  = ends.zw - ends.xy;
	float h  = clamp( dot( pa, ba ) / max( dot( ba, ba ), 1e-18 ), 0.0, 1.0 );
	float r  = mix( radii.x, radii.y, h );
	float d  = length( pa - ba * h );
	//A one-pixel box ramp: across a line of radius r (r of at least half a
	//pixel) the coverage sums to exactly 2r pixels, wherever it lands.
	float cover = clamp( ( r - d ) / PixelSize + 0.5, 0.0, 1.0 );
	float a     = cover * ink.a;
	fragColour  = vec4( ink.rgb * a, a );
}
`;

// kReduce, source/Shaders.cpp
export const REDUCE = `
in vec2 uv;
out vec4 fragColour;
uniform sampler2D Source;
uniform ivec2 SourceSize;
void main()
{
	ivec2 base = ivec2( gl_FragCoord.xy ) * 4;
	vec4 sum   = vec4( 0.0 );
	for( int y = 0; y < 4; ++y )
		for( int x = 0; x < 4; ++x )
			sum += texelFetch( Source, min( base + ivec2( x, y ), SourceSize - 1 ), 0 );
	fragColour = sum / 16.0;
}
`;

// kBlur, source/Shaders.cpp
export const BLUR = `
in vec2 uv;
out vec4 fragColour;
uniform sampler2D Source;
uniform ivec2 SourceSize;
uniform vec2 Direction;   //(1,0) or (0,1)
uniform float Sigma;      //texels
void main()
{
	ivec2 at   = ivec2( gl_FragCoord.xy );
	int reach  = int( ceil( 3.0 * Sigma ) );
	vec4 sum   = vec4( 0.0 );
	float norm = 0.0;
	for( int i = -24; i <= 24; ++i )
	{
		if( i < -reach || i > reach )
			continue;
		float w     = exp( -0.5 * float( i * i ) / ( Sigma * Sigma ) );
		ivec2 q     = clamp( at + ivec2( Direction ) * i, ivec2( 0 ), SourceSize - 1 );
		sum        += texelFetch( Source, q, 0 ) * w;
		norm       += w;
	}
	fragColour = sum / norm;
}
`;

// kComposite, source/Shaders.cpp
export const COMPOSITE = `
in vec2 uv;
out vec4 fragColour;
uniform sampler2D Clip;
uniform sampler2D Strokes;
uniform sampler2D Glow;
uniform vec2 ClipMax;      //the clip's texture coordinates at its far corner
uniform vec2 GlowMax;      //the same for the quarter-size glow, rounded up
uniform int HasClip;
uniform int BackdropMode;  //0 none, 1 black, 2 paper
uniform float GlowGain;
uniform float Strength;    //Mix, on the effect
void main()
{
	vec4 clip = HasClip == 1 ? texture( Clip, uv * ClipMax ) : vec4( 0.0 );
	vec4 back = BackdropMode == 0 ? vec4( 0.0 )
	          : BackdropMode == 1 ? vec4( 0.0, 0.0, 0.0, 1.0 ) : vec4( 0.95, 0.93, 0.88, 1.0 );
	vec4 base = clip + back * ( 1.0 - clip.a );
	vec4 g    = min( texture( Glow, uv * GlowMax ) * GlowGain, vec4( 1.0 ) );
	base      = g + base * ( 1.0 - g.a );
	vec4 s    = texture( Strokes, uv );
	vec4 o    = s + base * ( 1.0 - s.a );
	fragColour = HasClip == 1 ? mix( clip, o, Strength ) : o;
}
`;
