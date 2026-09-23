#include "render.h"

#include "box3d/collision.h"
#include "rlgl.h"

#include <cstring>
#include <string>

#if defined( __EMSCRIPTEN__ )
#include <GLES3/gl3.h>
// WebGL2: GLSL ES 3.00. Samplers default to low precision, which would ruin the shadow depth compare.
static const char* s_glslHeader = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";
#else
static const char* s_glslHeader = "#version 330\n";
#endif

// Shader sources below are written without a #version line; the right header is added at load time.
static Shader LoadShaderVersioned( const char* vs, const char* fs )
{
	std::string v = vs ? std::string( s_glslHeader ) + vs : std::string();
	std::string f = fs ? std::string( s_glslHeader ) + fs : std::string();
	return LoadShaderFromMemory( vs ? v.c_str() : nullptr, fs ? f.c_str() : nullptr );
}

// ---------------------------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------------------------

static const char* s_litVS = R"(
in vec3 vertexPosition;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform mat4 lightVP;
uniform vec3 objScale;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vLocalPos;
out vec3 vLocalNormal;
out vec4 vLightPos;
void main()
{
	vec4 wp = matModel * vec4(vertexPosition, 1.0);
	vWorldPos = wp.xyz;
	vNormal = normalize((matNormal * vec4(vertexNormal, 0.0)).xyz);
	vLocalPos = vertexPosition * objScale;
	vLocalNormal = vertexNormal;
	vLightPos = lightVP * wp;
	gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* s_litFS = R"(
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vLocalPos;
in vec3 vLocalNormal;
in vec4 vLightPos;

uniform vec4 colDiffuse;
uniform int matType;
uniform vec3 objScale;
uniform float flash;
uniform vec3 sunDir;
uniform vec3 viewPos;
uniform sampler2D shadowMap;
uniform int shadowOn;
uniform float texel;
uniform float time;

out vec4 finalColor;

const vec3 horizonColor = vec3(0.78, 0.86, 0.93);
const vec3 cloudColor = vec3(0.93, 0.94, 0.97);
const float cloudY = -22.0;

float hash(vec3 p)
{
	p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
	p *= 17.0;
	return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 x)
{
	vec3 i = floor(x);
	vec3 f = fract(x);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(hash(i + vec3(0, 0, 0)), hash(i + vec3(1, 0, 0)), f.x),
				   mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
			   mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x),
				   mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

float fbm(vec3 p)
{
	float a = 0.5;
	float s = 0.0;
	for (int i = 0; i < 4; i++)
	{
		s += a * noise(p);
		p *= 2.03;
		a *= 0.5;
	}
	return s;
}

float shadowFactor(vec3 n)
{
	vec3 proj = vLightPos.xyz / vLightPos.w * 0.5 + 0.5;
	if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z > 1.0)
		return 1.0;
	float bias = max(0.0012 * (1.0 - dot(n, sunDir)), 0.00035);
	float s = 0.0;
	for (int x = -1; x <= 1; x++)
	{
		for (int y = -1; y <= 1; y++)
		{
			float d = texture(shadowMap, proj.xy + vec2(x, y) * texel).r;
			s += (proj.z - bias > d) ? 0.0 : 1.0;
		}
	}
	return s / 9.0;
}

// Darken the edges of boxes so stacked blocks read clearly.
float boxEdge()
{
	vec3 h = objScale * 0.5;
	vec3 d = h - abs(vLocalPos);
	vec3 an = abs(vLocalNormal);
	float e = 1e9;
	if (an.x < 0.5) e = min(e, d.x);
	if (an.y < 0.5) e = min(e, d.y);
	if (an.z < 0.5) e = min(e, d.z);
	return smoothstep(0.0, 0.04, e);
}

