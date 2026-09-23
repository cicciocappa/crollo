// Small helpers bridging Box3D math types and raylib math types.
#pragma once

#include "box3d/math_functions.h"
#include "raylib.h"
#include "raymath.h"

#include <cmath>
#include <cstdint>

inline Vector3 ToRl( b3Vec3 v )
{
	return Vector3{ v.x, v.y, v.z };
}

inline b3Vec3 ToB3( Vector3 v )
{
	return b3Vec3{ v.x, v.y, v.z };
}

inline Quaternion ToRl( b3Quat q )
{
	return Quaternion{ q.v.x, q.v.y, q.v.z, q.s };
}

inline b3Quat ToB3( Quaternion q )
{
	return b3Quat{ { q.x, q.y, q.z }, q.w };
}

inline b3Quat QuatAxisAngle( Vector3 axis, float radians )
{
	return b3MakeQuatFromAxisAngle( b3Normalize( ToB3( axis ) ), radians );
}

inline b3Quat QuatYaw( float radians )
{
	return b3MakeQuatFromAxisAngle( b3Vec3_axisY, radians );
}

// Translation * rotation * scale
inline Matrix ComposeTRS( Vector3 t, Quaternion r, Vector3 s )
{
	Matrix m = QuaternionToMatrix( r );
	m.m0 *= s.x;
	m.m1 *= s.x;
	m.m2 *= s.x;
	m.m4 *= s.y;
	m.m5 *= s.y;
	m.m6 *= s.y;
	m.m8 *= s.z;
	m.m9 *= s.z;
	m.m10 *= s.z;
	m.m12 = t.x;
	m.m13 = t.y;
	m.m14 = t.z;
	return m;
}

inline float Clamp01( float x )
{
	return x < 0.0f ? 0.0f : ( x > 1.0f ? 1.0f : x );
}

inline float SmoothStep( float e0, float e1, float x )
{
	float t = Clamp01( ( x - e0 ) / ( e1 - e0 ) );
	return t * t * ( 3.0f - 2.0f * t );
}

inline float ExpDecay( float a, float b, float rate, float dt )
{
	return b + ( a - b ) * expf( -rate * dt );
}

inline Vector3 ExpDecay( Vector3 a, Vector3 b, float rate, float dt )
{
	float k = expf( -rate * dt );
	return Vector3{ b.x + ( a.x - b.x ) * k, b.y + ( a.y - b.y ) * k, b.z + ( a.z - b.z ) * k };
}

inline Color ColorMix( Color a, Color b, float t )
{
	t = Clamp01( t );
	return Color{ (unsigned char)( a.r + ( b.r - a.r ) * t ), (unsigned char)( a.g + ( b.g - a.g ) * t ),
				  (unsigned char)( a.b + ( b.b - a.b ) * t ), (unsigned char)( a.a + ( b.a - a.a ) * t ) };
}

inline Color WithAlpha( Color c, float a )
{
	c.a = (unsigned char)( Clamp01( a ) * 255.0f );
	return c;
}

// Deterministic, cheap random numbers (xorshift). Used for level variation and effects.
struct Rng
{
	uint32_t state = 0x9E3779B9u;

	explicit Rng( uint32_t seed = 1234u )
	{
		state = seed ? seed : 0x9E3779B9u;
	}

	uint32_t Next()
	{
		uint32_t x = state;
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		state = x;
		return x;
	}

	float Float()
	{
		return ( Next() >> 8 ) * ( 1.0f / 16777216.0f );
	}

	float Range( float lo, float hi )
	{
		return lo + ( hi - lo ) * Float();
	}

	int Int( int lo, int hiInclusive )
	{
		return lo + (int)( Next() % (uint32_t)( hiInclusive - lo + 1 ) );
	}

	Vector3 InSphere()
	{
		for ( ;; )
		{
			Vector3 v{ Range( -1, 1 ), Range( -1, 1 ), Range( -1, 1 ) };
			if ( Vector3LengthSqr( v ) <= 1.0f )
			{
				return v;
			}
		}
	}

	Vector3 OnSphere()
	{
		Vector3 v = InSphere();
		float l = Vector3Length( v );
		return l > 1e-4f ? Vector3Scale( v, 1.0f / l ) : Vector3{ 0, 1, 0 };
	}
};

// Global effect RNG (not used for anything that must be deterministic)
Rng& FxRng();
