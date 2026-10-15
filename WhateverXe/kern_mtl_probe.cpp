#include "kern_host.hpp"
#include "kern_mtl_probe.hpp"
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>

extern "C" const char version[];

namespace {
struct ProbeIO {
    IOPCIDevice *pci;
    IOMemoryMap *mapping {};
    uint16_t command() {return pci->configRead16(4);}
    bool map() {
        mapping=pci->mapDeviceMemoryWithRegister(0x10,kIOMapReadOnly | kIOMapInhibitCache);
        if(mapping && !mapping->getVirtualAddress()) {unmap();return false;}
        return mapping!=nullptr;
    }
    uint64_t bytes() const {return mapping->getLength();}
    uint32_t read(uint32_t offset) const {
        auto base=reinterpret_cast<const volatile uint8_t *>(mapping->getVirtualAddress());
        return *reinterpret_cast<const volatile uint32_t *>(base+offset);
    }
    void unmap() {if(mapping){mapping->release();mapping=nullptr;}}
};
}

void XeHost::probeMeteorLake(IOPCIDevice *pci,const XELP::Profile &profile) {
    ProbeIO io {pci};
    const auto result=MTLProbe::collect(io,profile,version);
    pci->setProperty("WhateverXeMeteorLakeProbe",&result,sizeof(result));
}
