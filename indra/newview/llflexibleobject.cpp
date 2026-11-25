/**
 * @file llflexibleobject.cpp
 * @brief Flexible object implementation
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

#include "llviewerprecompiledheaders.h"

#include "pipeline.h"
#include "lldrawpoolbump.h"
#include "llface.h"
#include "llflexibleobject.h"
#include "llglheaders.h"
#include "llrendersphere.h"
#include "llviewerobject.h"
#include "llagent.h"
#include "llsky.h"
#include "llviewercamera.h"
#include "llviewertexturelist.h"
#include "llviewercontrol.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llworld.h"
#include "llvoavatar.h"
#include "llsurface.h" // <FS:Outworldz/> FlexiFix: terrain height
// <FS:Outworldz> FlexiFix pins: tag parsing
#include <cctype>
#include <cmath>
#include <locale>
#include <sstream>
// </FS:Outworldz>

static const F32 SEC_PER_FLEXI_FRAME = 1.f / 60.f; // 60 flexi updates per second
/*static*/ F32 LLVolumeImplFlexible::sUpdateFactor = 1.0f;
std::vector<LLVolumeImplFlexible*> LLVolumeImplFlexible::sInstanceList;

// LLFlexibleObjectData::pack/unpack now in llprimitive.cpp

//-----------------------------------------------
// constructor
//-----------------------------------------------
LLVolumeImplFlexible::LLVolumeImplFlexible(LLViewerObject* vo, LLFlexibleObjectData* attributes) :
        mVO(vo),
        mAttributes(attributes),
        mLastFrameNum(0),
        mLastUpdatePeriod(0)
{
    static U32 seed = 0;
    mID = seed++;
    mInitialized = false;
    mUpdated = false;
    mInitializedRes = -1;
    mSimulateRes = 0;
    mCollisionSphereRadius = 0.f;
    mRenderRes = -1;
    mNumPins = 0; // <FS:Outworldz/> FlexiFix pins

    if(mVO->mDrawable.notNull())
    {
        mVO->mDrawable->makeActive() ;
    }

    mInstanceIndex = static_cast<S32>(sInstanceList.size());
    sInstanceList.push_back(this);
}//-----------------------------------------------

LLVolumeImplFlexible::~LLVolumeImplFlexible()
{
    S32 end_idx = static_cast<S32>(sInstanceList.size()) - 1;

    if (end_idx != mInstanceIndex)
    {
        sInstanceList[mInstanceIndex] = sInstanceList[end_idx];
        sInstanceList[mInstanceIndex]->mInstanceIndex = mInstanceIndex;
    }

    sInstanceList.pop_back();
}

//static
void LLVolumeImplFlexible::updateClass()
{
    LL_PROFILE_ZONE_SCOPED;

    U64 virtual_frame_num = (U64)(LLTimer::getElapsedSeconds() / SEC_PER_FLEXI_FRAME);
    for (std::vector<LLVolumeImplFlexible*>::iterator iter = sInstanceList.begin();
            iter != sInstanceList.end();
            ++iter)
    {
        // Note: by now update period might have changed
        if ((*iter)->mRenderRes == -1
            || (*iter)->mLastFrameNum + (*iter)->mLastUpdatePeriod <= virtual_frame_num
            || (*iter)->mLastFrameNum > virtual_frame_num) //time issues, overflow
        {
            (*iter)->doIdleUpdate();
        }
    }
}

LLVector3 LLVolumeImplFlexible::getFramePosition() const
{
    return mVO->getRenderPosition();
}

LLQuaternion LLVolumeImplFlexible::getFrameRotation() const
{
    return mVO->getRenderRotation();
}

void LLVolumeImplFlexible::onParameterChanged(U16 param_type, LLNetworkData *data, bool in_use, bool local_origin)
{
    if (param_type == LLNetworkData::PARAMS_FLEXIBLE)
    {
        mAttributes = (LLFlexibleObjectData*)data;
        setAttributesOfAllSections();
    }
}

void LLVolumeImplFlexible::onShift(const LLVector4a &shift_vector)
{
    //VECTORIZE THIS
    LLVector3 shift(shift_vector.getF32ptr());
    for (int section = 0; section < (1<<DS_FLEXI_MAX_LOCAL_SECTIONS)+1; ++section) // <FS:Outworldz/> FlexiFix: whole array
    {
        mSection[section].mPosition += shift;
    }
}

//-----------------------------------------------------------------------------------------------
void LLVolumeImplFlexible::setParentPositionAndRotationDirectly( LLVector3 p, LLQuaternion r )
{
    mParentPosition = p;
    mParentRotation = r;

}//-----------------------------------------------------------------------------------------------------

void LLVolumeImplFlexible::remapSections(LLFlexibleObjectSection *source, S32 source_sections,
                                         LLFlexibleObjectSection *dest, S32 dest_sections)
{
    S32 num_output_sections = 1<<dest_sections;
    LLVector3 scale = mVO->mDrawable->getScale();
    F32 section_length = scale.mV[VZ] / (F32)num_output_sections;
    if (source_sections == -1)
    {
        // Generate all from section 0
        dest[0] = source[0];
        for (S32 section=0; section<num_output_sections; ++section)
        {
            dest[section+1] = dest[section];
            dest[section+1].mPosition += dest[section].mDirection * section_length;
            dest[section+1].mVelocity.setVec( LLVector3::zero );
        }
    }
    else if (source_sections > dest_sections)
    {
        // Copy, skipping sections

        S32 num_steps = 1<<(source_sections-dest_sections);

        // Copy from left to right since it may be an in-place computation
        for (S32 section=0; section<num_output_sections; ++section)
        {
            dest[section+1] = source[(section+1)*num_steps];
        }
        dest[0] = source[0];
    }
    else if (source_sections < dest_sections)
    {
        // Interpolate section info
        // Iterate from right to left since it may be an in-place computation
        S32 step_shift = dest_sections-source_sections;
        S32 num_steps = 1<<step_shift;
        F32 source_section_length = scale.mV[VZ] / (F32)(1<<source_sections);
        for (S32 section=num_output_sections-num_steps; section>=0; section -= num_steps)
        {
            LLFlexibleObjectSection *last_source_section = &source[section>>step_shift];
            LLFlexibleObjectSection *source_section = &source[(section>>step_shift)+1];

            // Cubic interpolation of position
            // At^3 + Bt^2 + Ct + D = f(t)
            LLVector3 D = last_source_section->mPosition;
            LLVector3 C = last_source_section->mdPosition * source_section_length;
            LLVector3 Y = source_section->mdPosition * source_section_length - C; // Helper var
            LLVector3 X = (source_section->mPosition - D - C); // Helper var
            LLVector3 A = Y - 2*X;
            LLVector3 B = X - A;

            F32 t_inc = 1.f/F32(num_steps);
            F32 t = t_inc;
            for (S32 step=1; step<num_steps; ++step)
            {
                dest[section+step].mScale =
                    lerp(last_source_section->mScale, source_section->mScale, t);
                dest[section+step].mAxisRotation =
                    slerp(t, last_source_section->mAxisRotation, source_section->mAxisRotation);

                // Evaluate output interpolated values
                F32 t_sq = t*t;
                dest[section+step].mPosition = t_sq*(t*A + B) + t*C + D;
                dest[section+step].mRotation =
                    slerp(t, last_source_section->mRotation, source_section->mRotation);
                dest[section+step].mVelocity = lerp(last_source_section->mVelocity, source_section->mVelocity, t);
                dest[section+step].mDirection = lerp(last_source_section->mDirection, source_section->mDirection, t);
                dest[section+step].mdPosition = lerp(last_source_section->mdPosition, source_section->mdPosition, t);
                dest[section+num_steps] = *source_section;
                t += t_inc;
            }
        }
        dest[0] = source[0];
    }
    else
    {
        // numbers are equal. copy info
        for (S32 section=0; section <= num_output_sections; ++section)
        {
            dest[section] = source[section];
        }
    }
}

