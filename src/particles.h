// Lightweight particle effects: dust, smoke, sparks, debris chips, fireballs and stars.
#pragma once

#include "math_util.h"

#include <vector>

class Renderer;

enum class PType : uint8_t
{
	Smoke,	// soft billboard, grows and fades
	Fire,	// additive billboard
	Chip,	// small tumbling cube with gravity
	Spark,	// additive streak-ish billboard with gravity
	Star,	// sparkly billboard orbiting upwards
	Ring,	// expanding shockwave billboard
};

struct Particle
{
	Vector3 pos;
	Vector3 vel;
	Quaternion rot;
	Vector3 spin;
	Color color;
	float size;
	float grow;
	float life;
	float maxLife;
	float drag;
	float gravity;
	PType type;
};

class Particles
{
public:
	void Clear()
	{
		m_items.clear();
	}
	void Update( float dt, Vector3 wind );
	void Draw( Renderer& renderer, const Camera3D& camera );

	void Emit( const Particle& p );
	void Dust( Vector3 pos, Color color, int count, float speed );
	void Debris( Vector3 pos, Color color, int count, float speed, float size );
	void Explosion( Vector3 pos, float radius );
	void MuzzleBlast( Vector3 pos, Vector3 dir );
	void Stars( Vector3 pos, int count );
	void Sparkle( Vector3 pos, Color color, int count );
	void Trail( Vector3 pos, Color color, float size );

	int Count() const
	{
		return (int)m_items.size();
	}

private:
	std::vector<Particle> m_items;
};
