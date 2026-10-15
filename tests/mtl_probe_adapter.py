#!/usr/bin/env python3
"""Replay the actual IOKit probe adapter with read-only mock resources."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'WhateverXe/kern_mtl_probe.cpp').read_text()
source = re.sub(r'^#include .*\n', '', source, flags=re.M)
fixture = r'''
#include "kern_mtl_probe.hpp"
#include <cassert>
#include <cstring>
#include <vector>
constexpr unsigned kIOMapReadOnly=1,kIOMapInhibitCache=2;
extern "C" const char version[]="Darwin Kernel Version 20.6.0: Thu Jul  6 22:12:47 PDT 2023; root:xnu-7195.141.49.702.12~1/RELEASE_X86_64";
struct IOMemoryMap {
 std::vector<uint32_t> memory=std::vector<uint32_t>(MTLProbe::DisplayID/4+1);
 unsigned releases=0;bool nullAddress=false;uint64_t length=MTLProbe::DisplayID+4;
 uintptr_t getVirtualAddress(){return nullAddress?0:reinterpret_cast<uintptr_t>(memory.data());}
 uint64_t getLength(){return length;}
 void release(){++releases;}
};
struct IOPCIDevice {
 IOMemoryMap mapping;unsigned maps=0,publications=0;bool fail=false;
 MTLProbe::Result published;
 uint16_t configRead16(unsigned offset){assert(offset==4);return 2;}
 IOMemoryMap *mapDeviceMemoryWithRegister(unsigned offset,unsigned options){
  assert(offset==0x10 && options==(kIOMapReadOnly|kIOMapInhibitCache));
  ++maps;return fail?nullptr:&mapping;
 }
 void setProperty(const char *name,const void *bytes,unsigned length){
  assert(!strcmp(name,"WhateverXeMeteorLakeProbe") && length==sizeof(published));
  memcpy(&published,bytes,length);++publications;
 }
};
struct XeHost {void probeMeteorLake(IOPCIDevice *,const XELP::Profile &);};
// ADAPTER
int main(){
 XeHost host;const auto p=XELP::identify(MTLProbe::Identity,MTLProbe::Revision);
 for(unsigned mode=0;mode<4;++mode){
  IOPCIDevice gpu;gpu.mapping.memory.back()=14U<<22;
  gpu.fail=mode==1;gpu.mapping.nullAddress=mode==2;if(mode==3)gpu.mapping.length=4;
  const auto before=gpu.mapping.memory;
  host.probeMeteorLake(&gpu,p);
  assert(gpu.maps==1 && gpu.publications==1);
  assert(gpu.mapping.releases==(mode==1?0U:1U));
  assert(gpu.mapping.memory==before);
  const auto expected=mode==0?MTLProbe::Status::Complete:mode==3?
      MTLProbe::Status::MappingTooSmall:MTLProbe::Status::MappingFailed;
  assert(gpu.published.status==uint64_t(expected));
 }
 IOPCIDevice gpu;host.probeMeteorLake(&gpu,XELP::identify(0x9A498086,0));
 assert(!gpu.maps && gpu.publications==1 && gpu.published.status==uint64_t(MTLProbe::Status::IdentityRejected));
}
'''.replace('// ADAPTER', source)
with tempfile.TemporaryDirectory(prefix='mtl-probe-') as directory:
    directory = Path(directory)
    cpp, exe = directory / 'test.cpp', directory / 'test'
    cpp.write_text(fixture)
    subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined',
                    '-I' + str(root / 'WhateverXe'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS actual MTL adapter: read-only map, bounds, failure cleanup and publication')
