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
	Flake,	// weather: leaf, snowflake or sand grain, drifts with the wind and flutters
	Streak, // weather: a rain drop drawn as a short line along its velocity
	Grain,	// weather: a speck of sand, a round dot carried by the wind
};

enum class Ambient : uint8_t;

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
	Vector3 drift; // weather only: steady breeze on top of the level's wind
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
	void Implosion( Vector3 pos, float radius );
	void MuzzleBlast( Vector3 pos, Vector3 dir );
	void Stars( Vector3 pos, int count );
	void Sparkle( Vector3 pos, Color color, int count );
	void Trail( Vector3 pos, Color color, float size );
	// Keeps a realm's weather falling around the point the camera looks at.
	void Weather( Ambient kind, Vector3 focus, float dt, Vector3 wind, Color a, Color b );

	int Count() const
	{
		return (int)m_items.size();
	}

private:
	std::vector<Particle> m_items;
	float m_weatherDebt = 0.0f;
};
