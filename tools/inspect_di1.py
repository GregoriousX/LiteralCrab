"""Read-only PE/RPX inspection for the DI1 display port. No game files are changed."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).parent / "di1-deps"))
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32


def rpx(path):
    raw = Path(path).read_bytes()
    if raw[:6] != b"\x7fELF\x01\x02":
        raise ValueError("Expected ELF32 big endian RPX")
    shoff = struct.unpack_from(">I", raw, 0x20)[0]
    entsize, count, names_index = struct.unpack_from(">HHH", raw, 0x2E)
    sections = []
    for index in range(count):
        entry = struct.unpack_from(">10I", raw, shoff + index * entsize)
        name, kind, flags, addr, offset, size, link, info, align, stride = entry
        data = raw[offset:offset + size] if kind != 8 else b""
        if flags & 0x08000000:
            expected = struct.unpack_from(">I", data)[0]
            data = zlib.decompress(data[4:])
            assert len(data) == expected
        sections.append(dict(index=index, name_offset=name, type=kind, flags=flags,
                             address=addr, offset=offset, size=len(data), link=link,
                             stride=stride, data=data))
    names = sections[names_index]["data"]
    for sec in sections:
        off = sec["name_offset"]
        sec["name"] = names[off:names.find(b"\0", off)].decode("utf-8", "replace")
    symbols = []
    for sec in sections:
        if sec["type"] not in (2, 11):
            continue
        strings = sections[sec["link"]]["data"]
        for off in range(0, len(sec["data"]), sec["stride"] or 16):
            name, addr, size, info, other, section = struct.unpack_from(">IIIBBH", sec["data"], off)
            text = strings[name:strings.find(b"\0", name)].decode("utf-8", "replace")
            symbols.append(dict(name=text, address=f"0x{addr:08X}", size=size, section=section))
    return sections, symbols


def pe(path):
    raw = Path(path).read_bytes()
    image = pefile.PE(data=raw)
    print(json.dumps(dict(file=str(path), sha256=hashlib.sha256(raw).hexdigest(),
                         machine=hex(image.FILE_HEADER.Machine),
                         timestamp=hex(image.FILE_HEADER.TimeDateStamp),
                         image_base=hex(image.OPTIONAL_HEADER.ImageBase),
                         image_size=hex(image.OPTIONAL_HEADER.SizeOfImage),
                         checksum=hex(image.OPTIONAL_HEADER.CheckSum),
                         entrypoint=hex(image.OPTIONAL_HEADER.AddressOfEntryPoint)), indent=2))
    print("SECTIONS", [(s.Name.decode().strip("\0"), hex(s.VirtualAddress), hex(s.Misc_VirtualSize)) for s in image.sections])
    for imports in getattr(image, "DIRECTORY_ENTRY_IMPORT", []) + getattr(image, "DIRECTORY_ENTRY_DELAY_IMPORT", []):
        print("IMPORT", imports.dll.decode(), [i.name.decode() if i.name else i.ordinal for i in imports.imports])
    exports = getattr(image, "DIRECTORY_ENTRY_EXPORT", None)
    if exports:
        print("EXPORTS", [(s.name.decode() if s.name else s.ordinal, hex(s.address)) for s in exports.symbols])
    for pattern in [rb"[^\x00\r\n]{0,70}(?:[Rr]esolution|[Ff]ullscreen|[Bb]orderless|\.pdb|DisneyInfinity|SetViewport|[Ww]indowed)[^\x00\r\n]{0,80}"]:
        for match in list(re.finditer(pattern, raw))[:100]:
            try:
                rva = image.get_rva_from_offset(match.start())
            except pefile.PEFormatError:
                continue
            print("STRING", hex(rva), repr(match.group().decode("utf-8", "replace")))
    return image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("file")
    parser.add_argument("--rpx", action="store_true")
    parser.add_argument("--out")
    parser.add_argument("--disasm", type=lambda v: int(v, 0))
    parser.add_argument("--length", type=lambda v: int(v, 0), default=512)
    args = parser.parse_args()
    if args.rpx:
        sections, symbols = rpx(args.file)
        print("SECTIONS", [{k: v for k, v in s.items() if k not in ("data", "name_offset")} for s in sections])
        print("SYMBOL COUNT", len(symbols))
        relevant = [s for s in symbols if re.search(r"resolution|viewport|fullscreen|window|displaymode", s["name"], re.I)]
        print(json.dumps(relevant, indent=2))
        if args.out:
            Path(args.out).write_text(json.dumps(symbols, indent=2), encoding="utf-8")
    elif args.disasm is not None:
        image = pefile.PE(args.file)
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        for ins in md.disasm(image.get_data(args.disasm, args.length), image.OPTIONAL_HEADER.ImageBase + args.disasm):
            print(f"{ins.address:08X}: {ins.bytes.hex(' '):<27} {ins.mnemonic} {ins.op_str}")
    else:
        pe(args.file)


if __name__ == "__main__":
    main()
