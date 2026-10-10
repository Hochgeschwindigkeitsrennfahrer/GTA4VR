#include "../../source/includes/vrgrip.hpp"
#include "../../source/includes/vrwheel.hpp"
#include <cstdio>
#include <cstdlib>

static void Near(float a,float b,float tolerance=1.e-4f)
{
    if(!std::isfinite(a) || std::abs(a-b)>tolerance)
    {std::fprintf(stderr,"expected %g got %g\n",b,a);std::exit(1);}
}
static void PoseNear(const VRMath::Matrix& a,const VRMath::Matrix& b)
{for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)Near(a[i][j],b[i][j]);}

int main()
{
    constexpr float pi=3.141592654f;
    {
        const VRMath::Vector seat{-.4f,-.2f,-.2f,1};
        auto frame=VRMath::Identity();
        frame[1]={0,std::sin(pi/9),std::cos(pi/9),0};
        frame[2]={0,-std::cos(pi/9),std::sin(pi/9),0};frame[3]={-.4f,.04f,.19f,1};
        std::vector<VRMath::Vector> points;
        for(unsigned i=0;i<128;++i)
        {
            const float a=2*pi*i/128;
            points.push_back(VRMath::Transform({.16f*std::cos(a),.16f*std::sin(a),0,1},frame));
        }
        // Nearby dashboard geometry must not replace the complete circular rim.
        for(unsigned i=0;i<30;++i) points.push_back({-.6f+i*.01f,.4f,.3f,1});
        const auto fit=VRWheel::FindRim(points,seat);
        if(fit.support<100) return 1;
        for(unsigned a=0;a<3;++a) Near(fit.frame[3][a],frame[3][a],.01f);
        Near(fit.radius,.16f,.01f);
        if(!VRWheel::OnWheel(points[0],fit) || VRWheel::OnWheel({-.4f,.4f,.3f,1},fit))return 1;
    }
    const float origin[3]{};
    {
        auto primary=VRMath::Identity(),support=primary,relative=primary;
        primary[3]={3,5,7,1};relative[3]={.4f,0,0,1};
        support[3]={3,5.4f,7,1};
        const auto aim=VRGrip::TwoHandAim(primary,support,relative);
        Near(aim[0][0],0);Near(aim[0][1],1);
        PoseNear(VRGrip::TwoHandAim(primary,primary,relative),primary);
        for(unsigned i=0;i<4;++i) Near(aim[3][i],primary[3][i]);
        Near(VRMath::Length3(aim[0]),1);Near(VRMath::Dot3(aim[0],aim[1]),0);
        const auto seat=VRMath::Multiply(relative,aim);
        for(unsigned i=0;i<3;++i) Near(seat[3][i],support[3][i]);
        support[3]={2.6f,5,7,1};
        const auto opposite=VRGrip::TwoHandAim(primary,support,relative);
        Near(opposite[0][0],-1);Near(VRMath::Length3(opposite[2]),1);
        // Moving both hands together leaves relative aim unchanged.
        primary[3]={0,0,0,1};support[3]={0,.4f,0,1};
        const auto translated=VRGrip::TwoHandAim(primary,support,relative);
        for(unsigned row=0;row<3;++row) for(unsigned i=0;i<3;++i) Near(translated[row][i],aim[row][i]);
        relative[3]={.08f,0,0,1};support[3]={0,.08f,0,1};
        PoseNear(VRGrip::TwoHandAim(primary,support,relative),primary);
        relative[3]={.20f,0,0,1};support[3]={0,.20f,0,1};
        const auto medium=VRGrip::TwoHandAim(primary,support,relative);
        Near(medium[0][0],std::sqrt(.5f));Near(medium[0][1],std::sqrt(.5f));
        // Physical hands brought together also reduce influence, regardless
        // of a long weapon's configured support seat.
        relative[3]={.4f,0,0,1};support[3]={0,.08f,0,1};
        PoseNear(VRGrip::TwoHandAim(primary,support,relative),primary);
    }
    float hinge=0;
    if(!VRGrip::HingeTarget({0,0,0,1},{0,0,1,0},{1,0,0,1},{0,1,3,1},0,pi/2,hinge)) return 1;
    Near(hinge,1); // Height changes do not affect opening.
    if(!VRGrip::HingeTarget({0,0,0,1},{0,0,1,0},{1,0,0,1},{0,-1,0,1},0,-pi/2,hinge)) return 1;
    Near(hinge,1); // Opposite-handed door.
    if(!VRGrip::HingeTarget({0,0,0,1},{0,0,1,0},{0,1,0,1},{1,0,0,1},1,pi/2,hinge)) return 1;
    Near(hinge,0); // Push closed.
    if(VRGrip::HingeTarget({0,0,0,1},{0,0,1,0},{0,0,1,1},{1,0,0,1},0,pi/2,hinge)) return 1;
    for(unsigned axis=0;axis<3;++axis)
    {
        float q[4]{0,0,0,1};q[axis]=1;q[3]=0;
        const auto turn=VRMath::Pose(origin,q);
        const auto error=VRGrip::RotationError(VRMath::Identity(),turn);
        Near(std::abs(error[axis]),pi);Near(VRMath::Length3(error),pi);
        q[axis]=std::sin(.35f);q[3]=std::cos(.35f);
        const auto small=VRMath::Pose(origin,q);
        Near(VRGrip::RotationError(VRMath::Identity(),small)[axis],.7f);
        Near(VRGrip::RotationError(small,VRMath::Identity())[axis],-.7f);
        // Nonidentity grasp/frame: target and displayed wrist round-trip.
        auto hand=small;hand[3]={2,3,4,1};
        auto body=turn;body[3]={2.1f,3.04f,4.08f,1};
        const auto grasp=VRGrip::Capture(body,hand);
        PoseNear(VRGrip::Target(grasp,hand),body);
        auto moved=VRMath::Multiply(hand,turn);moved[3][1]+=.2f;
        const auto target=VRGrip::Target(grasp,moved);
        const auto wristLocal=VRMath::Multiply(hand,VRMath::InverseRigid(body));
        PoseNear(VRMath::Multiply(wristLocal,target),moved);
        // A fixed grasp does not accumulate drift across repeated evaluation.
        for(unsigned i=0;i<1000;++i)PoseNear(VRGrip::Target(grasp,hand),body);
    }
    auto a=VRMath::Identity(),b=VRMath::Identity();a[3][0]=-1;b[3][0]=1;
    PoseNear(VRGrip::Combine(a,b),VRMath::Identity());
    const float qa[4]{0,0,std::sin(1.5f),std::cos(1.5f)};
    const float qb[4]{0,0,-std::sin(1.5f),std::cos(1.5f)};
    Near(VRMath::Length3(VRGrip::RotationError(VRMath::Identity(),VRGrip::Combine(VRMath::Pose(origin,qa),VRMath::Pose(origin,qb)))),pi);
    // Model metadata does not enter affine transforms; reflected poses reject.
    a[0][3]=std::numeric_limits<float>::quiet_NaN();
    if(!VRGrip::Rigid(a))return 1;Near(a[0][3],0);
    a[2][2]=-1;if(VRGrip::Rigid(a))return 1;
    Near(VRMath::Length3(VRGrip::Bounded({300,400,0,0},2)),2);
    // Throw speed is invariant to sample rate and large world coordinates;
    // duplicate timestamps, stale tracking and short acquisitions reject.
    for(unsigned interval:{4u,11u,20u})
    {
        VRGrip::ThrowMotion motion;VRMath::Vector velocity{};
        for(unsigned ms=0;ms<=220;ms+=interval)
            motion.Add({1000.f+6.f*ms*.001f,-400.f-2.f*ms*.001f,20,1},10000+ms);
        const auto at=motion.samples[motion.count-1].at;
        motion.Add({99999,0,0,1},at); // Same timestamp must not corrupt the fit.
        if(!motion.Velocity(at,velocity)) return 1;
        Near(velocity[0],6,.01f);Near(velocity[1],-2,.01f);Near(velocity[2],0);
        if(motion.Velocity(at+31,velocity)) return 1;
        motion.Add({0,0,0,1},at+101);
        if(motion.Velocity(at+101,velocity)) return 1;
        motion.Add({std::numeric_limits<float>::quiet_NaN(),0,0,1},at+110);
        if(motion.count) return 1;
    }
    VRGrip::ThrowMotion shortGrip;VRMath::Vector velocity{};
    shortGrip.Add({0,0,0,1},100);shortGrip.Add({1,0,0,1},110);
    if(shortGrip.Velocity(110,velocity)) return 1;
    Near(VRGrip::ThrowVelocity({}, {8,0,0,0},.5f,900)[0],8);
    Near(VRGrip::ThrowVelocity({}, {50,0,0,0},.5f,900)[0],50);
    Near(VRGrip::ThrowVelocity({}, {3,0,0,0},.5f,900,4)[0],12);
    Near(VRGrip::ThrowVelocity({}, {3,0,0,0},50,900,4)[0],1.08f);
    Near(VRGrip::ThrowVelocity({}, {8,0,0,0},50,900)[0],1.08f);
    Near(VRGrip::ThrowVelocity({}, {8,0,0,0},50,1800)[0],2.16f);
    Near(VRGrip::ThrowVelocity({3,0,0,0}, {8,0,0,0},0,900)[0],3);
    // Implicit spring should settle a light body without oscillatory explosion
    // across supported timestep rates; strength limits remain caller-owned.
    for(float dt:{1.f/240,1.f/90,1.f/20})
    {
        float x=0,v=0;
        for(unsigned i=0;i<unsigned(2/dt);++i)
        {v+=VRGrip::Spring(1-x,v,0,.5f,12,dt)*dt/.5f;x+=v*dt;}
        Near(x,1,.001f);Near(v,0,.001f);
    }
    {
        float current[2]{.8f,-1.f},reference[2]{.8f,-1.f},acquired[2]{};
        bool held[2]{true,true};float angle=0;
        for(unsigned pass=0;pass<1000;++pass)
        {
            current[0]+=.4f;current[1]+=.4f;
            angle=VRGrip::WheelAngle(current,reference,acquired,held,angle,pi);Near(angle,.4f);
            current[0]-=.4f;current[1]-=.4f;
            angle=VRGrip::WheelAngle(current,reference,acquired,held,angle,pi);Near(angle,0);
        }
        // A hard stop does not redefine the reference. Returning the hand
        // returns the wheel, even after motion beyond the steering range.
        held[1]=false;current[0]=reference[0]+2.f;
        angle=VRGrip::WheelAngle(current,reference,acquired,held,angle,1.f);Near(angle,1);
        current[0]=reference[0];Near(VRGrip::WheelAngle(current,reference,acquired,held,angle,1.f),0);
    }
    std::puts("VR grip: pose, wheel return/stop, two-hand composition, spring and throws passed");
}