void main()
{
	vec3 N = normalize(vNormal);
	vec3 albedo = colDiffuse.rgb;
	float shininess = 16.0;
	float specK = 0.08;
	float edge = 1.0;
	float fresnelK = 0.0;
	vec3 p = vLocalPos;

	if (matType == 1) // wood
	{
		// grain runs along the longest axis
		vec2 across;
		float along;
		if (objScale.x >= objScale.y && objScale.x >= objScale.z) { across = p.yz; along = p.x; }
		else if (objScale.y >= objScale.z) { across = p.xz; along = p.y; }
		else { across = p.xy; along = p.z; }
		float n = fbm(vec3(along * 0.7, across * 5.0));
		float rings = sin((across.x * 1.3 + across.y * 0.7 + n * 0.9) * 26.0) * 0.5 + 0.5;
		float streak = fbm(vec3(along * 0.4, across * 22.0));
		albedo *= 0.78 + 0.22 * rings;
		albedo *= 0.85 + 0.3 * streak;
		edge = boxEdge();
		specK = 0.05;
	}
	else if (matType == 2) // stone
	{
		float n = fbm(p * 3.1 + vec3(7.0));
		float speck = noise(p * 38.0);
		albedo *= 0.78 + 0.35 * n;
		albedo *= 0.92 + 0.12 * speck;
		// subtle mossy tint on upward faces
		albedo = mix(albedo, vec3(0.45, 0.55, 0.35), smoothstep(0.62, 0.8, n) * max(N.y, 0.0) * 0.35);
		edge = boxEdge();
		specK = 0.04;
	}
	else if (matType == 3) // ice
	{
		float n = fbm(p * 4.0);
		float cracks = smoothstep(0.02, 0.0, abs(noise(p * 6.0) - 0.5)) * 0.35;
		albedo = mix(albedo, vec3(1.0), cracks + n * 0.15);
		shininess = 90.0;
		specK = 0.9;
		fresnelK = 0.55;
		edge = mix(1.0, boxEdge(), 0.35);
	}
	else if (matType == 4) // tnt
	{
		float h = objScale.y * 0.5;
		float band = step(abs(p.y), h * 0.28);
		float planks = step(0.9, fract((p.x + p.z) * 2.2));
		albedo = mix(albedo, vec3(0.95, 0.88, 0.65), band);
		albedo *= 1.0 - planks * 0.25 * (1.0 - band);
		edge = boxEdge();
		specK = 0.1;
	}
	else if (matType == 5) // metal
	{
		float n = fbm(p * 8.0);
		albedo *= 0.85 + 0.3 * n;
		shininess = 60.0;
		specK = 0.7;
		edge = mix(1.0, boxEdge(), 0.6);
	}
	else if (matType == 6) // island: grass on top, strata on the sides
	{
		if (N.y > 0.55)
		{
			float n = fbm(vWorldPos * 0.45);
			float m = fbm(vWorldPos * 2.5);
			albedo = mix(vec3(0.36, 0.60, 0.25), vec3(0.52, 0.72, 0.30), n);
			albedo *= 0.85 + 0.25 * m;
		}
		else
		{
			float strata = sin(vWorldPos.y * 5.0 + fbm(vWorldPos * 0.8) * 4.0) * 0.5 + 0.5;
			albedo = mix(vec3(0.46, 0.36, 0.27), vec3(0.60, 0.50, 0.40), strata);
			albedo *= 0.8 + 0.3 * fbm(vWorldPos * 3.0);
			// grass lip
			albedo = mix(albedo, vec3(0.36, 0.58, 0.24), smoothstep(-0.35, -0.1, p.y) * step(0.0, -p.y + 0.01));
		}
	}
	else if (matType == 7) // rock
	{
		float strata = sin(vWorldPos.y * 3.0 + fbm(vWorldPos * 0.6) * 5.0) * 0.5 + 0.5;
		albedo *= 0.75 + 0.3 * strata;
		albedo *= 0.8 + 0.3 * fbm(vWorldPos * 2.0);
	}
	else if (matType == 8) // royal robe with a golden hem
	{
		float hem = smoothstep(-objScale.y * 0.5 + 0.12, -objScale.y * 0.5 + 0.1, p.y);
		float fold = sin(atan(p.z, p.x) * 10.0) * 0.5 + 0.5;
		albedo *= 0.85 + 0.2 * fold;
		albedo = mix(albedo, vec3(1.0, 0.8, 0.2), hem);
		specK = 0.12 + hem * 0.6;
	}
	else if (matType == 10) // gold
	{
		shininess = 70.0;
		specK = 1.0;
		fresnelK = 0.3;
	}
	else if (matType == 11) // balloon
	{
		float stripes = step(0.5, fract(atan(p.z, p.x) * 1.2732));
		albedo = mix(albedo, vec3(1.0, 0.95, 0.85), stripes * 0.8);
		shininess = 50.0;
		specK = 0.6;
	}
	else if (matType == 0)
	{
		albedo *= 0.9 + 0.2 * fbm(vWorldPos * 2.0);
	}

	albedo *= mix(0.5, 1.0, edge);

	vec3 L = sunDir;
	vec3 V = normalize(viewPos - vWorldPos);
	vec3 H = normalize(L + V);
	float ndl = max(dot(N, L), 0.0);
	float sh = (shadowOn == 1) ? shadowFactor(N) : 1.0;

	vec3 sunCol = vec3(1.0, 0.93, 0.82) * 1.2;
	vec3 hemi = mix(vec3(0.55, 0.52, 0.52), vec3(0.52, 0.64, 0.86), N.y * 0.5 + 0.5);
	float spec = pow(max(dot(N, H), 0.0), shininess) * specK;
	float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0) * fresnelK;

	vec3 col = albedo * (hemi * 0.62 + sunCol * ndl * sh) + sunCol * spec * sh + vec3(fres);
	col += flash * vec3(1.0, 0.85, 0.5);

	float dist = length(viewPos - vWorldPos);
	float fog = 1.0 - exp(-pow(dist * 0.0075, 1.5));
	col = mix(col, horizonColor, clamp(fog, 0.0, 1.0) * 0.9);

	float sink = smoothstep(cloudY + 3.0, cloudY - 3.0, vWorldPos.y);
	col = mix(col, cloudColor, sink);

	finalColor = vec4(col, 1.0);
}
)";

static const char* s_depthVS = R"(
in vec3 vertexPosition;
uniform mat4 mvp;
void main()
{
	gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* s_depthFS = R"(
out vec4 finalColor;
void main()
{
	finalColor = vec4(1.0);
}
)";

static const char* s_skyFS = R"(
in vec2 fragTexCoord;
in vec4 fragColor;
uniform vec3 camPos;
uniform vec3 camFwd;
uniform vec3 camRight;
uniform vec3 camUp;
uniform float tanHalfFov;
uniform vec2 resolution;
uniform float time;
uniform vec3 sunDir;
out vec4 finalColor;

const vec3 horizon = vec3(0.78, 0.86, 0.93);
const vec3 zenith = vec3(0.20, 0.42, 0.78);
const float cloudY = -22.0;

