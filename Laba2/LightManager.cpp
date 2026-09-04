#include "LightManager.h"

LightManager::LightManager(XMFLOAT2 sceneMinXZ, XMFLOAT2 sceneMaxXZ, float floorY, float spawnY)
    : m_sceneMinXZ(sceneMinXZ), m_sceneMaxXZ(sceneMaxXZ), m_floorY(floorY), m_spawnY(spawnY)
{
    m_rainDrops.reserve(m_maxDrops);
    m_gpuScratch.reserve(m_maxDrops + 8);
}

void LightManager::AddDirectionalLight(const XMFLOAT3& direction, const XMFLOAT3& color, float intensity)
{
    GPULight l = {};
    l.Type = (int)LightType::Directional;
    XMVECTOR d = XMVector3Normalize(XMLoadFloat3(&direction));
    XMStoreFloat3(&l.Direction, d);
    l.Color = color;
    l.Intensity = intensity;
    m_staticLights.push_back(l);
}

void LightManager::AddPointLight(const XMFLOAT3& position, const XMFLOAT3& color, float intensity, float range)
{
    GPULight l = {};
    l.Type = (int)LightType::Point;
    l.Position = position;
    l.Color = color;
    l.Intensity = intensity;
    l.Range = range;
    m_staticLights.push_back(l);
}

void LightManager::AddSpotLight(const XMFLOAT3& position, const XMFLOAT3& direction, const XMFLOAT3& color,
    float intensity, float range, float innerAngleDeg, float outerAngleDeg)
{
    GPULight l = {};
    l.Type = (int)LightType::Spot;
    l.Position = position;
    XMVECTOR d = XMVector3Normalize(XMLoadFloat3(&direction));
    XMStoreFloat3(&l.Direction, d);
    l.Color = color;
    l.Intensity = intensity;
    l.Range = range;
    l.SpotCosine = cosf(XMConvertToRadians(innerAngleDeg));
    l.SpotOuterCosine = cosf(XMConvertToRadians(outerAngleDeg));
    m_staticLights.push_back(l);
}

void LightManager::SpawnDrop()
{
    std::uniform_real_distribution<float> distX(m_sceneMinXZ.x, m_sceneMaxXZ.x);
    std::uniform_real_distribution<float> distZ(m_sceneMinXZ.y, m_sceneMaxXZ.y);
    std::uniform_real_distribution<float> distHue(0.0f, 1.0f);
    std::uniform_real_distribution<float> distIntensity(6.0f, 14.0f); // brighter, so it reads clearly against the lit scene

    RainDrop drop;
    drop.Position = XMFLOAT3(distX(m_rng), m_spawnY, distZ(m_rng));
    drop.VelocityY = 0.0f;

    // Random saturated colour so the rain reads as colourful falling sparks.
    float hue = distHue(m_rng);
    XMFLOAT3 rainbow[6] = {
        {1,0,0}, {1,1,0}, {0,1,0}, {0,1,1}, {0,0,1}, {1,0,1}
    };
    int idx = (int)(hue * 6.0f) % 6;
    drop.Color = rainbow[idx];
    drop.Intensity = distIntensity(m_rng);
    drop.Grounded = false;
    if (m_rainDrops.size() < m_maxDrops)
    {
        m_rainDrops.push_back(drop);
    }
    else
    {
        // Recycle the oldest grounded drop so the rain can run forever
        // without unbounded memory / GPU buffer growth.
        for (auto& d : m_rainDrops)
        {
            if (d.Grounded)
            {
                d = drop;
                return;
            }
        }
        // Everything is still falling (unlikely) — just skip this spawn.
    }
}

void LightManager::Update(float deltaSeconds)
{
    if (m_rainEnabled)
    {
        m_spawnAccumulator += deltaSeconds * m_spawnRate;
        while (m_spawnAccumulator >= 1.0f)
        {
            SpawnDrop();
            m_spawnAccumulator -= 1.0f;
        }
    }

    for (auto& drop : m_rainDrops)
    {
        if (drop.Grounded)
            continue; // stays exactly where it landed — does not disappear

        drop.VelocityY -= m_gravity * deltaSeconds;
        drop.Position.y += drop.VelocityY * deltaSeconds;

        if (drop.Position.y <= m_floorY)
        {
            drop.Position.y = m_floorY + 0.05f; // small offset so it isn't clipped by the floor mesh
            drop.VelocityY = 0.0f;
            drop.Grounded = true;
        }
    }
}

const std::vector<GPULight>& LightManager::BuildGPULightList()
{
    m_gpuScratch.clear();
    m_gpuScratch.insert(m_gpuScratch.end(), m_staticLights.begin(), m_staticLights.end());

    for (const auto& drop : m_rainDrops)
    {
        GPULight l = {};
        l.Type = (int)LightType::Point;
        l.Position = drop.Position;
        l.Color = drop.Color;
        l.Intensity = drop.Intensity;
        l.Range = 6.0f; // small puddle of light around each drop (kept modest so the distance-based shader early-out stays cheap)
        m_gpuScratch.push_back(l);
    }

    return m_gpuScratch;
}
