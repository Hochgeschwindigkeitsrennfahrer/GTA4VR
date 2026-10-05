#pragma once

#include <array>
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <limits>

// GTA IV's sampled viewport uses row vectors, camera +Z backwards, and D3D
// depth [0,1]. OpenXR poses have the same local axis convention.
namespace VRMath
{
    using Vector = std::array<float, 4>;
    using Matrix = std::array<Vector, 4>;

    // Native weapon camera: forward=(-sin(yaw)*cos(pitch),
    // cos(yaw)*cos(pitch),sin(pitch)); viewport row 2 points backward.
    inline bool AimAngles(const Matrix& camera,float& pitch,float& yaw)
    {
        const auto& back=camera[2];
        const float horizontal=std::hypot(back[0],back[1]);
        if(!std::isfinite(horizontal+back[2]) || std::hypot(horizontal,back[2])<.5f)
            return false;
        pitch=std::atan2(-back[2],horizontal);
        // Retain the previous yaw at the vertical singularity.
        if(horizontal>.0001f) yaw=std::atan2(back[0],-back[1]);
        return std::isfinite(pitch+yaw);
    }

    // Floor-relative crouch uses configured player height and hysteresis.
    // Pause/tracking gates belong to the caller; live poses never recalibrate it.
    struct PhysicalCrouch
    {
        float reference=1.75f;
        bool active=false;
        bool inhibited=false;
        void CancelUntilStanding() {active=false; inhibited=true;}
        bool Update(float height)
        {
            if(!std::isfinite(height) || height<.45f || height>2.5f) return active;
            if(!std::isfinite(reference) || reference<.45f || reference>2.5f) return active;
            if(inhibited)
            {
                if(height>=.75f*reference) inhibited=false;
                return false;
            }
            if(!active && height<=.65f*reference) active=true;
            else if(active && height>=.75f*reference) active=false;
            return active;
        }
    };

    inline float CrouchCameraDrop(float standingHead,float animatedHead,float physicalDrop)
    {
        if(!std::isfinite(standingHead+animatedHead+physicalDrop)) return 0;
        // Physical lowering is already in the immediate headset pose. Add
        // only native crouch lowering beyond that, for keyboard/gamepad crouch.
        return std::clamp(standingHead-animatedHead-std::max(0.f,physicalDrop),0.f,.8f);
    }

    inline float JumpCameraRise(float standingHead,float animatedHead)
    {
        if(!std::isfinite(standingHead+animatedHead)) return 0;
        // Some jump motion lives in the skeleton while the native root stays
        // on the ground. Root travel is already in the anchor; add only this
        // relative rise, without teaching it to the standing-height baseline.
        return std::clamp(animatedHead-standingHead,0.f,2.f);
    }

    inline float CrouchAnchorAdjustment(float standingHead,float animatedHead,float rootDrop,float physicalDrop,
        bool physicallyRequested=false)
    {
        if(!std::isfinite(rootDrop+physicalDrop)) return -CrouchCameraDrop(standingHead,animatedHead,0);
        rootDrop=std::clamp(rootDrop,0.f,.8f);
        // Physical crouch is represented entirely by the tracked floor height.
        // Undo any native root lowering; do not also follow its duck animation.
        if(physicallyRequested) return rootDrop;
        physicalDrop=std::max(0.f,physicalDrop);
        // Root lowering is already present in the body's world position.
        // Consume physical lowering against that first, then head animation.
        return std::min(rootDrop,physicalDrop)-
            CrouchCameraDrop(standingHead,animatedHead,std::max(0.f,physicalDrop-rootDrop));
    }

    // Collision fraction reserves a small skin before contact. Invalid query
    // output blocks movement rather than permitting an unchecked displacement.
    inline float SafeSweepFraction(float fraction,float distance,float skin=.002f)
    {
        if(!std::isfinite(fraction) || !std::isfinite(distance) || distance<=0 ||
            fraction<0 || fraction>1) return 0;
        return std::max(0.f,fraction-skin/distance);
    }


