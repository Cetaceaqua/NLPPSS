#!/usr/bin/env python3
"""Generate binary message catalog from textual translation description.

This is a standalone reimplementation of GNU msgfmt in pure Python.
"""
import sys
import os
import struct
import argparse

def generate_mo(po_path, mo_path):
    MESSAGES = {}
    with open(po_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    msgid = []
    msgstr = []
    in_msgid = False
    in_msgstr = False

    def unescape(s):
        s = s.strip()
        if s.startswith('"') and s.endswith('"'):
            s = s[1:-1]
        res = []
        i = 0
        while i < len(s):
            if s[i] == '\\' and i + 1 < len(s):
                c = s[i+1]
                if c == 'n': res.append('\n'); i += 2
                elif c == 't': res.append('\t'); i += 2
                elif c == 'r': res.append('\r'); i += 2
                elif c == '"': res.append('"'); i += 2
                elif c == '\\': res.append('\\'); i += 2
                elif c in ('u', 'U') and i + 5 < len(s):
                    hex_len = 8 if (c == 'U' and i + 9 < len(s)) else 4
                    try:
                        res.append(chr(int(s[i+2:i+2+hex_len], 16)))
                        i += 2 + hex_len
                    except ValueError:
                        res.append(c)
                        i += 2
                else:
                    res.append(c)
                    i += 2
            else:
                res.append(s[i])
                i += 1
        return ''.join(res)

    def commit():
        nonlocal msgid, msgstr
        if msgid:
            orig = ''.join(msgid)
            trans = ''.join(msgstr)
            if orig or trans:
                MESSAGES[orig] = trans
        msgid = []
        msgstr = []

    for line in lines:
        line_s = line.strip()
        if not line_s or line_s.startswith('#'):
            continue
        if line_s.startswith('msgid '):
            commit()
            in_msgid = True
            in_msgstr = False
            content = line_s[6:].strip()
            msgid.append(unescape(content))
        elif line_s.startswith('msgstr '):
            in_msgid = False
            in_msgstr = True
            content = line_s[7:].strip()
            msgstr.append(unescape(content))
        elif line_s.startswith('"'):
            if in_msgid:
                msgid.append(unescape(line_s))
            elif in_msgstr:
                msgstr.append(unescape(line_s))
    commit()

    # Sort keys
    keys = sorted(MESSAGES.keys())
    num_strings = len(keys)

    # Offsets
    # Header: 7 unsigned ints = 28 bytes
    # orig_table: num_strings * 8 bytes
    # trans_table: num_strings * 8 bytes
    # data starts at: 28 + 2 * num_strings * 8
    keystart = 28 + 2 * num_strings * 8

    key_offsets = []
    val_offsets = []
    key_bytes = b''
    val_bytes = b''

    for k in keys:
        b_k = k.encode('utf-8') + b'\x00'
        key_offsets.append((len(b_k) - 1, keystart + len(key_bytes)))
        key_bytes += b_k

    valstart = keystart + len(key_bytes)
    for k in keys:
        v = MESSAGES[k]
        b_v = v.encode('utf-8') + b'\x00'
        val_offsets.append((len(b_v) - 1, valstart + len(val_bytes)))
        val_bytes += b_v

    os.makedirs(os.path.dirname(os.path.abspath(mo_path)), exist_ok=True)
    with open(mo_path, 'wb') as out:
        # Magic: 0x950412de
        out.write(struct.pack('<I', 0x950412de))
        # Version: 0
        out.write(struct.pack('<I', 0))
        # Number of strings
        out.write(struct.pack('<I', num_strings))
        # Offset of orig table
        out.write(struct.pack('<I', 28))
        # Offset of trans table
        out.write(struct.pack('<I', 28 + num_strings * 8))
        # Hash table size: 0
        out.write(struct.pack('<I', 0))
        # Offset of hash table: 0
        out.write(struct.pack('<I', 0))

        # Orig table
        for length, offset in key_offsets:
            out.write(struct.pack('<II', length, offset))

        # Trans table
        for length, offset in val_offsets:
            out.write(struct.pack('<II', length, offset))

        # Data
        out.write(key_bytes)
        out.write(val_bytes)

def main():
    parser = argparse.ArgumentParser(description="Compile .po file to .mo format")
    parser.add_argument('-o', '--output-file', required=True, help="Output .mo file")
    parser.add_argument('input_file', help="Input .po file")
    parser.add_argument('-c', '--check', action='store_true', help="Check format (compatibility)")
    args = parser.parse_args()

    generate_mo(args.input_file, args.output_file)

if __name__ == '__main__':
    main()
