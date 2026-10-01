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
    std::puts("VR math: clip corners, pose inverse, game matrix/plane regression, and conservative union passed");
}