float hash(vec2 p)
{
	p = fract(p * vec2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return fract(p.x * p.y);
}

float noise(vec2 x)
{
	vec2 i = floor(x);
	vec2 f = fract(x);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}

float fbm(vec2 p)
{
	float a = 0.5;
	float s = 0.0;
	for (int i = 0; i < 5; i++)
	{
		s += a * noise(p);
		p = p * 2.02 + vec2(1.7, 9.2);
		a *= 0.5;
	}
	return s;
}

void main()
{
	vec2 uv = (gl_FragCoord.xy / resolution) * 2.0 - 1.0;
	float aspect = resolution.x / resolution.y;
	vec3 dir = normalize(camFwd + uv.x * tanHalfFov * aspect * camRight + uv.y * tanHalfFov * camUp);

	float t = clamp(dir.y, 0.0, 1.0);
	vec3 sky = mix(horizon, zenith, pow(t, 0.5));
	float sd = max(dot(dir, sunDir), 0.0);
	sky += vec3(1.0, 0.9, 0.7) * pow(sd, 900.0) * 6.0;
	sky += vec3(1.0, 0.8, 0.55) * pow(sd, 10.0) * 0.22;

	if (dir.y > 0.0)
	{
		vec2 p = dir.xz / (dir.y + 0.1) * 1.3 + vec2(time * 0.006, 0.0);
		float c = fbm(p);
		sky = mix(sky, vec3(1.0, 0.99, 0.97), smoothstep(0.55, 0.85, c) * 0.6 * smoothstep(0.0, 0.25, dir.y));
	}
	else
	{
		float tt = (cloudY - camPos.y) / dir.y;
		if (tt > 0.0)
		{
			vec3 P = camPos + dir * tt;
			vec2 q = P.xz * 0.03 + vec2(time * 0.010, time * 0.004);
			float c = fbm(q);
			float c2 = fbm(q * 2.7 + vec2(3.1, 1.3));
			float dens = smoothstep(0.3, 0.8, c * 0.75 + c2 * 0.35);
			// fake lighting: sample towards the sun for self shadowing
			float cs = fbm(q + sunDir.xz * 0.08);
			float lit = clamp(0.6 + (c - cs) * 3.0, 0.3, 1.2);
			vec3 cl = mix(vec3(0.60, 0.67, 0.80), vec3(1.0, 0.98, 0.95) * lit, dens);
			float fogd = 1.0 - exp(-tt * 0.0035);
			sky = mix(cl, horizon, fogd);
		}
		else
		{
			sky = vec3(0.70, 0.76, 0.84);
		}
	}

	finalColor = vec4(sky, 1.0);
}
)";

// ---------------------------------------------------------------------------------------------
// Mesh helpers
// ---------------------------------------------------------------------------------------------

static Mesh BuildMesh( const std::vector<Vector3>& positions, const std::vector<Vector3>& normals )
{
	Mesh mesh = {};
	mesh.vertexCount = (int)positions.size();
	mesh.triangleCount = mesh.vertexCount / 3;
	mesh.vertices = (float*)MemAlloc( mesh.vertexCount * 3 * sizeof( float ) );
	mesh.normals = (float*)MemAlloc( mesh.vertexCount * 3 * sizeof( float ) );
	mesh.texcoords = (float*)MemAlloc( mesh.vertexCount * 2 * sizeof( float ) );
	for ( int i = 0; i < mesh.vertexCount; ++i )
	{
		mesh.vertices[3 * i + 0] = positions[i].x;
		mesh.vertices[3 * i + 1] = positions[i].y;
		mesh.vertices[3 * i + 2] = positions[i].z;
		mesh.normals[3 * i + 0] = normals[i].x;
		mesh.normals[3 * i + 1] = normals[i].y;
		mesh.normals[3 * i + 2] = normals[i].z;
		mesh.texcoords[2 * i + 0] = 0.0f;
		mesh.texcoords[2 * i + 1] = 0.0f;
	}
	UploadMesh( &mesh, false );
	return mesh;
}

static Mesh GenHullMesh( const b3HullData* hull )
{
	const b3Vec3* points = b3GetHullPoints( hull );
	const b3HullHalfEdge* edges = b3GetHullEdges( hull );
	const b3HullFace* faces = b3GetHullFaces( hull );
	const b3Plane* planes = b3GetHullPlanes( hull );

	std::vector<Vector3> pos;
	std::vector<Vector3> nrm;
	std::vector<Vector3> poly;

	for ( int f = 0; f < hull->faceCount; ++f )
	{
		poly.clear();
		int e0 = faces[f].edge;
		int e = e0;
		int guard = 0;
		do
		{
			poly.push_back( ToRl( points[edges[e].origin] ) );
			e = edges[e].next;
		}
		while ( e != e0 && ++guard < 256 );

		if ( poly.size() < 3 )
		{
			continue;
		}

		Vector3 n = ToRl( planes[f].normal );
		Vector3 a = poly[0];
		for ( size_t i = 1; i + 1 < poly.size(); ++i )
		{
			Vector3 b = poly[i];
			Vector3 c = poly[i + 1];
			Vector3 cr = Vector3CrossProduct( Vector3Subtract( b, a ), Vector3Subtract( c, a ) );
			if ( Vector3DotProduct( cr, n ) >= 0.0f )
			{
				pos.push_back( a );
				pos.push_back( b );
				pos.push_back( c );
			}
			else
			{
				pos.push_back( a );
				pos.push_back( c );
				pos.push_back( b );
			}
			nrm.push_back( n );
			nrm.push_back( n );
			nrm.push_back( n );
		}
	}

	return BuildMesh( pos, nrm );
}

