#pragma once
// Standalone only. Official Vulkan headers; DXVK COM slots pinned to v3.1.1.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <stdexcept>
#ifdef GTA4VR_COPY_PRODUCTION
#include "../../source/includes/dxvk_vulkan_bridge.hpp"
#endif

struct CopyCheck {
    VkDevice device{};
    VkCommandPool pool{};
    VkBuffer buffer{};
    VkDeviceMemory memory{};
    VkFence fence{};
    void* mapped{};
    bool pending=false;
    HMODULE loader=LoadLibraryW(L"vulkan-1.dll");
    PFN_vkGetInstanceProcAddr instanceProc{};
    PFN_vkGetDeviceProcAddr deviceProc{};
#define COPY_FUNCTIONS(X) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) \
    X(ResetCommandPool) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) \
    X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) \
    X(CreateFence) X(DestroyFence) X(ResetFences) X(WaitForFences) \
    X(CmdPipelineBarrier) X(CmdCopyImage) X(CmdCopyImageToBuffer) X(QueueSubmit)
#define DECLARE_COPY(name) PFN_vk##name name{};
    COPY_FUNCTIONS(DECLARE_COPY)
#undef DECLARE_COPY
    static void require(bool okay,const char* operation) {
        if(!okay) throw std::runtime_error(operation);
    }
    static void check(VkResult result,const char* operation) {
        if(result!=VK_SUCCESS) {
            std::printf("%s: VkResult %d\n",operation,result);
            throw std::runtime_error(operation);
        }
    }
    ~CopyCheck() {
        // Never free resources still referenced by a pending submission.
        // This standalone executable terminates immediately on a GPU timeout.
        if(pending) return;
        if(mapped) UnmapMemory(device,memory);
        if(fence) DestroyFence(device,fence,nullptr);
        if(pool) DestroyCommandPool(device,pool,nullptr);
        if(buffer) DestroyBuffer(device,buffer,nullptr);
        if(memory) FreeMemory(device,memory,nullptr);
        if(loader) FreeLibrary(loader);
    }
    void load(VkInstance instance,VkDevice selected) {
        device=selected;
        require(loader!=nullptr,"load Vulkan loader");
        instanceProc=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(loader,"vkGetInstanceProcAddr"));
        require(instanceProc!=nullptr,"vkGetInstanceProcAddr");
        deviceProc=reinterpret_cast<PFN_vkGetDeviceProcAddr>(instanceProc(instance,"vkGetDeviceProcAddr"));
        require(deviceProc!=nullptr,"vkGetDeviceProcAddr");
#define LOAD_COPY(name) name=reinterpret_cast<PFN_vk##name>(deviceProc(device,"vk" #name)); require(name!=nullptr,"vk" #name);
        COPY_FUNCTIONS(LOAD_COPY)
#undef LOAD_COPY
    }
    void imageBarrier(VkCommandBuffer command,VkImage image,VkImageLayout oldLayout,
        VkImageLayout newLayout,VkAccessFlags source,VkAccessFlags destination) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask=source; barrier.dstAccessMask=destination;
        barrier.oldLayout=oldLayout; barrier.newLayout=newLayout;
        barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=image; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        CmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0,0,nullptr,0,nullptr,1,&barrier);
    }
};

