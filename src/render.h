// Rendering: lit + shadowed meshes, procedural materials, sky with a sea of clouds.
#pragma once

#include "physics.h"

#include <unordered_map>
#include <vector>

struct DrawItem
{
	const Mesh* mesh;
	Matrix model;
	Vector3 scale; // object space size in meters, used by procedural patterns
	Mat mat;
	Color tint;
	float flash;
	bool castShadow;
};

struct CannonPose
{
	Vector3 position;
	float yaw;
	float pitch;
	float recoil;
	float wheelSpin;
	bool visible;
};

struct Decoration
{
	enum Type : uint8_t
	{
		Tree,
		Pine,
		Rock,
		Grass,
		Flag,
		Banner,
		Pylon,
	};
	Type type;
	Vector3 pos;
	float scale;
	float rot;
	Color color;
};

Vector3 CannonAimDir( float yaw, float pitch );

class Renderer
{
public:
	void Init();
	void Shutdown();

	// Frame
	void BeginScene( const Camera3D& camera, Vector3 shadowCenter, float shadowRadius, float time, Vector3 wind );
	void AddEntity( const Entity* e, float alpha );
	void AddParts( const std::vector<Part>& parts, Vector3 pos, Quaternion rot, float flash );
	void AddCannon( const CannonPose& pose );
	void AddDecoration( const Decoration& d );
	void AddRope( Vector3 a, Vector3 b, float radius, float sag, Color color );
	void AddMesh( const Mesh* mesh, Matrix model, Vector3 scale, Mat mat, Color tint, bool castShadow = true );
	void AddBox( Vector3 center, Quaternion rot, Vector3 half, Mat mat, Color tint, bool castShadow = true );
	void AddSphere( Vector3 center, float radius, Mat mat, Color tint, bool castShadow = true );
	void AddCylinder( Vector3 a, Vector3 b, float radius, Mat mat, Color tint, bool castShadow = true );

	void RenderShadows();
	void RenderSky();
	void RenderOpaque();
	void EndScene();

	// Helpers for effects drawn after the opaque pass (inside BeginMode3D).
	void DrawShadedCube( Vector3 center, Quaternion rot, float size, Color color );
	void DrawFlagCloth( Vector3 poleTop, float width, float height, Color color, float phase );
	Texture2D SoftTexture() const
	{
		return m_softTex;
	}

	const Mesh* HullMesh( const b3HullData* hull );
	// Hull meshes are cached by address; call when the hulls they came from are freed.
	void ClearHullCache();
	const Mesh* CapsuleMesh( float radius, float halfSegment );

	Vector3 LightDir() const
	{
		return m_lightDir;
	}

	const Camera3D& Camera() const
	{
		return m_camera;
	}

	bool shadowsEnabled = true;

private:
	void DrawItems( Shader shader, bool shadowPass );

	Shader m_lit{};
	Shader m_depth{};
	Shader m_sky{};
	Material m_litMat{};
	Material m_depthMat{};

	int m_locMatType = -1;
	int m_locObjScale = -1;
	int m_locFlash = -1;
	int m_locLightDir = -1;
	int m_locViewPos = -1;
	int m_locLightVP = -1;
	int m_locShadowMap = -1;
	int m_locTime = -1;
	int m_locShadowOn = -1;
	int m_locTexel = -1;

	int m_skyLocCamPos = -1, m_skyLocFwd = -1, m_skyLocRight = -1, m_skyLocUp = -1, m_skyLocTan = -1, m_skyLocRes = -1,
		m_skyLocTime = -1, m_skyLocSun = -1;

	RenderTexture2D m_shadowRT{};
	int m_shadowSize = 2048;
	Matrix m_lightVP{};

	Mesh m_cube{};
	Mesh m_sphere{};
	Mesh m_cylinder{};
	Mesh m_cone{};
	Texture2D m_softTex{};

	std::unordered_map<const b3HullData*, Mesh> m_hullMeshes;
	std::unordered_map<uint64_t, Mesh> m_capsuleMeshes;

	std::vector<DrawItem> m_items;
	Camera3D m_camera{};
	Vector3 m_lightDir{};
	Vector3 m_shadowCenter{};
	float m_shadowRadius = 40.0f;
	float m_time = 0.0f;
	Vector3 m_wind{};
};