static Mesh GenCapsule( float radius, float halfSegment, int slices, int ringsPerHemisphere )
{
	// Rings from the bottom pole to the top pole. The equator is duplicated to form the cylinder.
	struct Ring
	{
		float y;
		float r;
		float ny;
		float nr;
	};
	std::vector<Ring> rings;
	for ( int i = 0; i <= ringsPerHemisphere; ++i )
	{
		float lat = -PI * 0.5f + ( PI * 0.5f ) * i / ringsPerHemisphere;
		rings.push_back( { radius * sinf( lat ) - halfSegment, radius * cosf( lat ), sinf( lat ), cosf( lat ) } );
	}
	for ( int i = 0; i <= ringsPerHemisphere; ++i )
	{
		float lat = ( PI * 0.5f ) * i / ringsPerHemisphere;
		rings.push_back( { radius * sinf( lat ) + halfSegment, radius * cosf( lat ), sinf( lat ), cosf( lat ) } );
	}

	std::vector<Vector3> pos;
	std::vector<Vector3> nrm;
	auto vert = [&]( const Ring& ring, int s, Vector3& p, Vector3& n ) {
		float a = 2.0f * PI * s / slices;
		float c = cosf( a ), sn = sinf( a );
		p = { ring.r * c, ring.y, ring.r * sn };
		n = { ring.nr * c, ring.ny, ring.nr * sn };
	};

	for ( size_t j = 0; j + 1 < rings.size(); ++j )
	{
		for ( int s = 0; s < slices; ++s )
		{
			Vector3 p00, p01, p10, p11, n00, n01, n10, n11;
			vert( rings[j], s, p00, n00 );
			vert( rings[j], s + 1, p01, n01 );
			vert( rings[j + 1], s, p10, n10 );
			vert( rings[j + 1], s + 1, p11, n11 );
			// counter-clockwise when viewed from outside
			pos.push_back( p00 );
			nrm.push_back( n00 );
			pos.push_back( p10 );
			nrm.push_back( n10 );
			pos.push_back( p11 );
			nrm.push_back( n11 );
			pos.push_back( p00 );
			nrm.push_back( n00 );
			pos.push_back( p11 );
			nrm.push_back( n11 );
			pos.push_back( p01 );
			nrm.push_back( n01 );
		}
	}
	return BuildMesh( pos, nrm );
}

static RenderTexture2D LoadShadowmap( int size )
{
	RenderTexture2D target = {};
	target.id = rlLoadFramebuffer();
	target.texture.width = size;
	target.texture.height = size;
	if ( target.id > 0 )
	{
		rlEnableFramebuffer( target.id );
#if defined( __EMSCRIPTEN__ )
		// WebGL2 wants a sized depth format, which rlgl's ES path does not pick on its own
		unsigned int tex = 0;
		glGenTextures( 1, &tex );
		glBindTexture( GL_TEXTURE_2D, tex );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glBindTexture( GL_TEXTURE_2D, 0 );
		target.depth.id = tex;
#else
		target.depth.id = rlLoadTextureDepth( size, size, false );
#endif
		target.depth.width = size;
		target.depth.height = size;
		target.depth.format = 19;
		target.depth.mipmaps = 1;
		rlFramebufferAttach( target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0 );
		if ( rlFramebufferComplete( target.id ) == false )
		{
			TraceLog( LOG_WARNING, "Shadow map framebuffer incomplete" );
		}
		rlDisableFramebuffer();
	}
	return target;
}

// ---------------------------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------------------------

void Renderer::Init()
{
	m_lit = LoadShaderVersioned( s_litVS, s_litFS );
	m_lit.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation( m_lit, "matModel" );
	m_lit.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation( m_lit, "matNormal" );
	m_locMatType = GetShaderLocation( m_lit, "matType" );
	m_locObjScale = GetShaderLocation( m_lit, "objScale" );
	m_locFlash = GetShaderLocation( m_lit, "flash" );
	m_locLightDir = GetShaderLocation( m_lit, "sunDir" );
	m_locViewPos = GetShaderLocation( m_lit, "viewPos" );
	m_locLightVP = GetShaderLocation( m_lit, "lightVP" );
	m_locShadowMap = GetShaderLocation( m_lit, "shadowMap" );
	m_locTime = GetShaderLocation( m_lit, "time" );
	m_locShadowOn = GetShaderLocation( m_lit, "shadowOn" );
	m_locTexel = GetShaderLocation( m_lit, "texel" );

	m_depth = LoadShaderVersioned( s_depthVS, s_depthFS );
	m_sky = LoadShaderVersioned( nullptr, s_skyFS );
	m_skyLocCamPos = GetShaderLocation( m_sky, "camPos" );
	m_skyLocFwd = GetShaderLocation( m_sky, "camFwd" );
	m_skyLocRight = GetShaderLocation( m_sky, "camRight" );
	m_skyLocUp = GetShaderLocation( m_sky, "camUp" );
	m_skyLocTan = GetShaderLocation( m_sky, "tanHalfFov" );
	m_skyLocRes = GetShaderLocation( m_sky, "resolution" );
	m_skyLocTime = GetShaderLocation( m_sky, "time" );
	m_skyLocSun = GetShaderLocation( m_sky, "sunDir" );

	m_litMat = LoadMaterialDefault();
	m_litMat.shader = m_lit;
	m_depthMat = LoadMaterialDefault();
	m_depthMat.shader = m_depth;

	m_cube = GenMeshCube( 1.0f, 1.0f, 1.0f );
	m_sphere = GenMeshSphere( 1.0f, 14, 20 );
	m_cylinder = GenMeshCylinder( 1.0f, 1.0f, 14 );
	m_cone = GenMeshCone( 1.0f, 1.0f, 12 );

	m_shadowRT = LoadShadowmap( m_shadowSize );

	Image img = GenImageGradientRadial( 64, 64, 0.0f, WHITE, BLANK );
	m_softTex = LoadTextureFromImage( img );
	UnloadImage( img );
	SetTextureFilter( m_softTex, TEXTURE_FILTER_BILINEAR );

	m_lightDir = Vector3Normalize( { 0.45f, 0.78f, -0.42f } );
}

void Renderer::Shutdown()
{
	for ( auto& kv : m_hullMeshes )
	{
		UnloadMesh( kv.second );
	}
	m_hullMeshes.clear();
	for ( auto& kv : m_capsuleMeshes )
	{
		UnloadMesh( kv.second );
	}
	m_capsuleMeshes.clear();
	UnloadMesh( m_cube );
	UnloadMesh( m_sphere );
	UnloadMesh( m_cylinder );
	UnloadMesh( m_cone );
	UnloadTexture( m_softTex );
	if ( m_shadowRT.id > 0 )
	{
		rlUnloadFramebuffer( m_shadowRT.id );
		rlUnloadTexture( m_shadowRT.depth.id );
	}
	UnloadShader( m_lit );
	UnloadShader( m_depth );
	UnloadShader( m_sky );
}