// <FS:Outworldz> FlexiFix: floor collision helpers
namespace
{
    // Half of a section's cross-section extent along world Z. The profile is
    // scale.x by scale.y in the section's local X/Y plane, turned by rot (axis
    // twist * section rotation, as the path point is drawn). Box bound, so it
    // also covers round profiles.
    F32 ds_flexi_half_height(const LLVector2& scale, const LLQuaternion& rot)
    {
        const LLVector3 ex = LLVector3::x_axis * rot;
        const LLVector3 ey = LLVector3::y_axis * rot;
        return 0.5f * (scale.mV[0] * fabsf(ex.mV[VZ]) + scale.mV[1] * fabsf(ey.mV[VZ]));
    }

    // If pos (len from parent) is below min_z, swing it up about parent so its
    // Z is min_z (or straight up if that is out of reach), keeping the distance
    // to parent and the sideways heading. Returns true if pos was moved.
    bool ds_flexi_lift_keep_length(const LLVector3& parent, LLVector3& pos, F32 len,
                                   F32 min_z, const LLVector3& fallback_heading)
    {
        if (pos.mV[VZ] >= min_z || len <= 0.f)
        {
            return false;
        }
        const F32 dz = llclamp(min_z - parent.mV[VZ], -len, len);
        LLVector3 heading(pos.mV[VX] - parent.mV[VX], pos.mV[VY] - parent.mV[VY], 0.f);
        if (heading.magVecSquared() < 1.e-12f)
        {
            heading.setVec(fallback_heading.mV[VX], fallback_heading.mV[VY], 0.f);
        }
        if (heading.magVecSquared() < 1.e-12f)
        {
            heading = LLVector3::x_axis;
        }
        heading.normVec();
        const F32 horizontal = sqrtf(llmax(len * len - dz * dz, 0.f));
        pos = parent + heading * horizontal + LLVector3(0.f, 0.f, dz);
        return true;
    }
}

// FlexiFix: floor height in agent Z under pos_agent, or -F32_MAX.
F32 LLVolumeImplFlexible::sampleFloorZ(LLViewerRegion* regionp, const LLVector3& pos_agent,
                                       bool use_terrain, const LLVOAvatar* avatarp) const
{
    F32 floor_z = -F32_MAX;

    if (use_terrain)
    {
        // Use the object's region if the point is inside it, else look it up
        LLViewerRegion* land_regionp = regionp;
        LLVector3 pos_region;
        if (land_regionp)
        {
            pos_region = pos_agent - land_regionp->getOriginAgent();
            const F32 width = land_regionp->getWidth();
            if (pos_region.mV[VX] < 0.f || pos_region.mV[VX] > width ||
                pos_region.mV[VY] < 0.f || pos_region.mV[VY] > width)
            {
                land_regionp = nullptr;
            }
        }
        if (!land_regionp)
        {
            land_regionp = LLWorld::getInstance()->getRegionFromPosAgent(pos_agent);
            if (land_regionp)
            {
                pos_region = pos_agent - land_regionp->getOriginAgent();
            }
        }
        if (land_regionp)
        {
            floor_z = land_regionp->getLand().resolveHeightRegion(pos_region) + land_regionp->getOriginAgent().mV[VZ];
        }
    }

    if (avatarp && avatarp->getRegion() && !avatarp->mFootPlane.isExactlyClear())
    {
        // The foot plane (n . p = w) is in the avatar's region coordinates
        const LLVector4& plane = avatarp->mFootPlane;
        const F32 nz = plane.mV[VZ];
        if (nz > 0.1f) // only floors that face upward
        {
            const LLVector3 origin = avatarp->getRegion()->getOriginAgent();
            const LLVector3 p = pos_agent - origin;
            const F32 plane_z = (plane.mV[VW] - plane.mV[VX] * p.mV[VX] - plane.mV[VY] * p.mV[VY]) / nz + origin.mV[VZ];
            floor_z = llmax(floor_z, plane_z);
        }
    }

    return floor_z;
}

namespace
{
    // Strict float parse: the whole string must be a finite number.
    bool ds_flexi_parse_float(const std::string& s, F32& out)
    {
        if (s.empty() || s.size() > 32)
        {
            return false;
        }
        // Classic locale: "0.5" must parse the same whatever the UI language
        std::istringstream in(s);
        in.imbue(std::locale::classic());
        F32 value = 0.f;
        in >> value;
        if (in.fail() || in.peek() != std::char_traits<char>::eof() || !std::isfinite(value))
        {
            return false;
        }
        out = value;
        return true;
    }
}

