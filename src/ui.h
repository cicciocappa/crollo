// Immediate mode UI helpers drawn with raylib 2D primitives.
#pragma once

#include "raylib.h"

namespace ui
{
void Init();
void Shutdown();

Font Display();

// scale factor relative to a 1080p reference
float S();

Vector2 Measure( const char* text, float size );
void Text( const char* text, Vector2 pos, float size, Color color );
void TextShadow( const char* text, Vector2 pos, float size, Color color, float shadow = 3.0f );
void TextCentered( const char* text, float cx, float y, float size, Color color, bool shadow = true );
void TextOutlined( const char* text, float cx, float y, float size, Color fill, Color outline, float thickness );

void Panel( Rectangle r, Color fill, Color border, float roundness = 0.18f );
bool Button( Rectangle r, const char* label, bool enabled = true, bool highlighted = false );
bool Hovered( Rectangle r );

void Star( Vector2 c, float radius, Color fill, Color outline, float rotation = 0.0f );
void Crown( Vector2 c, float size, Color color );
void AmmoIcon( int ammo, Vector2 c, float r );
void Arrow( Vector2 from, Vector2 to, float thickness, Color color );

// true for the frame a click happened this frame inside an ui element (so the game ignores it)
bool ConsumedClick();
void BeginFrame();
} // namespace ui
