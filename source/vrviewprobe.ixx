module;
#include <common.hxx>
#include <cstdio>
#include <cwchar>
export module vrviewprobe;
import common;
import comvars;
import openxrvr;
import vrcamera;
export namespace VRViewProbe { void Install(); }

namespace
{
    SafetyHookInline sceneSetup{};
    bool captured = false;

    void Matrix(FILE* file, const char* name, const float (&matrix)[4][4])
    {
        fprintf(file, "%s\n", name);
        for (const auto& row : matrix)
            fprintf(file, "%.9g %.9g %.9g %.9g\n", row[0], row[1], row[2], row[3]);
    }

    void __fastcall SceneSetup(void* self, void* edx, void* parent)
    {
        sceneSetup.unsafe_fastcall(self, edx, parent);
        if (captured || !CTimer::ms_bUserPause || !CTimer::ms_bScriptPause ||
            *CTimer::ms_bUserPause || *CTimer::ms_bScriptPause) return;
        static unsigned sceneFrames = 0;
        if (++sceneFrames < 180) return;
        OpenXRVR::EyeView eyes[2]{};
        long long time = 0;
        if (!OpenXRVR::GetEyeViews(eyes, time)) return;
        wchar_t path[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;
        auto* name = wcsrchr(path, L'\\');
        if (!name) return;
        name[1] = L'\0';
        if (wcscat_s(path, L"plugins\\GTAIV.VR.eyeview.log")) return;
        FILE* file = nullptr;
        if (_wfopen_s(&file, path, L"w") || !file) return;
        captured = true;
        const auto& viewport = *reinterpret_cast<rage::grcViewport*>(static_cast<char*>(self) + 0xB0);
        fprintf(file, "Issue 006 coherent world viewport + latest valid LOCAL eye sample\n");
        fprintf(file, "displayTime=%lld fov=%g aspect=%g near=%g far=%g\n", time,
            viewport.mFov, viewport.mAspect, viewport.mNearClip, viewport.mFarClip);
        fprintf(file, "windows=%g,%g,%g,%g,%g,%g ; %g,%g,%g,%g,%g,%g\n",
            viewport.mGrcViewportWindow1.X,viewport.mGrcViewportWindow1.Y,
            viewport.mGrcViewportWindow1.Width,viewport.mGrcViewportWindow1.Height,
            viewport.mGrcViewportWindow1.MinZ,viewport.mGrcViewportWindow1.MaxZ,
            viewport.mGrcViewportWindow2.X,viewport.mGrcViewportWindow2.Y,
            viewport.mGrcViewportWindow2.Width,viewport.mGrcViewportWindow2.Height,
            viewport.mGrcViewportWindow2.MinZ,viewport.mGrcViewportWindow2.MaxZ);
        fprintf(file, "size=%d,%d scalar2c8=%g,%g scale=%g,%g scalar2d8=%g,%g vector2e0=%g,%g,%g,%g perspective=%u invertZ=%u\n",
            viewport.mWidth,viewport.mHeight,viewport.field_2C8,viewport.field_2CC,
            viewport.mScaleX,viewport.mScaleY,viewport.field_2D8,viewport.field_2DC,
            viewport.field_2E0.x,viewport.field_2E0.y,viewport.field_2E0.z,viewport.field_2E0.w,
            viewport.mIsPerspective,viewport.mInvertZInProjectionMatrix);
        Matrix(file, "camera", viewport.mCameraMatrix);
        Matrix(file, "world44", viewport.mWorldMatrix44);
        Matrix(file, "world", viewport.mWorldMatrix);
        Matrix(file, "worldView", viewport.mWorldViewMatrix);
        Matrix(file, "worldViewProj", viewport.mWorldViewProjMatrix);
        Matrix(file, "viewInverse", viewport.mViewInverseMatrix);
        Matrix(file, "view", viewport.mViewMatrix);
        Matrix(file, "projection", viewport.mProjectionMatrix);
        Matrix(file, "matrix200", viewport.mMatrix_200);
        Matrix(file, "matrix240", viewport.mMatrix_240);
        rage::grcViewport generated[2]{};
        const bool generatedValid=VRCamera::Generate(viewport,eyes,generated);
        fprintf(file,"generatedValid=%u\n",generatedValid);
        if(generatedValid)
            for(unsigned eye=0;eye<2;++eye)
            {
                fprintf(file,"generatedEye=%u\n",eye);
                Matrix(file,"generatedCamera",generated[eye].mViewInverseMatrix);
                Matrix(file,"generatedProjection",generated[eye].mProjectionMatrix);
                Matrix(file,"generatedWorldVP",generated[eye].mWorldViewProjMatrix);
                for(const auto& plane:generated[eye].mFrustumClipPlanes)
                    fprintf(file,"generatedPlane=%g,%g,%g,%g\n",plane.x,plane.y,plane.z,plane.w);
            }
        fprintf(file, "planes\n");
        for (const auto& plane : viewport.mFrustumClipPlanes)
            fprintf(file, "%g %g %g %g\n", plane.x, plane.y, plane.z, plane.w);
        for (unsigned i = 0; i < 2; ++i)
            fprintf(file, "eye%u position=%g,%g,%g orientation=%g,%g,%g,%g fov=%g,%g,%g,%g\n", i,
                eyes[i].position[0], eyes[i].position[1], eyes[i].position[2],
                eyes[i].orientation[0], eyes[i].orientation[1], eyes[i].orientation[2], eyes[i].orientation[3],
                eyes[i].fov[0], eyes[i].fov[1], eyes[i].fov[2], eyes[i].fov[3]);
        fclose(file);
    }
}

void VRViewProbe::Install()
{
    CIniReader ini("");
    if (!ini.ReadInteger("DEVELOPMENT", "ProbeEyeViewport", 0)) return;
    auto pattern = hook::pattern("53 8B 5C 24 08 56 57 8B F9 8D 73 10 56 8D 8F B0 00 00 00");
    if (!pattern.empty()) sceneSetup = safetyhook::create_inline(pattern.get_first(0), SceneSetup);
}