// FlexiFix pins. Tag format (hover text, set with alpha 0 so nobody sees it):
//   #dsflex pin=<joint>@<fraction>[+x,y,z] [pin=...]
// <joint>    avatar skeleton joint name, e.g. mPelvis, mKneeLeft (letters, digits, _)
// <fraction> 0 < f <= 1, where along the flexi the pin sits (1 = the free end);
//            rounded to the nearest simulated section
// +x,y,z     optional offset in meters, avatar-local (X forward, Y left, Z up),
//            each clamped to +-2 m
// Only the first line is read, at most 2 pins are used, and malformed pins
// are ignored.
void LLVolumeImplFlexible::parsePinTag(const std::string& text)
{
    if (text == mPinTagText)
    {
        return;
    }
    mPinTagText = text;
    mNumPins = 0;

    static const std::string TAG("#dsflex");
    if (text.compare(0, TAG.size(), TAG) != 0)
    {
        return;
    }
    std::string line = text.substr(0, llmin(text.find_first_of("\r\n"), (size_t) 254));
    if (line.size() > TAG.size() && !std::isspace((unsigned char) line[TAG.size()]))
    {
        return; // e.g. "#dsflexible", not our tag
    }

    size_t pos = TAG.size();
    while (pos < line.size() && mNumPins < DS_FLEXI_MAX_PINS)
    {
        while (pos < line.size() && std::isspace((unsigned char) line[pos]))
        {
            ++pos;
        }
        size_t token_end = pos;
        while (token_end < line.size() && !std::isspace((unsigned char) line[token_end]))
        {
            ++token_end;
        }
        const std::string token = line.substr(pos, token_end - pos);
        pos = token_end;

        if (token.compare(0, 4, "pin=") != 0)
        {
            continue;
        }
        const std::string body = token.substr(4);
        const size_t at = body.find('@');
        if (at == std::string::npos || at == 0 || at > 63)
        {
            continue;
        }
        const std::string joint = body.substr(0, at);
        bool joint_ok = true;
        for (char c : joint)
        {
            if (!std::isalnum((unsigned char) c) && c != '_')
            {
                joint_ok = false;
                break;
            }
        }
        if (!joint_ok)
        {
            continue;
        }

        const std::string rest = body.substr(at + 1);
        const size_t plus = rest.find('+');
        F32 fraction = 0.f;
        if (!ds_flexi_parse_float(rest.substr(0, plus), fraction) || fraction <= 0.f || fraction > 1.f)
        {
            continue;
        }

        LLVector3 offset;
        if (plus != std::string::npos)
        {
            const std::string offset_text = rest.substr(plus + 1);
            const size_t comma1 = offset_text.find(',');
            const size_t comma2 = (comma1 == std::string::npos) ? std::string::npos : offset_text.find(',', comma1 + 1);
            if (comma2 == std::string::npos || offset_text.find(',', comma2 + 1) != std::string::npos)
            {
                continue;
            }
            F32 x, y, z;
            if (!ds_flexi_parse_float(offset_text.substr(0, comma1), x) ||
                !ds_flexi_parse_float(offset_text.substr(comma1 + 1, comma2 - comma1 - 1), y) ||
                !ds_flexi_parse_float(offset_text.substr(comma2 + 1), z))
            {
                continue;
            }
            offset.setVec(llclamp(x, -2.f, 2.f), llclamp(y, -2.f, 2.f), llclamp(z, -2.f, 2.f));
        }

        DSFlexiPin& pin = mPins[mNumPins++];
        pin.mJoint = joint;
        pin.mFraction = fraction;
        pin.mOffset = offset;
        pin.mJointMissing = false;
    }
}
// </FS:Outworldz>

//-----------------------------------------------------------------------------
void LLVolumeImplFlexible::setAttributesOfAllSections(LLVector3* inScale)
{
    LLVector2 bottom_scale, top_scale;
    F32 begin_rot = 0, end_rot = 0;
    if (mVO->getVolume())
    {
        const LLPathParams &params = mVO->getVolume()->getParams().getPathParams();
        bottom_scale = params.getBeginScale();
        top_scale = params.getEndScale();
        begin_rot = F_PI * params.getTwistBegin();
        end_rot = F_PI * params.getTwist();
    }

    if (!mVO->mDrawable)
    {
        return;
    }

    S32 num_sections = 1 << mSimulateRes;

    LLVector3 scale;
    if (inScale == (LLVector3*)NULL)
    {
        scale = mVO->mDrawable->getScale();
    }
    else
    {
        scale = *inScale;
    }

    mSection[0].mPosition = getAnchorPosition();
    mSection[0].mDirection = LLVector3::z_axis * getFrameRotation();
    mSection[0].mdPosition = mSection[0].mDirection;
    mSection[0].mScale.setVec(scale.mV[VX]*bottom_scale.mV[0], scale.mV[VY]*bottom_scale.mV[1]);
    mSection[0].mVelocity.setVec(0,0,0);
    mSection[0].mAxisRotation.setQuat(begin_rot,0,0,1);

    remapSections(mSection, mInitializedRes, mSection, mSimulateRes);
    mInitializedRes = mSimulateRes;

    F32 t_inc = 1.f/F32(num_sections);
    F32 t = t_inc;

    for ( int i=1; i<= num_sections; i++)
    {
        mSection[i].mAxisRotation.setQuat(lerp(begin_rot,end_rot,t),0,0,1);
        mSection[i].mScale = LLVector2(
            scale.mV[VX] * lerp(bottom_scale.mV[0], top_scale.mV[0], t),
            scale.mV[VY] * lerp(bottom_scale.mV[1], top_scale.mV[1], t));
        t += t_inc;
    }
}//-----------------------------------------------------------------------------------


void LLVolumeImplFlexible::onSetVolume(const LLVolumeParams &volume_params, const S32 detail)
{
}


void LLVolumeImplFlexible::updateRenderRes()
{
    if (!mAttributes)
        return;

    LLDrawable* drawablep = mVO->mDrawable;

    S32 new_res = mAttributes->getSimulateLOD();

    // <FS:Outworldz> FlexiFix: optional extra rendered subdivisions (a finer
    // curve through the same simulated sections). At 0 (the default) max_res
    // stays FLEXIBLE_OBJECT_MAX_SECTIONS and nothing below changes.
    // Extra *simulated* sections were tried and removed: the per-section
    // tension/angle tuning made the free end oscillate without settling.
    static LLCachedControl<U32> extra_render_setting(gSavedSettings, "DSFlexiExtraRenderSubdivisions", 0);
    const S32 extra_render = (S32) llmin((U32) extra_render_setting, 3U);
    S32 max_res = (S32) FLEXIBLE_OBJECT_MAX_SECTIONS;
    if (extra_render > 0)
    {
        max_res = llmin((S32) FLEXIBLE_OBJECT_MAX_SECTIONS + extra_render, DS_FLEXI_MAX_LOCAL_SECTIONS);
    }
    // </FS:Outworldz>

#if 1 //optimal approximation of previous behavior that doesn't rely on atan2
    F32 app_angle = mVO->getScale().mV[2]/drawablep->mDistanceWRTCamera;

    // Rendering sections increases with visible angle on the screen
    F32 render_res = 12.f * app_angle;
    // <FS:Outworldz> FlexiFix: shift the screen-size curve up by the extra levels
    if (extra_render > 0)
    {
        render_res += (F32) extra_render;
    }
    mRenderRes = (render_res > (F32) max_res) ? max_res
               : (render_res > 0.f) ? (S32) render_res : 0;
    // </FS:Outworldz>

#else //legacy behavior
    //number of segments only cares about z axis
    F32 app_angle = ll_round((F32) atan2( mVO->getScale().mV[2]*2.f, drawablep->mDistanceWRTCamera) * RAD_TO_DEG, 0.01f);

    // Rendering sections increases with visible angle on the screen
    mRenderRes = (S32)(FLEXIBLE_OBJECT_MAX_SECTIONS*4*app_angle*DEG_TO_RAD/LLViewerCamera::getInstance()->getView());
#endif

    mRenderRes = llclamp(mRenderRes, new_res-1, max_res); // <FS:Outworldz/> FlexiFix: max_res == FLEXIBLE_OBJECT_MAX_SECTIONS by default

    // Throttle back simulation of segments we're not rendering
    if (mRenderRes < new_res)
    {
        new_res = mRenderRes;
    }

    if (!mInitialized || (mSimulateRes != new_res))
    {
        mSimulateRes = new_res;
        setAttributesOfAllSections();
        mInitialized = true;
    }
}
//---------------------------------------------------------------------------------
// This calculates the physics of the flexible object. Note that it has to be 0
// updated every time step. In the future, perhaps there could be an
// optimization similar to what Havok does for objects that are stationary.
//---------------------------------------------------------------------------------
void LLVolumeImplFlexible::doIdleUpdate()
{
    LLDrawable* drawablep = mVO->mDrawable;

    if (drawablep)
    {
        //ensure drawable is active
        drawablep->makeActive();

        if (gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_FLEXIBLE))
        {
            bool visible = drawablep->isVisible();

            if (mRenderRes == -1)
            {
                updateRenderRes();
                gPipeline.markRebuild(drawablep, LLDrawable::REBUILD_POSITION);
            }
            else
            {
                F32 pixel_area = mVO->getPixelArea();

                // Note: Flexies afar will be rarely updated, closer ones will be updated more frequently.
                // But frequency differences are extremely noticeable, so consider modifying update factor,
                // or at least clamping value a bit more from both sides.
                F32 period_ratio = LLViewerCamera::getInstance()->getScreenPixelArea() * 0.01f / (pixel_area * (sUpdateFactor + 1.f));
                // MAINT-1890 Clamp the update period to ensure that the update_period is no greater than 32 frames
                U32 update_period = (period_ratio >= 31.f) ? 32U
                                  : (period_ratio > 0.f) ? (U32) period_ratio + 1U : 1U;

                // We control how fast flexies update, buy splitting updates among frames
                U64 virtual_frame_num = (U64)(LLTimer::getElapsedSeconds() / SEC_PER_FLEXI_FRAME);

                if  (visible)
                {
                    if (!drawablep->isState(LLDrawable::IN_REBUILD_Q) &&
                        pixel_area > 256.f)
                    {
                        U32 id;
                        if (mVO->isRootEdit())
                        {
                            id = mID;
                        }
                        else
                        {
                            LLVOVolume* parent = (LLVOVolume*)mVO->getParent();
                            id = parent->getVolumeInterfaceID();
                        }


                        // Throttle flexies and spread load by preventing flexies from updating in same frame
                        // Shows how many frames we need to wait before next update
                        U64 throttling_delay = (virtual_frame_num + id) % update_period;

                        if ((throttling_delay == 0 && mLastFrameNum < virtual_frame_num) //one or more virtual frames per frame
                            || (mLastFrameNum + update_period < virtual_frame_num) // missed virtual frame
                            || mLastFrameNum > virtual_frame_num) // overflow
                        {
                            // We need mLastFrameNum to compensate for 'unreliable time' and to filter 'duplicate' frames
                            // If happened too late, subtract throttling_delay (it is zero otherwise)
                            mLastFrameNum = virtual_frame_num - throttling_delay;

                            // Store update period for updateClass()
                            // Note: Consider substituting update_period with mLastUpdatePeriod everywhere.
                            mLastUpdatePeriod = update_period;

                            updateRenderRes();

                            mVO->shrinkWrap();
                            gPipeline.markRebuild(drawablep, LLDrawable::REBUILD_POSITION);
                        }
                    }
                }
                else
                {
                    mLastFrameNum = virtual_frame_num;
                    mLastUpdatePeriod = update_period;
                }
            }

        }
    }
}