void Renderer::ClearHullCache()
{
	for ( auto& kv : m_hullMeshes )
	{
		UnloadMesh( kv.second );
	}
	m_hullMeshes.clear();
}

const Mesh* Renderer::HullMesh( const b3HullData* hull )
{
	auto it = m_hullMeshes.find( hull );
	if ( it != m_hullMeshes.end() )
	{
		return &it->second;
	}
	Mesh m = GenHullMesh( hull );
	auto res = m_hullMeshes.emplace( hull, m );
	return &res.first->second;
}

const Mesh* Renderer::CapsuleMesh( float radius, float halfSegment )
{
	uint32_t a = (uint32_t)( radius * 1000.0f + 0.5f );
	uint32_t b = (uint32_t)( halfSegment * 1000.0f + 0.5f );
	uint64_t key = ( (uint64_t)a << 32 ) | b;
	auto it = m_capsuleMeshes.find( key );
	if ( it != m_capsuleMeshes.end() )
	{
		return &it->second;
	}
	Mesh m = GenCapsule( radius, halfSegment, 18, 7 );
	auto res = m_capsuleMeshes.emplace( key, m );
	return &res.first->second;
}

void Renderer::BeginScene( const Camera3D& camera, Vector3 shadowCenter, float shadowRadius, float time, Vector3 wind )
{
	m_items.clear();
	m_camera = camera;
	m_shadowCenter = shadowCenter;
	m_shadowRadius = shadowRadius;
	m_time = time;
	m_wind = wind;
}

void Renderer::AddMesh( const Mesh* mesh, Matrix model, Vector3 scale, Mat mat, Color tint, bool castShadow )
{
	m_items.push_back( { mesh, model, scale, mat, tint, 0.0f, castShadow } );
}

void Renderer::AddBox( Vector3 center, Quaternion rot, Vector3 half, Mat mat, Color tint, bool castShadow )
{
	Vector3 s = Vector3Scale( half, 2.0f );
	m_items.push_back( { &m_cube, ComposeTRS( center, rot, s ), s, mat, tint, 0.0f, castShadow } );
}

void Renderer::AddSphere( Vector3 center, float radius, Mat mat, Color tint, bool castShadow )
{
	Vector3 s{ radius, radius, radius };
	m_items.push_back( { &m_sphere, ComposeTRS( center, QuaternionIdentity(), s ), s, mat, tint, 0.0f, castShadow } );
}

void Renderer::AddCylinder( Vector3 a, Vector3 b, float radius, Mat mat, Color tint, bool castShadow )
{
	Vector3 d = Vector3Subtract( b, a );
	float len = Vector3Length( d );
	if ( len < 1e-4f )
	{
		return;
	}
	Vector3 dn = Vector3Scale( d, 1.0f / len );
	Quaternion q = QuaternionFromVector3ToVector3( { 0, 1, 0 }, dn );
	if ( dn.y < -0.9999f )
	{
		q = QuaternionFromAxisAngle( { 1, 0, 0 }, PI );
	}
	Vector3 s{ radius, len, radius };
	m_items.push_back( { &m_cylinder, ComposeTRS( a, q, s ), s, mat, tint, 0.0f, castShadow } );
}

void Renderer::AddEntity( const Entity* e, float alpha )
{
	Vector3 pos = Vector3Lerp( e->prevPos, e->pos, alpha );
	Quaternion rot = QuaternionNlerp( e->prevRot, e->rot, alpha );
	AddParts( e->parts, pos, rot, e->flash );
}

void Renderer::AddParts( const std::vector<Part>& parts, Vector3 pos, Quaternion rot, float flash )
{
	for ( const Part& part : parts )
	{
		if ( part.visible == false )
		{
			continue;
		}

		Vector3 wp = Vector3Add( pos, Vector3RotateByQuaternion( part.localPos, rot ) );
		Quaternion wr = QuaternionMultiply( rot, part.localRot );
		DrawItem item{};
		item.mat = part.mat;
		item.tint = part.tint;
		item.flash = flash;
		item.castShadow = true;

		switch ( part.geo )
		{
			case Geo::Box:
				item.mesh = &m_cube;
				item.scale = Vector3Scale( part.size, 2.0f );
				item.model = ComposeTRS( wp, wr, item.scale );
				break;
			case Geo::Sphere:
				item.mesh = &m_sphere;
				item.scale = part.size;
				item.model = ComposeTRS( wp, wr, part.size );
				break;
			case Geo::Capsule:
				item.mesh = CapsuleMesh( part.size.x, part.size.y );
				item.scale = { 1, 1, 1 };
				item.model = ComposeTRS( wp, wr, { 1, 1, 1 } );
				// the robe shader wants the full height in objScale.y
				item.scale = { 1.0f, 2.0f * ( part.size.x + part.size.y ), 1.0f };
				break;
			case Geo::Hull:
				item.mesh = HullMesh( part.hull );
				item.scale = { 1, 1, 1 };
				item.model = ComposeTRS( wp, wr, { 1, 1, 1 } );
				break;
		}
		m_items.push_back( item );
	}
}

Vector3 CannonAimDir( float yaw, float pitch )
{
	return Vector3{ sinf( yaw ) * cosf( pitch ), sinf( pitch ), cosf( yaw ) * cosf( pitch ) };
}

