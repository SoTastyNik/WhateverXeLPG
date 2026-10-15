#!/usr/bin/env python3
"""Inspect a saved VBT without changing it or qualifying it for driver use."""
import argparse
import json
from pathlib import Path
import struct

# Independently implemented field decoding, scoped to BDB 253.
# Layout reference: Linux v6.12 drivers/gpu/drm/i915/display/intel_vbt_defs.h
# Panel selection/units: intel_bios.c and intel_pps.c in the same directory.
# https://github.com/torvalds/linux/tree/v6.12/drivers/gpu/drm/i915/display
# A VBT panel index is a firmware suggestion; OpRegion can override it.


def inspect(data):
    def u16(offset):
        return struct.unpack_from('<H', data, offset)[0]

    if len(data) < 48 or data[:4] != b'$VBT':
        raise ValueError('Missing or incomplete VBT header')
    header, size = u16(22), u16(24)
    bdb = struct.unpack_from('<I', data, 28)[0]
    if not 48 <= header <= size <= len(data):
        raise ValueError('Invalid VBT bounds')
    if bdb < header or bdb + 22 > size:
        raise ValueError('Invalid BDB offset')
    if data[bdb:bdb + 16] != b'BIOS_DATA_BLOCK ':
        raise ValueError('Invalid BDB signature')
    version, bheader, bsize = (u16(bdb + i) for i in (16, 18, 20))
    if not 22 <= bheader <= bsize or bdb + bsize > size:
        raise ValueError('Invalid BDB bounds')
    warnings = []
    result = dict(vbt_version=u16(20), declared_bytes=size,
                  trailing_bytes=len(data) - size, bdb_version=version,
                  checksum_remainder=sum(data[:size]) % 256,
                  warnings=warnings, blocks=[])
    if result['checksum_remainder']:
        warnings.append('Nonzero VBT checksum; decoded fields are observations only')
    sections = {}
    duplicates = set()
    pos, end = bdb + bheader, bdb + bsize
    while pos < end:
        if end - pos < 3:
            warnings.append('Incomplete block header at BDB end')
            break
        tag, count = data[pos], u16(pos + 1)
        payload = pos + 3
        # Do not guess the special MIPI v3 length convention.
        if tag == 53:
            warnings.append('MIPI sequence block is outside this inspector scope; scan stopped')
            break
        if count > end - payload:
            warnings.append(f'Block {tag} exceeds declared BDB boundary; scan stopped')
            break
        result['blocks'].append(dict(id=tag, offset=payload, bytes=count))
        if tag in sections:
            duplicates.add(tag)
        else:
            sections[tag] = data[payload:payload + count]
        pos = payload + count
    for tag in sorted(duplicates):
        del sections[tag]
        warnings.append(f'Duplicate block {tag}; decoding suppressed')
    if version != 253:
        warnings.append('Field decoding supports only BDB 253')
        return result
    children = sections.get(2)
    if children is not None:
        if len(children) < 5 or children[4] != 39 or (len(children) - 5) % 39:
            warnings.append('Unsupported General Definitions child layout')
        else:
            result['active_children'] = []
            for index, offset in enumerate(range(5, len(children), 39)):
                child = children[offset:offset + 39]
                kind = int.from_bytes(child[2:4], 'little')
                if kind:
                    result['active_children'].append(dict(
                        index=index, device_type=kind, internal=bool(kind & 0x1000),
                        displayport=bool(kind & 4), dvo_port=child[16],
                        aux_channel=child[25], flags=child[23]))
    options = sections.get(40)
    if options:
        index = options[0]
        result['vbt_panel_index'] = index
        if index >= 16:
            warnings.append('Panel index needs external selection; no power sequence decoded')
        elif 27 in sections:
            edp = sections[27]
            if len(edp) < 160:
                warnings.append('Incomplete eDP power sequence array')
            else:
                fields = ('t1_t3', 't8', 't9', 't10', 't11_t12')
                values = struct.unpack_from('<5H', edp, index * 10)
                # Raw 100-us units, not the effective Linux/firmware delays.
                # In particular, do not infer the final power-cycle delay here.
                result['vbt_power_sequence_100us'] = dict(zip(fields, values))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('vbt', type=Path)
    args = parser.parse_args()
    try:
        result = inspect(args.vbt.read_bytes())
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
