/**
 * @file llvowater.h
 * @brief Description of LLVOWater class
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#ifndef LL_VOWATER_H
#define LL_VOWATER_H

#include "llviewerobject.h"
#include "llviewertexture.h"
#include "pipeline.h"
#include "v2math.h"

const U32 N_RES = 16; //32          // number of subdivisions of wave tile
const U8  WAVE_STEP     = 8;

class LLSurface;
class LLViewerRegion;
class LLVector3d;
class LLHeavenBody;
class LLVOSky;
class LLFace;

class LLVOWater : public LLStaticViewerObject
{
public:
    enum
    {
        VERTEX_DATA_MASK =  (1 << LLVertexBuffer::TYPE_VERTEX) |
                            (1 << LLVertexBuffer::TYPE_NORMAL) |
                            (1 << LLVertexBuffer::TYPE_TEXCOORD0)
    };

    LLVOWater(const LLUUID &id, const LLPCode pcode, LLViewerRegion *regionp);

    /*virtual*/ void markDead();

    // Initialize data that's only inited once per class.
    static void initClass();
    static void cleanupClass();

    /*virtual*/ void idleUpdate(LLAgent &agent, const F64 &time);
    /*virtual*/ LLDrawable* createDrawable(LLPipeline *pipeline);
    /*virtual*/ bool        updateGeometry(LLDrawable *drawable);
    /*virtual*/ void        updateSpatialExtents(LLVector4a& newMin, LLVector4a& newMax);

    /*virtual*/ void updateTextures();
    /*virtual*/ void setPixelAreaAndAngle(LLAgent &agent); // generate accurate apparent angle and area

    virtual U32 getPartitionType() const;

    /*virtual*/ bool isActive() const; // Whether this object needs to do an idleUpdate.

    void setIsEdgePatch(const bool edge_patch);
    bool getIsEdgePatch() const { return mIsEdgePatch; }

protected:
    // <FS:Outworldz> Sea waves: a vertex every few metres so the sea can move.
    bool updateWaveGeometry(LLFace* face);
    // </FS:Outworldz>

    bool mIsEdgePatch;
    S32  mRenderType;
};

class LLVOVoidWater : public LLVOWater
{
public:
    LLVOVoidWater(LLUUID const& id, LLPCode pcode, LLViewerRegion* regionp) : LLVOWater(id, pcode, regionp)
    {
        mRenderType = LLPipeline::RENDER_TYPE_VOIDWATER;
    }

    /*virtual*/ U32 getPartitionType() const;
};

// <FS:Outworldz> Sea waves. The OpenSim PhysX engine moves boats on four
// Gerstner waves and sends their settings in SimulatorFeatures
// OpenSimExtras.PhysXWaves { Height, Style, Direction }. This is the same
// math as PhysXWaves.cs, so the water drawn here is the water boats ride.
// Both sides use grid-wide metres and Unix time, so no messages are needed
// to keep them in step.
class LLWaterWaves
{
public:
    static const S32 COUNT = 4;

    // The waves of a region's sea; false when its water is flat.
    static bool forRegion(LLViewerRegion* regionp, LLWaterWaves& waves);

    // Shader uniforms for vertices in agent coordinates. agent_origin is the
    // agent region's global origin and unix_time the shared wave clock.
    void getUniforms(const LLVector3d& agent_origin, F64 unix_time,
                     F32 k[4], F32 amp[4], F32 qa[4], F32 dir_x[4], F32 dir_y[4], F32 phase[4]) const;

private:
    void set(F64 height, F64 style, F64 direction_degrees);

    F64 mDirX[COUNT];
    F64 mDirY[COUNT];
    F64 mK[COUNT];      // wave number, 2 pi / length
    F64 mOmega[COUNT];  // angular speed, deep water sqrt(g k)
    F64 mAmp[COUNT];
    F64 mSteep[COUNT];  // Gerstner Q, the sideways pull toward the crests
};
// </FS:Outworldz>

#endif // LL_VOSURFACEPATCH_H
