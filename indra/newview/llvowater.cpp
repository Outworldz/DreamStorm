/**
 * @file llvowater.cpp
 * @brief LLVOWater class implementation
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llvowater.h"

#include "llviewercontrol.h"

#include "lldrawable.h"
#include "lldrawpoolwater.h"
#include "llface.h"
#include "llsky.h"
#include "llsurface.h"
#include "llviewercamera.h"
#include "llviewertexturelist.h"
#include "llviewerregion.h"
#include "llworld.h"
#include "pipeline.h"
#include "llspatialpartition.h"

///////////////////////////////////

template<class T> inline T LERP(T a, T b, F32 factor)
{
    return a + (b - a) * factor;
}

LLVOWater::LLVOWater(const LLUUID &id,
                     const LLPCode pcode,
                     LLViewerRegion *regionp) :
    LLStaticViewerObject(id, pcode, regionp),
    mRenderType(LLPipeline::RENDER_TYPE_WATER)
{
    // Terrain must draw during selection passes so it can block objects behind it.
    mbCanSelect = false;
// <FS:CR> Aurora Sim
    //setScale(LLVector3(256.f, 256.f, 0.f)); // Hack for setting scale for bounding boxes/visibility.
    setScale(LLVector3(mRegionp->getWidth(), mRegionp->getWidth(), 0.f));
// </FS:CR> Aurora Sim

    mIsEdgePatch = false;
}


void LLVOWater::markDead()
{
    LLViewerObject::markDead();
}


bool LLVOWater::isActive() const
{
    return false;
}


void LLVOWater::setPixelAreaAndAngle(LLAgent &agent)
{
    mAppAngle = 50;
    mPixelArea = 500*500;
}


// virtual
void LLVOWater::updateTextures()
{
}

// Never gets called
void  LLVOWater::idleUpdate(LLAgent &agent, const F64 &time)
{
}

LLDrawable *LLVOWater::createDrawable(LLPipeline *pipeline)
{
    pipeline->allocDrawable(this);
    mDrawable->setLit(false);
    mDrawable->setRenderType(mRenderType);

    LLDrawPoolWater *pool = (LLDrawPoolWater*) gPipeline.getPool(LLDrawPool::POOL_WATER);

    mDrawable->setNumFaces(1, pool, LLWorld::getInstance()->getDefaultWaterTexture());

    return mDrawable;
}

bool LLVOWater::updateGeometry(LLDrawable *drawable)
{
    LL_PROFILE_ZONE_SCOPED;
    LLFace *face;

    if (drawable->getNumFaces() < 1)
    {
        LLDrawPoolWater *poolp = (LLDrawPoolWater*) gPipeline.getPool(LLDrawPool::POOL_WATER);
        drawable->addFace(poolp, NULL);
    }
    face = drawable->getFace(0);
    if (!face)
    {
        return true;
    }

    // <FS:Outworldz> Sea waves
    LLWaterWaves waves;
    if (!mIsEdgePatch && LLPipeline::sRenderTransparentWater && LLWaterWaves::forRegion(getRegion(), waves))
    {
        return updateWaveGeometry(face);
    }
    // </FS:Outworldz>

//  LLVector2 uvs[4];
//  LLVector3 vtx[4];

    LLStrider<LLVector3> verticesp, normalsp;
    LLStrider<LLVector2> texCoordsp;
    LLStrider<U16> indicesp;
    U16 index_offset;


    // A quad is 4 vertices and 6 indices (making 2 triangles)
    static const unsigned int vertices_per_quad = 4;
    static const unsigned int indices_per_quad = 6;

    S32 size_x = LLPipeline::sRenderTransparentWater ? 8 : 1;
    S32 size_y = LLPipeline::sRenderTransparentWater ? 8 : 1;

    const LLVector3& scale = getScale();
    size_x *= (S32)llmin(llround(scale.mV[0] / 256.f), 8);
    size_y *= (S32)llmin(llround(scale.mV[1] / 256.f), 8);

    const S32 num_quads = size_x * size_y;
    face->setSize(vertices_per_quad * num_quads,
                  indices_per_quad * num_quads);

    LLVertexBuffer* buff = face->getVertexBuffer();
    if (!buff ||
        buff->getNumIndices() != face->getIndicesCount() ||
        buff->getNumVerts() != face->getGeomCount() ||
        face->getIndicesStart() != 0 ||
        face->getGeomIndex() != 0)
    {
        buff = new LLVertexBuffer(LLDrawPoolWater::VERTEX_DATA_MASK);
        if (!buff->allocateBuffer(face->getGeomCount(), face->getIndicesCount()))
        {
            LL_WARNS() << "Failed to allocate Vertex Buffer on water update to "
                << face->getGeomCount() << " vertices and "
                << face->getIndicesCount() << " indices" << LL_ENDL;
        }
        face->setIndicesIndex(0);
        face->setGeomIndex(0);
        face->setVertexBuffer(buff);
    }

    index_offset = face->getGeometry(verticesp,normalsp,texCoordsp, indicesp);

    LLVector3 position_agent;
    position_agent = getPositionAgent();
    face->mCenterAgent = position_agent;
    face->mCenterLocal = position_agent;

    S32 x, y;
    F32 step_x = getScale().mV[0] / size_x;
    F32 step_y = getScale().mV[1] / size_y;

    const LLVector3 up(0.f, step_y * 0.5f, 0.f);
    const LLVector3 right(step_x * 0.5f, 0.f, 0.f);
    const LLVector3 normal(0.f, 0.f, 1.f);

    F32 size_inv_x = 1.f / size_x;
    F32 size_inv_y = 1.f / size_y;

    for (y = 0; y < size_y; y++)
    {
        for (x = 0; x < size_x; x++)
        {
            S32 toffset = index_offset + 4*(y*size_x + x);
            position_agent = getPositionAgent() - getScale() * 0.5f;
            position_agent.mV[VX] += (x + 0.5f) * step_x;
            position_agent.mV[VY] += (y + 0.5f) * step_y;

            position_agent.mV[VX] = (F32)llround(position_agent.mV[VX]);
            position_agent.mV[VY] = (F32)llround(position_agent.mV[VY]);

            *verticesp++  = position_agent - right + up;
            *verticesp++  = position_agent - right - up;
            *verticesp++  = position_agent + right + up;
            *verticesp++  = position_agent + right - up;

            *texCoordsp++ = LLVector2(x*size_inv_x, (y+1)*size_inv_y);
            *texCoordsp++ = LLVector2(x*size_inv_x, y*size_inv_y);
            *texCoordsp++ = LLVector2((x+1)*size_inv_x, (y+1)*size_inv_y);
            *texCoordsp++ = LLVector2((x+1)*size_inv_x, y*size_inv_y);

            *normalsp++   = normal;
            *normalsp++   = normal;
            *normalsp++   = normal;
            *normalsp++   = normal;

            *indicesp++ = toffset + 0;
            *indicesp++ = toffset + 1;
            *indicesp++ = toffset + 2;

            *indicesp++ = toffset + 1;
            *indicesp++ = toffset + 3;
            *indicesp++ = toffset + 2;
        }
    }

    buff->unmapBuffer();

    mDrawable->movePartition();
    LLPipeline::sCompiles++;
    return true;
}

// <FS:Outworldz> Sea waves
namespace
{
    const F64 WAVE_GRAVITY = 9.81;
    const F64 WAVE_LENGTH_RATIO[LLWaterWaves::COUNT] = { 1.0, 0.61, 0.37, 0.23 };
    const F64 WAVE_TURN_DEGREES[LLWaterWaves::COUNT] = { 0.0, 25.0, -35.0, 60.0 };
    const F64 WAVE_PHASE[LLWaterWaves::COUNT]        = { 0.0, 1.7, 4.1, 2.9 };
    const F64 WAVE_PI = 3.14159265358979323846;

    const F32 WAVE_GRID_SPACING = 2.f;  // metres between water vertices on a moving sea
    const S32 WAVE_GRID_MAX     = 254;  // (n + 1)^2 vertices must fit 16-bit indices

    F64 waveLerp(F64 a, F64 b, F64 t) { return a + (b - a) * t; }
}

bool LLWaterWaves::forRegion(LLViewerRegion* regionp, LLWaterWaves& waves)
{
    if (!regionp || !regionp->simulatorFeaturesReceived())
    {
        return false;
    }
    LLSD features;
    regionp->getSimulatorFeatures(features);
    if (!features.has("OpenSimExtras") || !features["OpenSimExtras"].has("PhysXWaves"))
    {
        return false;
    }
    const LLSD& settings = features["OpenSimExtras"]["PhysXWaves"];
    F64 height = settings["Height"].asReal();
    if (height <= 0.0)
    {
        return false;
    }
    waves.set(height, settings["Style"].asReal(), settings["Direction"].asReal());
    return true;
}

void LLWaterWaves::set(F64 height, F64 style, F64 direction_degrees)
{
    style = llclamp(style, 0.0, 1.0);
    F64 main_length = waveLerp(120.0, 12.0, style);
    F64 steepness   = waveLerp(0.15, 0.7, style);
    F64 spread      = waveLerp(0.4, 1.0, style);

    F64 amp_sum = 0.0;
    for (S32 i = 0; i < COUNT; i++)
    {
        amp_sum += WAVE_LENGTH_RATIO[i];
    }
    for (S32 i = 0; i < COUNT; i++)
    {
        F64 length  = main_length * WAVE_LENGTH_RATIO[i];
        F64 heading = (direction_degrees + WAVE_TURN_DEGREES[i] * spread) * WAVE_PI / 180.0;
        mDirX[i]  = sin(heading);
        mDirY[i]  = cos(heading);
        mK[i]     = 2.0 * WAVE_PI / length;
        mOmega[i] = sqrt(WAVE_GRAVITY * mK[i]);
        mAmp[i]   = 0.5 * height * WAVE_LENGTH_RATIO[i] / amp_sum;
        mSteep[i] = mAmp[i] > 0.0 ? steepness / (mK[i] * mAmp[i] * COUNT) : 0.0;
    }
}

void LLWaterWaves::getUniforms(const LLVector3d& agent_origin, F64 unix_time,
                               F32 k[4], F32 amp[4], F32 qa[4], F32 dir_x[4], F32 dir_y[4], F32 phase[4]) const
{
    for (S32 i = 0; i < COUNT; i++)
    {
        k[i]     = (F32)mK[i];
        amp[i]   = (F32)mAmp[i];
        qa[i]    = (F32)(mSteep[i] * mAmp[i]);
        dir_x[i] = (F32)mDirX[i];
        dir_y[i] = (F32)mDirY[i];
        // The huge part of the phase (grid position and clock) in doubles here;
        // the shader adds only k * (dir . agent position).
        F64 p = mK[i] * (mDirX[i] * agent_origin.mdV[VX] + mDirY[i] * agent_origin.mdV[VY])
              - mOmega[i] * unix_time + WAVE_PHASE[i];
        phase[i] = (F32)fmod(p, 2.0 * WAVE_PI);
    }
}

bool LLVOWater::updateWaveGeometry(LLFace* face)
{
    const LLVector3& scale = getScale();
    const S32 cells_x = llclamp((S32)llround(scale.mV[VX] / WAVE_GRID_SPACING), 1, WAVE_GRID_MAX);
    const S32 cells_y = llclamp((S32)llround(scale.mV[VY] / WAVE_GRID_SPACING), 1, WAVE_GRID_MAX);
    const S32 row = cells_x + 1;

    face->setSize(row * (cells_y + 1), cells_x * cells_y * 6);

    LLVertexBuffer* buff = face->getVertexBuffer();
    if (!buff ||
        buff->getNumIndices() != face->getIndicesCount() ||
        buff->getNumVerts() != face->getGeomCount() ||
        face->getIndicesStart() != 0 ||
        face->getGeomIndex() != 0)
    {
        buff = new LLVertexBuffer(LLDrawPoolWater::VERTEX_DATA_MASK);
        if (!buff->allocateBuffer(face->getGeomCount(), face->getIndicesCount()))
        {
            LL_WARNS() << "Failed to allocate Vertex Buffer for wave water, "
                << face->getGeomCount() << " vertices and "
                << face->getIndicesCount() << " indices" << LL_ENDL;
        }
        face->setIndicesIndex(0);
        face->setGeomIndex(0);
        face->setVertexBuffer(buff);
    }

    LLStrider<LLVector3> verticesp, normalsp;
    LLStrider<LLVector2> texCoordsp;
    LLStrider<U16> indicesp;
    U16 index_offset = face->getGeometry(verticesp, normalsp, texCoordsp, indicesp);

    face->mCenterAgent = getPositionAgent();
    face->mCenterLocal = getPositionAgent();

    const LLVector3 corner = getPositionAgent() - scale * 0.5f;
    const F32 water_z = getPositionAgent().mV[VZ];
    const F32 step_x = scale.mV[VX] / cells_x;
    const F32 step_y = scale.mV[VY] / cells_y;
    const LLVector3 normal(0.f, 0.f, 1.f);

    for (S32 y = 0; y <= cells_y; y++)
    {
        for (S32 x = 0; x <= cells_x; x++)
        {
            *verticesp++  = LLVector3(corner.mV[VX] + x * step_x, corner.mV[VY] + y * step_y, water_z);
            *texCoordsp++ = LLVector2((F32)x / cells_x, (F32)y / cells_y);
            *normalsp++   = normal;
        }
    }

    for (S32 y = 0; y < cells_y; y++)
    {
        for (S32 x = 0; x < cells_x; x++)
        {
            U16 i0 = (U16)(index_offset + y * row + x);
            U16 i1 = i0 + 1;
            U16 i2 = (U16)(i0 + row);
            U16 i3 = i2 + 1;
            *indicesp++ = i0;
            *indicesp++ = i1;
            *indicesp++ = i2;
            *indicesp++ = i1;
            *indicesp++ = i3;
            *indicesp++ = i2;
        }
    }

    buff->unmapBuffer();

    mDrawable->movePartition();
    LLPipeline::sCompiles++;
    return true;
}
// </FS:Outworldz>

void LLVOWater::initClass()
{
}

void LLVOWater::cleanupClass()
{
}

void setVecZ(LLVector3& v)
{
    v.mV[VX] = 0;
    v.mV[VY] = 0;
    v.mV[VZ] = 1;
}

void LLVOWater::setIsEdgePatch(const bool edge_patch)
{
    mIsEdgePatch = edge_patch;
}

void LLVOWater::updateSpatialExtents(LLVector4a &newMin, LLVector4a& newMax)
{
    LLVector4a pos;
    pos.load3(getPositionAgent().mV);
    LLVector4a scale;
    scale.load3(getScale().mV);
    scale.mul(0.5f);

    newMin.setSub(pos, scale);
    newMax.setAdd(pos, scale);

    pos.setAdd(newMin,newMax);
    pos.mul(0.5f);

    mDrawable->setPositionGroup(pos);
}

U32 LLVOWater::getPartitionType() const
{
    if (mIsEdgePatch)
    {
        return LLViewerRegion::PARTITION_VOIDWATER;
    }

    return LLViewerRegion::PARTITION_WATER;
}

U32 LLVOVoidWater::getPartitionType() const
{
    return LLViewerRegion::PARTITION_VOIDWATER;
}

LLWaterPartition::LLWaterPartition(LLViewerRegion* regionp)
: LLSpatialPartition(0, false, regionp)
{
    mInfiniteFarClip = true;
    mDrawableType = LLPipeline::RENDER_TYPE_WATER;
    mPartitionType = LLViewerRegion::PARTITION_WATER;
}

LLVoidWaterPartition::LLVoidWaterPartition(LLViewerRegion* regionp) : LLWaterPartition(regionp)
{
    mOcclusionEnabled = false;
    mDrawableType = LLPipeline::RENDER_TYPE_VOIDWATER;
    mPartitionType = LLViewerRegion::PARTITION_VOIDWATER;
}
