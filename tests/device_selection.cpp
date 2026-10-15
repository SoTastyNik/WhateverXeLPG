#include "kern_xelp_platform.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
using namespace XELP;
int main() {
 unsigned implemented=0;
 for(const auto &entry:DeviceTable) {
  const uint32_t pci=uint32_t(entry.device)<<16|0x8086;
  auto p=identify(pci,0);
  assert(p.identity==pci && p.effectiveIdentity==pci && !p.overridden());
  assert(p.family==entry.family && p.display==entry.display && p.gt==entry.backend);
  assert(!strcmp(model(p),entry.model));
  assert(p.admitted()==(entry.backend!=GT::Unknown));
  implemented+=p.admitted();
  const uint32_t value=entry.device;
  assert(same(p,select(pci,0,true,&value,4)));
  // Every catalogued backend can be selected for an unknown physical ID.
  auto f=select(0x12348086,0,true,&value,4);
  assert(f.overridden() && f.identity==0x12348086 && f.effectiveIdentity==pci);
  assert(f.admitted()==p.admitted());
  assert(!strcmp(model(f),entry.model));
  auto nonIntel=select(0x123410DE,0,true,&value,4);assert(!nonIntel.admitted());
 }
 assert(implemented==8); // Recognition of other table rows adds no native backend.
 // Meteor Lake recognition must not select the TGL donor or a display backend.
 const auto mtl=identify(0x7D558086,8);
 assert(mtl.identity==0x7D558086 && mtl.revision==8);
 assert(mtl.family==Family::MeteorLake && mtl.display==Display::XeLPDP);
 assert(mtl.gt==GT::Unknown && mtl.donor==Donor::Unknown && !mtl.admitted());
 uint32_t mtlId=0x7D55;
 assert(same(mtl,select(0x7D558086,8,true,&mtlId,4)));
 assert(!select(0x9A498086,0,true,&mtlId,4).admitted());
 ProbeConfig mtlProbe;mtlProbe.profile=mtl;mtlProbe.combo=mtlProbe.comboBackend=3;
 assert(!mtlProbe.valid());
 uint32_t id=0x46D2;
 assert(select(0x12348086,0,true,&id,4).admitted());
 assert(!select(0x12348086,1,true,&id,4).admitted()); // Keep physical stepping guard.
 assert(!select(0x12348086,0,false,nullptr,0).admitted());
 for(size_t n:{size_t(0),size_t(1),size_t(2),size_t(3),size_t(5),size_t(8)})
  assert(!select(0x9A498086,0,true,&id,n).admitted());
 assert(!select(0x9A498086,0,true,nullptr,4).admitted());
 for(uint32_t bad:{0U,0xFFFFU,0x808646D2U,0x1234U})
  assert(!select(0x9A498086,0,true,&bad,4).admitted());
 auto p=select(0x46A68086,0,true,&id,4);assert(p.admitted() && p.family==Family::AlderLakeN);
 assert(!strcmp(model(p),"Intel Iris Xe Graphics")); // Name is physical, backend is overridden.
 auto q=p;q.effectiveIdentity=0x46D18086;assert(!same(p,q));
 ProbeConfig c;c.profile=p;c.combo=c.comboBackend=3;assert(c.valid());
 c.abi=2;assert(!c.valid());c.abi=3;c.profile.gt=GT::TGLGT2;assert(!c.valid());
 puts("PASS device table, physical/effective identity, strict override bytes/vendor/revision, model choice and ABI3");
}