void Renderer::AddCannon( const CannonPose& pose )
{
	if ( pose.visible == false )
	{
		return;
	}

	Quaternion qYaw = QuaternionFromAxisAngle( { 0, 1, 0 }, pose.yaw );
	auto local = [&]( Vector3 v ) { return Vector3Add( pose.position, Vector3RotateByQuaternion( v, qYaw ) ); };

	Color wood{ 150, 98, 56, 255 };
	Color darkWood{ 96, 62, 36, 255 };
	Color iron{ 58, 60, 68, 255 };

	// carriage
	AddBox( local( { 0.0f, 0.42f, -0.2f } ), qYaw, { 0.36f, 0.12f, 0.85f }, Mat::Wood, wood );
	AddBox( local( { 0.34f, 0.72f, -0.05f } ), qYaw, { 0.07f, 0.3f, 0.55f }, Mat::Wood, darkWood );
	AddBox( local( { -0.34f, 0.72f, -0.05f } ), qYaw, { 0.07f, 0.3f, 0.55f }, Mat::Wood, darkWood );
	AddBox( local( { 0.0f, 0.3f, -1.05f } ), qYaw, { 0.3f, 0.1f, 0.12f }, Mat::Wood, darkWood );

	// wheels with spokes
	for ( int side = -1; side <= 1; side += 2 )
	{
		Vector3 c = local( { 0.52f * side, 0.46f, 0.15f } );
		Vector3 axis = Vector3RotateByQuaternion( { 1, 0, 0 }, qYaw );
		AddCylinder( Vector3Add( c, Vector3Scale( axis, -0.07f ) ), Vector3Add( c, Vector3Scale( axis, 0.07f ) ), 0.46f, Mat::Wood,
					 darkWood );
		AddCylinder( Vector3Add( c, Vector3Scale( axis, -0.09f ) ), Vector3Add( c, Vector3Scale( axis, 0.09f ) ), 0.12f, Mat::Metal,
					 iron );
		// iron tyre ring approximated by a thin, wider disk
		AddCylinder( Vector3Add( c, Vector3Scale( axis, -0.05f ) ), Vector3Add( c, Vector3Scale( axis, 0.05f ) ), 0.49f, Mat::Metal,
					 iron );
		for ( int k = 0; k < 3; ++k )
		{
			float a = pose.wheelSpin + k * PI / 3.0f;
			Quaternion qs = QuaternionMultiply( qYaw, QuaternionFromAxisAngle( { 1, 0, 0 }, a ) );
			Vector3 off = Vector3RotateByQuaternion( { 0.08f * side, 0, 0 }, qYaw );
			AddBox( Vector3Add( c, off ), qs, { 0.03f, 0.44f, 0.04f }, Mat::Wood, wood );
		}
	}

	// barrel
	Quaternion qPitch = QuaternionMultiply( qYaw, QuaternionFromAxisAngle( { 1, 0, 0 }, -pose.pitch ) );
	Vector3 pivot = local( { 0.0f, 0.95f, 0.0f } );
	Vector3 dir = CannonAimDir( pose.yaw, pose.pitch );
	Vector3 back = Vector3Add( pivot, Vector3Scale( dir, -0.75f - pose.recoil ) );
	Vector3 front = Vector3Add( pivot, Vector3Scale( dir, 1.55f - pose.recoil ) );
	AddCylinder( back, front, 0.24f, Mat::Metal, iron );
	AddCylinder( Vector3Add( front, Vector3Scale( dir, -0.14f ) ), Vector3Add( front, Vector3Scale( dir, 0.02f ) ), 0.3f, Mat::Metal,
				 iron );
	AddCylinder( Vector3Add( back, Vector3Scale( dir, 0.1f ) ), Vector3Add( back, Vector3Scale( dir, 0.3f ) ), 0.3f, Mat::Metal, iron );
	AddSphere( Vector3Add( back, Vector3Scale( dir, -0.06f ) ), 0.2f, Mat::Metal, iron );
	AddSphere( Vector3Add( back, Vector3Scale( dir, -0.25f ) ), 0.09f, Mat::Metal, iron );
	// trunnion
	Vector3 axis = Vector3RotateByQuaternion( { 1, 0, 0 }, qYaw );
	AddCylinder( Vector3Add( pivot, Vector3Scale( axis, -0.36f ) ), Vector3Add( pivot, Vector3Scale( axis, 0.36f ) ), 0.08f, Mat::Metal,
				 iron );
	(void)qPitch;
}

void Renderer::AddDecoration( const Decoration& d )
{
	Quaternion q = QuaternionFromAxisAngle( { 0, 1, 0 }, d.rot );
	float s = d.scale;
	switch ( d.type )
	{
		case Decoration::Tree:
		{
			AddCylinder( d.pos, Vector3Add( d.pos, { 0, 1.6f * s, 0 } ), 0.14f * s, Mat::Wood, { 110, 76, 48, 255 } );
			Color leaf = d.color;
			AddSphere( Vector3Add( d.pos, { 0, 2.0f * s, 0 } ), 0.9f * s, Mat::Plain, leaf );
			AddSphere( Vector3Add( d.pos, Vector3RotateByQuaternion( { 0.55f * s, 1.65f * s, 0.2f * s }, q ) ), 0.6f * s, Mat::Plain,
					   ColorBrightness( leaf, -0.1f ) );
			AddSphere( Vector3Add( d.pos, Vector3RotateByQuaternion( { -0.45f * s, 1.75f * s, -0.3f * s }, q ) ), 0.62f * s, Mat::Plain,
					   ColorBrightness( leaf, 0.08f ) );
			break;
		}
		case Decoration::Pine:
		{
			AddCylinder( d.pos, Vector3Add( d.pos, { 0, 0.8f * s, 0 } ), 0.12f * s, Mat::Wood, { 100, 70, 45, 255 } );
			for ( int i = 0; i < 3; ++i )
			{
				float y = ( 0.6f + i * 0.65f ) * s;
				float r = ( 0.95f - i * 0.25f ) * s;
				Vector3 sc{ r, 1.1f * s, r };
				AddMesh( &m_cone, ComposeTRS( Vector3Add( d.pos, { 0, y, 0 } ), q, sc ), sc, Mat::Plain,
						 ColorBrightness( d.color, -0.08f * i ) );
			}
			break;
		}
		case Decoration::Rock:
		{
			Vector3 sc{ s, s * 0.7f, s };
			AddMesh( &m_sphere, ComposeTRS( d.pos, q, sc ), sc, Mat::Rock, { 140, 130, 120, 255 } );
			break;
		}
		case Decoration::Grass:
		{
			for ( int i = 0; i < 3; ++i )
			{
				Quaternion qi = QuaternionMultiply( q, QuaternionFromAxisAngle( { 0, 1, 0 }, i * 1.05f ) );
				Quaternion tilt = QuaternionMultiply( qi, QuaternionFromAxisAngle( { 1, 0, 0 }, 0.25f ) );
				AddBox( Vector3Add( d.pos, { 0, 0.15f * s, 0 } ), tilt, { 0.02f * s, 0.16f * s, 0.06f * s }, Mat::Plain, d.color, false );
			}
			break;
		}
		case Decoration::Flag:
		case Decoration::Banner:
		{
			AddCylinder( d.pos, Vector3Add( d.pos, { 0, 2.6f * s, 0 } ), 0.05f * s, Mat::Wood, { 90, 64, 40, 255 } );
			AddSphere( Vector3Add( d.pos, { 0, 2.62f * s, 0 } ), 0.08f * s, Mat::Gold, { 255, 200, 60, 255 } );
			break;
		}
	}
}