inline S32 log2(S32 x)
{
    S32 ret = 0;
    while (x > 1)
    {
        ++ret;
        x >>= 1;
    }
    return ret;
}

void LLVolumeImplFlexible::doFlexibleUpdate()
{
    LL_PROFILE_ZONE_SCOPED;
    LLVolume* volume = mVO->getVolume();
    LLPath *path = &volume->getPath();
    if ((mSimulateRes == 0 || !mInitialized) && mVO->mDrawable->isVisible())
    {
        bool force_update = mSimulateRes == 0;
        doIdleUpdate();

        if (!force_update || !gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_FLEXIBLE))
        {
            return; // we did not get updated or initialized, proceeding without can be dangerous
        }
    }

    if (!mInitialized || !mAttributes)
    {
        //the object is not visible
        return;
    }

    // Fix for MAINT-1894
    // Skipping the flexible update if render res is negative.  If we were to continue with a negative value,
    // the subsequent S32 num_render_sections = 1<<mRenderRes; code will specify a really large number of
    // render sections which will then create a length exception in the std::vector::resize() method.
    if (mRenderRes < 0)
    {
        return;
    }

    S32 num_sections = 1 << mSimulateRes;

    F32 secondsThisFrame = mTimer.getElapsedTimeAndResetF32();
    if (secondsThisFrame > 0.2f)
    {
        secondsThisFrame = 0.2f;
    }

    LLVector3 BasePosition = getFramePosition();
    LLQuaternion BaseRotation = getFrameRotation();
    LLQuaternion parentSegmentRotation = BaseRotation;
    LLVector3 anchorDirectionRotated = LLVector3::z_axis * parentSegmentRotation;
    LLVector3 anchorScale = mVO->mDrawable->getScale();

    F32 section_length = anchorScale.mV[VZ] / (F32)num_sections;
    F32 inv_section_length = 1.f / section_length;

    S32 i;

    // ANCHOR position is offset from BASE position (centroid) by half the length
    LLVector3 AnchorPosition = BasePosition - (anchorScale.mV[VZ]/2 * anchorDirectionRotated);

    mSection[0].mPosition = AnchorPosition;
    mSection[0].mDirection = anchorDirectionRotated;
    mSection[0].mRotation = BaseRotation;

    LLQuaternion deltaRotation;

    LLVector3 lastPosition;

    // Coefficients which are constant across sections
    F32 t_factor = mAttributes->getTension() * 0.1f;
    t_factor = t_factor*(1 - pow(0.85f, secondsThisFrame*30));
    if ( t_factor > FLEXIBLE_OBJECT_MAX_INTERNAL_TENSION_FORCE )
    {
        t_factor = FLEXIBLE_OBJECT_MAX_INTERNAL_TENSION_FORCE;
    }

    F32 friction_coeff = (mAttributes->getAirFriction()*2+1);
    friction_coeff = pow(10.f, friction_coeff*secondsThisFrame);
    friction_coeff = (friction_coeff > 1) ? friction_coeff : 1;
    F32 momentum = 1.0f / friction_coeff;

    F32 wind_factor = (mAttributes->getWindSensitivity()*0.1f) * section_length * secondsThisFrame;
    F32 max_angle = atan(section_length*2.f);

    F32 force_factor = section_length * secondsThisFrame;

    // <FS:Outworldz> FlexiFix: optional floor collision (terrain and/or the
    // wearer's foot plane). Both settings default off; then nothing below runs.
    static LLCachedControl<bool> terrain_collision(gSavedSettings, "DSFlexiTerrainCollision", false);
    static LLCachedControl<bool> foot_plane_floor(gSavedSettings, "DSFlexiFloorFromFootPlane", false);
    static LLCachedControl<F32> ground_friction_setting(gSavedSettings, "DSFlexiGroundFriction", 1.0f);
    static LLCachedControl<F32> floor_offset_setting(gSavedSettings, "DSFlexiFloorOffset", 0.05f);
    const bool use_terrain = terrain_collision;
    const LLVOAvatar* floor_avatarp = nullptr;
    if (foot_plane_floor && mVO->isAttachment())
    {
        LLViewerObject* parent = (LLViewerObject*) mVO->getParent();
        while (parent && !parent->isAvatar())
        {
            parent = (LLViewerObject*) parent->getParent();
        }
        floor_avatarp = (const LLVOAvatar*) parent;
    }
    const bool collide_floor = use_terrain || floor_avatarp;
    LLViewerRegion* floor_regionp = nullptr;
    F32 ground_friction = 0.f;
    F32 floor_offset = 0.f;
    LLVector3 floor_heading; // which way a section straight below its parent falls over
    if (collide_floor)
    {
        // One region lookup per object per update; sampleFloorZ falls back
        // to a per-point lookup only for points outside this region.
        floor_regionp = LLWorld::getInstance()->getRegionFromPosAgent(AnchorPosition);
        ground_friction = llclamp((F32) ground_friction_setting, 0.f, 1.f);
        floor_offset = llclamp((F32) floor_offset_setting, -0.5f, 0.5f);
        floor_heading = LLVector3::x_axis * BaseRotation;
    }
    // </FS:Outworldz>

    // <FS:Outworldz> FlexiFix pins: hold chosen sections of a worn flexi at
    // avatar joints. Off unless DSFlexiPinsEnable is on and the object carries a
    // "#dsflex pin=..." hover text tag.
    static LLCachedControl<bool> pins_enable(gSavedSettings, "DSFlexiPinsEnable", false);
    S32 num_pins = 0;
    S32 pin_index[DS_FLEXI_MAX_PINS] = { 0, 0 };
    LLVector3 pin_target[DS_FLEXI_MAX_PINS];
    F32 pin_prev_pos[3 * ((1 << DS_FLEXI_MAX_LOCAL_SECTIONS) + 1)] = {}; // filled only when pins are active
    if (pins_enable && mVO->isAttachment() && !mVO->isHUDAttachment()) // HUDs are not in world space
    {
        LLViewerObject* parent = (LLViewerObject*) mVO->getParent();
        while (parent && !parent->isAvatar())
        {
            parent = (LLViewerObject*) parent->getParent();
        }
        LLVOAvatar* pin_avatarp = (LLVOAvatar*) parent;
        if (pin_avatarp)
        {
            parsePinTag(mVO->mHudText);

            DSFlexiPin* pins = mPins;
            const S32 pin_count = mNumPins;

            const LLQuaternion avatar_rotation = pin_avatarp->getRenderRotation();
            for (S32 p = 0; p < pin_count; ++p)
            {
                DSFlexiPin& pin = pins[p];
                if (pin.mJointMissing)
                {
                    continue;
                }
                LLJoint* jointp = pin_avatarp->getJoint(pin.mJoint);
                if (!jointp)
                {
                    pin.mJointMissing = true; // don't search the skeleton every update
                    continue;
                }
                const S32 index = llclamp(ll_round(pin.mFraction * (F32) num_sections), 1, num_sections);
                LLVector3 target = jointp->getWorldPosition() + pin.mOffset * avatar_rotation;
                if (!target.isFinite())
                {
                    continue;
                }
                bool duplicate = false;
                for (S32 q = 0; q < num_pins; ++q)
                {
                    duplicate = duplicate || (pin_index[q] == index);
                }
                if (duplicate)
                {
                    continue;
                }
                pin_index[num_pins] = index;
                pin_target[num_pins] = target;
                ++num_pins;
            }

            // Order by index, then pull each target within reach of the anchor
            // or the previous pin so the chain never has to stretch.
            if (num_pins == 2 && pin_index[0] > pin_index[1])
            {
                std::swap(pin_index[0], pin_index[1]);
                std::swap(pin_target[0], pin_target[1]);
            }
            LLVector3 reach_from = AnchorPosition;
            S32 reach_index = 0;
            for (S32 p = 0; p < num_pins; ++p)
            {
                const F32 reach = (F32) (pin_index[p] - reach_index) * section_length;
                LLVector3 span = pin_target[p] - reach_from;
                const F32 span_length = span.magVec();
                if (span_length > reach && span_length > 0.f)
                {
                    pin_target[p] = reach_from + span * (reach / span_length);
                }
                reach_from = pin_target[p];
                reach_index = pin_index[p];
            }
        }
    }
    const bool pins_active = num_pins > 0;
    if (pins_active)
    {
        for (i = 1; i <= num_sections; ++i)
        {
            pin_prev_pos[3 * i + 0] = mSection[i].mPosition.mV[VX];
            pin_prev_pos[3 * i + 1] = mSection[i].mPosition.mV[VY];
            pin_prev_pos[3 * i + 2] = mSection[i].mPosition.mV[VZ];
        }
    }
    // </FS:Outworldz>

    // Update simulated sections
    for (i=1; i<=num_sections; ++i)
    {
        LLVector3 parentSectionVector;
        LLVector3 parentSectionPosition;
        LLVector3 parentDirection;

        //---------------------------------------------------
        // save value of position as lastPosition
        //---------------------------------------------------
        lastPosition = mSection[i].mPosition;

        //------------------------------------------------------------------------------------------
        // gravity
        //------------------------------------------------------------------------------------------
        mSection[i].mPosition.mV[2] -= mAttributes->getGravity() * force_factor;

        //------------------------------------------------------------------------------------------
        // wind force
        //------------------------------------------------------------------------------------------
        if (mAttributes->getWindSensitivity() > 0.001f)
        {
            mSection[i].mPosition += gAgent.getRegion()->mWind.getVelocity( mSection[i].mPosition ) * wind_factor;
        }

        //------------------------------------------------------------------------------------------
        // user-defined force
        //------------------------------------------------------------------------------------------
        mSection[i].mPosition += mAttributes->getUserForce() * force_factor;

        //---------------------------------------------------
        // tension (rigidity, stiffness)
        //---------------------------------------------------
        parentSectionPosition = mSection[i-1].mPosition;
        parentDirection = mSection[i-1].mDirection;

        if ( i == 1 )
        {
            parentSectionVector = mSection[0].mDirection;
        }
        else
        {
            parentSectionVector = mSection[i-2].mDirection;
        }

        LLVector3 currentVector = mSection[i].mPosition - parentSectionPosition;

        LLVector3 difference = (parentSectionVector*section_length) - currentVector;
        LLVector3 tensionForce = difference * t_factor;

        mSection[i].mPosition += tensionForce;

        //------------------------------------------------------------------------------------------
        // sphere collision, currently not used
        //------------------------------------------------------------------------------------------
        /*if ( mAttributes->mUsingCollisionSphere )
        {
            LLVector3 vectorToCenterOfCollisionSphere = mCollisionSpherePosition - mSection[i].mPosition;
            if ( vectorToCenterOfCollisionSphere.magVecSquared() < mCollisionSphereRadius * mCollisionSphereRadius )
            {
                F32 distanceToCenterOfCollisionSphere = vectorToCenterOfCollisionSphere.magVec();
                F32 penetration = mCollisionSphereRadius - distanceToCenterOfCollisionSphere;

                LLVector3 normalToCenterOfCollisionSphere;

                if ( distanceToCenterOfCollisionSphere > 0.0f )
                {
                    normalToCenterOfCollisionSphere = vectorToCenterOfCollisionSphere / distanceToCenterOfCollisionSphere;
                }
                else // rare
                {
                    normalToCenterOfCollisionSphere = LLVector3::x_axis; // arbitrary
                }

                // push the position out to the surface of the collision sphere
                mSection[i].mPosition -= normalToCenterOfCollisionSphere * penetration;
            }
        }*/

        //------------------------------------------------------------------------------------------
        // inertia
        //------------------------------------------------------------------------------------------
        mSection[i].mPosition += mSection[i].mVelocity * momentum;

        // <FS:Outworldz> FlexiFix: floor collision, first pass. Lifts the free
        // position before the length/angle clamp; the clamp can pull it back
        // down, so a second pass follows the clamp. The margin is the section's
        // half-height along world Z (last frame's orientation) plus DSFlexiFloorOffset.
        if (collide_floor)
        {
            const F32 floor_z = sampleFloorZ(floor_regionp, mSection[i].mPosition, use_terrain, floor_avatarp);
            const F32 margin = floor_offset +
                ds_flexi_half_height(mSection[i].mScale, mSection[i].mAxisRotation * mSection[i].mRotation);
            if (floor_z > -F32_MAX && mSection[i].mPosition.mV[VZ] < floor_z + margin)
            {
                mSection[i].mPosition.mV[VZ] = floor_z + margin;
                // Ground friction: pull the sideways step back toward last frame's spot
                mSection[i].mPosition.mV[VX] -= (mSection[i].mPosition.mV[VX] - lastPosition.mV[VX]) * ground_friction;
                mSection[i].mPosition.mV[VY] -= (mSection[i].mPosition.mV[VY] - lastPosition.mV[VY]) * ground_friction;
            }
        }
        // </FS:Outworldz>

        //------------------------------------------------------------------------------------------
        // clamp length & rotation
        //------------------------------------------------------------------------------------------
        mSection[i].mDirection = mSection[i].mPosition - parentSectionPosition;
        mSection[i].mDirection.normVec();
        deltaRotation.shortestArc( parentDirection, mSection[i].mDirection );

        F32 angle;
        LLVector3 axis;
        deltaRotation.getAngleAxis(&angle, axis);
        if (angle > F_PI) angle -= 2.f*F_PI;
        if (angle < -F_PI) angle += 2.f*F_PI;
        if (angle > max_angle)
        {
            //angle = 0.5f*(angle+max_angle);
            deltaRotation.setQuat(max_angle, axis);
        } else if (angle < -max_angle)
        {
            //angle = 0.5f*(angle-max_angle);
            deltaRotation.setQuat(-max_angle, axis);
        }
        LLQuaternion segment_rotation = parentSegmentRotation * deltaRotation;
        parentSegmentRotation = segment_rotation;

        mSection[i].mDirection = (parentDirection * deltaRotation);
        mSection[i].mPosition = parentSectionPosition + mSection[i].mDirection * section_length;
        mSection[i].mRotation = segment_rotation;

        // <FS:Outworldz> FlexiFix: floor collision, second pass. The bend limit
        // (max_angle) above can leave a section still heading down through the
        // floor; swing it up about its parent, keeping the section length, and
        // turn its direction and rotation by the same amount.
        if (collide_floor)
        {
            const F32 floor_z = sampleFloorZ(floor_regionp, mSection[i].mPosition, use_terrain, floor_avatarp);
            if (floor_z > -F32_MAX)
            {
                const F32 min_z = floor_z + floor_offset +
                    ds_flexi_half_height(mSection[i].mScale, mSection[i].mAxisRotation * mSection[i].mRotation);
                LLVector3 lifted = mSection[i].mPosition;
                if (ds_flexi_lift_keep_length(parentSectionPosition, lifted, section_length, min_z, floor_heading))
                {
                    LLVector3 lifted_dir = lifted - parentSectionPosition;
                    lifted_dir.normVec();
                    LLQuaternion lift_rotation;
                    lift_rotation.shortestArc(mSection[i].mDirection, lifted_dir);
                    mSection[i].mDirection = lifted_dir;
                    mSection[i].mPosition = lifted;
                    mSection[i].mRotation = mSection[i].mRotation * lift_rotation;
                    parentSegmentRotation = mSection[i].mRotation;
                }
            }
        }
        // </FS:Outworldz>

        if (i > 1)
        {
            // Propogate half the rotation up to the parent
            LLQuaternion halfDeltaRotation(angle/2, axis);
            mSection[i-1].mRotation = mSection[i-1].mRotation * halfDeltaRotation;
        }

        //------------------------------------------------------------------------------------------
        // calculate velocity
        //------------------------------------------------------------------------------------------
        mSection[i].mVelocity = mSection[i].mPosition - lastPosition;
        if (mSection[i].mVelocity.magVecSquared() > 1.f)
        {
            mSection[i].mVelocity.normVec();
        }
    }

    // <FS:Outworldz> FlexiFix pins: put pinned sections on their targets, then
    // a few backward/forward distance passes between the anchor and the pins
    // keep every section one section_length from the next. Pins win over the
    // floor: pinned sections are never lifted. Runs only when a pin is active.
    if (pins_active)
    {
        const S32 last_pin = pin_index[num_pins - 1];
        auto is_pinned = [&](S32 index)
        {
            for (S32 p = 0; p < num_pins; ++p)
            {
                if (pin_index[p] == index)
                {
                    return true;
                }
            }
            return false;
        };

        const S32 RELAX_PASSES = 3;
        for (S32 pass = 0; pass < RELAX_PASSES; ++pass)
        {
            for (S32 p = 0; p < num_pins; ++p)
            {
                mSection[pin_index[p]].mPosition = pin_target[p];
            }

            // Backward: from the last pin toward the anchor
            for (S32 j = last_pin - 1; j >= 1; --j)
            {
                if (is_pinned(j))
                {
                    continue;
                }
                LLVector3 dir = mSection[j].mPosition - mSection[j + 1].mPosition;
                if (dir.normVec() < 1.e-6f)
                {
                    dir = -mSection[j + 1].mDirection;
                }
                mSection[j].mPosition = mSection[j + 1].mPosition + dir * section_length;
            }

            // Forward: from the anchor (fixed) to the free end
            for (S32 j = 1; j <= num_sections; ++j)
            {
                if (is_pinned(j))
                {
                    continue;
                }
                const LLVector3& parent_pos = mSection[j - 1].mPosition;
                LLVector3 dir = mSection[j].mPosition - parent_pos;
                if (dir.normVec() < 1.e-6f)
                {
                    dir = mSection[j - 1].mDirection;
                }
                mSection[j].mPosition = parent_pos + dir * section_length;
                if (collide_floor)
                {
                    const F32 floor_z = sampleFloorZ(floor_regionp, mSection[j].mPosition, use_terrain, floor_avatarp);
                    if (floor_z > -F32_MAX)
                    {
                        const F32 min_z = floor_z + floor_offset +
                            ds_flexi_half_height(mSection[j].mScale, mSection[j].mAxisRotation * mSection[j].mRotation);
                        ds_flexi_lift_keep_length(parent_pos, mSection[j].mPosition, section_length, min_z, floor_heading);
                    }
                }
            }
        }

        // Rebuild directions, rotations and velocities from the final positions
        LLQuaternion segment_rotation = BaseRotation;
        LLVector3 previous_direction = mSection[0].mDirection;
        for (S32 j = 1; j <= num_sections; ++j)
        {
            LLVector3 dir = mSection[j].mPosition - mSection[j - 1].mPosition;
            if (dir.normVec() < 1.e-6f)
            {
                dir = previous_direction;
            }
            LLQuaternion bend;
            bend.shortestArc(previous_direction, dir);
            segment_rotation = segment_rotation * bend;
            mSection[j].mDirection = dir;
            mSection[j].mRotation = segment_rotation;
            previous_direction = dir;

            if (is_pinned(j))
            {
                mSection[j].mVelocity.setVec(0.f, 0.f, 0.f);
            }
            else
            {
                const LLVector3 previous_pos(pin_prev_pos[3 * j + 0], pin_prev_pos[3 * j + 1], pin_prev_pos[3 * j + 2]);
                mSection[j].mVelocity = mSection[j].mPosition - previous_pos;
                if (mSection[j].mVelocity.magVecSquared() > 1.f)
                {
                    mSection[j].mVelocity.normVec();
                }
            }
        }
        parentSegmentRotation = segment_rotation;
    }
    // </FS:Outworldz>

    // Calculate derivatives (not necessary until normals are automagically generated)
    mSection[0].mdPosition = (mSection[1].mPosition - mSection[0].mPosition) * inv_section_length;
    // i = 1..NumSections-1
    for (i=1; i<num_sections; ++i)
    {
        // Quadratic numerical derivative of position

        // f(-L1) = aL1^2 - bL1 + c = f1
        // f(0)   =               c = f2
        // f(L2)  = aL2^2 + bL2 + c = f3
        // f = ax^2 + bx + c
        // d/dx f = 2ax + b
        // d/dx f(0) = b

        // c = f2
        // a = [(f1-c)/L1 + (f3-c)/L2] / (L1+L2)
        // b = (f3-c-aL2^2)/L2

        LLVector3 a = (mSection[i-1].mPosition-mSection[i].mPosition +
                    mSection[i+1].mPosition-mSection[i].mPosition) * 0.5f * inv_section_length * inv_section_length;
        LLVector3 b = (mSection[i+1].mPosition-mSection[i].mPosition - a*(section_length*section_length));
        b *= inv_section_length;

        mSection[i].mdPosition = b;
    }

    // i = NumSections
    mSection[i].mdPosition = (mSection[i].mPosition - mSection[i-1].mPosition) * inv_section_length;

    // Create points
    llassert(mRenderRes > -1);
    S32 num_render_sections = 1<<mRenderRes;
    if (path->getPathLength() != num_render_sections+1)
    {
        ((LLVOVolume*) mVO)->mVolumeChanged = true;
        volume->resizePath(num_render_sections+1);
    }

    LLPath::PathPt *new_point;

    LLFlexibleObjectSection newSection[ (1<<DS_FLEXI_MAX_LOCAL_SECTIONS)+1 ]; // <FS:Outworldz/> FlexiFix: was FLEXIBLE_OBJECT_MAX_SECTIONS
    remapSections(mSection, mSimulateRes, newSection, mRenderRes);

    // <FS:Outworldz> FlexiFix: with extra subdivisions on, take each render point's
    // twist from the remapped section (newSection) rather than from the simulated
    // array at the render index. Off by default, so the stock behaviour is kept.
    static LLCachedControl<U32> extra_render_twist(gSavedSettings, "DSFlexiExtraRenderSubdivisions", 0);
    const bool remapped_twist = (U32) extra_render_twist > 0;
    // </FS:Outworldz>

    // <FS:Outworldz> FlexiFix: floor collision for the drawn points. The cubic
    // remap between simulated sections can bulge below them, and terrain between
    // two sections can be higher than under either, so lift each drawn point too.
    if (collide_floor)
    {
        for (i = 1; i <= num_render_sections; ++i)
        {
            // FlexiFix pins win over the floor: leave drawn points that sit on a pin alone
            bool on_pin = false;
            for (S32 p = 0; p < num_pins; ++p)
            {
                const S32 render_index = (mRenderRes >= mSimulateRes) ? (pin_index[p] << (mRenderRes - mSimulateRes))
                                                                      : (pin_index[p] >> (mSimulateRes - mRenderRes));
                on_pin = on_pin || (render_index == i);
            }
            if (on_pin)
            {
                continue;
            }
            const F32 floor_z = sampleFloorZ(floor_regionp, newSection[i].mPosition, use_terrain, floor_avatarp);
            if (floor_z > -F32_MAX)
            {
                const LLQuaternion& twist = remapped_twist ? newSection[i].mAxisRotation : mSection[i].mAxisRotation;
                const F32 min_z = floor_z + floor_offset +
                    ds_flexi_half_height(newSection[i].mScale, twist * newSection[i].mRotation);
                if (newSection[i].mPosition.mV[VZ] < min_z)
                {
                    newSection[i].mPosition.mV[VZ] = min_z;
                }
            }
        }
    }
    // </FS:Outworldz>

    //generate transform from global to prim space
    LLVector3 delta_scale = LLVector3(1,1,1);
    LLVector3 delta_pos;
    LLQuaternion delta_rot;

    delta_rot = ~getFrameRotation();
    delta_pos = -getFramePosition()*delta_rot;

    // Vertex transform (4x4)
    LLVector3 x_axis = LLVector3(delta_scale.mV[VX], 0.f, 0.f) * delta_rot;
    LLVector3 y_axis = LLVector3(0.f, delta_scale.mV[VY], 0.f) * delta_rot;
    LLVector3 z_axis = LLVector3(0.f, 0.f, delta_scale.mV[VZ]) * delta_rot;

    LLMatrix4 rel_xform;
    rel_xform.initRows(LLVector4(x_axis, 0.f),
                                LLVector4(y_axis, 0.f),
                                LLVector4(z_axis, 0.f),
                                LLVector4(delta_pos, 1.f));

    LL_CHECK_MEMORY
    for (i=0; i<=num_render_sections; ++i)
    {
        new_point = &path->mPath[i];
        LLVector3 pos = newSection[i].mPosition * rel_xform;
        // <FS:Outworldz> FlexiFix: see remapped_twist above
        const LLQuaternion& axis_rot = remapped_twist ? newSection[i].mAxisRotation : mSection[i].mAxisRotation;
        LLQuaternion rot = axis_rot * newSection[i].mRotation * delta_rot;
        // </FS:Outworldz>

        LLVector3 np(new_point->mPos.getF32ptr());

        if (!mUpdated || (np-pos).magVec()/mVO->mDrawable->mDistanceWRTCamera > 0.001f)
        {
            new_point->mPos.load3((newSection[i].mPosition * rel_xform).mV);
            mUpdated = false;
        }

        new_point->mRot.loadu(LLMatrix3(rot));
        new_point->mScale.set(newSection[i].mScale.mV[0], newSection[i].mScale.mV[1], 0,1);
        new_point->mTexT = ((F32)i)/(num_render_sections);
    }
    LL_CHECK_MEMORY
    mLastSegmentRotation = parentSegmentRotation;
}


