#ifndef SimTK_SIMBODY_RIGID_BODY_NODE_SPEC_ELLIPSOID_H_
#define SimTK_SIMBODY_RIGID_BODY_NODE_SPEC_ELLIPSOID_H_

/* -------------------------------------------------------------------------- *
 *                      SimTK Core: SimTK Simbody(tm)                         *
 * -------------------------------------------------------------------------- *
 * This is part of the SimTK Core biosimulation toolkit originating from      *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2005-9 Stanford University and the Authors.         *
 * Authors: Michael Sherman                                                   *
 * Contributors:                                                              *
 *    Charles Schwieters (NIH): wrote the public domain IVM code from which   *
 *                              this was derived.                             *
 *    Ajay Seth: wrote the Ellipsoid joint as a Custom mobilizer; Sherm       *
 *               derived this implementation from Ajay's                      *     
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person obtaining a    *
 * copy of this software and associated documentation files (the "Software"), *
 * to deal in the Software without restriction, including without limitation  *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,   *
 * and/or sell copies of the Software, and to permit persons to whom the      *
 * Software is furnished to do so, subject to the following conditions:       *
 *                                                                            *
 * The above copyright notice and this permission notice shall be included in *
 * all copies or substantial portions of the Software.                        *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
 * THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,    *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR      *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE  *
 * USE OR OTHER DEALINGS IN THE SOFTWARE.                                     *
 * -------------------------------------------------------------------------- */


/**@file
 * Define the RigidBodyNode that implements an Ellipsoid mobilizer.
 */

#include "SimbodyMatterSubsystemRep.h"
#include "RigidBodyNode.h"
#include "RigidBodyNodeSpec.h"


    // ELLIPSOID //

// ELLIPSOID mobilizer. This provides three degrees of rotational freedom, i.e.,
// unrestricted orientation, of the body's M frame in the parent's F frame, 
// along with coordinated translation that keeps the M frame origin on
// the surface of an ellipsoid fixed in F and centered on the F origin.
// The surface point is chosen for a given orientation of M in F as the 
// unique point on the ellipsoid surface where the surface normal is aligned 
// with Mz. That is, the z axis of M is assumed to be normal to the ellipsoid 
// at all times, and the translation is chosen to make that true.
//
// Unlike most joints, the reference configuration (i.e., X_FM when q=0 is
// not the identity transform. Instead, although the frames are aligned the
// M frame origin is offset from F along their shared +z axis, so that it lies
// on the ellipsoid surface at the point (0,0,rz) where rz is the z-radius
// (semiaxis) of the ellipsoid.
//
// The generalized coordinates are:
//   * 4 quaternions or 3 1-2-3 body fixed Euler angles (that is, fixed in M)
//     In Euler angles, axis 3 is just the spin of the outboard body about
//     its Mz axis, which is always normal to the ellipse. For small 1,2 angles
//     you can think of angle 1 (about x) as latitude, and angle 2 (about y)
//     as longitude when viewing the ellipsoid by looking down the z axis.
//     (That would be true for large angles too if we were using space fixed
//     angles, that is, angles defined about F rather than M axes.)
//
// Generalized speeds are:
//   * angular velocity w_FM as a vector expressed in the F frame.
//
// Thus rotational qdots have to be derived from the generalized speeds to
// be turned into either 4 quaternion derivatives or 3 Euler angle derivatives.
//
// This mobilizer was written by Ajay Seth and hacked somewhat by Sherm.

