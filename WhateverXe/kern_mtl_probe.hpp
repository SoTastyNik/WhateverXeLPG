// Initial Meteor Lake display-IP observation. No panel or power-domain access.
#pragma once
#include "kern_xelp_platform.hpp"
#include <string.h>
namespace MTLProbe {
constexpr uint32_t Identity=0x7D558086, Revision=8, DisplayID=0x510A0;
// Register layout and direct early-probe access: Linux v6.12 i915_reg.h and
// display/intel_display_device.c (probe_gmdid_display).
// https://github.com/torvalds/linux/tree/v6.12/drivers/gpu/drm/i915
enum class Status : uint64_t {
    IdentityRejected, KernelRejected, MemoryDisabled, MappingFailed,
    MappingTooSmall, Unavailable, DisplayRejected, Complete
};
struct Result {
    uint64_t abi {1}, status {}, physical {}, revision {}, effective {},
        command {}, mappedBytes {}, gmd {}, architecture {}, release {}, step {};
};
inline bool kernelAllowed(const char *kernel) {
    return kernel && !strcmp(kernel,"Darwin Kernel Version 20.6.0: Thu Jul  6 22:12:47 PDT 2023; root:xnu-7195.141.49.702.12~1/RELEASE_X86_64");
}
template<class IO> Result collect(IO &io,const XELP::Profile &p,const char *kernel) {
    Result r;r.physical=p.identity;r.revision=p.revision;r.effective=p.effectiveIdentity;
    const auto finish=[&](Status status){r.status=static_cast<uint64_t>(status);return r;};
    if(p.identity!=Identity || p.revision!=Revision || p.effectiveIdentity!=Identity ||
       !XELP::same(p,XELP::identify(Identity,Revision)) || p.admitted())
        return finish(Status::IdentityRejected);
    if(!kernelAllowed(kernel))return finish(Status::KernelRejected);
    r.command=io.command();
    if(r.command==UINT16_MAX || !(r.command&2))return finish(Status::MemoryDisabled);
    if(!io.map())return finish(Status::MappingFailed);
    r.mappedBytes=io.bytes();
    if(r.mappedBytes<uint64_t(DisplayID)+4) {
        io.unmap();return finish(Status::MappingTooSmall);
    }
    r.gmd=io.read(DisplayID);
    io.unmap();
    if(!r.gmd || r.gmd==UINT32_MAX)return finish(Status::Unavailable);
    r.architecture=r.gmd>>22;r.release=(r.gmd>>14)&255;r.step=r.gmd&63;
    if(r.architecture!=14 || r.release!=0)return finish(Status::DisplayRejected);
    return finish(Status::Complete);
}
}