void LLVolumeImplFlexible::preRebuild()
{
    if (!mUpdated)
    {
        LL_PROFILE_ZONE_SCOPED;
        doFlexibleRebuild(false);
    }
}

void LLVolumeImplFlexible::doFlexibleRebuild(bool rebuild_volume)
{
    LLVolume* volume = mVO->getVolume();
    if (volume)
    {
        if (rebuild_volume)
        {
            volume->setDirty();
        }
        volume->regen();
    }

    mUpdated = true;
}

//------------------------------------------------------------------

void LLVolumeImplFlexible::onSetScale(const LLVector3& scale, bool damped)
{
    setAttributesOfAllSections((LLVector3*) &scale);
}

bool LLVolumeImplFlexible::doUpdateGeometry(LLDrawable *drawable)
{
    LL_PROFILE_ZONE_SCOPED;
    LLVOVolume *volume = (LLVOVolume*)mVO;

    if (mVO->isAttachment())
    {   //don't update flexible attachments for impostored avatars unless the
        //impostor is being updated this frame (w00!)
        LLViewerObject* parent = (LLViewerObject*) mVO->getParent();
        while (parent && !parent->isAvatar())
        {
            parent = (LLViewerObject*) parent->getParent();
        }

        if (parent)
        {
            LLVOAvatar* avatar = (LLVOAvatar*) parent;
            if (avatar->isImpostor() && !avatar->needsImpostorUpdate())
            {
                return true;
            }
        }
    }

    if (volume->mDrawable.isNull() || volume->mDrawable->isDead())
    {
        return true; // No update to complete
    }

    if (volume->mLODChanged)
    {
        LLVolumeParams volume_params = volume->getVolume()->getParams();
        volume->setVolume(volume_params, 0);
        mUpdated = false;
    }

    volume->updateRelativeXform();

    doFlexibleUpdate();

    // Object may have been rotated, which means it needs a rebuild.  See SL-47220
    bool    rotated = false;
    LLQuaternion cur_rotation = getFrameRotation();
    if ( cur_rotation != mLastFrameRotation )
    {
        mLastFrameRotation = cur_rotation;
        rotated = true;
    }

    if (volume->mLODChanged || volume->mFaceMappingChanged ||
        volume->mVolumeChanged || drawable->isState(LLDrawable::REBUILD_MATERIAL))
    {
        volume->regenFaces();
        volume->mDrawable->setState(LLDrawable::REBUILD_VOLUME);
        volume->dirtySpatialGroup();
        {
            doFlexibleRebuild(volume->mVolumeChanged);
        }
        volume->genBBoxes(isVolumeGlobal());
    }
    else if (!mUpdated || rotated)
    {
        volume->mDrawable->setState(LLDrawable::REBUILD_POSITION);
        LLSpatialGroup* group = volume->mDrawable->getSpatialGroup();
        if (group)
        {
            group->dirtyMesh();
        }
        volume->genBBoxes(isVolumeGlobal());
    }

    volume->mVolumeChanged = false;
    volume->mLODChanged = false;
    volume->mFaceMappingChanged = false;

    // clear UV flag
    drawable->clearState(LLDrawable::UV);

    return true;
}

