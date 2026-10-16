import os
import csv
import struct

# Paths
CSV_PATH = '/mnt/d/Files/NLPP_CHS/Save/Research/resources/name_reading_id_map.csv'
if not os.path.exists(CSV_PATH):
    CSV_PATH = 'D:/Files/NLPP_CHS/Save/Research/resources/name_reading_id_map.csv'

OUTPUT_BIN = 'romfs/dict/readings.bin'
os.makedirs('romfs/dict', exist_ok=True)

# 24 Preset titles in Slot 3:
PRESET_IDS = {
    958, 959, 960, 961, 962, 963, 964, 965, 966, 967,
    2939, 2940, 2941, 2942, 2943, 2944, 2945, 2946, 2947, 2948, 2949, 2950, 2951, 2952
}

def determine_slots(rid, final_reading, suffix):
    slot_mask = 0
    heroine_mask = 0x07 # Default all heroines (Manaka=1, Rinko=2, Nene=4)

    # Slot 4: Heroine unique nicknames
    if 30000 <= rid <= 30135:
        slot_mask = 0x10 # Slot 4
        heroine_mask = 0x01 # Manaka only
        return slot_mask, heroine_mask
    elif 40000 <= rid <= 40149:
        slot_mask = 0x10 # Slot 4
        heroine_mask = 0x02 # Rinko only
        return slot_mask, heroine_mask
    elif 50000 <= rid <= 50153:
        slot_mask = 0x10 # Slot 4
        heroine_mask = 0x04 # Nene only
        return slot_mask, heroine_mask

    # Slot 3: Preset romantic titles
    if rid in PRESET_IDS:
        slot_mask = 0x08 # Slot 3
        return slot_mask, heroine_mask

    # Slot 2: Suffix combinations (くん/ちゃん)
    # Range 3181 to 5187
    if suffix or (3181 <= rid <= 5187):
        slot_mask = 0x04 # Slot 2
        return slot_mask, heroine_mask

    # Slot 0 / 1: Base readings
    # Surnames: 0..350
    if 0 <= rid <= 350:
        slot_mask |= 0x01 # Slot 0 (LastName)
    # First names: 351..577
    if 351 <= rid <= 577:
        slot_mask |= 0x02 # Slot 1 (FirstName)
    # Elongation / short syllables: 578..957
    if 578 <= rid <= 957:
        slot_mask |= 0x02 # Slot 1 (FirstName)
    # Extended base names: 968..2938, 2955..3180, etc.
    if 968 <= rid <= 2938 or 2955 <= rid <= 3180:
        slot_mask |= 0x03 # Can be used as Slot 0 or Slot 1

    if slot_mask == 0:
        # Fallback to Slot 0 | Slot 1
        slot_mask = 0x03

    return slot_mask, heroine_mask

entries = []
with open(CSV_PATH, 'r', encoding='utf-8-sig') as f:
    reader = csv.DictReader(f)
    for row in reader:
        rid = int(row['id'])
        final_reading = row['final_reading']
        suffix = row['suffix']
        slot_mask, heroine_mask = determine_slots(rid, final_reading, suffix)
        entries.append({
            'id': rid,
            'slot_mask': slot_mask,
            'heroine_mask': heroine_mask,
            'text': final_reading
        })

entries.sort(key=lambda x: x['id'])

print(f"Total entries: {len(entries)}")

# Build binary file
# Format:
# Header:
#   magic: 'NLRD' (4 bytes)
#   count: uint32 (5628)
#   index_offset: uint32
#   strings_offset: uint32
# Index table: count * 8 bytes
#   id: uint16
#   slot_mask: uint8
#   heroine_mask: uint8
#   str_offset: uint32 (relative to strings_offset)
# Strings table:
#   UTF-8 null-terminated strings

header_size = 16
index_size = len(entries) * 8
strings_offset = header_size + index_size

string_pool = bytearray()
str_offsets = []

for e in entries:
    str_bytes = e['text'].encode('utf-8') + b'\x00'
    offset = len(string_pool)
    str_offsets.append(offset)
    string_pool.extend(str_bytes)

with open(OUTPUT_BIN, 'wb') as f:
    # Header
    f.write(struct.pack('<4sIII', b'NLRD', len(entries), header_size, strings_offset))
    # Index
    for i, e in enumerate(entries):
        f.write(struct.pack('<HBB I', e['id'], e['slot_mask'], e['heroine_mask'], str_offsets[i]))
    # Strings
    f.write(string_pool)

file_size = os.path.getsize(OUTPUT_BIN)
print(f"Generated {OUTPUT_BIN} successfully! File size: {file_size} bytes ({file_size / 1024:.2f} KB)")