class RBNodeEllipsoid : public RigidBodyNodeSpec<3> {
    Vec3 semi; // semi axis dimensions in x,y,z resp.
public:

virtual const char* type() { return "ellipsoid"; }

RBNodeEllipsoid(const MassProperties& mProps_B,
              const Transform&      X_PF,
              const Transform&      X_BM,
              const Vec3&           radii, // x,y,z
              bool                  isReversed,
              UIndex&               nextUSlot,
              USquaredIndex&        nextUSqSlot,
              QIndex&               nextQSlot)
  : RigidBodyNodeSpec<3>(mProps_B,X_PF,X_BM,nextUSlot,nextUSqSlot,nextQSlot,
                         QDotMayDifferFromU, QuaternionMayBeUsed, isReversed),
    semi(radii)
{
    updateSlots(nextUSlot,nextUSqSlot,nextQSlot);
}

void setQToFitRotationImpl(const SBStateDigest& sbs, const Rotation& R_FM,
                       Vector& q) const 
{
    if (getUseEulerAngles(sbs.getModelVars()))
        toQ(q)    = R_FM.convertRotationToBodyFixedXYZ();
    else
        toQuat(q) = R_FM.convertRotationToQuaternion().asVec4();
}

// We can't hope to represent arbitrary translations with a joint that has 
// only rotational coordinates! However, since F is at the center of the 
// ellipsoid and M on its surface, we can at least obtain a translation in 
// the *direction* of the requested translation. The magnitude must of 
// course be set to end up with the M origin right on the surface of the 
// ellipsoid, and Mz will be the normal at that point.
//
// Expressed as an x-y-z body fixed Euler sequence, the z rotation is just 
// the spin around the Mz (surface normal) and could be anything, so we'll 
// just leave it at its current value. The x and y rotations act like polar 
// coordinates to get the M origin point on the direction indicated by the 
// requested translation.
//
// If the requested translation is near zero we can't do anything since we 
// can't find a direction to align with. And of course we can't do anything 
// if "only" is true here -- that means we aren't allowed to touch the 
// rotations, and for this joint that's all there is.
void setQToFitTranslationImpl(const SBStateDigest& sbs, const Vec3& p_FM, Vector& q) const {
    if (p_FM.norm() < Eps) return;

    const UnitVec3 e(p_FM); // direction from F origin towards desired M origin
    const Real latitude  = std::atan2(-e[1],e[2]); // project onto F's yz plane
    const Real longitude = std::atan2( e[0],e[2]); // project onto F's xz plane

    // Calculate the current value of the spin coordinate (3rd Euler angle).
    Real spin;
    if (getUseEulerAngles(sbs.getModelVars())){
        spin = fromQ(q)[2];
    } else {
        const Rotation R_FM_now(Quaternion(fromQuat(q)));
        spin = R_FM_now.convertRotationToBodyFixedXYZ()[2];
    }

    // Calculate the desired rotation, which is a space-fixed 1-2 sequence for
    // latitude and longitude, followed by a body fixed rotation for spin.
    const Rotation R_FM = Rotation( SpaceRotationSequence, latitude, XAxis, longitude, YAxis ) * Rotation(spin,ZAxis);

    if (getUseEulerAngles(sbs.getModelVars())) {
        const Vec3 q123 = R_FM.convertRotationToBodyFixedXYZ();
        toQ(q) = q123;
    } else {
        const Quaternion quat = R_FM.convertRotationToQuaternion();
        toQuat(q) = quat.asVec4();
    }
}

void setUToFitAngularVelocityImpl(const SBStateDigest& sbs, const Vector& q, const Vec3& w_FM,
                              Vector& u) const
{
        toU(u) = w_FM; // relative ang. vel. always used as generalized speeds
}

// We can't do general linear velocity with this rotation-only mobilizer, but 
// we can express any velocity which is tangent to the ellipsoid surface. So 
// we'll find the current surface normal (Mz) and ignore any component of the 
// requested velocity which is along that direction. (The resulting vz won't 
// be zero, though, but it is completely determined by vx,vy.)
void setUToFitLinearVelocityImpl
   (const SBStateDigest& sbs, const Vector& q, const Vec3& v_FM, Vector& u) const
{
    Transform X_FM;
    calcAcrossJointTransform(sbs,q,X_FM);

    const Vec3 v_FM_M    = ~X_FM.R()*v_FM; // we can only do vx and vy in this frame
    const Vec3 r_FM_M    = ~X_FM.R()*X_FM.p(); 
    const Vec3 wnow_FM_M = ~X_FM.R()*fromU(u); // preserve z component

    // Now vx can only result from angular velocity about y, vy from x.
    // TODO: THIS IS ONLY RIGHT FOR A SPHERE!!!
    const Real wx = -v_FM_M[1]/r_FM_M[2];
    const Real wy = v_FM_M[0]/r_FM_M[2];
    const Vec3 w_FM_M(wx, wy, wnow_FM_M[2]);
    const Vec3 w_FM = X_FM.R()*w_FM_M;

    toU(u) = w_FM;
}

// When we're using Euler angles, we're going to want to cache cos and sin for
// each angle, and also 1/cos of the middle angle will be handy to have around.
// When we're using quaternions, we'll cache 1/|q| for convenient normalizing
// of quaternions.
enum {AnglePoolSize=7, QuatPoolSize=1};
// cos x,y,z sin x,y,z 1/cos(y)
enum {AngleCosQ=0, AngleSinQ=3, AngleOOCosQy=6};
enum {QuatOONorm=0};
int calcQPoolSize(const SBModelVars& mv) const
{   return getUseEulerAngles(mv) ? AnglePoolSize : QuatPoolSize; }

void performQPrecalculations(const SBStateDigest& sbs,
                             const Real* q,      int nq,
                             Real*       qCache, int nQCache,
                             Real*       qErr,   int nQErr) const
{
    if (getUseEulerAngles(sbs.getModelVars())) {
        assert(q && nq==3 && qCache && nQCache==AnglePoolSize && nQErr==0);
        const Real cy = std::cos(q[1]);
        Vec3::updAs(&qCache[AngleCosQ]) =
            Vec3(std::cos(q[0]), cy, std::cos(q[2]));
        Vec3::updAs(&qCache[AngleSinQ]) =
            Vec3(std::sin(q[0]), std::sin(q[1]), std::sin(q[2]));
        qCache[AngleOOCosQy] = 1/cy; // trouble at 90 degrees
    } else {
        assert(q && nq==4 && qCache && nQCache==QuatPoolSize && 
               qErr && nQErr==1);
        const Real quatLen = Vec4::getAs(q).norm();
        qErr[0] = quatLen - Real(1);    // normalization error
        qCache[QuatOONorm] = 1/quatLen; // save for later
    }
}

void calcX_FM(const SBStateDigest& sbs,
              const Real* q,      int nq,
              const Real* qCache, int nQCache,
              Transform&  X_F0M0) const
{
    // Rotation
    if (getUseEulerAngles(sbs.getModelVars())) {
        assert(q && nq==3 && qCache && nQCache==AnglePoolSize);
        X_F0M0.updR().setRotationToBodyFixedXYZ // 18 flops
           (Vec3::getAs(&qCache[AngleCosQ]), Vec3::getAs(&qCache[AngleSinQ]));
    } else {
        assert(q && nq==4 && qCache && nQCache==QuatPoolSize);
        // Must use a normalized quaternion to generate the rotation matrix.
        // Here we normalize with just 4 flops using precalculated 1/norm(q).
        const Quaternion quat(Vec4::getAs(q)*qCache[QuatOONorm], true); 
        X_F0M0.updR().setRotationFromQuaternion(quat); // 29 flops
    }

    // Translation.
    const Vec3& n = X_F0M0.z(); // just calculated above
    X_F0M0.updP() = Vec3(semi[0]*n[0], semi[1]*n[1], semi[2]*n[2]);
}

// Generalized speeds are the angular velocity expressed in F, so they
// cause rotations around F x,y,z axes respectively.
void calcAcrossJointVelocityJacobian(
    const SBStateDigest& sbs,
    HType&               H_FM) const
{
    const SBTreePositionCache& pc = sbs.updTreePositionCache(); // "upd" because we're realizing positions now

    // The normal is M's z axis, expressed in F but only in the frames we
    // used to *define* this mobilizer, not necessarily the ones used after
    // handling mobilizer reversal.
    const Vec3 n = findX_F0M0(pc).z();

    H_FM(0) = SpatialVec( Vec3(1,0,0), Vec3(      0,      -n[2]*semi[1], n[1]*semi[2]) );
    H_FM(1) = SpatialVec( Vec3(0,1,0), Vec3( n[2]*semi[0],       0,     -n[0]*semi[2]) );
    H_FM(2) = SpatialVec( Vec3(0,0,1), Vec3(-n[1]*semi[0], n[0]*semi[1],       0     ) );
}

void calcAcrossJointVelocityJacobianDot(
    const SBStateDigest& sbs,
    HType&               HDot_FM) const
{
    const SBTreePositionCache& pc = sbs.getTreePositionCache();
    const SBTreeVelocityCache& vc = sbs.updTreeVelocityCache(); // "upd" because we're realizing velocities now

    // We need the normal and cross-joint velocity in the frames we're 
    // using to *define* the mobilizer, not necessarily the frames we're 
    // using to compute it it (if it has been reversed).
    const Vec3       n      = findX_F0M0(pc).z();
    const Vec3       w_F0M0 = find_w_F0M0(pc, vc);
    const Vec3       ndot   = w_F0M0 % n; // w_FM x n

    HDot_FM(0) = SpatialVec( Vec3(0), Vec3(      0,         -ndot[2]*semi[1], ndot[1]*semi[2]) );
    HDot_FM(1) = SpatialVec( Vec3(0), Vec3( ndot[2]*semi[0],       0,        -ndot[0]*semi[2]) );
    HDot_FM(2) = SpatialVec( Vec3(0), Vec3(-ndot[1]*semi[0], ndot[0]*semi[1],       0        ) );
}

// CAUTION: we do not zero the unused 4th element of q for Euler angles; it
// is up to the caller to do that if it is necessary.
// Compute out_q = N * in_u
//   or    out_u = in_q * N
// The u quantity is a vector v_F in the parent frame (e.g. angular 
// velocity of child in parent), while the q quantity is the derivative 
// of a quaternion representing R_FM, i.e. orientation of child in parent.
void multiplyByN(const SBStateDigest& sbs, bool matrixOnRight, 
                 const Real* in, Real* out) const
{
    assert(sbs.getStage() >= Stage::Position);
    assert(in && out);

    const SBModelVars&          mv   = sbs.getModelVars();
    const SBTreePositionCache&  pc   = sbs.getTreePositionCache();
    const Vector&               allQ = sbs.getQ();

    if (getUseEulerAngles(mv)) {
        const Rotation& R_FM = getX_FM(pc).R();
        const Mat33 N_FM = // N here mixes parent and body frames
            Rotation::calcNForBodyXYZInBodyFrame(fromQ(allQ)) ;
        if (matrixOnRight) 
                Row3::updAs(out) = (Row3::getAs(in)*N_FM) * ~R_FM;
        else    Vec3::updAs(out) = N_FM * (~R_FM*Vec3::getAs(in));
    } else {
        // Quaternion
        const Mat43 N_FF = // This method returns N in the parent frame
            Rotation::calcUnnormalizedNForQuaternion(fromQuat(allQ));
        if (matrixOnRight) 
                Row3::updAs(out) = Row4::getAs(in) * N_FF;
        else    Vec4::updAs(out) = N_FF * Vec3::getAs(in);
    }
}


// Compute out_u = inv(N) * in_q
//   or    out_q = in_u * inv(N)
void multiplyByNInv(const SBStateDigest& sbs, bool matrixOnRight,
                    const Real* in, Real* out) const
{
    assert(sbs.getStage() >= Stage::Position);
    assert(in && out);

    const SBModelVars&          mv   = sbs.getModelVars();
    const SBTreePositionCache&  pc   = sbs.getTreePositionCache();
    const Vector&               allQ = sbs.getQ();

    if (getUseEulerAngles(mv)) {
        const Rotation& R_FM = getX_FM(pc).R();
        const Mat33 NInv_MF = // NInv here mixes parent and body frames
            Rotation::calcNInvForBodyXYZInBodyFrame(fromQ(allQ));
        if (matrixOnRight) 
                Row3::updAs(out) = (Row3::getAs(in)*R_FM) * NInv_MF;
        else    Vec3::updAs(out) = R_FM * (NInv_MF*Vec3::getAs(in));
    } else {
        // Quaternion
        const Mat34 NInv_FF = // This method returns NInv in  parent frame
            Rotation::calcUnnormalizedNInvForQuaternion(fromQuat(allQ));
        if (matrixOnRight) 
                Row4::updAs(out) = Row3::getAs(in) * NInv_FF;
        else    Vec3::updAs(out) = NInv_FF * Vec4::getAs(in);
    }
}

// Compute out_q = NDot * in_u
//   or    out_u = in_q * NDot
void multiplyByNDot(const SBStateDigest& sbs, bool matrixOnRight,
                    const Real* in, Real* out) const
{
    assert(sbs.getStage() >= Stage::Velocity);
    assert(in && out);

    const SBModelVars&          mv      = sbs.getModelVars();
    const SBTreePositionCache&  pc      = sbs.getTreePositionCache();
    const Vector&               allQ    = sbs.getQ();
    const Vector&               allQDot = sbs.getQDot();

    if (getUseEulerAngles(mv)) {
        const Rotation& R_FM = getX_FM(pc).R();
        const Mat33 NDot_FM = // NDot here mixes parent and body frames
            Rotation::calcNDotForBodyXYZInBodyFrame(fromQ(allQ), 
                                                    fromQ(allQDot)) ;
        if (matrixOnRight) 
                Row3::updAs(out) = (Row3::getAs(in)*NDot_FM) * ~R_FM;
        else    Vec3::updAs(out) = NDot_FM * (~R_FM*Vec3::getAs(in));
    } else {
        // Quaternion
        const Mat43 NDot_FF = // This method returns NDot in the parent frame
            Rotation::calcUnnormalizedNDotForQuaternion(fromQuat(allQDot));
        if (matrixOnRight) 
                Row3::updAs(out) = Row4::getAs(in) * NDot_FF;
        else    Vec4::updAs(out) = NDot_FF * Vec3::getAs(in);
    }
}

void calcQDot(
    const SBStateDigest&   sbs,
    const Vector&          u, 
    Vector&                qdot) const 
{
    const SBModelVars&          mv = sbs.getModelVars();
    const SBTreePositionCache&  pc = sbs.getTreePositionCache();
    const Vec3& w_FM = fromU(u); // angular velocity of M in F 
    if (getUseEulerAngles(mv)) {
        toQuat(qdot) = Vec4(0); // TODO: kludge, clear unused element
        const Rotation& R_FM = getX_FM(pc).R();
        toQ(qdot) = Rotation::convertAngVelInBodyFrameToBodyXYZDot(fromQ(sbs.getQ()),
                                    ~R_FM*w_FM); // need w in *body*, not parent
    } else
        toQuat(qdot) = Rotation::convertAngVelToQuaternionDot(fromQuat(sbs.getQ()),w_FM);
}

void calcQDotDot(
    const SBStateDigest&   sbs,
    const Vector&          udot, 
    Vector&                qdotdot) const 
{
    const SBModelVars&          mv = sbs.getModelVars();
    const SBTreePositionCache&  pc = sbs.getTreePositionCache();
    const Vec3& w_FM     = fromU(sbs.getU()); // angular velocity of J in Jb, expr in Jb
    const Vec3& w_FM_dot = fromU(udot);

    if (getUseEulerAngles(mv)) {
        toQuat(qdotdot) = Vec4(0); // TODO: kludge, clear unused element
        const Rotation& R_FM = getX_FM(pc).R();
        toQ(qdotdot)    = Rotation::convertAngVelDotInBodyFrameToBodyXYZDotDot
                              (fromQ(sbs.getQ()), ~R_FM*w_FM, ~R_FM*w_FM_dot);
    } else
        toQuat(qdotdot) = Rotation::convertAngVelDotToQuaternionDotDot
                              (fromQuat(sbs.getQ()),w_FM,w_FM_dot);
}

void copyQ(
    const SBModelVars& mv, 
    const Vector&      qIn, 
    Vector&            q) const 
{
    if (getUseEulerAngles(mv))
        toQ(q) = fromQ(qIn);
    else
        toQuat(q) = fromQuat(qIn);
}

int getMaxNQ() const {return 4;}
int getNQInUse(const SBModelVars& mv) const {
    return getUseEulerAngles(mv) ? 3 : 4;
} 
bool isUsingQuaternion(const SBStateDigest& sbs, MobilizerQIndex& startOfQuaternion) const {
    if (getUseEulerAngles(sbs.getModelVars())) {startOfQuaternion.invalidate(); return false;}
    startOfQuaternion = MobilizerQIndex(0); // quaternion comes first
    return true;
}

void setMobilizerDefaultPositionValues(
    const SBModelVars& mv,
    Vector&            q) const 
{
    if (getUseEulerAngles(mv)) {
        //TODO: kludge
        toQuat(q) = Vec4(0); // clear unused element
        toQ(q) = 0.;
    }
    else toQuat(q) = Vec4(1.,0.,0.,0.);
}

bool enforceQuaternionConstraints(
    const SBStateDigest& sbs,
    Vector&             q,
    Vector&             qErrest) const 
{
    if (getUseEulerAngles(sbs.getModelVars())) 
        return false;   // no change

    Vec4& quat = toQuat(q);
    quat = quat / quat.norm();

    if (qErrest.size()) {
        Vec4& qerr = toQuat(qErrest);
        qerr -= dot(qerr,quat) * quat;
    }

    return true;
}

void convertToEulerAngles(const Vector& inputQ, Vector& outputQ) const {
    toQuat(outputQ) = Vec4(0); // clear unused element
    toQ(outputQ) = Rotation(Quaternion(fromQuat(inputQ))).convertRotationToBodyFixedXYZ();
}

void convertToQuaternions(const Vector& inputQ, Vector& outputQ) const {
    Rotation rot;
    rot.setRotationToBodyFixedXYZ(fromQ(inputQ));
    toQuat(outputQ) = rot.convertRotationToQuaternion().asVec4();
}

};


#endif // SimTK_SIMBODY_RIGID_BODY_NODE_SPEC_ELLIPSOID_H_

