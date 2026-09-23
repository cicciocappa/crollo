#include "ui.h"

#include "raymath.h"

#include <cmath>
#include <vector>

namespace ui
{
static Font s_font{};
static bool s_loaded = false;
static bool s_consumed = false;

void Init()
{
	// ASCII plus the Italian accented letters and a few symbols
	std::vector<int> cps;
	for ( int c = 32; c < 127; ++c )
	{
		cps.push_back( c );
	}
	const int extra[] = { 0xE0, 0xE8, 0xE9, 0xEC, 0xF2, 0xF9, 0xC0, 0xC8, 0xC9, 0xCC, 0xD2, 0xD9, 0xB0, 0x2022, 0x2192, 0x2190, 0xD7 };
	for ( int c : extra )
	{
		cps.push_back( c );
	}

	const char* path = TextFormat( "%sassets/fonts/LilitaOne-Regular.ttf", GetApplicationDirectory() );
	if ( FileExists( path ) == false )
	{
		path = "assets/fonts/LilitaOne-Regular.ttf";
	}
	if ( FileExists( path ) )
	{
		s_font = LoadFontEx( path, 96, cps.data(), (int)cps.size() );
		GenTextureMipmaps( &s_font.texture );
		SetTextureFilter( s_font.texture, TEXTURE_FILTER_TRILINEAR );
		s_loaded = true;
	}
	else
	{
		s_font = GetFontDefault();
	}
}

void Shutdown()
{
	if ( s_loaded )
	{
		UnloadFont( s_font );
	}
}

Font Display()
{
	return s_font;
}

float S()
{
	return GetScreenHeight() / 1080.0f;
}

Vector2 Measure( const char* text, float size )
{
	return MeasureTextEx( s_font, text, size * S(), 0.0f );
}

void Text( const char* text, Vector2 pos, float size, Color color )
{
	DrawTextEx( s_font, text, pos, size * S(), 0.0f, color );
}

void TextShadow( const char* text, Vector2 pos, float size, Color color, float shadow )
{
	float s = S();
	DrawTextEx( s_font, text, { pos.x + shadow * s, pos.y + shadow * s }, size * s, 0.0f, Color{ 0, 0, 0, (unsigned char)( color.a * 0.45f ) } );
	DrawTextEx( s_font, text, pos, size * s, 0.0f, color );
}

void TextCentered( const char* text, float cx, float y, float size, Color color, bool shadow )
{
	Vector2 m = Measure( text, size );
	Vector2 p{ cx - m.x * 0.5f, y };
	if ( shadow )
	{
		TextShadow( text, p, size, color );
	}
	else
	{
		Text( text, p, size, color );
	}
}

void TextOutlined( const char* text, float cx, float y, float size, Color fill, Color outline, float thickness )
{
	float s = S();
	Vector2 m = Measure( text, size );
	Vector2 p{ cx - m.x * 0.5f, y };
	float t = thickness * s;
	for ( int i = 0; i < 16; ++i )
	{
		float a = i * 2.0f * PI / 16.0f;
		DrawTextEx( s_font, text, { p.x + cosf( a ) * t, p.y + sinf( a ) * t }, size * s, 0.0f, outline );
	}
	DrawTextEx( s_font, text, { p.x, p.y + t * 1.6f }, size * s, 0.0f, outline );
	DrawTextEx( s_font, text, p, size * s, 0.0f, fill );
}

void Panel( Rectangle r, Color fill, Color border, float roundness )
{
	DrawRectangleRounded( { r.x + 4 * S(), r.y + 6 * S(), r.width, r.height }, roundness, 8, Color{ 0, 0, 0, 60 } );
	DrawRectangleRounded( r, roundness, 8, fill );
	DrawRectangleRoundedLinesEx( r, roundness, 8, 3.0f * S(), border );
}

bool Hovered( Rectangle r )
{
	return IsCursorHidden() == false && CheckCollisionPointRec( GetMousePosition(), r );
}

bool Button( Rectangle r, const char* label, bool enabled, bool highlighted )
{
	bool hover = enabled && Hovered( r );
	bool down = hover && IsMouseButtonDown( MOUSE_BUTTON_LEFT );
	Color base = enabled ? Color{ 246, 190, 60, 255 } : Color{ 150, 150, 150, 255 };
	if ( hover || highlighted )
	{
		base = ColorBrightness( base, 0.15f );
	}
	Rectangle rr = r;
	if ( down )
	{
		rr.y += 3 * S();
	}
	DrawRectangleRounded( { r.x, r.y + 6 * S(), r.width, r.height }, 0.35f, 8, Color{ 120, 70, 20, 255 } );
	DrawRectangleRounded( rr, 0.35f, 8, base );
	DrawRectangleRounded( { rr.x + 6 * S(), rr.y + 4 * S(), rr.width - 12 * S(), rr.height * 0.42f }, 0.5f, 8, Color{ 255, 255, 255, 50 } );
	DrawRectangleRoundedLinesEx( rr, 0.35f, 8, 3.0f * S(), Color{ 90, 50, 15, 255 } );
	float size = r.height / S() * 0.55f;
	Vector2 m = Measure( label, size );
	Text( label, { rr.x + ( rr.width - m.x ) * 0.5f, rr.y + ( rr.height - m.y ) * 0.5f }, size, Color{ 70, 35, 10, 255 } );

	if ( hover && IsMouseButtonPressed( MOUSE_BUTTON_LEFT ) )
	{
		s_consumed = true;
		return true;
	}
	return false;
}

void Star( Vector2 c, float radius, Color fill, Color outline, float rotation )
{
	Vector2 pts[11];
	pts[0] = c;
	for ( int i = 0; i < 10; ++i )
	{
		float a = -PI * 0.5f + rotation + i * PI / 5.0f;
		float r = ( i % 2 == 0 ) ? radius : radius * 0.45f;
		pts[i + 1] = { c.x + cosf( a ) * r, c.y + sinf( a ) * r };
	}
	for ( int i = 0; i < 10; ++i )
	{
		Vector2 a = pts[i + 1];
		Vector2 b = pts[( i + 1 ) % 10 + 1];
		DrawTriangle( c, b, a, fill );
	}
	for ( int i = 0; i < 10; ++i )
	{
		DrawLineEx( pts[i + 1], pts[( i + 1 ) % 10 + 1], fmaxf( 2.0f, radius * 0.12f ), outline );
	}
}

void Crown( Vector2 c, float s, Color color )
{
	Color dark = ColorBrightness( color, -0.4f );
	Rectangle band{ c.x - s * 0.5f, c.y + s * 0.05f, s, s * 0.3f };
	Vector2 p0{ c.x - s * 0.5f, c.y + s * 0.1f };
	Vector2 p1{ c.x - s * 0.5f, c.y - s * 0.35f };
	Vector2 p2{ c.x - s * 0.25f, c.y - s * 0.05f };
	Vector2 p3{ c.x, c.y - s * 0.45f };
	Vector2 p4{ c.x + s * 0.25f, c.y - s * 0.05f };
	Vector2 p5{ c.x + s * 0.5f, c.y - s * 0.35f };
	Vector2 p6{ c.x + s * 0.5f, c.y + s * 0.1f };
	DrawTriangle( p0, p2, p1, color );
	DrawTriangle( p0, p6, p2, color );
	DrawTriangle( p2, p4, p3, color );
	DrawTriangle( p2, p6, p4, color );
	DrawTriangle( p4, p6, p5, color );
	DrawRectangleRec( band, color );
	DrawRectangleLinesEx( band, fmaxf( 1.0f, s * 0.05f ), dark );
	DrawCircleV( p1, s * 0.08f, color );
	DrawCircleV( p3, s * 0.09f, color );
	DrawCircleV( p5, s * 0.08f, color );
	DrawCircleV( { c.x, c.y + s * 0.2f }, s * 0.07f, Color{ 220, 40, 60, 255 } );
}

void AmmoIcon( int ammo, Vector2 c, float r )
{
	Color iron{ 50, 52, 60, 255 };
	switch ( ammo )
	{
		case 0: // ball
			DrawCircleV( c, r, iron );
			DrawCircleV( { c.x - r * 0.3f, c.y - r * 0.3f }, r * 0.3f, Color{ 140, 140, 150, 255 } );
			break;
		case 1: // bomb
			DrawCircleV( c, r, Color{ 30, 30, 34, 255 } );
			DrawRectangleV( { c.x - r * 0.25f, c.y - r * 1.25f }, { r * 0.5f, r * 0.4f }, Color{ 90, 90, 90, 255 } );
			DrawLineEx( { c.x, c.y - r * 1.2f }, { c.x + r * 0.5f, c.y - r * 1.6f }, r * 0.12f, Color{ 180, 150, 100, 255 } );
			DrawCircleV( { c.x + r * 0.55f, c.y - r * 1.65f }, r * 0.22f, Color{ 255, 170, 40, 255 } );
			DrawCircleV( { c.x - r * 0.3f, c.y - r * 0.3f }, r * 0.25f, Color{ 110, 110, 120, 255 } );
			break;
		case 2: // cluster
			for ( int i = 0; i < 5; ++i )
			{
				float a = i * 2.0f * PI / 5.0f;
				DrawCircleV( { c.x + cosf( a ) * r * 0.55f, c.y + sinf( a ) * r * 0.55f }, r * 0.42f, Color{ 70, 90, 60, 255 } );
			}
			DrawCircleV( c, r * 0.42f, Color{ 90, 115, 75, 255 } );
			break;
		case 3: // chain shot
			DrawLineEx( { c.x - r * 0.7f, c.y + r * 0.4f }, { c.x + r * 0.7f, c.y - r * 0.4f }, r * 0.15f, Color{ 120, 120, 128, 255 } );
			DrawCircleV( { c.x - r * 0.7f, c.y + r * 0.4f }, r * 0.5f, iron );
			DrawCircleV( { c.x + r * 0.7f, c.y - r * 0.4f }, r * 0.5f, iron );
			break;
		case 4: // boulder
		{
			Vector2 pts[8];
			for ( int i = 0; i < 8; ++i )
			{
				float a = i * 2.0f * PI / 8.0f + 0.2f;
				float rr = r * ( ( i % 3 == 0 ) ? 1.05f : 0.85f );
				pts[i] = { c.x + cosf( a ) * rr, c.y + sinf( a ) * rr };
			}
			for ( int i = 0; i < 8; ++i )
			{
				DrawTriangle( c, pts[( i + 1 ) % 8], pts[i], Color{ 125, 110, 98, 255 } );
			}
			DrawCircleV( { c.x - r * 0.25f, c.y - r * 0.3f }, r * 0.2f, Color{ 160, 145, 130, 255 } );
			break;
		}
		case 5: // implosion: a violet orb with arrows pointing in
		{
			DrawCircleV( c, r, Color{ 70, 35, 110, 255 } );
			DrawRing( c, r * 0.55f, r * 0.75f, 0, 360, 24, Color{ 190, 140, 255, 255 } );
			for ( int i = 0; i < 4; ++i )
			{
				float a = i * PI * 0.5f + PI * 0.25f;
				Vector2 d{ cosf( a ), sinf( a ) };
				Vector2 tip{ c.x + d.x * r * 1.05f, c.y + d.y * r * 1.05f };
				Vector2 base{ c.x + d.x * r * 1.55f, c.y + d.y * r * 1.55f };
				Vector2 n{ -d.y * r * 0.25f, d.x * r * 0.25f };
				DrawTriangle( tip, Vector2Add( base, n ), Vector2Subtract( base, n ), Color{ 190, 140, 255, 255 } );
				DrawTriangle( tip, Vector2Subtract( base, n ), Vector2Add( base, n ), Color{ 190, 140, 255, 255 } );
			}
			break;
		}
		case 6: // sticky bomb: a green bomb dripping glue
		{
			DrawCircleV( c, r, Color{ 55, 120, 55, 255 } );
			DrawCircleV( { c.x - r * 0.45f, c.y + r * 0.95f }, r * 0.22f, Color{ 120, 210, 90, 255 } );
			DrawCircleV( { c.x + r * 0.2f, c.y + r * 1.1f }, r * 0.28f, Color{ 120, 210, 90, 255 } );
			DrawCircleV( { c.x + r * 0.6f, c.y + r * 0.85f }, r * 0.18f, Color{ 120, 210, 90, 255 } );
			DrawRectangleV( { c.x - r * 0.25f, c.y - r * 1.25f }, { r * 0.5f, r * 0.4f }, Color{ 90, 90, 90, 255 } );
			DrawCircleV( { c.x - r * 0.3f, c.y - r * 0.3f }, r * 0.25f, Color{ 150, 220, 130, 255 } );
			break;
		}
		default:
			break;
	}
}

void Arrow( Vector2 from, Vector2 to, float thickness, Color color )
{
	DrawLineEx( from, to, thickness, color );
	Vector2 d = Vector2Normalize( Vector2Subtract( to, from ) );
	Vector2 n{ -d.y, d.x };
	float h = thickness * 3.0f;
	Vector2 a = Vector2Add( to, Vector2Scale( d, h ) );
	Vector2 b = Vector2Add( to, Vector2Scale( n, h ) );
	Vector2 c = Vector2Subtract( to, Vector2Scale( n, h ) );
	DrawTriangle( a, c, b, color );
	DrawTriangle( a, b, c, color );
}

bool ConsumedClick()
{
	return s_consumed;
}

void BeginFrame()
{
	s_consumed = false;
}
} // namespace ui
