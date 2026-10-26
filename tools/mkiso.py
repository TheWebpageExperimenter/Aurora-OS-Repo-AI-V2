#!/usr/bin/env python3
"""Small standards-shaped ISO-9660 / El Torito builder for Aurora's BIOS+UEFI CD.
This avoids needing xorriso to package the supplied Limine boot images. It does
not create Rock Ridge/Joliet names; ISO 9660 identifiers are uppercase + ;1.
"""
from __future__ import annotations
import argparse, math, os, struct, subprocess
from pathlib import Path

SECTOR = 2048
ROOT = Path(__file__).resolve().parents[1]

def ceildiv(a, b=SECTOR): return (a + b - 1) // b

def both32(n): return struct.pack('<I', n) + struct.pack('>I', n)
def both16(n): return struct.pack('<H', n) + struct.pack('>H', n)
def dt7(): return bytes([126, 10, 22, 8, 0, 0, 0])
def dt17(): return b'2026102208000000\x00'

def iso_id(name: str, is_dir: bool):
    if is_dir:
        return name.upper().encode('ascii')
    return (name.upper() + ';1').encode('ascii')

class Node:
    def __init__(self, name, source=None, is_dir=False, parent=None):
        self.name, self.source, self.is_dir, self.parent = name, source, is_dir, parent
        self.children=[]
        self.data=b''
        self.lba=0
        self.size=0
        if source is not None: self.data=Path(source).read_bytes()
    def add_dir(self, name):
        n=Node(name,is_dir=True,parent=self); self.children.append(n); return n
    def add_file(self, name, path):
        n=Node(name,path,False,self); self.children.append(n); return n

def record(namebytes, extent, size, is_dir=False):
    nlen=len(namebytes)
    rlen=33+nlen+(1 if nlen % 2 == 0 else 0)
    b=bytearray(rlen)
    b[0]=rlen; b[1]=0
    b[2:10]=both32(extent); b[10:18]=both32(size)
    b[18:25]=dt7(); b[25]=2 if is_dir else 0
    b[26]=0; b[27]=0; b[28:32]=both16(1)
    b[32]=nlen; b[33:33+nlen]=namebytes
    return bytes(b)

def serialize_dir(node):
    out=bytearray()
    entries=[(b'\x00',node), (b'\x01',node.parent if node.parent else node)]
    entries += [(iso_id(c.name,c.is_dir),c) for c in sorted(node.children,key=lambda n:(not n.is_dir,n.name.upper()))]
    for ident,child in entries:
        rec=record(ident,child.lba,child.size,child.is_dir)
        sector_off=len(out)%SECTOR
        if sector_off+len(rec)>SECTOR:
            out.extend(b'\0'*(SECTOR-sector_off))
        out.extend(rec)
    node.data=bytes(out); node.size=len(out)

def dirs_breadth(root):
    out=[]; q=[root]
    while q:
        d=q.pop(0); out.append(d)
        q.extend(c for c in d.children if c.is_dir)
    return out

def path_table(dirs, endian):
    out=bytearray()
    numbers={d:i+1 for i,d in enumerate(dirs)}
    for d in dirs:
        ident=b'\0' if d.parent is None else d.name.upper().encode('ascii')
        out.append(len(ident)); out.append(0)
        out.extend(struct.pack('<I' if endian=='L' else '>I', d.lba))
        parent_no=1 if d.parent is None else numbers[d.parent]
        out.extend(struct.pack('<H' if endian=='L' else '>H', parent_no))
        out.extend(ident)
        if len(ident)&1: out.append(0)
    return bytes(out)

def set_pvd_string(buf, off, length, val):
    raw=val.encode('ascii')[:length]
    buf[off:off+length]=raw+b' '*(length-len(raw))