//----------------------------------------------------------------------------------
void LLVolumeImplFlexible::setCollisionSphere( LLVector3 p, F32 r )
{
    mCollisionSpherePosition = p;
    mCollisionSphereRadius   = r;

}//------------------------------------------------------------------


//----------------------------------------------------------------------------------
void LLVolumeImplFlexible::setUsingCollisionSphere( bool u )
{
}//------------------------------------------------------------------


//----------------------------------------------------------------------------------
void LLVolumeImplFlexible::setRenderingCollisionSphere( bool r )
{
}//------------------------------------------------------------------

//------------------------------------------------------------------
LLVector3 LLVolumeImplFlexible::getEndPosition()
{
    S32 num_sections = 1 << mAttributes->getSimulateLOD();
    return mSection[ num_sections ].mPosition;

}//------------------------------------------------------------------


//------------------------------------------------------------------
LLVector3 LLVolumeImplFlexible::getNodePosition( int nodeIndex )
{
    S32 num_sections = 1 << mAttributes->getSimulateLOD();
    if ( nodeIndex > num_sections - 1 )
    {
        nodeIndex = num_sections - 1;
    }
    else if ( nodeIndex < 0 )
    {
        nodeIndex = 0;
    }

    return mSection[ nodeIndex ].mPosition;

}//------------------------------------------------------------------

