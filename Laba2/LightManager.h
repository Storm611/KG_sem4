#pragma once
#include "Light.h"
#include <vector>
#include <random>


struct RainDrop
{
    XMFLOAT3 Position;
    float    VelocityY;
    XMFLOAT3 Color;
    float    Intensity;
    bool     Grounded = false;
};

class LightManager
{
public:

    LightManager(XMFLOAT2 sceneMinXZ, XMFLOAT2 sceneMaxXZ, float floorY, float spawnY);


    void AddDirectionalLight(const XMFLOAT3& direction, const XMFLOAT3& color, float intensity);
    void AddPointLight(const XMFLOAT3& position, const XMFLOAT3& color, float intensity, float range);
    void AddSpotLight(const XMFLOAT3& position, const XMFLOAT3& direction, const XMFLOAT3& color,
                       float intensity, float range, float innerAngleDeg, float outerAngleDeg);

    // Rain control
    void SetRainEnabled(bool enabled) { m_rainEnabled = enabled; }
    bool IsRainEnabled() const { return m_rainEnabled; }
    void SetSpawnRatePerSecond(float rate) { m_spawnRate = rate; }
    void SetMaxDrops(size_t maxDrops) { m_maxDrops = maxDrops; }
    void ClearRain() { m_rainDrops.clear(); }


    void Update(float deltaSeconds);


    const std::vector<GPULight>& BuildGPULightList();

    size_t StaticLightCount() const { return m_staticLights.size(); }
    size_t RainDropCount() const { return m_rainDrops.size(); }


    XMFLOAT2 GetSceneCenterXZ() const { return { (m_sceneMinXZ.x + m_sceneMaxXZ.x) * 0.5f, (m_sceneMinXZ.y + m_sceneMaxXZ.y) * 0.5f }; }
    float GetSpawnY() const { return m_spawnY; }
    float GetFloorY() const { return m_floorY; }

private:
    void SpawnDrop();

    std::vector<GPULight> m_staticLights;
    std::vector<RainDrop> m_rainDrops;
    std::vector<GPULight> m_gpuScratch;

    bool  m_rainEnabled = true;
    float m_spawnRate = 6.0f;     // drops per second
    float m_spawnAccumulator = 0.0f;
    size_t m_maxDrops = 256;
    float m_gravity = 9.8f;

    XMFLOAT2 m_sceneMinXZ;
    XMFLOAT2 m_sceneMaxXZ;
    float m_floorY;
    float m_spawnY;

    std::mt19937 m_rng{ std::random_device{}() };
};
