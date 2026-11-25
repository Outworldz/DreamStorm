/**
 * @file llflexicloth.cpp
 * @brief FlexiFix: a small Verlet cloth solver for flexible prims
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * DreamStorm Viewer Source Code (Outworldz)
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
 * $/LicenseInfo$
 */

// <FS:Outworldz> FlexiFix cloth (whole file). See llflexicloth.h.
// No viewer headers on purpose: the viewer build force-includes its
// precompiled header, and a unit test can build this file on its own.

#include "llflexicloth.h"

#include <cmath>

namespace
{
    const F32 SUBSTEP_SECONDS = 1.f / 60.f;
    const S32 MAX_SUBSTEPS = 8;             // at most 0.133 s of motion per step()
    const F32 MAX_STEP_SECONDS = 0.2f;      // longer gaps are dropped, not simulated
    const F32 MAX_MOVE_PER_SUBSTEP = 0.5f;  // m; caps speed at 30 m/s
    const F32 SHEAR_STIFFNESS = 0.5f;
    const F32 MAX_STRETCH = 1.03f;          // hanging rows may stretch 3% at most
    const F32 MIN_LENGTH = 1.e-6f;

    inline F32 ds_cloth_clamp(F32 v, F32 lo, F32 hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    inline bool ds_cloth_finite(F32 v)
    {
        return std::isfinite(v);
    }
}

LLFlexiCloth::LLFlexiCloth()
:   mCols(0),
    mRows(0),
    mTube(false),
    mAccumulator(0.f),
    mMaxSpeed(0.f)
{
}

void LLFlexiCloth::clear()
{
    mCols = 0;
    mRows = 0;
    mTube = false;
    mAccumulator = 0.f;
    mMaxSpeed = 0.f;
    mPos.clear();
    mPrev.clear();
    mPinFrom.clear();
    mConstraints.clear();
}

void LLFlexiCloth::addConstraint(S32 a, S32 b, S32 kind, const F32* rest_xyz)
{
    const F32 dx = rest_xyz[3 * b + 0] - rest_xyz[3 * a + 0];
    const F32 dy = rest_xyz[3 * b + 1] - rest_xyz[3 * a + 1];
    const F32 dz = rest_xyz[3 * b + 2] - rest_xyz[3 * a + 2];
    Constraint c;
    c.mA = a;
    c.mB = b;
    c.mRest = std::sqrt(dx * dx + dy * dy + dz * dz);
    c.mKind = kind;
    mConstraints.push_back(c);
}

bool LLFlexiCloth::init(S32 cols, S32 rows, bool tube, const F32* rest_xyz, const F32* start_xyz)
{
    clear();
    if (!rest_xyz || !start_xyz ||
        cols < MIN_COLS || cols > MAX_COLS || rows < MIN_ROWS || rows > MAX_ROWS)
    {
        return false;
    }
    const S32 count = cols * rows;
    for (S32 i = 0; i < 3 * count; ++i)
    {
        if (!ds_cloth_finite(rest_xyz[i]) || !ds_cloth_finite(start_xyz[i]))
        {
            return false;
        }
    }

    mCols = cols;
    mRows = rows;
    mTube = tube;
    mPos.assign(start_xyz, start_xyz + 3 * count);
    mPrev = mPos;
    mPinFrom.assign(start_xyz, start_xyz + 3 * cols);

    // Row by row from the held edge down, so each pass pulls from the top.
    const S32 wrap_cols = tube ? cols : cols - 1;   // columns that have a right neighbour
    for (S32 r = 0; r < rows; ++r)
    {
        for (S32 c = 0; c < cols; ++c)
        {
            const S32 i = r * cols + c;
            if (c < wrap_cols)
            {
                addConstraint(i, r * cols + (c + 1) % cols, STRUCTURAL, rest_xyz);
            }
            if (r + 1 < rows)
            {
                addConstraint(i, i + cols, STRUCTURAL, rest_xyz);
                if (c < wrap_cols)
                {
                    const S32 right = (c + 1) % cols;
                    addConstraint(i, (r + 1) * cols + right, SHEAR, rest_xyz);
                    addConstraint(r * cols + right, i + cols, SHEAR, rest_xyz);
                }
            }
            // Bend: skip one particle across and down
            if ((tube && cols >= 5) || (!tube && c + 2 < cols))
            {
                addConstraint(i, r * cols + (c + 2) % cols, BEND, rest_xyz);
            }
            if (r + 2 < rows)
            {
                addConstraint(i, i + 2 * cols, BEND, rest_xyz);
            }
        }
    }

    mRestDown.assign(cols * (rows - 1), 0.f);
    for (S32 i = 0; i < cols * (rows - 1); ++i)
    {
        const F32 dx = rest_xyz[3 * (i + cols) + 0] - rest_xyz[3 * i + 0];
        const F32 dy = rest_xyz[3 * (i + cols) + 1] - rest_xyz[3 * i + 1];
        const F32 dz = rest_xyz[3 * (i + cols) + 2] - rest_xyz[3 * i + 2];
        mRestDown[i] = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    return true;
}

// A few constraint passes cannot hold a long hanging cloth against strong
// gravity, so it sags like elastic. Walk down from the held edge and pull any
// particle that is too far from the one above it back within reach.
void LLFlexiCloth::limitStretch()
{
    for (S32 i = 0; i < mCols * (mRows - 1); ++i)
    {
        const S32 j = i + mCols;
        const F32 max_length = mRestDown[i] * MAX_STRETCH;
        F32* pa = &mPos[3 * i];
        F32* pb = &mPos[3 * j];
        const F32 dx = pb[0] - pa[0];
        const F32 dy = pb[1] - pa[1];
        const F32 dz = pb[2] - pa[2];
        const F32 length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length > max_length && length > MIN_LENGTH)
        {
            const F32 scale = max_length / length;
            pb[0] = pa[0] + dx * scale;
            pb[1] = pa[1] + dy * scale;
            pb[2] = pa[2] + dz * scale;
        }
    }
}

void LLFlexiCloth::shift(F32 dx, F32 dy, F32 dz)
{
    const S32 count = (S32) mPos.size() / 3;
    for (S32 i = 0; i < count; ++i)
    {
        mPos[3 * i + 0] += dx;
        mPos[3 * i + 1] += dy;
        mPos[3 * i + 2] += dz;
        mPrev[3 * i + 0] += dx;
        mPrev[3 * i + 1] += dy;
        mPrev[3 * i + 2] += dz;
    }
    const S32 pins = (S32) mPinFrom.size() / 3;
    for (S32 i = 0; i < pins; ++i)
    {
        mPinFrom[3 * i + 0] += dx;
        mPinFrom[3 * i + 1] += dy;
        mPinFrom[3 * i + 2] += dz;
    }
}

bool LLFlexiCloth::isFinite() const
{
    for (F32 v : mPos)
    {
        if (!ds_cloth_finite(v))
        {
            return false;
        }
    }
    return true;
}

void LLFlexiCloth::solveConstraints(const Params& params)
{
    const F32 bend = ds_cloth_clamp(params.mBendStiffness, 0.f, 1.f);
    for (const Constraint& c : mConstraints)
    {
        F32 stiffness = 1.f;
        if (c.mKind == SHEAR)
        {
            stiffness = SHEAR_STIFFNESS;
        }
        else if (c.mKind == BEND)
        {
            stiffness = bend;
            if (stiffness <= 0.f)
            {
                continue;
            }
        }

        F32* pa = &mPos[3 * c.mA];
        F32* pb = &mPos[3 * c.mB];
        const F32 dx = pb[0] - pa[0];
        const F32 dy = pb[1] - pa[1];
        const F32 dz = pb[2] - pa[2];
        const F32 length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (length < MIN_LENGTH)
        {
            continue;
        }
        const F32 error = (length - c.mRest) / length * stiffness;

        // Row 0 is held: the other end takes the whole correction
        const bool a_held = c.mA < mCols;
        const bool b_held = c.mB < mCols;
        if (a_held && b_held)
        {
            continue;
        }
        const F32 wa = a_held ? 0.f : (b_held ? 1.f : 0.5f);
        const F32 wb = b_held ? 0.f : (a_held ? 1.f : 0.5f);
        pa[0] += dx * error * wa;
        pa[1] += dy * error * wa;
        pa[2] += dz * error * wa;
        pb[0] -= dx * error * wb;
        pb[1] -= dy * error * wb;
        pb[2] -= dz * error * wb;
    }
}

void LLFlexiCloth::applyFloor(const Params& params, const F32* floor_z)
{
    if (!floor_z)
    {
        return;
    }
    const S32 count = mCols * mRows;
    for (S32 i = mCols; i < count; ++i)
    {
        if (floor_z[i] == -FLT_MAX)
        {
            continue;
        }
        const F32 min_z = floor_z[i] + params.mFloorMargin;
        if (mPos[3 * i + 2] < min_z)
        {
            mPos[3 * i + 2] = min_z;
        }
    }
}

void LLFlexiCloth::step(F32 seconds, const Params& params, const F32* pinned_xyz,
                        const F32* floor_z, const F32* accel_xyz)
{
    if (!isInitialized() || !pinned_xyz)
    {
        return;
    }
    if (!(seconds > 0.f))
    {
        seconds = 0.f;
    }
    mAccumulator += (seconds > MAX_STEP_SECONDS) ? MAX_STEP_SECONDS : seconds;
    S32 substeps = (S32) (mAccumulator / SUBSTEP_SECONDS);
    if (substeps <= 0)
    {
        return;
    }
    if (substeps > MAX_SUBSTEPS)
    {
        substeps = MAX_SUBSTEPS;
        mAccumulator = 0.f; // fell behind: drop the rest rather than catch up
    }
    else
    {
        mAccumulator -= (F32) substeps * SUBSTEP_SECONDS;
    }

    const F32 dt = SUBSTEP_SECONDS;
    const F32 dt2 = dt * dt;
    const F32 keep = std::exp(-(params.mDamping > 0.f ? params.mDamping : 0.f) * dt);
    const F32 friction = ds_cloth_clamp(params.mFloorFriction, 0.f, 1.f);
    const S32 iterations = params.mIterations < 1 ? 1 : (params.mIterations > 8 ? 8 : params.mIterations);
    const S32 count = mCols * mRows;

    for (S32 sub = 0; sub < substeps; ++sub)
    {
        // Held row: slide from where it was to the new targets
        const F32 t = (F32) (sub + 1) / (F32) substeps;
        for (S32 c = 0; c < mCols; ++c)
        {
            for (S32 k = 0; k < 3; ++k)
            {
                const F32 target = mPinFrom[3 * c + k] + (pinned_xyz[3 * c + k] - mPinFrom[3 * c + k]) * t;
                mPrev[3 * c + k] = mPos[3 * c + k];
                mPos[3 * c + k] = target;
            }
        }

        // Integrate the free particles
        for (S32 i = mCols; i < count; ++i)
        {
            F32* p = &mPos[3 * i];
            F32* q = &mPrev[3 * i];
            F32 v[3];
            F32 move2 = 0.f;
            for (S32 k = 0; k < 3; ++k)
            {
                v[k] = (p[k] - q[k]) * keep;
                move2 += v[k] * v[k];
            }
            if (move2 > MAX_MOVE_PER_SUBSTEP * MAX_MOVE_PER_SUBSTEP)
            {
                const F32 scale = MAX_MOVE_PER_SUBSTEP / std::sqrt(move2);
                v[0] *= scale;
                v[1] *= scale;
                v[2] *= scale;
            }
            F32 a[3] = { params.mAccel[0], params.mAccel[1], params.mAccel[2] - params.mGravity };
            if (accel_xyz)
            {
                a[0] += accel_xyz[3 * i + 0];
                a[1] += accel_xyz[3 * i + 1];
                a[2] += accel_xyz[3 * i + 2];
            }
            for (S32 k = 0; k < 3; ++k)
            {
                q[k] = p[k];
                p[k] += v[k] + a[k] * dt2;
            }
        }

        for (S32 it = 0; it < iterations; ++it)
        {
            solveConstraints(params);
            applyFloor(params, floor_z);
        }
        limitStretch();
        applyFloor(params, floor_z);

        // Floor friction: particles resting on the floor lose sideways speed
        // and all downward speed, so they settle instead of bouncing.
        if (floor_z)
        {
            for (S32 i = mCols; i < count; ++i)
            {
                if (floor_z[i] == -FLT_MAX)
                {
                    continue;
                }
                const F32 contact_z = floor_z[i] + params.mFloorMargin + 1.e-4f;
                if (mPos[3 * i + 2] <= contact_z)
                {
                    mPrev[3 * i + 0] += (mPos[3 * i + 0] - mPrev[3 * i + 0]) * friction;
                    mPrev[3 * i + 1] += (mPos[3 * i + 1] - mPrev[3 * i + 1]) * friction;
                    if (mPrev[3 * i + 2] > mPos[3 * i + 2])
                    {
                        mPrev[3 * i + 2] = mPos[3 * i + 2];
                    }
                }
            }
        }

        F32 max_move2 = 0.f;
        for (S32 i = mCols; i < count; ++i)
        {
            const F32 dx = mPos[3 * i + 0] - mPrev[3 * i + 0];
            const F32 dy = mPos[3 * i + 1] - mPrev[3 * i + 1];
            const F32 dz = mPos[3 * i + 2] - mPrev[3 * i + 2];
            const F32 move2 = dx * dx + dy * dy + dz * dz;
            max_move2 = move2 > max_move2 ? move2 : max_move2;
        }
        mMaxSpeed = std::sqrt(max_move2) / dt;
    }

    mPinFrom.assign(pinned_xyz, pinned_xyz + 3 * mCols);
}
// </FS:Outworldz>