static bool verifyVulkanCopies(IDirect3DDevice9* d3d,IDirect3DTexture9* texture,
    IUnknown* interop,IUnknown* textureInterop,VkOpaque opaqueInstance,VkOpaque opaquePhysical,
    VkOpaque opaqueDevice,VkOpaque opaqueQueue,uint32_t family,XrSwapchain chain,
    const std::vector<VulkanSwapchainImage>& images) {
    CopyCheck vk;
#ifdef GTA4VR_COPY_PRODUCTION
    DxvkVulkan::Bridge production;
#endif
    const auto instance=reinterpret_cast<VkInstance>(opaqueInstance);
    const auto physical=reinterpret_cast<VkPhysicalDevice>(opaquePhysical);
    const auto device=reinterpret_cast<VkDevice>(opaqueDevice);
    const auto queue=reinterpret_cast<VkQueue>(opaqueQueue);
    using ImageInfo=HRESULT(STDMETHODCALLTYPE*)(IUnknown*,VkImage*,VkImageLayout*,VkImageCreateInfo*);
    using Action=void(STDMETHODCALLTYPE*)(IUnknown*);
#ifndef GTA4VR_COPY_PRODUCTION
    using Transition=void(STDMETHODCALLTYPE*)(IUnknown*,IUnknown*,const VkImageSubresourceRange*,VkImageLayout,VkImageLayout);
    using WaitResource=bool(STDMETHODCALLTYPE*)(IUnknown*,IDirect3DResource9*,DWORD);
    const auto transition=slot<Transition>(interop,5);
    const auto waitResource=slot<WaitResource>(interop,11);
    const auto flush=slot<Action>(interop,6);
#endif
    const auto lock=slot<Action>(interop,7);
    const auto unlock=slot<Action>(interop,8);
    ComPtr<IDirect3DSurface9> surface,original;
    bool acquired=false;
    try {
        vk.load(instance,device);
#ifdef GTA4VR_COPY_PRODUCTION
        IDirect3DTexture9* sourcePair[2]{texture,texture};
        CopyCheck::require(production.Attach(d3d) && production.SetTextures(sourcePair,64,64),"production Vulkan bridge setup");
#endif
        VkImage source{}; VkImageLayout originalLayout{};
        VkImageCreateInfo sourceInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        CopyCheck::require(SUCCEEDED(slot<ImageInfo>(textureInterop,3)(textureInterop,&source,&originalLayout,&sourceInfo)),"source image info");
        std::printf("Copy source: format=%d layout=%d usage=0x%X size=%ux%u\n",
            sourceInfo.format,originalLayout,sourceInfo.usage,sourceInfo.extent.width,sourceInfo.extent.height);
        CopyCheck::require(sourceInfo.format==VK_FORMAT_B8G8R8A8_UNORM &&
            (sourceInfo.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
            sourceInfo.extent.width==64 && sourceInfo.extent.height==64 &&
            sourceInfo.samples==VK_SAMPLE_COUNT_1_BIT,"source compatibility");
        CopyCheck::require(SUCCEEDED(texture->GetSurfaceLevel(0,&surface)) &&
            SUCCEEDED(d3d->GetRenderTarget(0,&original)),"D3D9 render targets");
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex=family;
        CopyCheck::check(vk.CreateCommandPool(device,&poolInfo,nullptr,&vk.pool),"create command pool");
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool=vk.pool; allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount=1;
        VkCommandBuffer command{};
        CopyCheck::check(vk.AllocateCommandBuffers(device,&allocation,&command),"allocate command buffer");
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size=64*64*4; bufferInfo.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        CopyCheck::check(vk.CreateBuffer(device,&bufferInfo,nullptr,&vk.buffer),"create readback buffer");
        VkMemoryRequirements requirements{};
        vk.GetBufferMemoryRequirements(device,vk.buffer,&requirements);
        auto memoryPropertiesFn=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(vk.instanceProc(instance,"vkGetPhysicalDeviceMemoryProperties"));
        CopyCheck::require(memoryPropertiesFn!=nullptr,"memory properties function");
        VkPhysicalDeviceMemoryProperties properties{}; memoryPropertiesFn(physical,&properties);
        uint32_t memoryType=UINT32_MAX;
        const auto wanted=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for(uint32_t i=0;i<properties.memoryTypeCount;++i)
            if((requirements.memoryTypeBits & (1u<<i)) && (properties.memoryTypes[i].propertyFlags & wanted)==wanted) {memoryType=i;break;}
        CopyCheck::require(memoryType!=UINT32_MAX,"host coherent memory type");
        VkMemoryAllocateInfo memoryInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        memoryInfo.allocationSize=requirements.size; memoryInfo.memoryTypeIndex=memoryType;
        CopyCheck::check(vk.AllocateMemory(device,&memoryInfo,nullptr,&vk.memory),"allocate readback memory");
        CopyCheck::check(vk.BindBufferMemory(device,vk.buffer,vk.memory,0),"bind readback memory");
        CopyCheck::check(vk.MapMemory(device,vk.memory,0,VK_WHOLE_SIZE,0,&vk.mapped),"map readback memory");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        CopyCheck::check(vk.CreateFence(device,&fenceInfo,nullptr,&vk.fence),"create fence");
        std::vector<bool> seen(images.size(),false);
        constexpr unsigned iterations=32;
        for(unsigned iteration=0;iteration<iterations;++iteration) {
            const unsigned char r=static_cast<unsigned char>(51+iteration*3);
            const unsigned char g=static_cast<unsigned char>(102+iteration*2);
            const unsigned char b=static_cast<unsigned char>(204-iteration*4);
            CopyCheck::require(SUCCEEDED(d3d->SetRenderTarget(0,surface.Get())) &&
                SUCCEEDED(d3d->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_ARGB(255,r,g,b),1.0f,0)),"D3D9 clear");
            if(iteration==0) {
                D3DVIEWPORT9 viewport{}; d3d->GetViewport(&viewport);
                std::printf("D3D9 viewport: %u,%u %ux%u\n",viewport.X,viewport.Y,viewport.Width,viewport.Height);
                if(GetEnvironmentVariableW(L"GTA4VR_COPY_PRODUCER_READBACK",nullptr,0)) {
                    ComPtr<IDirect3DSurface9> diagnostic;
                    CopyCheck::require(SUCCEEDED(d3d->CreateOffscreenPlainSurface(64,64,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&diagnostic,nullptr)),"diagnostic surface");
                    CopyCheck::require(SUCCEEDED(d3d->GetRenderTargetData(surface.Get(),diagnostic.Get())),"diagnostic producer copy");
                    D3DLOCKED_RECT pixels{};
                    CopyCheck::require(SUCCEEDED(diagnostic->LockRect(&pixels,nullptr,D3DLOCK_READONLY)),"diagnostic producer map");
                    const auto* p=static_cast<const unsigned char*>(pixels.pBits);
                    std::printf("Diagnostic producer BGRA: %02X %02X %02X %02X\n",p[0],p[1],p[2],p[3]);
                    diagnostic->UnlockRect();
                }
            }
            // Ask DXVK to resolve deferred clears and transition the source on
            // its own command stream before handing it to external commands.
            const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
#ifndef GTA4VR_COPY_PRODUCTION
            transition(interop,textureInterop,&range,originalLayout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            flush(interop);
            // Explicit producer ownership: CPU flush/queue serialization alone
            // returned zero/stale pixels on this installation. Wait for writes.
            CopyCheck::require(waitResource(interop,texture,D3DLOCK_READONLY),"wait DXVK source writes");
#endif
            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            uint32_t index=0;
            lock(interop);
            const auto acquireResult=xrAcquireSwapchainImage(chain,&acquireInfo,&index);
            unlock(interop);
            CopyCheck::require(XR_SUCCEEDED(acquireResult),"acquire XR image");
            acquired=true;
            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitInfo.timeout=5'000'000'000;
            CopyCheck::require(xrWaitSwapchainImage(chain,&waitInfo)==XR_SUCCESS,"wait XR image");
            CopyCheck::require(index<images.size(),"XR image index");
            const VkImage target=images[index].image;
#ifdef GTA4VR_COPY_PRODUCTION
            CopyCheck::require(production.Copy(iteration%2,target),"production Vulkan eye copy");
#endif
            CopyCheck::check(vk.ResetCommandPool(device,vk.pool,0),"reset command pool");
            CopyCheck::check(vk.ResetFences(device,1,&vk.fence),"reset fence");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            CopyCheck::check(vk.BeginCommandBuffer(command,&begin),"begin commands");
#ifndef GTA4VR_COPY_PRODUCTION
            vk.imageBarrier(command,target,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                0,VK_ACCESS_TRANSFER_WRITE_BIT); // Discard prior contents after runtime wait.
            VkImageCopy region{};
            region.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            region.dstSubresource=region.srcSubresource; region.extent={64,64,1};
            vk.CmdCopyImage(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
            vk.imageBarrier(command,target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
#else
            vk.imageBarrier(command,target,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
            VkImageCopy region{};
            region.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; region.extent={64,64,1};
#endif
            VkBufferImageCopy readback{};
            readback.imageSubresource=region.srcSubresource; readback.imageExtent=region.extent;
            vk.CmdCopyImageToBuffer(command,target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,vk.buffer,1,&readback);
            vk.imageBarrier(command,target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            host.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            host.buffer=vk.buffer; host.size=VK_WHOLE_SIZE;
            vk.CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
                0,0,nullptr,1,&host,0,nullptr);
            CopyCheck::check(vk.EndCommandBuffer(command),"end commands");
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount=1; submit.pCommandBuffers=&command;
            lock(interop);
            const auto submitted=vk.QueueSubmit(queue,1,&submit,vk.fence);
            unlock(interop); // No COM/D3D9 call while the queue is locked.
            CopyCheck::check(submitted,"submit copy");
            vk.pending=true;
            const auto completed=vk.WaitForFences(device,1,&vk.fence,VK_TRUE,5'000'000'000);
            if(completed!=VK_SUCCESS) {
                std::printf("GPU copy failed/timed out: %d; exiting without freeing pending resources\n",completed);
                // Avoid unwinding COM owners while submitted resources may be live.
                std::fflush(stdout); ExitProcess(8);
            }
            vk.pending=false;
#ifndef GTA4VR_COPY_PRODUCTION
            transition(interop,textureInterop,&range,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,originalLayout);
            flush(interop);
#endif
            const auto* pixels=static_cast<const unsigned char*>(vk.mapped);
            bool correct=true;
            for(unsigned pixel=0;pixel<64*64;++pixel) {
                const auto* p=pixels+pixel*4;
                if(p[0]!=b || p[1]!=g || p[2]!=r || p[3]!=255) {correct=false;break;}
            }
            std::printf("Copy %u XR image %u BGRA %02X %02X %02X %02X all pixels=%s\n",
                iteration,index,pixels[0],pixels[1],pixels[2],pixels[3],correct ? "PASS":"FAIL");
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            lock(interop);
            const auto releaseResult=xrReleaseSwapchainImage(chain,&release);
            unlock(interop);
            CopyCheck::require(XR_SUCCEEDED(releaseResult),"release XR image");
            acquired=false; seen[index]=true;
            CopyCheck::require(correct,"pixel mismatch");
        }
        CopyCheck::require(SUCCEEDED(d3d->SetRenderTarget(0,original.Get())),"restore D3D9 target");
#ifdef GTA4VR_COPY_PRODUCTION
        CopyCheck::require(production.Destroy(),"production bridge cleanup");
#endif
        std::printf("PASS %u repeated Vulkan copies/readbacks; runtime returned %u of %u images\n",
            iterations,static_cast<unsigned>(std::count(seen.begin(),seen.end(),true)),static_cast<unsigned>(images.size()));
        return true;
    } catch(const std::exception& error) {
        std::printf("FAIL Vulkan copy check: %s\n",error.what());
        if(acquired) {
            // Destruction of the swapchain handles an un-waited/acquired image;
            // do not release unless the normal path successfully waited it.
            std::puts("Abandoning acquired image; caller destroys swapchain");
        }
        if(original) d3d->SetRenderTarget(0,original.Get());
#ifdef GTA4VR_COPY_PRODUCTION
        if(!production.Destroy()) {std::fflush(stdout);ExitProcess(8);}
#endif
        return false;
    }
}