void Renderer::AddRope( Vector3 a, Vector3 b, float radius, float sag, Color color )
{
	const int segs = sag > 0.01f ? 8 : 1;
	Vector3 prev = a;
	for ( int i = 1; i <= segs; ++i )
	{
		float t = (float)i / segs;
		Vector3 p = Vector3Lerp( a, b, t );
		p.y -= sag * 4.0f * t * ( 1.0f - t );
		AddCylinder( prev, p, radius, Mat::Plain, color, false );
		prev = p;
	}
}

void Renderer::DrawItems( Shader shader, bool shadowPass )
{
	Material& mat = shadowPass ? m_depthMat : m_litMat;
	(void)shader;

	for ( const DrawItem& it : m_items )
	{
		if ( shadowPass && it.castShadow == false )
		{
			continue;
		}

		if ( shadowPass == false )
		{
			int mt = (int)it.mat;
			SetShaderValue( m_lit, m_locMatType, &mt, SHADER_UNIFORM_INT );
			SetShaderValue( m_lit, m_locObjScale, &it.scale, SHADER_UNIFORM_VEC3 );
			SetShaderValue( m_lit, m_locFlash, &it.flash, SHADER_UNIFORM_FLOAT );
			mat.maps[MATERIAL_MAP_DIFFUSE].color = it.tint;
		}
		DrawMesh( *it.mesh, mat, it.model );
	}
}

void Renderer::RenderShadows()
{
	if ( shadowsEnabled == false || m_shadowRT.id == 0 )
	{
		return;
	}

	Camera3D lightCam = {};
	lightCam.target = m_shadowCenter;
	lightCam.position = Vector3Add( m_shadowCenter, Vector3Scale( m_lightDir, 120.0f ) );
	lightCam.up = { 0, 1, 0 };
	lightCam.fovy = 2.0f * m_shadowRadius;
	lightCam.projection = CAMERA_ORTHOGRAPHIC;

	BeginTextureMode( m_shadowRT );
	ClearBackground( WHITE );
	BeginMode3D( lightCam );
	Matrix lightView = rlGetMatrixModelview();
	Matrix lightProj = rlGetMatrixProjection();
	rlDisableBackfaceCulling();
	DrawItems( m_depth, true );
	rlEnableBackfaceCulling();
	EndMode3D();
	EndTextureMode();

	m_lightVP = MatrixMultiply( lightView, lightProj );
}

void Renderer::RenderSky()
{
	Vector3 fwd = Vector3Normalize( Vector3Subtract( m_camera.target, m_camera.position ) );
	Vector3 right = Vector3Normalize( Vector3CrossProduct( fwd, m_camera.up ) );
	Vector3 up = Vector3CrossProduct( right, fwd );
	float tanHalf = tanf( m_camera.fovy * DEG2RAD * 0.5f );
	Vector2 res{ (float)GetRenderWidth(), (float)GetRenderHeight() };

	SetShaderValue( m_sky, m_skyLocCamPos, &m_camera.position, SHADER_UNIFORM_VEC3 );
	SetShaderValue( m_sky, m_skyLocFwd, &fwd, SHADER_UNIFORM_VEC3 );
	SetShaderValue( m_sky, m_skyLocRight, &right, SHADER_UNIFORM_VEC3 );
	SetShaderValue( m_sky, m_skyLocUp, &up, SHADER_UNIFORM_VEC3 );
	SetShaderValue( m_sky, m_skyLocTan, &tanHalf, SHADER_UNIFORM_FLOAT );
	SetShaderValue( m_sky, m_skyLocRes, &res, SHADER_UNIFORM_VEC2 );
	SetShaderValue( m_sky, m_skyLocTime, &m_time, SHADER_UNIFORM_FLOAT );
	SetShaderValue( m_sky, m_skyLocSun, &m_lightDir, SHADER_UNIFORM_VEC3 );

	BeginShaderMode( m_sky );
	DrawRectangle( 0, 0, GetScreenWidth(), GetScreenHeight(), WHITE );
	EndShaderMode();
}