    // Attribute only observed displacement. Subtracting intended ordinary
    // velocity would invent motion when a collision stops the body. Mixed
    // movement uses a bounded share; it remains an estimate, not a contact solve.
    inline Vector RoomDisplacementShare(const Vector& actual,const Vector& ordinaryVelocity,
        const Vector& roomVelocity,bool settling)
    {
        const float ordinary=std::hypot(ordinaryVelocity[0],ordinaryVelocity[1]);
        const float room=std::hypot(roomVelocity[0],roomVelocity[1]);
        if(!std::isfinite(ordinary+room) || !std::isfinite(actual[0]+actual[1]) ||
            std::hypot(actual[0],actual[1])>.3f) return {};
        const float fraction=ordinary<.01f ? (settling ? 1.f : 0.f) : room/(ordinary+room);
        return {actual[0]*fraction,actual[1]*fraction,0,0};
    }

    // Short-lived horizontal motion requests. Physics consumes each distance
    // once, independent of eye rendering; no catch-up after a tracking gap.
    struct RoomMotion
    {
        Vector pending{}, consumed{};
        void ClearPending() {pending={};}
        void Reset() {pending={}; consumed={};}
        bool Add(const Vector& delta)
        {
            const float length=std::hypot(delta[0],delta[1]);
            if(!std::isfinite(length) || length>.3f) {ClearPending(); return false;}
            pending[0]+=delta[0]; pending[1]+=delta[1];
            const float total=std::hypot(pending[0],pending[1]);
            if(total>.15f)
                for(unsigned axis=0;axis<2;++axis) pending[axis]*=.15f/total;
            return true;
        }
        Vector Consume(float dt)
        {
            Vector velocity{};
            if(!std::isfinite(dt) || dt<=0 || dt>.05f) {ClearPending(); return velocity;}
            const float length=std::hypot(pending[0],pending[1]);
            if(length<1.e-5f) return velocity;
            const float fraction=std::min(1.f,1.5f*dt/length);
            for(unsigned axis=0;axis<2;++axis)
            {
                const float distance=pending[axis]*fraction;
                velocity[axis]=distance/dt;
                consumed[axis]+=distance;
                pending[axis]-=distance;
            }
            return velocity;
        }
    };

    // CE 00431F09..00431F7C, null-distance branch. Preserve the ordered
    // float additions and compare the expanded distance (not an algebraic
    // side<0 shortcut, which can differ at a rounded boundary).
#ifdef _MSC_VER
#pragma float_control(precise,on,push)
#endif
    inline int ClassifySphere(const std::array<Vector,6>& planes,const float* center,float radius)
    {
        int result=2;
        for(const auto& plane:planes)
        {
            const float xy=plane[0]*center[0]+plane[1]*center[1];
            const float xyz=xy+plane[2]*center[2];
            const float side=xyz+plane[3];
            const float expanded=side+radius;
            if(0.f>expanded) return 0;
            if(radius>expanded) result=1;
        }
        return result;
    }
#ifdef _MSC_VER
#pragma float_control(pop)
#endif

    // A sphere is excluded only when wholly outside a normalized plane.
    inline bool SphereOutside(const std::array<Vector,6>& planes, const float* center, float radius)
    {
        if(!center || !std::isfinite(radius) || radius<0) return false;
        for(unsigned axis=0;axis<3;++axis) if(!std::isfinite(center[axis])) return false;
        for(const auto& plane:planes)
        {
            const float distance=plane[0]*center[0]+plane[1]*center[1]+plane[2]*center[2]+plane[3];
            if(distance < -radius) return true;
        }
        return false;
    }

