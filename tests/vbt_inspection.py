#!/usr/bin/env python3
"""Offline VBT bounds and field decoding using synthetic firmware only."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_vbt import inspect


def fixture(extra=b'', version=253):
    def block(tag, body):
        return bytes([tag]) + struct.pack('<H', len(body)) + body
    child = bytearray(39)
    struct.pack_into('<H', child, 2, 0x1004)
    child[16], child[25] = 10, 0x40
    power = bytearray(160)
    struct.pack_into('<5H', power, 30, 110, 220, 330, 440, 550)
    body = (block(2, bytes([0, 0, 0, 0, 39]) + child) +
            block(27, power) + block(40, bytes([3])) + extra)
    data = bytearray(70) + body
    data[:4] = b'$VBT'
    struct.pack_into('<HHH', data, 20, 100, 48, len(data))
    struct.pack_into('<I', data, 28, 48)
    data[48:64] = b'BIOS_DATA_BLOCK '
    struct.pack_into('<HHH', data, 64, version, 22, len(data) - 48)
    data[26] = (-sum(data)) % 256
    return data


class InspectionTests(unittest.TestCase):
    def test_valid_fields_and_unrelated_padding(self):
        result = inspect(fixture() + b'padding')
        self.assertEqual(result['checksum_remainder'], 0)
        self.assertEqual(result['trailing_bytes'], 7)
        self.assertEqual(result['warnings'], [])
        self.assertEqual(result['active_children'][0]['aux_channel'], 0x40)
        self.assertEqual(result['vbt_panel_index'], 3)
        self.assertEqual(list(result['vbt_power_sequence_100us'].values()),
                         [110, 220, 330, 440, 550])

    def test_all_truncations_rejected(self):
        data = fixture()
        for size in range(len(data)):
            with self.assertRaises(ValueError):
                inspect(data[:size])

    def test_checksum_is_reported_not_repaired(self):
        data = fixture()
        data[26] ^= 1
        before = bytes(data)
        self.assertNotEqual(inspect(data)['checksum_remainder'], 0)
        self.assertEqual(bytes(data), before)

    def test_boundary_overrun_is_not_decoded(self):
        data = fixture(bytes([58, 2, 0, 1]))
        result = inspect(data)
        self.assertIn('Block 58 exceeds', result['warnings'][0])
        self.assertNotIn(58, [b['id'] for b in result['blocks']])

    def test_duplicate_panel_selection_suppressed(self):
        result = inspect(fixture(bytes([40, 1, 0, 4])))
        self.assertNotIn('vbt_panel_index', result)
        self.assertNotIn('vbt_power_sequence_100us', result)

    def test_unknown_version_has_no_decoded_fields(self):
        result = inspect(fixture(version=254))
        self.assertNotIn('active_children', result)
        self.assertNotIn('vbt_panel_index', result)

    def test_external_panel_selection_not_guessed(self):
        data = fixture()
        data[-1] = 255
        result = inspect(data)
        self.assertEqual(result['vbt_panel_index'], 255)
        self.assertNotIn('vbt_power_sequence_100us', result)

    def test_invalid_bdb_bounds(self):
        for offset, value in ((66, 21), (68, 65535)):
            data = fixture()
            struct.pack_into('<H', data, offset, value)
            with self.assertRaises(ValueError):
                inspect(data)

    def test_mipi_stops_scan(self):
        result = inspect(fixture(bytes([53, 1, 0, 3])))
        self.assertIn('MIPI', result['warnings'][0])

    def test_bad_child_stride(self):
        data = fixture()
        data[77] = 0
        result = inspect(data)
        self.assertNotIn('active_children', result)
        self.assertTrue(any('child layout' in w for w in result['warnings']))

    def test_short_edp_array(self):
        data = fixture()
        edp = next(b for b in inspect(data)['blocks'] if b['id'] == 27)
        # Preserve subsequent block alignment while replacing the array with
        # a short block and a separate unknown block occupying its remainder.
        struct.pack_into('<H', data, edp['offset'] - 2, 10)
        data[edp['offset'] + 10] = 250
        struct.pack_into('<H', data, edp['offset'] + 11, 147)
        result = inspect(data)
        self.assertNotIn('vbt_power_sequence_100us', result)
        self.assertTrue(any('Incomplete eDP' in w for w in result['warnings']))


if __name__ == '__main__':
    unittest.main()
