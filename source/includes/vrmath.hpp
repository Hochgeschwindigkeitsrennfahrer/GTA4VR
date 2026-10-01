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

    inline Matrix Identity()
    {
        return {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}}};
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

    inline Matrix InverseRigid(const Matrix& camera)
    {
        auto result = Identity();
        for (unsigned i=0; i<3; ++i)
            for (unsigned j=0; j<3; ++j) result[i][j] = camera[j][i];
        for (unsigned j=0; j<3; ++j)
            for (unsigned i=0; i<3; ++i) result[3][j] -= camera[3][i]*result[i][j];
        return result;
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