def build_iso(output: Path, kernel_path: Path):
    root=Node('',is_dir=True,parent=None)
    boot=root.add_dir('BOOT')
    limine=boot.add_dir('LIMINE')
    assets=boot.add_dir('ASSETS')
    efi=root.add_dir('EFI'); efiboot=efi.add_dir('BOOT')
    kernel=boot.add_file('KERNEL.ELF',kernel_path)
    config=limine.add_file('LIMINE.CONF',ROOT/'kernel/limine.conf')
    bios_sys=limine.add_file('LIMINE-BIOS.SYS',ROOT/'limine/limine-bios.sys')
    bios_cd=limine.add_file('LIMINE-BIOS-CD.BIN',ROOT/'limine/limine-bios-cd.bin')
    uefi_cd=limine.add_file('LIMINE-UEFI-CD.BIN',ROOT/'limine/limine-uefi-cd.bin')
    efiboot.add_file('BOOTX64.EFI',ROOT/'limine/BOOTX64.EFI')
    assets.add_file('CA-CERTIFICATES.CRT',ROOT/'assets/certificates/ca-certificates.crt')
    assets.add_file('SEGOEUI.TTF',ROOT/'assets/fonts/SegoeUI.ttf')
    assets.add_file('FONT-NOTICE.TXT',ROOT/'assets/fonts/SegoeUI-Font-Notice.txt')
    assets.add_file('README.TXT',ROOT/'README.md')
    assets.add_file('AURORA-WALLPAPER.JPG', ROOT/'assets/wallpapers/aurora.jpg')

    # Descriptors: PVD 16, boot record 17, terminator 18; El Torito catalog at 19.
    catalog_lba=19
    lba=20
    dirs=dirs_breadth(root)
    pt_l=path_table(dirs,'L'); pt_m=path_table(dirs,'M')
    pt_l_lba=lba; lba += ceildiv(len(pt_l))
    pt_m_lba=lba; lba += ceildiv(len(pt_m))

    # Allocate directory extents by actual directory record sizes; lengths only depend on children names/counts.
    for d in dirs:
        children=[(b'\x00',d),(b'\x01',d.parent if d.parent else d)]
        children += [(iso_id(c.name,c.is_dir),c) for c in sorted(d.children,key=lambda n:(not n.is_dir,n.name.upper()))]
        sz=0
        for ident,_ in children:
            rec_len=33+len(ident)+(1 if len(ident)%2==0 else 0)
            off=sz%SECTOR
            if off+rec_len>SECTOR: sz += SECTOR-off
            sz += rec_len
        d.size=sz; d.lba=lba; lba += ceildiv(sz)

    # Path tables reference these directory extents; their byte lengths are unchanged.
    pt_l=path_table(dirs,'L'); pt_m=path_table(dirs,'M')

    # File extents follow directories.
    files=[]
    for d in dirs:
        files += [c for c in d.children if not c.is_dir]
    for f in files:
        f.size=len(f.data); f.lba=lba; lba += ceildiv(f.size)

    # Patch the El Torito boot information table in the BIOS boot image.
    bios = bytearray(bios_cd.data)
    if len(bios) >= 64:
        struct.pack_into('<I',bios,8,16)            # primary volume descriptor LBA
        struct.pack_into('<I',bios,12,bios_cd.lba) # boot image LBA
        struct.pack_into('<I',bios,16,len(bios))
        struct.pack_into('<I',bios,20,0)
        sm=0
        for off in range(64,len(bios),4):
            sm=(sm+struct.unpack_from('<I',bios,off)[0]) & 0xffffffff
        struct.pack_into('<I',bios,20,sm)
        bios_cd.data=bytes(bios)

    # Serialize directories now that all extents are assigned.
    for d in dirs: serialize_dir(d)

    # Primary Volume Descriptor.
    pvd=bytearray(SECTOR); pvd[0]=1; pvd[1:6]=b'CD001'; pvd[6]=1
    set_pvd_string(pvd,8,32,'AURORA OS')
    set_pvd_string(pvd,40,32,'AURORA_110')
    pvd[80:88]=both32(lba)
    pvd[120:124]=both16(1); pvd[124:128]=both16(1); pvd[128:132]=both16(SECTOR)
    pvd[132:140]=both32(len(pt_l))
    pvd[140:144]=struct.pack('>I',len(pt_m)); pvd[144:148]=struct.pack('>I',0)
    pvd[140:144]=struct.pack('<I',pt_l_lba); pvd[148:152]=struct.pack('>I',pt_m_lba)
    pvd[156:190]=record(b'\0',root.lba,root.size,True)
    set_pvd_string(pvd,190,128,'AURORA OS ALPHA 1.0.10')
    set_pvd_string(pvd,318,128,'AURORA OS PROJECT')
    set_pvd_string(pvd,446,128,'AURORA')
    for off in (813,830,847,864): pvd[off:off+17]=dt17()
    pvd[881]=1  # file structure version

    # El Torito boot record descriptor.
    br=bytearray(SECTOR); br[0]=0; br[1:6]=b'CD001'; br[6]=1
    # El Torito Boot System Identifier is NUL-padded, not space-padded.
    # Several BIOS implementations fail to find the boot catalog if this is
    # written with ordinary ISO text-field padding.
    br[7:39]=b'EL TORITO SPECIFICATION'.ljust(32, b'\0')
    # Boot Identifier [39:71] and reserved bytes are already zero.
    br[71:75]=struct.pack('<I',catalog_lba)
    term=bytearray(SECTOR); term[0]=255; term[1:6]=b'CD001'; term[6]=1

    # Boot catalog validation entry, BIOS default boot entry, then UEFI section.
    cat=bytearray(SECTOR)
    cat[0]=1; cat[1]=0
    cat[4:28]=(b'AURORA ALPHA 1.0.10'[:24]).ljust(24,b' ')
    cat[30:32]=b'\x55\xaa'
    words=struct.unpack('<16H',cat[:32]); struct.pack_into('<H',cat,28,(-sum(words))&0xffff)
    def entry(off, image_lba):
        cat[off]=0x88; cat[off+1]=0; cat[off+2:off+4]=b'\0\0'
        cat[off+4]=0; cat[off+5]=0
        cat[off+6:off+8]=struct.pack('<H',4)
        cat[off+8:off+12]=struct.pack('<I',image_lba)
    entry(32,bios_cd.lba)
    cat[64]=0x91; cat[65]=0xef; cat[66:68]=struct.pack('<H',1)
    cat[68:96]=b'AURORA UEFI BOOT'.ljust(28,b' ')
    entry(96,uefi_cd.lba)

    # Write a complete sector-addressed image.
    total_sectors=lba
    image=bytearray(total_sectors*SECTOR)
    image[16*SECTOR:17*SECTOR]=pvd
    image[17*SECTOR:18*SECTOR]=br
    image[18*SECTOR:19*SECTOR]=term
    image[catalog_lba*SECTOR:(catalog_lba+1)*SECTOR]=cat
    image[pt_l_lba*SECTOR:pt_l_lba*SECTOR+len(pt_l)]=pt_l
    image[pt_m_lba*SECTOR:pt_m_lba*SECTOR+len(pt_m)]=pt_m
    for d in dirs:
        off=d.lba*SECTOR; image[off:off+len(d.data)]=d.data
    for f in files:
        off=f.lba*SECTOR; image[off:off+len(f.data)]=f.data
    output.write_bytes(image)
    print(f'Wrote {output} ({len(image):,} bytes, {total_sectors} sectors)')
    print(f'El Torito catalog: LBA {catalog_lba}; BIOS image: LBA {bios_cd.lba}; UEFI image: LBA {uefi_cd.lba}')

if __name__ == '__main__':
    ap=argparse.ArgumentParser()
    ap.add_argument('--output', default=str(ROOT/'aurora.iso'))
    ap.add_argument('--kernel', default=str(ROOT/'build/kernel.elf'))
    ns=ap.parse_args()
    build_iso(Path(ns.output), Path(ns.kernel))
