/**
 * @file llflexibleobject.h
 * @author JJ Ventrella, Andrew Meadows, Tom Yedwab
 * @brief Flexible object definition
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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

/**
 * This is for specifying objects in the world that are animated and
 * rendered locally - on the viewer. Flexible Objects are linear arrays
 * of positions, which stay at a fixed distance from each other. One
 * position is fixed as an "anchor" and is attached to some other object
 * in the world, determined by the server. All the other positions are
 * updated according to local physics.
 */

#ifndef LL_LLFLEXIBLEOBJECT_H
#define LL_LLFLEXIBLEOBJECT_H

#include "llprimitive.h"
#include "llvovolume.h"
#include "llwind.h"

// 10 ms for the whole thing!
const F32   FLEXIBLE_OBJECT_TIMESLICE       = 0.003f;
const U32   FLEXIBLE_OBJECT_MAX_LOD         = 10;

// See llprimitive.h for LLFlexibleObjectData and DEFAULT/MIN/MAX values

// <FS:Outworldz> FlexiFix: viewer-local limit on simulated/rendered sections
// (2^6 = 64). The wire "softness" stays 0..3 (FLEXIBLE_OBJECT_MAX_SECTIONS);
// the DSFlexiExtra*Subdivisions debug settings add up to 3 more levels locally.
const S32   DS_FLEXI_MAX_LOCAL_SECTIONS     = 6;
static_assert(DS_FLEXI_MAX_LOCAL_SECTIONS >= (S32)FLEXIBLE_OBJECT_MAX_SECTIONS + 3,
              "local section limit must cover the wire maximum plus the extra levels");
class LLViewerRegion;
class LLVOAvatar;
// </FS:Outworldz>

//-------------------------------------------------------------------

struct LLFlexibleObjectSection
{
    // Input parameters
    LLVector2       mScale;
    LLQuaternion    mAxisRotation;
    // Simulated state
    LLVector3       mPosition;
    LLVector3       mVelocity;
    LLVector3       mDirection;
    LLQuaternion    mRotation;
    // Derivatives (Not all currently used, will come back with LLVolume changes to automagically generate normals)
    LLVector3       mdPosition;
    //LLMatrix4     mRotScale;
    //LLMatrix4     mdRotScale;
};

//---------------------------------------------------------
// The LLVolumeImplFlexible class
//---------------------------------------------------------
class LLVolumeImplFlexible : public LLVolumeInterface
{
private:
    static std::vector<LLVolumeImplFlexible*> sInstanceList;
    S32 mInstanceIndex;

    public:
        static void updateClass();

        LLVolumeImplFlexible(LLViewerObject* volume, LLFlexibleObjectData* attributes);
        ~LLVolumeImplFlexible();

        // Implements LLVolumeInterface
        U32 getID() const { return mID; }
        LLVector3 getFramePosition() const;
        LLQuaternion getFrameRotation() const;
        LLVolumeInterfaceType getInterfaceType() const      { return INTERFACE_FLEXIBLE; }
        void updateRenderRes();
        void doIdleUpdate();
        bool doUpdateGeometry(LLDrawable *drawable);
        LLVector3 getPivotPosition() const;
        void onSetVolume(const LLVolumeParams &volume_params, const S32 detail);
        void onSetScale(const LLVector3 &scale, bool damped);
        void onParameterChanged(U16 param_type, LLNetworkData *data, bool in_use, bool local_origin);
        void onShift(const LLVector4a &shift_vector);
        bool isVolumeUnique() const { return true; }
        bool isVolumeGlobal() const { return true; }
        bool isActive() const { return true; }
        const LLMatrix4& getWorldMatrix(LLXformMatrix* xform) const;
        void updateRelativeXform(bool force_identity);
        void doFlexibleUpdate(); // Called to update the simulation
        void doFlexibleRebuild(bool rebuild_volume); // Called to rebuild the geometry
        void preRebuild();

        //void              setAttributes( LLFlexibleObjectData );
        void                setParentPositionAndRotationDirectly( LLVector3 p, LLQuaternion r );
        void                setUsingCollisionSphere( bool u );
        void                setCollisionSphere( LLVector3 position, F32 radius );
        void                setRenderingCollisionSphere( bool r);

        LLVector3           getEndPosition();
        LLQuaternion        getEndRotation();
        LLVector3           getNodePosition( int nodeIndex );
        LLVector3           getAnchorPosition() const;

    private:
        //--------------------------------------
        // private members
        //--------------------------------------
        // Backlink only; don't make this an LLPointer.
        LLViewerObject*             mVO;
        LLTimer                     mTimer;
        LLVector3                   mAnchorPosition;
        LLVector3                   mParentPosition;
        LLQuaternion                mParentRotation;
        LLQuaternion                mLastFrameRotation;
        LLQuaternion                mLastSegmentRotation;
        bool                        mInitialized;
        bool                        mUpdated;
        LLFlexibleObjectData*       mAttributes;
        LLFlexibleObjectSection     mSection    [ (1<<DS_FLEXI_MAX_LOCAL_SECTIONS)+1 ]; // <FS:Outworldz/> FlexiFix: was FLEXIBLE_OBJECT_MAX_SECTIONS
        S32                         mInitializedRes;
        S32                         mSimulateRes;
        S32                         mRenderRes;
        U64                         mLastFrameNum;
        U32                         mLastUpdatePeriod;
        LLVector3                   mCollisionSpherePosition;
        F32                         mCollisionSphereRadius;
        U32                         mID;

        // <FS:Outworldz> FlexiFix pins: parsed from an invisible hover text tag,
        // "#dsflex pin=<joint>@<fraction>[+x,y,z] ...". See parsePinTag().
        struct DSFlexiPin
        {
            std::string             mJoint;                 // avatar joint name, e.g. mPelvis
            F32                     mFraction = 1.f;        // (0,1] along the chain, 1 = free end
            LLVector3               mOffset;                // meters, avatar-local (X forward, Y left, Z up)
            bool                    mJointMissing = false;  // lookup failed; skip until the tag changes
        };
        static constexpr S32        DS_FLEXI_MAX_PINS = 2;
        std::string                 mPinTagText;    // hover text last parsed
        DSFlexiPin                  mPins[DS_FLEXI_MAX_PINS];
        S32                         mNumPins;
        // </FS:Outworldz>

        //--------------------------------------
        // private methods
        //--------------------------------------
        void setAttributesOfAllSections (LLVector3* inScale = NULL);

        void remapSections(LLFlexibleObjectSection *source, S32 source_sections,
                                         LLFlexibleObjectSection *dest, S32 dest_sections);

        // <FS:Outworldz> FlexiFix: floor height (agent Z) under an agent-space point,
        // from terrain and/or the wearer's foot plane. Returns -F32_MAX if neither applies.
        F32 sampleFloorZ(LLViewerRegion* regionp, const LLVector3& pos_agent,
                         bool use_terrain, const LLVOAvatar* avatarp) const;

        // FlexiFix pins: re-parse mPins from the hover text tag if it changed.
        void parsePinTag(const std::string& text);
        // </FS:Outworldz>

public:
        // Global setting for update rate
        static F32                  sUpdateFactor;

};// end of class definition


#endif // LL_LLFLEXIBLEOBJECT_H
