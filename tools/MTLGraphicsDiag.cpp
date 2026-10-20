// SPDX-License-Identifier: BSD-3-Clause
// Standalone read-only recovery diagnostics; no GPU backend or MMIO writes.
#include <mach/kmod.h>
#include <mach/mach_types.h>
#include <kern/thread.h>
#include <sys/proc.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/graphics/IOFramebuffer.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSIterator.h>
#include <libkern/c++/OSString.h>
#include <libkern/c++/OSDictionary.h>
extern "C" const char version[];
class MTLGraphicsDiag : public IOService {
    OSDeclareDefaultStructors(MTLGraphicsDiag)
public:
    bool start(IOService *provider) override;
};
OSDefineMetaClassAndStructors(MTLGraphicsDiag,IOService)
static const char ExpectedKernel[]="Darwin Kernel Version 20.6.0: Mon Aug 30 06:12:21 PDT 2021; root:xnu-7195.141.6~3/RELEASE_X86_64";
static OSIterator *services(const char *name) {
    auto dict=IOService::serviceMatching(name);
    if(!dict)return nullptr;
    auto iterator=IOService::getMatchingServices(dict);
    dict->release();
    return iterator;
}
static IOPCIDevice *targetGPU() {
    auto it=services("IOPCIDevice");if(!it)return nullptr;
    IOPCIDevice *found=nullptr;
    while(auto obj=it->getNextObject()) {
        auto pci=OSDynamicCast(IOPCIDevice,obj);
        if(pci && pci->configRead32(0)==0x7D558086 && pci->configRead8(8)==8) {
            pci->retain();found=pci;break;
        }
    }
    it->release();return found;
}
static void property(IORegistryEntry *entry,const char *key) {
    OSObject *value=entry->copyProperty(key);
    if(!value) {IOLog("MTLGD property %s=ABSENT\n",key);return;}
    if(auto data=OSDynamicCast(OSData,value)) {
        unsigned len=data->getLength();char hex[33]{};
        const auto *bytes=static_cast<const unsigned char *>(data->getBytesNoCopy());
        const char digits[]="0123456789ABCDEF";
        for(unsigned i=0;i<len && i<16;++i){hex[i*2]=digits[bytes[i]>>4];hex[i*2+1]=digits[bytes[i]&15];}
        IOLog("MTLGD property %s: data[%u]=%s\n",key,len,hex);
    } else if(auto str=OSDynamicCast(OSString,value)) {
        IOLog("MTLGD property %s: string=%.64s\n",key,str->getCStringNoCopy());
    } else IOLog("MTLGD property %s: class=%s\n",key,value->getMetaClass()->getClassName());
    value->release();
}
static unsigned serviceCount(const char *name) {
    auto it=services(name);if(!it)return 0;unsigned count=0;
    while(it->getNextObject())++count;
    it->release();return count;
}
static void snapshot(IOPCIDevice *gpu,unsigned seconds) {
    IOLog("\nMTLGD BEGIN t=%u GPU=7D55 rev08\n",seconds);
    property(gpu,"revision-id");property(gpu,"AAPL,boot-display");property(gpu,"device_type");
    auto platform=IOService::getPlatform();PE_Video video{};
    const IOReturn consoleResult=platform?platform->getConsoleInfo(&video):kIOReturnNotReady;
    IOLog("MTLGD console rc=0x%x base=0x%llx %llux%llu depth=%llu stride=%llu\n",consoleResult,
          (unsigned long long)video.v_baseAddr,(unsigned long long)video.v_width,(unsigned long long)video.v_height,
          (unsigned long long)video.v_depth,(unsigned long long)video.v_rowBytes);
    auto it=services("IOFramebuffer");unsigned count=0,boot=0;
    if(it) {
        while(auto obj=it->getNextObject()) {
            auto fb=OSDynamicCast(IOService,obj);if(!fb)continue;++count;
            bool isBoot=fb->metaCast("IOBootFramebuffer")!=nullptr;if(isBoot)++boot;
            auto provider=fb->getProvider();
            IOLog("MTLGD FB[%u] class=%s provider=%s inactive=%u busy=%u\n",count,
                  fb->getMetaClass()->getClassName(),provider?provider->getName():"NONE",fb->isInactive(),fb->getBusyState());
            // Only query the verified native class, never an unknown GPU backend.
            if(isBoot && provider==gpu && !fb->isInactive() && !fb->getBusyState()) {
                auto framebuffer=OSDynamicCast(IOFramebuffer,fb);IODisplayModeID mode=0;IOIndex depth=0;IOPixelInformation pixel{};
                auto result=framebuffer->getCurrentDisplayMode(&mode,&depth);
                auto pixelResult=result==kIOReturnSuccess?framebuffer->getPixelInformation(mode,depth,kIOFBSystemAperture,&pixel):result;
                IOLog("MTLGD boot mode rc=0x%x mode=%d depth=%d pixelrc=0x%x %ux%u stride=%u bpp=%u\n",
                      result,mode,depth,pixelResult,pixel.activeWidth,pixel.activeHeight,pixel.bytesPerRow,pixel.bitsPerPixel);
            }
        }
        it->release();
    }
    bool window=false,recovery=false;unsigned windowPid=0,recoveryPid=0;
    for(int pid=1;pid<=2048;++pid) {
        char name[32]{};proc_name(pid,name,sizeof(name));
        if(!strcmp(name,"WindowServer")){window=true;windowPid=pid;}
        if(!strcmp(name,"recoveryosd")){recovery=true;recoveryPid=pid;}
    }
    IOLog("MTLGD services framebuffer=%u bootfb=%u wrangler=%u\n",count,boot,serviceCount("IODisplayWrangler"));
    IOLog("MTLGD processes(scan<=2048) WindowServer=%u(pid=%u) recoveryosd=%u(pid=%u)\n",window,windowPid,recovery,recoveryPid);
    IOLog("MTLGD END t=%u -- photograph BEGIN through END\n\n",seconds);
}
static void worker(void *,wait_result_t) {
    IOPCIDevice *gpu=nullptr;
    for(unsigned i=0;i<60 && !gpu;++i){IOSleep(1000);gpu=targetGPU();}
    if(!gpu)IOLog("MTLGD identity not present; no inspection\n");
    else {
        for(unsigned i=1;i<=6;++i){IOSleep(45000);snapshot(gpu,i*45);}
        gpu->release();
    }
    thread_terminate(current_thread());
}
bool MTLGraphicsDiag::start(IOService *provider) {
    if(!PE_parse_boot_argn("-mtlgraphicsdiag",nullptr,0))return false;
    if(strcmp(version,ExpectedKernel)){IOLog("MTLGD rejected: exact recovery kernel mismatch\n");return false;}
    if(!IOService::start(provider))return false;
    thread_t thread=THREAD_NULL;
    kern_return_t r=kernel_thread_start(worker,nullptr,&thread);
    if(r==KERN_SUCCESS){thread_deallocate(thread);IOLog("MTLGD armed: read-only snapshots every 45s\n");}
    if(r==KERN_SUCCESS)registerService();
    return r==KERN_SUCCESS;
}
extern "C" kern_return_t MTLGraphicsDiag_start(kmod_info_t *,void *) {return KERN_SUCCESS;}
extern "C" kern_return_t MTLGraphicsDiag_stop(kmod_info_t *,void *) {return KERN_FAILURE;}
extern "C" {
extern kern_return_t _start(kmod_info_t *,void *);
extern kern_return_t _stop(kmod_info_t *,void *);
KMOD_EXPLICIT_DECL(org.whateverxelpg.MTLGraphicsDiag,"0.0.1",_start,_stop)
kmod_start_func_t *_realmain=MTLGraphicsDiag_start;
kmod_stop_func_t *_antimain=MTLGraphicsDiag_stop;
int _kext_apple_cc=__APPLE_CC__;
}
