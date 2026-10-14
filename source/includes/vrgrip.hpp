#pragma once
#include "vrmath.hpp"
#include <cstdint>

// Row-vector rigid poses. Grasp offsets are captured once; never integrate
// controller deltas into an attachment, which would permit accumulated drift.
namespace VRGrip
{
    inline std::array<float,2> CyclicAngles(const VRMath::Vector& point)
    {return {std::atan2(point[1],std::hypot(point[0],point[2])),std::atan2(point[0],point[2])};}
    inline VRMath::Matrix CyclicHandle(float pitch,float roll,float shaft)
    {
        const VRMath::Vector direction{std::sin(roll)*std::cos(pitch),std::sin(pitch),std::cos(roll)*std::cos(pitch),0};
        auto frame=VRMath::SegmentRotation({0,0,1,0},direction);
        frame[3]={shaft*direction[0],shaft*direction[1],shaft*direction[2],1};return frame;
    }
    // A stable body-relative pole replaces animation-derived elbow selection.
    // Outside the reachable interval keep the limb extended/folded on the ray;
    // the caller retains ownership of the tracked wrist endpoint.
    inline bool StableElbow(const VRMath::Vector& shoulder,const VRMath::Vector& target,
        const VRMath::Vector& pole,float upper,float lower,VRMath::Vector& elbow)
    {
        auto ray=VRMath::Difference3(target,shoulder);
        const float distance=VRMath::Length3(ray);
        if(!std::isfinite(distance+upper+lower) || distance<1.e-6f || upper<=0 || lower<=0)return false;
        for(float v:pole)if(!std::isfinite(v))return false;
        for(unsigned axis=0;axis<3;++axis)ray[axis]/=distance;
        const float reach=std::clamp(distance,std::abs(upper-lower),upper+lower);
        if(reach<1.e-6f)return false;
        auto bend=pole;
        const float parallel=VRMath::Dot3(bend,ray);
        for(unsigned axis=0;axis<3;++axis)bend[axis]-=parallel*ray[axis];
        float length=VRMath::Length3(bend);
        if(length<1.e-6f)
        {
            unsigned axis=0;for(unsigned a=1;a<3;++a)if(std::abs(ray[a])<std::abs(ray[axis]))axis=a;
            bend={};bend[axis]=1;
            for(unsigned a=0;a<3;++a)bend[a]-=ray[axis]*ray[a];
            length=VRMath::Length3(bend);
        }
        const float along=(upper*upper-lower*lower+reach*reach)/(2*reach);
        const float across=std::sqrt(std::max(0.f,upper*upper-along*along));
        elbow=shoulder;
        for(unsigned a=0;a<3;++a)elbow[a]+=along*ray[a]+across*bend[a]/length;
        return std::isfinite(VRMath::Length3(elbow));
    }
    // Capture wrist orientation but seat its palm exactly on an animated grip.
    // Following the native grip afterward retains suspension/steering motion.
    inline VRMath::Matrix PalmAttachment(const VRMath::Matrix& wrist,const VRMath::Vector& palm,const VRMath::Matrix& grip)
    {
        auto relative=VRMath::Multiply(wrist,VRMath::InverseRigid(grip));
        auto direction=palm;direction[3]=0;
        const auto offset=VRMath::Transform(direction,relative);
        relative[3]={-offset[0],-offset[1],-offset[2],1};return relative;
    }
    // Absolute circular grab reference. Evaluate each physical hand against
    // its acquisition ray, then fit one common hinge angle. In particular,
    // clamping at a stop must not consume controller motion or move the zero.
    inline float WheelAngle(const float* current,const float* reference,
        const float* acquired,const bool* held,float previous,float limit)
    {
        float sum=0; unsigned count=0;
        for(unsigned hand=0;hand<2;++hand) if(held[hand])
        {
            float candidate=acquired[hand]+std::remainder(current[hand]-reference[hand],6.283185307f);
            candidate=previous+std::remainder(candidate-previous,6.283185307f);
            sum+=candidate; ++count;
        }
        return count ? std::clamp(sum/count,-limit,limit):0.f;
    }
    // Project a tracked grasp onto a native one-axis hinge. Only motion in
    // the hinge plane contributes; pulling along its axis cannot open it.
    inline bool HingeTarget(const VRMath::Vector& origin,const VRMath::Vector& axis,
        const VRMath::Vector& current,const VRMath::Vector& requested,float fraction,
        float span,float& target)
    {
        auto a=VRMath::Difference3(current,origin),b=VRMath::Difference3(requested,origin);
        const float aa=VRMath::Dot3(a,axis),ba=VRMath::Dot3(b,axis);
        for(unsigned i=0;i<3;++i) {a[i]-=axis[i]*aa;b[i]-=axis[i]*ba;}
        if(!std::isfinite(span+fraction) || std::abs(span)<.1f ||
            VRMath::Length3(a)<.08f || VRMath::Length3(b)<.08f) return false;
        const float angle=std::atan2(VRMath::Dot3(VRMath::Cross3(a,b),axis),VRMath::Dot3(a,b));
        target=std::clamp(fraction+angle/span,0.f,1.f);
        return std::isfinite(target);
    }
    using VRMath::Vector;
    using VRMath::Matrix;

