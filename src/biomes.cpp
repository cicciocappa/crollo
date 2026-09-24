// The six realms of the Kingdom Above, one look per campaign.
#include "render.h"

static const Biome s_biomes[] = {
	// Prati Alti: the original look, bright noon over green meadows
	{ "Prati Alti",
	  { 0.20f, 0.42f, 0.78f }, { 0.78f, 0.86f, 0.93f },
	  { 1.00f, 0.98f, 0.95f }, { 0.60f, 0.67f, 0.80f }, { 0.93f, 0.94f, 0.97f }, -22.0f,
	  { 0.45f, 0.78f, -0.42f }, { 1.20f, 1.12f, 0.98f },
	  { 0.52f, 0.64f, 0.86f }, { 0.55f, 0.52f, 0.52f },
	  { 0.36f, 0.60f, 0.25f }, { 0.52f, 0.72f, 0.30f },
	  { 0.46f, 0.36f, 0.27f }, { 0.60f, 0.50f, 0.40f },
	  { 45, 105, 55, 255 }, { 95, 160, 60, 255 }, { 70, 130, 50, 255 }, { 120, 170, 60, 255 }, { 120, 104, 92, 255 },
	  0.0075f, Ambient::None, 0, 0.0f, false },

	// Valle dei Mulini: golden hour, wheat and autumn trees
	{ "Valle dei Mulini",
	  { 0.30f, 0.32f, 0.58f }, { 1.00f, 0.74f, 0.48f },
	  { 1.00f, 0.82f, 0.62f }, { 0.58f, 0.44f, 0.52f }, { 0.98f, 0.80f, 0.64f }, -22.0f,
	  { 0.62f, 0.38f, -0.55f }, { 1.35f, 1.02f, 0.72f },
	  { 0.58f, 0.52f, 0.68f }, { 0.62f, 0.46f, 0.36f },
	  { 0.64f, 0.60f, 0.24f }, { 0.82f, 0.70f, 0.30f },
	  { 0.50f, 0.36f, 0.25f }, { 0.66f, 0.48f, 0.32f },
	  { 170, 100, 35, 255 }, { 215, 150, 50, 255 }, { 175, 150, 60, 255 }, { 215, 185, 85, 255 }, { 128, 98, 80, 255 },
	  0.0075f, Ambient::Leaves, 1, 0.0f, false },

	// Picchi Gelati: snow on every island, pale cold light
	{ "Picchi Gelati",
	  { 0.42f, 0.60f, 0.86f }, { 0.88f, 0.92f, 0.98f },
	  { 1.00f, 1.00f, 1.00f }, { 0.70f, 0.77f, 0.88f }, { 0.95f, 0.96f, 0.99f }, -22.0f,
	  { 0.30f, 0.70f, -0.62f }, { 1.05f, 1.08f, 1.16f },
	  { 0.66f, 0.76f, 0.94f }, { 0.76f, 0.80f, 0.88f },
	  { 0.86f, 0.90f, 0.96f }, { 0.97f, 0.98f, 1.00f },
	  { 0.44f, 0.47f, 0.56f }, { 0.62f, 0.65f, 0.74f },
	  { 40, 90, 75, 255 }, { 70, 115, 95, 255 }, { 200, 212, 225, 255 }, { 235, 242, 250, 255 }, { 110, 116, 132, 255 },
	  0.0085f, Ambient::Snow, 2, 0.0f, true },

	// Dune Sospese: sandstone islands in an ochre haze
	{ "Dune Sospese",
	  { 0.42f, 0.54f, 0.76f }, { 0.96f, 0.83f, 0.62f },
	  { 1.00f, 0.93f, 0.76f }, { 0.76f, 0.62f, 0.48f }, { 0.97f, 0.88f, 0.72f }, -22.0f,
	  { 0.40f, 0.85f, -0.32f }, { 1.35f, 1.18f, 0.95f },
	  { 0.70f, 0.66f, 0.62f }, { 0.76f, 0.60f, 0.45f },
	  { 0.84f, 0.71f, 0.45f }, { 0.93f, 0.80f, 0.53f },
	  { 0.70f, 0.50f, 0.32f }, { 0.83f, 0.62f, 0.40f },
	  { 95, 130, 60, 255 }, { 135, 150, 70, 255 }, { 170, 150, 90, 255 }, { 195, 175, 110, 255 }, { 150, 112, 80, 255 },
	  0.0090f, Ambient::Sand, 3, 0.0f, false, true },

	// Arcipelago delle Tempeste: grey sky, dark sea of clouds, rain
	{ "Arcipelago delle Tempeste",
	  { 0.22f, 0.26f, 0.33f }, { 0.52f, 0.56f, 0.62f },
	  { 0.72f, 0.75f, 0.80f }, { 0.30f, 0.33f, 0.40f }, { 0.55f, 0.58f, 0.64f }, -22.0f,
	  { 0.20f, 0.82f, -0.52f }, { 0.78f, 0.80f, 0.88f },
	  { 0.46f, 0.50f, 0.60f }, { 0.36f, 0.37f, 0.41f },
	  { 0.25f, 0.42f, 0.28f }, { 0.32f, 0.50f, 0.32f },
	  { 0.30f, 0.28f, 0.28f }, { 0.40f, 0.38f, 0.36f },
	  { 35, 75, 50, 255 }, { 55, 95, 60, 255 }, { 50, 90, 55, 255 }, { 70, 110, 65, 255 }, { 80, 76, 76, 255 },
	  0.0120f, Ambient::Rain, 4, 0.0f, false },

	// Fucina del Vulcano: basalt islands above a glowing sea of lava
	{ "Fucina del Vulcano",
	  { 0.12f, 0.06f, 0.08f }, { 0.56f, 0.24f, 0.13f },
	  { 1.00f, 0.46f, 0.12f }, { 0.34f, 0.08f, 0.04f }, { 0.90f, 0.36f, 0.10f }, -22.0f,
	  { 0.35f, 0.62f, -0.70f }, { 1.22f, 0.78f, 0.58f },
	  { 0.46f, 0.30f, 0.28f }, { 0.72f, 0.32f, 0.14f },
	  { 0.19f, 0.17f, 0.17f }, { 0.28f, 0.23f, 0.21f },
	  { 0.20f, 0.16f, 0.15f }, { 0.31f, 0.22f, 0.18f },
	  { 60, 48, 42, 255 }, { 85, 65, 55, 255 }, { 85, 62, 50, 255 }, { 110, 80, 60, 255 }, { 58, 46, 42, 255 },
	  0.0100f, Ambient::Embers, 5, 1.0f, false },
};

const Biome& GetBiome( int index )
{
	int n = BiomeCount();
	return s_biomes[( index % n + n ) % n];
}

int BiomeCount()
{
	return (int)( sizeof( s_biomes ) / sizeof( s_biomes[0] ) );
}
