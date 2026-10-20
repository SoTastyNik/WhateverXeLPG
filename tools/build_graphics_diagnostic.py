#!/usr/bin/env python3
"""Build an opt-in, read-only graphics diagnostic kext on macOS."""
from pathlib import Path
import argparse, hashlib, json, plistlib, struct, subprocess, zipfile
ROOT=Path(__file__).resolve().parents[1]
def run(cmd):
    try:return subprocess.check_output([str(x) for x in cmd],text=True,stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as error:
        print(error.output,flush=True);raise
def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--sdk',type=Path,default=ROOT/'MacKernelSDK');a.add_argument('--output',type=Path,default=ROOT/'build/graphics-diagnostic');args=a.parse_args()
    sdk=args.sdk.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    source=ROOT/'tools/MTLGraphicsDiag.cpp';obj=out/'MTLGraphicsDiag.o';contents=out/'MTLGraphicsDiag.kext/Contents';(contents/'MacOS').mkdir(parents=True,exist_ok=True);exe=contents/'MacOS/MTLGraphicsDiag'
    flags=['xcrun','clang++','-arch','x86_64','-std=c++17','-O1','-g','-fapple-kext','-ffreestanding','-fno-exceptions','-fno-rtti','-fno-stack-protector','-fno-builtin','-fno-common','-fno-asynchronous-unwind-tables','-mno-red-zone','-nostdinc','-DKERNEL','-DKERNEL_PRIVATE','-DDRIVER_PRIVATE','-DAPPLE','-DNeXT','-DAPPLE_KEXT_ASSERTIONS=1','-I'+str(sdk/'Headers'),'-mmacosx-version-min=10.6','-fdebug-compilation-dir=.','-ffile-prefix-map='+str(ROOT)+'=/WhateverXe','-ffile-prefix-map='+str(sdk)+'=/MacKernelSDK','-Wall','-Wextra','-Wno-unused-parameter','-Wno-deprecated-declarations','-Wno-ossharedptr-misuse']
    run(flags+['-c',source,'-o',obj])
    run(['xcrun','ld','-arch','x86_64','-kext','-undefined','dynamic_lookup','-no_fixup_chains','-macos_version_min','10.6','-o',exe,obj,sdk/'Library/x86_64/libkmod.a'])
    data=exe.read_bytes();assert data[:4]==bytes.fromhex('cffaedfe');assert struct.unpack_from('<I',data,12)[0]==11,'Expected MH_KEXT_BUNDLE'
    assert run(['xcrun','lipo','-archs',exe]).strip()=='x86_64'
    nm=run(['xcrun','nm',exe]);assert ' _kmod_info' in nm and ' __realmain' in nm
    undefined=run(['xcrun','nm','-u',exe])
    forbidden=['chkstk','stack_chk','__cxa','DispatchE5IORPC','configWrite','mapDeviceMemory','_IOMalloc','_IOMemoryDescriptor']
    assert not any(x in undefined for x in forbidden),undefined
    libraries={x:'10.0.0' for x in ['com.apple.kpi.bsd','com.apple.kpi.iokit','com.apple.kpi.libkern','com.apple.kpi.mach','com.apple.kpi.dsep','com.apple.kpi.unsupported']}
    libraries.update({'com.apple.iokit.IOPCIFamily':'1.1','com.apple.iokit.IOGraphicsFamily':'585.1'})
    identity='org.whateverxelpg.MTLGraphicsDiag'
    info=dict(CFBundleIdentifier=identity,CFBundleExecutable='MTLGraphicsDiag',CFBundleName='MTLGraphicsDiag',CFBundleVersion='0.0.1',CFBundleShortVersionString='0.0.1',CFBundlePackageType='KEXT',OSBundleRequired='Root',OSBundleLibraries=libraries,IOKitPersonalities={identity:dict(CFBundleIdentifier=identity,IOClass='MTLGraphicsDiag',IOMatchCategory='MTLGraphicsDiag',IOProviderClass='IOResources',IOResourceMatch='IOKit')})
    (contents/'Info.plist').write_bytes(plistlib.dumps(info))
    manifest=dict(identifier=identity,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),binary_sha256=hashlib.sha256(data).hexdigest(),kernel='20G165 / xnu-7195.141.6~3',physical='8086:7D55 revision08',boot_argument='-mtlgraphicsdiag',required_sdk_commit='05094e5e88cec7caedbfb35e8449ed0db94bf95b')
    (out/'BUILD.json').write_text(json.dumps(manifest,indent=2)+'\n');(out/'undefined-symbols.txt').write_text(undefined)
    archive=out/'MTLGraphicsDiag-0.0.1.zip'
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted((out/'MTLGraphicsDiag.kext').rglob('*')):
            if p.is_file():z.write(p,p.relative_to(out))
        z.write(out/'BUILD.json','BUILD.json')
    print(json.dumps(dict(archive=archive.name,sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),**manifest),indent=2))
if __name__=='__main__':main()