    // Aim a held weapon using both tracked positions. The primary wrist is
    // the pivot; its orientation supplies roll. Rotate the authored support
    // baseline onto the physical hand baseline without stretching the weapon.
    inline Matrix TwoHandAim(const Matrix& primary,const Matrix& support,
        const Matrix& relative)
    {
        const auto seat=VRMath::Multiply(relative,primary);
        auto from=VRMath::Difference3(seat[3],primary[3]);
        auto to=VRMath::Difference3(support[3],primary[3]);
        const float a=VRMath::Length3(from),b=VRMath::Length3(to);
        // Coincident hands have no defined aiming direction.
        if(!std::isfinite(a+b) || a<1.e-5f || b<.05f) return primary;
        for(unsigned i=0;i<3;++i) {from[i]/=a;to[i]/=b;}
        auto axis=VRMath::Cross3(from,to);
        float sine=VRMath::Length3(axis);
        float cosine=std::clamp(VRMath::Dot3(from,to),-1.f,1.f);
        if(sine<1.e-5f)
        {
            if(cosine>0) return primary;
            // Deterministic half-turn axis at an antiparallel baseline.
            const Vector reference=std::abs(from[2])<.9f ? Vector{0,0,1,0}:Vector{0,1,0,0};
            axis=VRMath::Cross3(from,reference);
            const float length=VRMath::Length3(axis);
            for(unsigned i=0;i<3;++i) axis[i]/=length;
            sine=0;
        }
        else for(unsigned i=0;i<3;++i) axis[i]/=sine;
        // A short baseline is a poor direction measurement. Smoothly give
        // steering back to the primary wrist as either the authored grip span
        // or the physical hand span shrinks. This applies to every weapon.
        const float spacing=std::min(a,b);
        const float t=std::clamp((spacing-.10f)/.20f,0.f,1.f);
        const float influence=t*t*(3.f-2.f*t);
        const float angle=std::atan2(sine,cosine)*influence;
        cosine=std::cos(angle);sine=std::sin(angle);
        auto result=primary;
        for(unsigned row=0;row<3;++row)
        {
            const auto cross=VRMath::Cross3(axis,primary[row]);
            const float along=VRMath::Dot3(axis,primary[row]);
            for(unsigned i=0;i<3;++i)
                result[row][i]=primary[row][i]*cosine+cross[i]*sine+axis[i]*along*(1-cosine);
        }
        return result;
    }

    inline bool Rigid(Matrix& pose)
    {
        for(unsigned row=0;row<4;++row) for(unsigned axis=0;axis<3;++axis)
            if(!std::isfinite(pose[row][axis])) return false;
        const float x=VRMath::Length3(pose[0]);
        if(x<1.e-5f) return false;
        for(unsigned axis=0;axis<3;++axis) pose[0][axis]/=x;
        const float parallel=VRMath::Dot3(pose[0],pose[1]);
        for(unsigned axis=0;axis<3;++axis) pose[1][axis]-=parallel*pose[0][axis];
        const float y=VRMath::Length3(pose[1]);
        if(y<1.e-5f) return false;
        for(unsigned axis=0;axis<3;++axis) pose[1][axis]/=y;
        const auto z=VRMath::Cross3(pose[0],pose[1]);
        if(VRMath::Dot3(z,pose[2])<.5f) return false;
        pose[2]=z;pose=VRMath::AffineXYZ(pose);
        return true;
    }

    inline Matrix Capture(const Matrix& body,const Matrix& hand)
    {return VRMath::Multiply(body,VRMath::InverseRigid(hand));}

    inline Matrix Target(const Matrix& offset,const Matrix& hand)
    {return VRMath::Multiply(offset,hand);}

    // Shortest world-space rotation vector in radians, including the 180-degree
    // case where the former sum-of-cross-products controller returned zero.
    inline Vector RotationError(Matrix from,Matrix to)
    {
        from[3]=to[3]={0,0,0,1};
        auto q=VRMath::Quaternion(VRMath::Multiply(VRMath::InverseRigid(from),to));
        const float length=std::sqrt(VRMath::Dot3(q,q)+q[3]*q[3]);
        if(!std::isfinite(length) || length<1.e-6f) return {};
        for(auto& value:q) value/=length;
        if(q[3]<0) for(auto& value:q) value=-value;
        const float sine=VRMath::Length3(q);
        if(sine<1.e-6f) return {2*q[0],2*q[1],2*q[2],0};
        const float scale=2*std::atan2(sine,std::max(0.f,q[3]))/sine;
        return {q[0]*scale,q[1]*scale,q[2]*scale,0};
    }

