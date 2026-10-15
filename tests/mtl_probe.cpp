#include "kern_mtl_probe.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace MTLProbe;
constexpr const char *Kernel="Darwin Kernel Version 20.6.0: Thu Jul  6 22:12:47 PDT 2023; root:xnu-7195.141.49.702.12~1/RELEASE_X86_64";
struct IO {
    unsigned commands {},maps {},reads {},unmaps {};
    uint16_t cmd {2};bool mapped {true};uint64_t length {DisplayID+4};
    uint32_t value {(14U<<22)|3};
    uint16_t command(){++commands;return cmd;}
    bool map(){++maps;return mapped;}
    uint64_t bytes(){return length;}
    uint32_t read(uint32_t offset){assert(offset==DisplayID);++reads;return value;}
    void unmap(){++unmaps;}
};
int main() {
    const auto profile=XELP::identify(Identity,Revision);
    for(unsigned revision=0;revision<256;++revision) {
        if(revision==Revision)continue;
        IO io;auto r=collect(io,XELP::identify(Identity,revision),Kernel);
        assert(r.status==uint64_t(Status::IdentityRejected) && !io.commands && !io.maps);
    }
    for(uint32_t identity:{0x9A498086U,0x7D548086U,0x7D5510DEU,0xFFFFFFFFU}) {
        IO io;assert(collect(io,XELP::identify(identity,Revision),Kernel).status==uint64_t(Status::IdentityRejected));
        assert(!io.commands && !io.maps);
    }
    for(uint32_t selected:{0x9A498086U,0x46D28086U,0U}) {
        IO io;auto p=XELP::identify(Identity,Revision,selected);
        assert(collect(io,p,Kernel).status==uint64_t(Status::IdentityRejected));assert(!io.commands);
    }
    {IO io;auto p=profile;p.gt=XELP::GT::TGLGT2;
     assert(collect(io,p,Kernel).status==uint64_t(Status::IdentityRejected));assert(!io.commands);}
    for(const char *kernel:{static_cast<const char *>(nullptr),"", "Darwin Kernel Version 20.6.0"}) {
        IO io;assert(collect(io,profile,kernel).status==uint64_t(Status::KernelRejected));assert(!io.commands);
    }
    for(uint16_t cmd:{uint16_t(0),uint16_t(1),uint16_t(0xFFFF)}) {
        IO io;io.cmd=cmd;assert(collect(io,profile,Kernel).status==uint64_t(Status::MemoryDisabled));
        assert(io.commands==1 && !io.maps);
    }
    {IO io;io.mapped=false;assert(collect(io,profile,Kernel).status==uint64_t(Status::MappingFailed));assert(!io.reads && !io.unmaps);}
    for(uint64_t length:{uint64_t(0),uint64_t(DisplayID),uint64_t(DisplayID+3)}) {
        IO io;io.length=length;assert(collect(io,profile,Kernel).status==uint64_t(Status::MappingTooSmall));
        assert(io.maps==1 && !io.reads && io.unmaps==1);
    }
    for(uint32_t value:{0U,UINT32_MAX,13U<<22,(14U<<22)|(1U<<14),(14U<<22)|63U}) {
        IO io;io.value=value;auto r=collect(io,profile,Kernel);
        const auto expected=(!value || value==UINT32_MAX)?Status::Unavailable:
            value==((14U<<22)|63U)?Status::Complete:Status::DisplayRejected;
        assert(r.status==uint64_t(expected));assert(r.gmd==value);
        assert(io.maps==1 && io.reads==1 && io.unmaps==1);
    }
    IO io;auto r=collect(io,profile,Kernel);
    assert(r.status==uint64_t(Status::Complete) && r.architecture==14 && r.release==0 && r.step==3);
    assert(r.physical==Identity && r.effective==Identity && r.revision==Revision && !profile.admitted());
    puts("PASS MTL physical/override/kernel guards, command and mapping bounds, single read and balanced release");
}