LLVector3 LLVolumeImplFlexible::getPivotPosition() const
{
    return getAnchorPosition();
}

//------------------------------------------------------------------
LLVector3 LLVolumeImplFlexible::getAnchorPosition() const
{
    LLVector3 BasePosition = getFramePosition();
    LLQuaternion parentSegmentRotation = getFrameRotation();
    LLVector3 anchorDirectionRotated = LLVector3::z_axis * parentSegmentRotation;
    LLVector3 anchorScale = mVO->mDrawable->getScale();
    return BasePosition - (anchorScale.mV[VZ]/2 * anchorDirectionRotated);

}//------------------------------------------------------------------


//------------------------------------------------------------------
LLQuaternion LLVolumeImplFlexible::getEndRotation()
{
    return mLastSegmentRotation;

}//------------------------------------------------------------------


void LLVolumeImplFlexible::updateRelativeXform(bool force_identity)
{
    LLQuaternion delta_rot;
    LLVector3 delta_pos, delta_scale;
    LLVOVolume* vo = (LLVOVolume*) mVO;

    bool use_identity = vo->mDrawable->isSpatialRoot() || force_identity;

    //matrix from local space to parent relative/global space
    delta_rot = use_identity ? LLQuaternion() : vo->mDrawable->getRotation();
    delta_pos = use_identity ? LLVector3(0,0,0) : vo->mDrawable->getPosition();
    delta_scale = LLVector3(1,1,1);

    // Vertex transform (4x4)
    LLVector3 x_axis = LLVector3(delta_scale.mV[VX], 0.f, 0.f) * delta_rot;
    LLVector3 y_axis = LLVector3(0.f, delta_scale.mV[VY], 0.f) * delta_rot;
    LLVector3 z_axis = LLVector3(0.f, 0.f, delta_scale.mV[VZ]) * delta_rot;

    vo->mRelativeXform.initRows(LLVector4(x_axis, 0.f),
                            LLVector4(y_axis, 0.f),
                            LLVector4(z_axis, 0.f),
                            LLVector4(delta_pos, 1.f));

    x_axis.normVec();
    y_axis.normVec();
    z_axis.normVec();

    vo->mRelativeXformInvTrans.setRows(x_axis, y_axis, z_axis);
}

const LLMatrix4& LLVolumeImplFlexible::getWorldMatrix(LLXformMatrix* xform) const
{
    return xform->getWorldMatrix();
}