void Renderer::RenderOpaque()
{
	SetShaderValue( m_lit, m_locLightDir, &m_lightDir, SHADER_UNIFORM_VEC3 );
	SetShaderValue( m_lit, m_locViewPos, &m_camera.position, SHADER_UNIFORM_VEC3 );
	SetShaderValueMatrix( m_lit, m_locLightVP, m_lightVP );
	SetShaderValue( m_lit, m_locTime, &m_time, SHADER_UNIFORM_FLOAT );
	int shadowOn = ( shadowsEnabled && m_shadowRT.id > 0 ) ? 1 : 0;
	SetShaderValue( m_lit, m_locShadowOn, &shadowOn, SHADER_UNIFORM_INT );
	float texel = 1.0f / m_shadowSize;
	SetShaderValue( m_lit, m_locTexel, &texel, SHADER_UNIFORM_FLOAT );

	rlEnableShader( m_lit.id );
	int slot = 10;
	rlActiveTextureSlot( slot );
	rlEnableTexture( m_shadowRT.depth.id );
	rlSetUniform( m_locShadowMap, &slot, SHADER_UNIFORM_INT, 1 );
	rlActiveTextureSlot( 0 );

	DrawItems( m_lit, false );
}

void Renderer::EndScene()
{
	rlActiveTextureSlot( 10 );
	rlDisableTexture();
	rlActiveTextureSlot( 0 );
}

void Renderer::DrawShadedCube( Vector3 c, Quaternion rot, float size, Color color )
{
	static const Vector3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
	float h = size * 0.5f;
	rlBegin( RL_TRIANGLES );
	for ( int f = 0; f < 6; ++f )
	{
		Vector3 n = normals[f];
		Vector3 u = fabsf( n.y ) > 0.5f ? Vector3{ 1, 0, 0 } : Vector3{ 0, 1, 0 };
		Vector3 v = Vector3CrossProduct( n, u );
		Vector3 wn = Vector3RotateByQuaternion( n, rot );
		float shade = 0.55f + 0.45f * fmaxf( 0.0f, Vector3DotProduct( wn, m_lightDir ) ) + 0.1f * wn.y;
		Color col = { (unsigned char)fminf( 255.0f, color.r * shade ), (unsigned char)fminf( 255.0f, color.g * shade ),
					  (unsigned char)fminf( 255.0f, color.b * shade ), color.a };
		Vector3 corners[4];
		for ( int k = 0; k < 4; ++k )
		{
			float su = ( k == 0 || k == 3 ) ? -1.0f : 1.0f;
			float sv = ( k < 2 ) ? -1.0f : 1.0f;
			Vector3 local = Vector3Add( Vector3Scale( n, h ), Vector3Add( Vector3Scale( u, su * h ), Vector3Scale( v, sv * h ) ) );
			corners[k] = Vector3Add( c, Vector3RotateByQuaternion( local, rot ) );
		}
		rlColor4ub( col.r, col.g, col.b, col.a );
		// two triangles, emitted in both windings so culling never hides a face
		rlVertex3f( corners[0].x, corners[0].y, corners[0].z );
		rlVertex3f( corners[1].x, corners[1].y, corners[1].z );
		rlVertex3f( corners[2].x, corners[2].y, corners[2].z );
		rlVertex3f( corners[0].x, corners[0].y, corners[0].z );
		rlVertex3f( corners[2].x, corners[2].y, corners[2].z );
		rlVertex3f( corners[3].x, corners[3].y, corners[3].z );
		rlVertex3f( corners[0].x, corners[0].y, corners[0].z );
		rlVertex3f( corners[2].x, corners[2].y, corners[2].z );
		rlVertex3f( corners[1].x, corners[1].y, corners[1].z );
		rlVertex3f( corners[0].x, corners[0].y, corners[0].z );
		rlVertex3f( corners[3].x, corners[3].y, corners[3].z );
		rlVertex3f( corners[2].x, corners[2].y, corners[2].z );
	}
	rlEnd();
}

void Renderer::DrawFlagCloth( Vector3 poleTop, float width, float height, Color color, float phase )
{
	// A waving strip of cloth that follows the wind direction.
	Vector3 w = m_wind;
	w.y = 0.0f;
	float strength = Vector3Length( w );
	Vector3 dir = strength > 0.05f ? Vector3Scale( w, 1.0f / strength ) : Vector3{ 1, 0, 0 };
	float droop = 1.0f - Clamp01( strength / 6.0f );
	Vector3 side = Vector3Normalize( Vector3CrossProduct( dir, { 0, 1, 0 } ) );

	const int segs = 10;
	rlDisableBackfaceCulling();
	rlBegin( RL_TRIANGLES );
	for ( int i = 0; i < segs; ++i )
	{
		float t0 = (float)i / segs;
		float t1 = (float)( i + 1 ) / segs;
		auto point = [&]( float t, float v ) {
			float wave = sinf( t * 7.0f - m_time * ( 4.0f + strength ) + phase ) * 0.12f * t * ( 0.4f + strength * 0.15f );
			Vector3 along = Vector3Scale( dir, t * width * ( 1.0f - 0.5f * droop ) );
			Vector3 p = Vector3Add( poleTop, along );
			p = Vector3Add( p, Vector3Scale( side, wave ) );
			p.y -= v * height + droop * t * width * 0.6f;
			return p;
		};
		Vector3 a = point( t0, 0.0f ), b = point( t1, 0.0f ), c = point( t1, 1.0f ), d = point( t0, 1.0f );
		float shade = 0.75f + 0.25f * sinf( t0 * 7.0f - m_time * 4.0f + phase );
		rlColor4ub( (unsigned char)( color.r * shade ), (unsigned char)( color.g * shade ), (unsigned char)( color.b * shade ), 255 );
		rlVertex3f( a.x, a.y, a.z );
		rlVertex3f( b.x, b.y, b.z );
		rlVertex3f( c.x, c.y, c.z );
		rlVertex3f( a.x, a.y, a.z );
		rlVertex3f( c.x, c.y, c.z );
		rlVertex3f( d.x, d.y, d.z );
	}
	rlEnd();
	rlDrawRenderBatchActive();
	rlEnableBackfaceCulling();
}
