/**
 * @file llflexicloth.h
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

// <FS:Outworldz> FlexiFix cloth (whole file)
//
// A grid of particles, cols x rows, stored row by row as x,y,z floats.
// Row 0 is the edge held by the prim's base; it follows the pinned targets
// given to step(). A "tube" grid wraps around (column cols-1 joins column 0),
// for round prims such as skirts; otherwise it is an open sheet (capes, trains).
//
// Verlet integration with fixed 1/60 s substeps, velocity damping, and
// structural, shear and bend distance constraints solved a few times per
// substep. Positions are corrected, never forced, so the solver stays stable
// and comes to rest. Depends only on stdtypes.h and the standard library, so
// it can be unit tested outside the viewer.

#ifndef LL_LLFLEXICLOTH_H
#define LL_LLFLEXICLOTH_H

#include "stdtypes.h"

#include <vector>

class LLFlexiCloth
{
public:
    static constexpr S32 MIN_COLS = 4;
    static constexpr S32 MAX_COLS = 16;
    static constexpr S32 MIN_ROWS = 2;
    static constexpr S32 MAX_ROWS = 33;

    struct Params
    {
        F32 mGravity = 9.8f;            // m/s^2, pulls toward -Z
        F32 mAccel[3] = { 0.f, 0.f, 0.f }; // extra acceleration for every particle, m/s^2
        F32 mDamping = 2.f;             // velocity loss per second, >= 0
        F32 mBendStiffness = 0.1f;      // 0..1, how much the cloth resists folding
        F32 mFloorMargin = 0.f;         // gap kept above floor_z, meters
        F32 mFloorFriction = 1.f;       // 0..1, 1 = no sliding on the floor
        S32 mIterations = 4;            // constraint passes per substep, 1..8
    };

    LLFlexiCloth();

    // Build the grid. rest_xyz gives the shape the constraint lengths come
    // from (any frame), start_xyz the first positions (agent space). Both hold
    // cols * rows points. Returns false (and stays empty) on bad input.
    bool init(S32 cols, S32 rows, bool tube, const F32* rest_xyz, const F32* start_xyz);
    void clear();

    bool isInitialized() const      { return mCols > 0; }
    S32 getCols() const             { return mCols; }
    S32 getRows() const             { return mRows; }
    bool isTube() const             { return mTube; }
    const F32* getPositions() const { return mPos.empty() ? nullptr : &mPos[0]; }

    // Advance by seconds, in fixed substeps (leftover time carries over).
    // pinned_xyz: cols targets for row 0, reached smoothly over the substeps.
    // floor_z: one floor height per particle, or nullptr for no floor.
    // accel_xyz: one extra acceleration per particle (wind), or nullptr.
    void step(F32 seconds, const Params& params, const F32* pinned_xyz,
              const F32* floor_z, const F32* accel_xyz);

    // Move everything (agent region crossing / origin shift).
    void shift(F32 dx, F32 dy, F32 dz);

    // Fastest particle speed in the last substep, m/s.
    F32 getMaxSpeed() const         { return mMaxSpeed; }

    // False if any position became NaN or infinite.
    bool isFinite() const;

private:
    enum EKind
    {
        STRUCTURAL = 0,
        SHEAR = 1,
        BEND = 2
    };
    struct Constraint
    {
        S32 mA;
        S32 mB;
        F32 mRest;
        S32 mKind;
    };

    void addConstraint(S32 a, S32 b, S32 kind, const F32* rest_xyz);
    void solveConstraints(const Params& params);
    void applyFloor(const Params& params, const F32* floor_z);
    void limitStretch();

    S32 mCols;
    S32 mRows;
    bool mTube;
    F32 mAccumulator;
    F32 mMaxSpeed;
    std::vector<F32> mPos;
    std::vector<F32> mPrev;
    std::vector<F32> mPinFrom;   // row 0 targets at the start of this step
    std::vector<Constraint> mConstraints;
    std::vector<F32> mRestDown;  // rest distance from each particle to the one below it
};
// </FS:Outworldz>

#endif // LL_LLFLEXICLOTH_H