    inline Matrix Combine(const Matrix& a,const Matrix& b)
    {
        auto qa=VRMath::Quaternion(a),qb=VRMath::Quaternion(b);
        float dot=0;for(unsigned i=0;i<4;++i) dot+=qa[i]*qb[i];
        if(dot<0) for(auto& value:qb) value=-value;
        float q[4]{},p[3]{};
        for(unsigned i=0;i<4;++i) q[i]=qa[i]+qb[i];
        for(unsigned i=0;i<3;++i) p[i]=(a[3][i]+b[3][i])*.5f;
        return VRMath::Pose(p,q);
    }

    inline Vector Bounded(Vector value,float maximum)
    {
        const float length=VRMath::Length3(value);
        if(!std::isfinite(length) || !std::isfinite(maximum) || maximum<0) return {};
        if(length>maximum) for(unsigned axis=0;axis<3;++axis) value[axis]*=maximum/length;
        value[3]=0;return value;
    }

    // Fit recent world-space grasp targets, including wrist rotation about
    // an offset grip. Wall-clock seconds avoid frame-rate-dependent throws.
    struct ThrowMotion
    {
        struct Sample {Vector position{};uint64_t at=0;};
        std::array<Sample,16> samples{};
        unsigned count=0;
        void Add(const Vector& position,uint64_t now)
        {
            for(unsigned axis=0;axis<3;++axis) if(!std::isfinite(position[axis])) {count=0;return;}
            if(count && now<=samples[count-1].at) return;
            if(count && now-samples[count-1].at>100) count=0;
            if(count==samples.size())
            {for(unsigned i=1;i<count;++i) samples[i-1]=samples[i];--count;}
            samples[count++]={position,now};
        }
        bool Velocity(uint64_t now,Vector& velocity) const
        {
            velocity={};
            if(count<2 || now<samples[count-1].at || now-samples[count-1].at>30) return false;
            unsigned first=count-1;
            while(first && now>=samples[first-1].at && now-samples[first-1].at<=60) --first;
            if(samples[count-1].at-samples[first].at<20) return false;
            double meanTime=0;Vector mean{};
            const float n=float(count-first);
            for(unsigned i=first;i<count;++i)
            {
                meanTime+=double(samples[i].at-samples[first].at)*.001;
                for(unsigned axis=0;axis<3;++axis) mean[axis]+=samples[i].position[axis]/n;
            }
            meanTime/=n;double variance=0;
            for(unsigned i=first;i<count;++i)
            {
                const double time=double(samples[i].at-samples[first].at)*.001-meanTime;
                variance+=time*time;
                for(unsigned axis=0;axis<3;++axis) velocity[axis]+=float(time*(samples[i].position[axis]-mean[axis]));
            }
            if(variance<1.e-8) return false;
            for(unsigned axis=0;axis<3;++axis) velocity[axis]/=float(variance);
            return std::isfinite(VRMath::Length3(velocity));
        }
    };

    inline Vector ThrowVelocity(const Vector& current,const Vector& tracked,float mass,float strength,float scale=1.f)
    {
        if(!std::isfinite(mass+strength+scale) || mass<=0 || strength<=0) return current;
        // Transfer at most 60ms of the existing grip's force budget. Heavy
        // furniture cannot acquire the same velocity correction as a mug.
        auto requested=tracked;
        for(unsigned axis=0;axis<3;++axis) requested[axis]*=scale;
        if(!std::isfinite(VRMath::Length3(requested))) return current;
        const auto delta=Bounded(VRMath::Difference3(requested,current),strength*.06f/mass);
        auto result=current;
        for(unsigned axis=0;axis<3;++axis) result[axis]+=delta[axis];
        return result;
    }

    inline float Spring(float error,float velocity,float targetVelocity,float mass,float frequency,float dt)
    {
        const float omega=6.283185307f*frequency,k=mass*omega*omega,d=2*mass*omega;
        return (k*(error-velocity*dt)+d*(targetVelocity-velocity))/(1+d*dt/mass+k*dt*dt/mass);
    }

    // Uniform-box estimate, not a claim about GTA's native inertia tensor.
    inline Vector BoxInertia(const Vector& size,float mass)
    {
        Vector inertia{};
        for(unsigned axis=0;axis<3;++axis)
            inertia[axis]=std::max(.0001f,mass*(size[(axis+1)%3]*size[(axis+1)%3]+size[(axis+2)%3]*size[(axis+2)%3])/12.f);
        return inertia;
    }
}
