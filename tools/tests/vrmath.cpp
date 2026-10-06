#include "../../source/includes/vrmath.hpp"
#include <cstdio>
#include <cstdlib>

static void Near(float actual, float expected, float tolerance=1e-4f)
{
    if (!std::isfinite(actual) || std::abs(actual-expected) > tolerance)
    {
        std::fprintf(stderr, "Expected %.9g, got %.9g (tolerance %g)\n", expected, actual, tolerance);
        std::exit(1);
    }
}

int main()
{
    float u=0,v=0,distance=0;
    auto menuScreen=VRMath::Identity();menuScreen[3]={0,1.5f,-2,1};
    if(!VRMath::RayPanel(menuScreen,{0,1.5f,0,1},{0,0,-1,0},2.4f,1.35f,u,v,distance))return 1;
    Near(u,.5f);Near(v,.5f);Near(distance,2);
    if(!VRMath::RayPanel(menuScreen,{.6f,1.8375f,0,1},{0,0,-1,0},2.4f,1.35f,u,v,distance))return 1;
    Near(u,.75f);Near(v,.25f);
    if(VRMath::RayPanel(menuScreen,{0,1.5f,-3,1},{0,0,1,0},2.4f,1.35f,u,v,distance))return 1;
    if(VRMath::RayPanel(menuScreen,{0,1.5f,0,1},{1,0,0,0},2.4f,1.35f,u,v,distance))return 1;
    if(VRMath::RayPanel(menuScreen,{4,1.5f,0,1},{0,0,-1,0},2.4f,1.35f,u,v,distance))return 1;
    // Quaternion reconstruction includes 180-degree branches used by beam quads.
    for(const auto& rotation:{VRMath::Identity(),VRMath::SegmentRotation({0,1,0,0},{0,0,-1,0}),
        VRMath::SegmentRotation({0,0,1,0},{0,0,-1,0})})
    {
        const auto q=VRMath::Quaternion(rotation);const float origin[]{0,0,0},quat[]{q[0],q[1],q[2],q[3]};
        const auto restored=VRMath::Pose(origin,quat);
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)Near(restored[r][c],rotation[r][c]);
    }
    auto fitBone=VRMath::Identity();
    fitBone[3]={12,22,34,NAN}; fitBone[0][3]=NAN;
    const VRMath::Vector fitPivot{10,20,30,1};
    const auto fitted=VRMath::ScaleBoneAbout(fitBone,fitPivot,.8f);
    Near(fitted[3][0],11.6f); Near(fitted[3][2],33.2f); Near(fitted[0][0],.8f);
    if(!std::isnan(fitted[0][3]) || !std::isnan(fitted[3][3])) return 1;
    // Inverse-scaled tracked target returns to the real controller position.
    auto fitHand=VRMath::Identity();
    for(unsigned a=0;a<3;++a) fitHand[3][a]=fitPivot[a]+(fitBone[3][a]-fitPivot[a])/.8f;
    const auto fittedHand=VRMath::ScaleBoneAbout(fitHand,fitPivot,.8f);
    for(unsigned a=0;a<3;++a) Near(fittedHand[3][a],fitBone[3][a]);
    const auto padCenter=VRMath::TouchpadMovement(.1f,.1f);
    Near(padCenter[0],0); Near(padCenter[1],0);
    const auto padForward=VRMath::TouchpadMovement(0,1);
    Near(padForward[0],0); Near(padForward[1],1);
    const auto padBack=VRMath::TouchpadMovement(0,-1);
    Near(padBack[1],-1);
    const auto padDiagonal=VRMath::TouchpadMovement(1,1);
    Near(std::hypot(padDiagonal[0],padDiagonal[1]),1);
    const auto padInvalid=VRMath::TouchpadMovement(NAN,1);
    Near(padInvalid[0],0); Near(padInvalid[1],0);
    // Native GTA bone matrices store metadata (including NaNs) in W lanes.
    // Carry an equipped finger with a moved wrist using only affine XYZ.
    auto oldWrist=VRMath::Identity(),newWrist=VRMath::Identity(),finger=VRMath::Identity();
    oldWrist[3]={10,20,30,NAN};
    newWrist[3]={11,22,33,-4.31602e8f};
    finger[3]={10.2f,20.3f,30.4f,NAN};
    for(unsigned row=0;row<3;++row)
    {
        oldWrist[row][3]=newWrist[row][3]=NAN;
        finger[row][3]=-4.31602e8f;
    }
    const auto carried=VRMath::Multiply(VRMath::AffineXYZ(finger),
        VRMath::Multiply(VRMath::InverseRigid(VRMath::AffineXYZ(oldWrist)),VRMath::AffineXYZ(newWrist)));
    Near(carried[3][0],11.2f); Near(carried[3][1],22.3f); Near(carried[3][2],33.4f);
    for(unsigned row=0;row<4;++row) for(float value:carried[row])
        if(!std::isfinite(value)) return 1;
    if(!std::isnan(oldWrist[3][3]) || finger[0][3]!=-4.31602e8f) return 1;
    // Reconstruct native weapon forward from the extracted headset angles.
    // Sweep pitch and yaw, including wrap and the vertical singularity.
    for(float pitch:{-1.5707963f,-1.1f,0.f,.8f,1.5707963f})
        for(float yaw:{-3.13f,-1.5f,0.f,1.5f,3.13f})
        {
            auto camera=VRMath::Identity();
            camera[2]={std::sin(yaw)*std::cos(pitch),-std::cos(yaw)*std::cos(pitch),-std::sin(pitch),0};
            float actualPitch=0,actualYaw=yaw;
            if(!VRMath::AimAngles(camera,actualPitch,actualYaw)) return 1;
            Near(-std::sin(actualYaw)*std::cos(actualPitch),-camera[2][0]);
            Near(std::cos(actualYaw)*std::cos(actualPitch),-camera[2][1]);
            Near(std::sin(actualPitch),-camera[2][2]);
        }
    auto invalidAim=VRMath::Identity(); invalidAim[2]={NAN,0,0,0};
    float invalidPitch=0,invalidYaw=0;
    if(VRMath::AimAngles(invalidAim,invalidPitch,invalidYaw)) return 1;
    invalidAim[2]={0,0,0,0};
    if(VRMath::AimAngles(invalidAim,invalidPitch,invalidYaw)) return 1;
    VRMath::PhysicalCrouch crouch{};
    Near(crouch.Update(1.75f),false);
    Near(crouch.Update(1.14f),false); // just above 65% of 1.75m
    Near(crouch.Update(.65f*1.75f),true);
    Near(crouch.Update(1.25f),true); // hysteresis avoids threshold chatter
    Near(crouch.Update(.75f*1.75f),false);
    Near(crouch.Update(NAN),false);
    VRMath::PhysicalCrouch seated{};
    seated.reference=1.1f;
    Near(seated.Update(1.1f),false);
    Near(seated.Update(.72f),false);
    Near(seated.Update(.70f),true);
    Near(seated.Update(.80f),true);
    Near(seated.Update(.83f),false);
    Near(seated.reference,1.1f); // tracked poses do not change configuration
    crouch.Update(1.0f);
    crouch.CancelUntilStanding();
    Near(crouch.Update(1.0f),false); // releasing native crouch cannot retrigger
    Near(crouch.Update(1.2f),false);
    Near(crouch.Update(NAN),false); // tracking gap does not rearm
    Near(crouch.Update(.75f*1.75f-.001f),false);
    Near(crouch.inhibited,true);
    Near(crouch.Update(.75f*1.75f),false);
    Near(crouch.inhibited,false);
    Near(crouch.Update(1.0f),true); // only a fresh crouch after standing rearms
    Near(VRMath::CrouchCameraDrop(.7f,.3f,0),.4f); // ordinary control crouch
    Near(VRMath::CrouchCameraDrop(.7f,.3f,.4f),0); // physical pose already lowers view
    Near(VRMath::CrouchCameraDrop(.7f,.3f,.2f),.2f);
    Near(VRMath::CrouchCameraDrop(.7f,NAN,0),0);
    Near(VRMath::CrouchAnchorAdjustment(.7f,.4f,.3f,0),-.3f);
    Near(VRMath::CrouchAnchorAdjustment(.7f,.4f,.3f,.6f),.3f);
    Near(VRMath::CrouchAnchorAdjustment(.7f,.4f,.3f,.2f),-.1f);
    Near(VRMath::JumpCameraRise(.7f,1.2f),.5f); // skeleton jump with stationary root
    Near(VRMath::JumpCameraRise(.7f,.7f),0); // root-only jump is already in anchor
    Near(VRMath::JumpCameraRise(.7f,.4f),0); // crouch must not become jump movement
    Near(VRMath::JumpCameraRise(.7f,NAN),0);
    // Native duck can lower the model farther than a small physical crouch.
    // Its root/animation must not introduce additional camera lowering.
    Near(-.2f-.3f+VRMath::CrouchAnchorAdjustment(.7f,.1f,.2f,.3f,true),-.3f);
    Near(-.3f+VRMath::CrouchAnchorAdjustment(.7f,.1f,0,.3f,true),-.3f);
    Near(VRMath::SafeSweepFraction(.5f,.1f),.48f);
    Near(VRMath::SafeSweepFraction(.01f,.1f),0);
    Near(VRMath::SafeSweepFraction(NAN,.1f),0);
    Near(VRMath::SafeSweepFraction(1.1f,.1f),0);
    // A wall stopping ordinary movement cannot manufacture room compensation.
    auto share=VRMath::RoomDisplacementShare({0,0,0,0},{0,4,0,0},{.1f,0,0,0},true);
    Near(share[0],0); Near(share[1],0);
    share=VRMath::RoomDisplacementShare({.02f,-.01f,0,0},{0,0,0,0},{.3f,0,0,0},true);
    Near(share[0],.02f); Near(share[1],-.01f);
    share=VRMath::RoomDisplacementShare({0,.04f,0,0},{0,4,0,0},{0,0,0,0},true);
    Near(share[0],0); Near(share[1],0); // pure keyboard motion stays in rig origin
    share=VRMath::RoomDisplacementShare({.02f,.02f,0,0},{0,1,0,0},{1,0,0,0},true);
    Near(share[0],.01f); Near(share[1],.01f); // bounded mixed-motion estimate
    // STAGE floor survives table-height startup and recenter while seated.
    // Both calibrations yield the same world height for the same worn pose.
    for(float startupHeight:{.4f,1.2f,1.8f})
    {
        const float startup[]{2,startupHeight,-3}, worn[]{2,1.2f,-3}, q[]{0,0,0,1};
        const auto reference=VRMath::LevelTrackingReference(VRMath::Pose(startup,q),true);
        const auto relative=VRMath::Multiply(VRMath::Pose(worn,q),VRMath::InverseRigid(reference));
        Near(relative[3][1],1.2f);
        const auto world=VRMath::Multiply(relative,VRMath::UprightGameCamera(30,{0,0,18.5f,1}));
        Near(world[3][2],19.7f);
    }
    VRMath::RoomMotion room;
    if(!room.Add({.1f,0,1,0})) return 1;
    auto velocity=room.Consume(.01f);
    Near(velocity[0],1.5f); Near(velocity[1],0); Near(velocity[2],0);
    Near(room.consumed[0],.015f); Near(room.pending[0],.085f);
    for(unsigned i=0;i<20;++i) room.Consume(.01f);
    Near(room.consumed[0],.1f); Near(room.pending[0],0);
    Near(room.Consume(.01f)[0],0); // second eye/ticks cannot replay distance
    room.Add({.1f,.1f,0,0}); room.Consume(.1f);
    Near(room.pending[0],0); Near(room.pending[1],0); // physics stall drops backlog
    if(room.Add({1,0,0,0})) return 1; // tracking jump cannot teleport body
    room.Reset(); room.Add({.1f,0,0,0}); room.Add({-.1f,0,0,0});
    Near(room.Consume(.01f)[0],0);
    room.Add({.2f,.2f,0,0});
    Near(std::hypot(room.pending[0],room.pending[1]),.15f);
    room.ClearPending(); Near(room.Consume(.01f)[0],0);
    // Calibrating while pitched vertically must keep OpenXR Y-up and retain
    // real headset pitch when composed with the game's upright camera.
    for(float pitch:{0.f,1.57079632679f,-1.57079632679f,.7f})
        for(float yaw:{0.f,.9f,-2.f})
        {
            const float p[]{2,1.2f,-3};
            const float qPitch[]{std::sin(pitch/2),0,0,std::cos(pitch/2)};
            const float qYaw[]{0,std::sin(yaw/2),0,std::cos(yaw/2)};
            const float zero[]{0,0,0};
            auto startup=VRMath::Multiply(VRMath::Pose(zero,qPitch),VRMath::Pose(p,qYaw));
            auto reference=VRMath::LevelTrackingReference(startup);
            Near(reference[1][0],0); Near(reference[1][1],1); Near(reference[1][2],0);
            auto relative=VRMath::Multiply(startup,VRMath::InverseRigid(reference));
            Near(relative[3][0],0); Near(relative[3][1],0); Near(relative[3][2],0);
            Near(relative[2][1],-std::sin(pitch));
            const auto levelNow=VRMath::Multiply(VRMath::Pose(p,qYaw),VRMath::InverseRigid(reference));
            Near(levelNow[2][0],0); Near(levelNow[2][1],0); Near(levelNow[2][2],1);
        }
    float heading=0;
    for(float degrees:{0.f,90.f,-90.f,179.f,-179.f})
    {
        auto level=VRMath::UprightGameCamera(degrees,{0,0,0,1});
        if(!VRMath::LevelHeading(level,heading)) return 1;
        Near(heading,degrees*3.14159265358979323846f/180.f);
        // Tilt the forward vector toward world up without changing its yaw.
        level[2][0]*=.2f; level[2][1]*=.2f; level[2][2]=std::sqrt(.96f);
        if(!VRMath::LevelHeading(level,heading)) return 1;
        Near(heading,degrees*3.14159265358979323846f/180.f);
        // Roll only changes the right/up rows, not the facing reference.
        std::swap(level[0],level[1]);
        if(!VRMath::LevelHeading(level,heading)) return 1;
        Near(heading,degrees*3.14159265358979323846f/180.f);
    }
    auto vertical=VRMath::Identity(); vertical[2]={0,0,1,0};
    heading=.7f;
    if(VRMath::LevelHeading(vertical,heading)) return 1;
    Near(heading,.7f);
    // Independently measured local character heading/right axis from #014.
    const auto upright=VRMath::UprightGameCamera(187.954895f,{892.437927f,-499.804688f,20.162432f,1});
    Near(upright[0][0],-.990377f);
    Near(upright[0][1],-.138393f);
    const auto up=VRMath::Transform({0,1,0,0},upright);
    Near(up[0],0); Near(up[1],0); Near(up[2],1);
    const auto forward=VRMath::Transform({0,0,-1,0},upright);
    Near(forward[0],.138393f); Near(forward[1],-.990377f); Near(forward[2],0);
    const auto uprightIdentity=VRMath::Multiply(upright,VRMath::InverseRigid(upright));
    for(unsigned i=0;i<4;++i) for(unsigned j=0;j<4;++j)
        Near(uprightIdentity[i][j],i==j ? 1.f : 0.f,.0002f);
    // Asymmetric OpenXR frustum: every corner must map to the D3D clip cube.
    const float fov[]{-.932127f,.88042f,.958711f,-.956876f};
    const auto projection=VRMath::Projection(fov,.05f,1500);
    // Deferred reconstruction must invert the asymmetric projection, including
    // its off-center terms. Test interior pixels at multiple depths, not only
    // frustum corners. The old symmetric ray loses this offset.
    for(float ndcX : {-.8f,0.f,.7f})
        for(float ndcY : {-.6f,0.f,.9f})
            for(float distance : {.5f,12.f,100.f})
            {
                const float x=(ndcX+projection[2][0])/projection[0][0];
                const float y=(ndcY+projection[2][1])/projection[1][1];
                const auto clip=VRMath::Transform({x*distance,y*distance,-distance,1},projection);
                Near(clip[0]/clip[3],ndcX);
                Near(clip[1]/clip[3],ndcY);
            }
    for (float distance : {.05f,1500.f})
        for (unsigned x=0; x<2; ++x)
            for (unsigned y=0; y<2; ++y)
            {
                auto clip=VRMath::Transform({distance*std::tan(fov[x]),
                    distance*std::tan(fov[y ? 2 : 3]),-distance,1},projection);
                Near(clip[0]/clip[3],x ? 1.f : -1.f);
                Near(clip[1]/clip[3],y ? 1.f : -1.f);
                Near(clip[2]/clip[3],distance < 1 ? 0.f : 1.f);
            }
    const float position[]{3,4,5}, quaternion[]{0,.70710678f,0,.70710678f};
    auto pose=VRMath::Pose(position,quaternion);
    auto identity=VRMath::Multiply(pose,VRMath::InverseRigid(pose));
    for (unsigned i=0;i<4;++i)
        for (unsigned j=0;j<4;++j) Near(identity[i][j],i==j ? 1.f : 0.f);

    // Regression against issue 006's actual game sample. Screen matrix is
    // view-projection * window; matrix offsets are not guessed frustum data.
    VRMath::Matrix camera{{{-.990377367f,-.138393313f,0,0},
        {.0088366922f,-.0632376f,.997959375f,0},
        {-.138110891f,.988356352f,.0638520271f,0},
        {891.773987f,-496.49649f,20.2677002f,1}}};
    const float symmetric[]{-std::atan(1/1.35799503f),std::atan(1/1.35799503f),
        .3926990817f,-.3926990817f};
    auto vp=VRMath::Multiply(VRMath::InverseRigid(camera),VRMath::Projection(symmetric,.05f,1500));
    Near(vp[0][0],-1.34492755f);
    Near(vp[3][0],1106.06104f,.001f);
    auto screen=VRMath::Multiply(vp,VRMath::Window(0,0,1920,1080,0,1));
    Near(screen[0][0],-1158.54407f,.001f);
    Near(screen[3][0],473737.f,.2f);
    auto planes=VRMath::Planes(vp);
    Near(planes[0][0],.138111f);
    Near(planes[1][0],-.715591f);
    Near(planes[4][2],-.946429f);
    Near(planes[0][3],-612.635f,.001f);
    // Far-plane normalization loses precision at this near/far ratio. Report
    // tolerance explicitly; later generation can use camera-space distances.
    Near(planes[3][3],2112.01f,3.f);
    VRMath::Matrix cameras[2]{pose,VRMath::Identity()};
    cameras[1][3][0]=-.034f;
    float eyeFov[2][4]{};
    for(unsigned eye=0;eye<2;++eye)
        for(unsigned i=0;i<4;++i) eyeFov[eye][i]=fov[i];
    const auto bounds=VRMath::UnionBounds(cameras,eyeFov,.05f,1500);
    for(unsigned eye=0;eye<2;++eye)
        for(float distance:{.05f,1500.f})
            for(unsigned x=0;x<2;++x)
                for(unsigned y=0;y<2;++y)
                {
                    const auto corner=VRMath::Transform({distance*std::tan(fov[x]),
                        distance*std::tan(fov[y ? 2 : 3]),-distance,1},cameras[eye]);
                    for(const auto& plane:bounds)
                    {
                        float side=0;
                        for(unsigned i=0;i<4;++i) side+=corner[i]*plane[i];
                        if(side < -.001f) {std::fputs("Union clipped eye corner\n",stderr); return 1;}
                    }
                }
    // Vertical head rotation must keep every eye-frustum corner inside the
    // visibility union, including near-vertical orientations in both directions.
    for(float pitch:{-.5f,.5f,-1.55f,1.55f})
    {
        const float q[]{std::sin(pitch/2),0,0,std::cos(pitch/2)},origin[]{0,0,0};
        VRMath::Matrix tilted[2]{VRMath::Pose(origin,q),VRMath::Pose(origin,q)};
        tilted[0][3][0]=-.034f; tilted[1][3][0]=.034f;
        const auto verticalBounds=VRMath::UnionBounds(tilted,eyeFov,.05f,1500);
        for(const auto& eye:tilted)
            for(float distance:{.05f,1500.f})
                for(unsigned x=0;x<2;++x)
                    for(unsigned y=0;y<2;++y)
                    {
                        const auto corner=VRMath::Transform({distance*std::tan(fov[x]),
                            distance*std::tan(fov[y ? 2 : 3]),-distance,1},eye);
                        for(const auto& plane:verticalBounds)
                        {
                            float side=0;
                            for(unsigned i=0;i<4;++i) side+=corner[i]*plane[i];
                            if(side<-.001f) {std::fputs("Vertical union clipped eye corner\n",stderr); return 1;}
                        }
                    }
    }
    // A rear-facing headset frustum must retain furniture behind the ordinary
    // camera. Boundary-crossing spheres remain visible, including near clip.
    VRMath::Matrix rearCameras[2]{VRMath::Identity(),VRMath::Identity()};
    rearCameras[0][0][0]=rearCameras[1][0][0]=-1;
    rearCameras[0][2][2]=rearCameras[1][2][2]=-1;
    const auto rearBounds=VRMath::UnionBounds(rearCameras,eyeFov,.05f,10);
    const float behind[]{0,0,2}, inFront[]{0,0,-2}, boundary[]{0,0,0};
    if(VRMath::SphereOutside(rearBounds,behind,.2f) ||
        !VRMath::SphereOutside(rearBounds,inFront,.2f) ||
        VRMath::SphereOutside(rearBounds,boundary,.1f)) return 1;
    std::array<VRMath::Vector,6> exactEyes[2];
    for(unsigned eye=0;eye<2;++eye)
        exactEyes[eye]=VRMath::Planes(VRMath::Multiply(VRMath::InverseRigid(rearCameras[eye]),VRMath::Projection(fov,.05f,10)));
    if(VRMath::OutsideBothEyes(exactEyes,behind,.2f) ||
        !VRMath::OutsideBothEyes(exactEyes,inFront,.2f) ||
        VRMath::OutsideBothEyes(exactEyes,boundary,.1f) ||
        VRMath::OutsideBothEyes(exactEyes,nullptr,.2f) ||
        VRMath::OutsideBothEyes(exactEyes,inFront,-.005f)) return 1;
    // Visibility in just one eye must survive union rejection.
    exactEyes[0]=rearBounds;
    exactEyes[1]=VRMath::UnionBounds(cameras,eyeFov,.05f,1500);
    if(VRMath::OutsideBothEyes(exactEyes,behind,.2f)) return 1;
    // Independently expected native classifications in a unit box: wholly
    // outside=0, center outside but sphere intersects=1, center inside=2.
    const std::array<VRMath::Vector,6> box{{{1,0,0,1},{-1,0,0,1},
        {0,1,0,1},{0,-1,0,1},{0,0,1,1},{0,0,-1,1}}};
    const float inside[]{0,0,0}, crossing[]{1.1f,0,0}, touching[]{1.25f,0,0}, outside[]{1.251f,0,0};
    if(VRMath::ClassifySphere(box,inside,.25f)!=2 ||
        VRMath::ClassifySphere(box,crossing,.25f)!=1 ||
        VRMath::ClassifySphere(box,touching,.25f)!=1 ||
        VRMath::ClassifySphere(box,outside,.25f)!=0) return 1;
    // Knee location preserves both segment lengths with the foot planted.
    VRMath::Vector knee{};
    const VRMath::Vector hip{0,0,.6f,1},foot{0,0,0,1},pole{0,1,0,0};
    if(!VRMath::TwoBoneKnee(hip,foot,pole,.4f,.4f,knee) || knee[1]<=0 ||
        std::abs(VRMath::Length3(VRMath::Difference3(knee,hip))-.4f)>1.e-5f ||
        std::abs(VRMath::Length3(VRMath::Difference3(foot,knee))-.4f)>1.e-5f ||
        VRMath::TwoBoneKnee({0,0,1,1},foot,pole,.4f,.4f,knee)) return 1;
    const auto turned=VRMath::Transform({0,0,-1,0},VRMath::SegmentRotation({0,0,-1,0},{0,1,0,0}));
    if(std::abs(turned[1]-1)>1.e-5f || std::abs(turned[2])>1.e-5f) return 1;
    auto stance=VRMath::Identity(); stance[0][3]=123;
    auto settled=VRMath::UprightGameCamera(30,{0,0,0,1});
    // Use a conventional XYZ bone rotation, not camera right/up/back.
    settled=VRMath::SegmentRotation({1,0,0,0},{.8660254f,.5f,0,0}); settled[3][0]=2;
    const auto halfway=VRMath::BlendRigidPose(stance,settled,.5f);
    if(std::abs(VRMath::Dot3(halfway[0],halfway[1]))>1.e-5f ||
        std::abs(VRMath::Length3(halfway[2])-1)>1.e-5f || halfway[0][3]!=123 ||
        halfway[3][0]!=1 || VRMath::BlendRigidPose(stance,settled,0)!=stance) return 1;
    // A binocular HUD must converge on one plane despite asymmetric eye FOVs.
    auto hudLeft=VRMath::Identity(),hudRight=VRMath::Identity();
    hudLeft[3][0]=-.032f;hudRight[3][0]=.032f;
    const auto hudHead=VRMath::BlendRigidPose(hudLeft,hudRight,.5f);
    const auto hudPoint=VRMath::Transform({.3f,.1f,-2.f,1},hudHead);
    const float hudFovs[2][4]{{-.9f,.7f,.8f,-.6f},{-.7f,.9f,.85f,-.65f}};
    float pixels[2]{};
    for(unsigned eye=0;eye<2;++eye)
    {
        const auto& camera=eye ? hudRight:hudLeft;
        const auto p=VRMath::Projection(hudFovs[eye],.05f,1500.f);
        const auto clip=VRMath::Transform(hudPoint,VRMath::Multiply(VRMath::InverseRigid(camera),p));
        const float x=clip[0]/clip[3],y=clip[1]/clip[3];
        pixels[eye]=x;
        // Recover the ray and intersect the plane at2m: both eyes see the same point.
        Near(camera[3][0]+2.f*(x+p[2][0])/p[0][0],hudPoint[0]);
        Near(camera[3][1]+2.f*(y+p[2][1])/p[1][1],hudPoint[1]);
    }
    if(std::abs(pixels[0]-pixels[1])<.01f) return 1;
    std::puts("VR math: camera/visibility, rig, and binocular HUD convergence passed");
}