    // Keep boundary-crossing bounds for either eye. Invalid input is retained
    // by SphereOutside; the extra skin avoids precision-driven edge popping.
    inline bool OutsideBothEyes(const std::array<Vector,6> (&eyes)[2],const float* center,float radius)
    {
        if(!std::isfinite(radius) || radius<0) return false;
        return SphereOutside(eyes[0],center,radius+.01f) &&
            SphereOutside(eyes[1],center,radius+.01f);
    }

    inline Matrix Identity()
    {
        return {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}}};
    }

    // GTA IV heading rotates the character's right/forward axes in XY; the
    // view camera uses right/up/back, with game +Z up. Keep a level VR basis.
    inline Matrix UprightGameCamera(float headingDegrees, const Vector& position)
    {
        if(!std::isfinite(headingDegrees+position[0]+position[1]+position[2]))
            throw std::invalid_argument("Invalid first-person anchor");
        const float angle=headingDegrees*3.14159265358979323846f/180.f;
        const float c=std::cos(angle),s=std::sin(angle);
        return {{{c,s,0,0},{0,0,1,0},{s,-c,0,0},
            {position[0],position[1],position[2],1}}};
    }

    inline Matrix Multiply(const Matrix& a, const Matrix& b)
    {
        Matrix result{};
        for (unsigned i=0; i<4; ++i)
            for (unsigned j=0; j<4; ++j)
                for (unsigned k=0; k<4; ++k) result[i][j] += a[i][k]*b[k][j];
        return result;
    }

    inline Vector Transform(const Vector& v, const Matrix& matrix)
    {
        Vector result{};
        for (unsigned j=0; j<4; ++j)
            for (unsigned i=0; i<4; ++i) result[j] += v[i]*matrix[i][j];
        return result;
    }

    // GTA heading is atan2(-forward.x, forward.y); camera row 2 is back.
    // Project to the horizontal plane so pitch/roll cannot tilt locomotion.
    // A vertical gaze has no heading: callers retain their last level heading.
    inline bool LevelHeading(const Matrix& camera,float& radians)
    {
        const float x=camera[2][0],y=-camera[2][1];
        if(!std::isfinite(x) || !std::isfinite(y) || x*x+y*y<1.e-6f) return false;
        radians=std::atan2(x,y);
        return true;
    }

    inline Matrix InverseRigid(const Matrix& camera)
    {
        auto result = Identity();
        for (unsigned i=0; i<3; ++i)
            for (unsigned j=0; j<3; ++j) result[i][j] = camera[j][i];
        for (unsigned j=0; j<3; ++j)
            for (unsigned i=0; i<3; ++i) result[3][j] -= camera[3][i]*result[i][j];
        return result;
    }

    // OpenXR is Y-up. Recenter translation and yaw only; never make the
    // headset's startup pitch/roll the game's new horizon. Near vertical gaze
    // use the projected right axis, which remains defined for upright pitching.
    inline Matrix LevelTrackingReference(const Matrix& pose,bool floorRelative=false)
    {
        float x=pose[2][0],z=pose[2][2];
        if(x*x+z*z<.01f) {x=-pose[0][2]; z=pose[0][0];}
        const float length=std::sqrt(x*x+z*z);
        if(!std::isfinite(length) || length<1.e-6f)
            throw std::invalid_argument("Undefined tracking yaw");
        x/=length; z/=length;
        auto position=pose[3];
        if(floorRelative) position[1]=0;
        return {{{z,0,-x,0},{0,1,0,0},{x,0,z,0},position}};
    }

    inline Matrix Pose(const float (&position)[3], const float (&quaternion)[4])
    {
        for(float value:position)
            if(!std::isfinite(value)) throw std::invalid_argument("Invalid eye position");
        const float length = std::sqrt(quaternion[0]*quaternion[0] + quaternion[1]*quaternion[1] +
            quaternion[2]*quaternion[2] + quaternion[3]*quaternion[3]);
        if (!std::isfinite(length) || length < 1e-6f) throw std::invalid_argument("Invalid eye quaternion");
        const float x=quaternion[0]/length, y=quaternion[1]/length,
            z=quaternion[2]/length, w=quaternion[3]/length;
        return {{{1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w), 0},
            {2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w), 0},
            {2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y), 0},
            {position[0],position[1],position[2],1}}};
    }

    inline Matrix Projection(const float (&angles)[4], float nearClip, float farClip)
    {
        const float left=std::tan(angles[0]), right=std::tan(angles[1]),
            up=std::tan(angles[2]), down=std::tan(angles[3]);
        if (!(nearClip > 0 && farClip > nearClip && right > left && up > down) ||
            !std::isfinite(left+right+up+down+nearClip+farClip))
            throw std::invalid_argument("Invalid eye frustum");
        Matrix result{};
        result[0][0]=2/(right-left);
        result[1][1]=2/(up-down);
        result[2][0]=(right+left)/(right-left);
        result[2][1]=(up+down)/(up-down);
        result[2][2]=-farClip/(farClip-nearClip);
        result[2][3]=-1;
        result[3][2]=-farClip*nearClip/(farClip-nearClip);
        return result;
    }

    inline Matrix Window(float x, float y, float width, float height, float minZ, float maxZ)
    {
        return {{{width/2,0,0,0}, {0,-height/2,0,0}, {0,0,maxZ-minZ,0},
            {x+width/2,y+height/2,minZ,1}}};
    }

    // Order observed in the game: near, left, right, far, top, bottom.
    // Inside is dot(world position, plane) >= 0.
    inline std::array<Vector, 6> Planes(const Matrix& vp)
    {
        std::array<Vector, 6> result{};
        for (unsigned i=0; i<4; ++i)
        {
            result[0][i]=vp[i][2];
            result[1][i]=vp[i][3]+vp[i][0];
            result[2][i]=vp[i][3]-vp[i][0];
            result[3][i]=vp[i][3]-vp[i][2];
            result[4][i]=vp[i][3]-vp[i][1];
            result[5][i]=vp[i][3]+vp[i][1];
        }
        for (auto& plane : result)
        {
            const float length=std::sqrt(plane[0]*plane[0]+plane[1]*plane[1]+plane[2]*plane[2]);
            if (!(length > 0) || !std::isfinite(length)) throw std::invalid_argument("Degenerate frustum plane");
            for (auto& value : plane) value/=length;
        }
        return result;
    }

    // First-image conservative visibility: the world AABB of both finite eye
    // frusta. It can overdraw heavily, but contains both frusta even when they
    // cannot be represented by one centered perspective cone.
    inline std::array<Vector,6> UnionBounds(const Matrix (&cameras)[2],
        const float (&fov)[2][4], float nearClip, float farClip)
    {
        if(!(nearClip>0 && farClip>nearClip) || !std::isfinite(farClip))
            throw std::invalid_argument("Invalid visibility clip range");
        float low[3]{INFINITY,INFINITY,INFINITY},high[3]{-INFINITY,-INFINITY,-INFINITY};
        for(unsigned eye=0;eye<2;++eye)
            for(float distance:{nearClip,farClip})
                for(unsigned x=0;x<2;++x)
                    for(unsigned y=0;y<2;++y)
                    {
                        const auto corner=Transform({distance*std::tan(fov[eye][x]),
                            distance*std::tan(fov[eye][y ? 2 : 3]),-distance,1},cameras[eye]);
                        for(unsigned axis=0;axis<3;++axis)
                        {
                            if(!std::isfinite(corner[axis])) throw std::invalid_argument("Invalid frustum corner");
                            low[axis]=std::min(low[axis],corner[axis]);
                            high[axis]=std::max(high[axis],corner[axis]);
                        }
                    }
        constexpr float margin=.01f;
        return {{{1,0,0,-low[0]+margin},{-1,0,0,high[0]+margin},
            {0,1,0,-low[1]+margin},{0,-1,0,high[1]+margin},
            {0,0,1,-low[2]+margin},{0,0,-1,high[2]+margin}}};
    }
}
