import os
import csv
import struct

CSV_PATH = 'D:/Files/NLPP_CHS/Save/Research/resources/name_reading_id_map.csv'
if not os.path.exists(CSV_PATH):
    CSV_PATH = '/mnt/d/Files/NLPP_CHS/Save/Research/resources/name_reading_id_map.csv'

OUTPUT_BIN = 'D:/Files/GitHub Desktop/Cetaceaqua/NLPPSS/romfs/dict/readings.bin'
if not os.path.exists(os.path.dirname(OUTPUT_BIN)):
    OUTPUT_BIN = '/mnt/d/Files/GitHub Desktop/Cetaceaqua/NLPPSS/romfs/dict/readings.bin'

os.makedirs(os.path.dirname(OUTPUT_BIN), exist_ok=True)

# Fixed 24 Preset titles in Slot 3 (from IDA sub_2493F0 & Table 0x7200)
PRESET_IDS = {
    958, 959, 962, 963, 964, 965, 966, 967,
    2939, 2940, 2941, 2942, 2943, 2944, 2945, 2946, 2947, 2948, 2949, 2950, 2951, 2952, 2953, 2954
}

# 1. First pass: find all base_reading texts that have suffix versions (Slot 2)
with open(CSV_PATH, 'r', encoding='utf-8-sig') as f:
    reader = csv.DictReader(f)
    suffix_base_texts = set(r['base_reading'] for r in reader if r['suffix'])

print(f"Unique base texts with suffix forms: {len(suffix_base_texts)}")

# Kanji manual overrides for the 15 non-kana entries
KANJI_MAP = {
    'おとうと君': 'otoutokun',
    'ご主人様': 'goshujinsama',
    '先輩': 'senpai',
    'だんな様': 'dannasama',
    'お兄様': 'oniisama',
    '閣下': 'kakka',
    '先生': 'sensei',
    '上様': 'uesama',
    '王子様': 'oujisama',
    '店长': 'tenchou',
    '店長': 'tenchou',
    '博士': 'hakase',
    'お姉様': 'oneesama',
    'お嬢様': 'ojousama',
    'これ やみねこ': 'kore yamineko',
    'あ な たっ': "a na ta'",
}

# 2-character combinations (youon & foreign katakana)
TWO_CHAR_MAP = {
    'きゃ': 'kya', 'きゅ': 'kyu', 'きょ': 'kyo',
    'しゃ': 'sha', 'しゅ': 'shu', 'しょ': 'sho',
    'ちゃ': 'cha', 'ちゅ': 'chu', 'ちょ': 'cho',
    'にゃ': 'nya', 'にゅ': 'nyu', 'にょ': 'nyo',
    'ひゃ': 'hya', 'ひゅ': 'hyu', 'ひょ': 'hyo',
    'みゃ': 'mya', 'みゅ': 'myu', 'みょ': 'myo',
    'りゃ': 'rya', 'りゅ': 'ryu', 'りょ': 'ryo',
    'ぎゃ': 'gya', 'ぎゅ': 'gyu', 'ぎょ': 'gyo',
    'じゃ': 'ja',  'じゅ': 'ju',  'じょ': 'jo',
    'ぢゃ': 'ja',  'ぢゅ': 'ju',  'ぢょ': 'jo',
    'びゃ': 'bya', 'びゅ': 'byu', 'びょ': 'byo',
    'ぴゃ': 'pya', 'ぴゅ': 'pyu', 'ぴょ': 'pyo',
    'しぇ': 'she', 'ちぇ': 'che', 'じぇ': 'je',
    'ふぁ': 'fa',  'ふぃ': 'fi',  'ふぇ': 'fe', 'ふぉ': 'fo',
    'てぃ': 'ti',  'でぃ': 'di',  'デュ': 'dyu',
    'うぃ': 'wi',  'うぇ': 'we',  'うぉ': 'wo',

    'キャ': 'kya', 'キュ': 'kyu', 'キョ': 'kyo',
    'シャ': 'sha', 'シュ': 'shu', 'ショ': 'sho',
    'チャ': 'cha', 'チュ': 'chu', 'チョ': 'cho',
    'ニャ': 'nya', 'ニュ': 'nyu', 'ニョ': 'nyo',
    'ヒャ': 'hya', 'ヒュ': 'hyu', 'ヒョ': 'hyo',
    'ミャ': 'mya', 'ミュ': 'myu', 'ミョ': 'myo',
    'リャ': 'rya', 'リュ': 'ryu', 'リョ': 'ryo',
    'ギャ': 'gya', 'ギュ': 'gyu', 'ギョ': 'gyo',
    'ジャ': 'ja',  'ジュ': 'ju',  'ジョ': 'jo',
    'ヂャ': 'ja',  'ヂュ': 'ju',  'ヂョ': 'jo',
    'ビャ': 'bya', 'ビュ': 'byu', 'ビョ': 'byo',
    'ピャ': 'pya', 'ピュ': 'pyu', 'ピョ': 'pyo',
    'シェ': 'she', 'チェ': 'che', 'ジェ': 'je',
    'ファ': 'fa',  'フィ': 'fi',  'フェ': 'fe', 'フォ': 'fo',
    'ティ': 'ti',  'ディ': 'di',  'デュ': 'dyu',
    'トゥ': 'tu',  'ドゥ': 'du',
    'ウィ': 'wi',  'ウェ': 'we',  'ウォ': 'wo',
    'ヴァ': 'va',  'ヴィ': 'vi',  'ヴ': 'vu', 'ヴェ': 've', 'ヴォ': 'vo',
    'ツァ': 'tsa', 'ツィ': 'tsi', 'ツェ': 'tse', 'ツォ': 'tso',
}

# Single characters
ONE_CHAR_MAP = {
    'あ': 'a',  'い': 'i',  'う': 'u',  'え': 'e',  'お': 'o',
    'か': 'ka', 'き': 'ki', 'く': 'ku', 'け': 'ke', 'こ': 'ko',
    'さ': 'sa', 'し': 'shi','す': 'su', 'せ': 'se', 'そ': 'so',
    'た': 'ta', 'ち': 'chi','つ': 'tsu','て': 'te', 'と': 'to',
    'な': 'na', 'に': 'ni', 'ぬ': 'nu', 'ね': 'ne', 'の': 'no',
    'は': 'ha', 'ひ': 'hi', 'ふ': 'fu', 'へ': 'he', 'ほ': 'ho',
    'ま': 'ma', 'み': 'mi', 'む': 'mu', 'め': 'me', 'も': 'mo',
    'や': 'ya',             'ゆ': 'yu',             'よ': 'yo',
    'ら': 'ra', 'り': 'ri', 'る': 'ru', 'れ': 're', 'ろ': 'ro',
    'わ': 'wa',                                     'を': 'wo',
    'ん': 'n',

    'が': 'ga', 'ぎ': 'gi', 'ぐ': 'gu', 'げ': 'ge', 'ご': 'go',
    'ざ': 'za', 'じ': 'ji', 'ず': 'zu', 'ぜ': 'ze', 'ぞ': 'zo',
    'だ': 'da', 'ぢ': 'ji', 'づ': 'zu', 'で': 'de', 'ど': 'do',
    'ば': 'ba', 'び': 'bi', 'ぶ': 'bu', 'べ': 'be', 'ぼ': 'bo',
    'ぱ': 'pa', 'ぴ': 'pi', 'ぷ': 'pu', 'ぺ': 'pe', 'ぽ': 'po',

    'ぁ': 'a',  'ぃ': 'i',  'ぅ': 'u',  'ぇ': 'e',  'ぉ': 'o',
    'ゃ': 'ya', 'ゅ': 'yu', 'ょ': 'yo', 'ゎ': 'wa',

    'ア': 'a',  'イ': 'i',  'ウ': 'u',  'エ': 'e',  'オ': 'o',
    'カ': 'ka', 'キ': 'ki', 'ク': 'ku', 'ケ': 'ke', 'コ': 'ko',
    'サ': 'sa', 'シ': 'shi','ス': 'su', 'セ': 'se', 'ソ': 'so',
    'タ': 'ta', 'チ': 'chi','ツ': 'tsu','テ': 'te', 'ト': 'to',
    'ナ': 'na', 'ニ': 'ni', 'ヌ': 'nu', 'ネ': 'ne', 'ノ': 'no',
    'ハ': 'ha', 'ヒ': 'hi', 'フ': 'fu', 'へ': 'he', 'ホ': 'ho',
    'マ': 'ma', 'ミ': 'mi', 'ム': 'mu', 'メ': 'me', 'モ': 'mo',
    'ヤ': 'ya',             'ユ': 'yu',             'ヨ': 'yo',
    'ラ': 'ra', 'リ': 'ri', 'ル': 'ru', 'レ': 're', 'ロ': 'ro',
    'ワ': 'wa',                                     'ヲ': 'wo',
    'ン': 'n',

    'ガ': 'ga', 'ギ': 'gi', 'グ': 'gu', 'ゲ': 'ge', 'ゴ': 'go',
    'ザ': 'za', 'ジ': 'ji', 'ズ': 'zu', 'ゼ': 'ze', 'ゾ': 'zo',
    'ダ': 'da', 'ヂ': 'ji', 'ヅ': 'zu', 'デ': 'de', 'ド': 'do',
    'バ': 'ba', 'ビ': 'bi', 'ブ': 'bu', 'ベ': 'be', 'ボ': 'bo',
    'パ': 'pa', 'ピ': 'pi', 'プ': 'pu', 'ペ': 'pe', 'ポ': 'po',
    'ヴ': 'vu',

    'ァ': 'a',  'ィ': 'i',  'ゥ': 'u',  'ェ': 'e',  'ォ': 'o',
    'ャ': 'ya', 'ュ': 'yu', 'ョ': 'yo', 'ヮ': 'wa',

    'ー': '-',
    ' ': ' ',
}

def kana_to_romaji(text):
    if text in KANJI_MAP:
        return KANJI_MAP[text]
    
    result = []
    i = 0
    n = len(text)
    while i < n:
        if text[i] in ('っ', 'ッ'):
            if i + 1 < n:
                next_part = ""
                if i + 2 <= n and text[i+1:i+2] in TWO_CHAR_MAP:
                    next_part = TWO_CHAR_MAP[text[i+1:i+2]]
                elif text[i+1] in ONE_CHAR_MAP:
                    next_part = ONE_CHAR_MAP[text[i+1]]
                
                if next_part:
                    c = next_part[0]
                    if c in 'aeioun- ':
                        result.append("'")
                    elif next_part.startswith('ch'):
                        result.append('t')
                    else:
                        result.append(c)
                else:
                    result.append("'")
            else:
                result.append("'")
            i += 1
            continue

        if i + 1 < n and text[i:i+2] in TWO_CHAR_MAP:
            result.append(TWO_CHAR_MAP[text[i:i+2]])
            i += 2
            continue

        if text[i] in ONE_CHAR_MAP:
            result.append(ONE_CHAR_MAP[text[i]])
            i += 1
            continue

        result.append(text[i])
        i += 1

    return "".join(result)

def determine_slots(rid, base_reading, final_reading, suffix):
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

    # Slot 3: Preset romantic titles (exactly 24 entries)
    if rid in PRESET_IDS:
        slot_mask = 0x08 # Slot 3
        return slot_mask, heroine_mask

    # Slot 2: Suffix combinations (くん/ちゃん)
    if suffix or (3181 <= rid <= 4717) or (4970 <= rid <= 5063) or (5126 <= rid <= 5187):
        slot_mask = 0x04 # Slot 2
        return slot_mask, heroine_mask

    # Slot 0 / 1: 3,495 Base readings
    is_base = (
        (0 <= rid <= 957) or
        (968 <= rid <= 1365) or
        (1438 <= rid <= 2938) or
        (2955 <= rid <= 3180) or
        (4718 <= rid <= 4875) or
        (4876 <= rid <= 4969) or
        (5064 <= rid <= 5125)
    )
    if is_base:
        slot_mask = 0x03 # Slot 0 (LastName) & Slot 1 (FirstName)
        # Bit 7: Suffix capability badge (1,787 items)
        if base_reading in suffix_base_texts or (5064 <= rid <= 5125) or (4876 <= rid <= 4969):
            slot_mask |= 0x80

    if slot_mask == 0:
        slot_mask = 0x03

    return slot_mask, heroine_mask

entries = []
with open(CSV_PATH, 'r', encoding='utf-8-sig') as f:
    reader = csv.DictReader(f)
    for row in reader:
        rid = int(row['id'])
        base_reading = row['base_reading']
        final_reading = row['final_reading']
        suffix = row['suffix']
        slot_mask, heroine_mask = determine_slots(rid, base_reading, final_reading, suffix)
        romaji = kana_to_romaji(final_reading)
        entries.append({
            'id': rid,
            'slot_mask': slot_mask,
            'heroine_mask': heroine_mask,
            'text': final_reading,
            'romaji': romaji
        })

entries.sort(key=lambda x: x['id'])

print(f"Total entries: {len(entries)}")
preset_count = sum(1 for e in entries if e['slot_mask'] & 0x08)
suffix_support_count = sum(1 for e in entries if (e['slot_mask'] & 0x03) and (e['slot_mask'] & 0x80))
base_count = sum(1 for e in entries if (e['slot_mask'] & 0x03))
print(f"Preset titles (Slot 3): {preset_count} (expected 24)")
print(f"Base titles (Slot 0/1): {base_count} (expected 3495)")
print(f"Base titles supporting suffix: {suffix_support_count}")

# Binary format:
# Header: 'NLRD' (4), count (4), header_size (4), strings_offset (4)
# Index: count * 8 bytes (id: uint16, slot_mask: uint8, heroine_mask: uint8, str_offset: uint32)
# Strings pool: text\0romaji\0 for each entry

header_size = 16
index_size = len(entries) * 8
strings_offset = header_size + index_size

string_pool = bytearray()
str_offsets = []

for e in entries:
    str_bytes = e['text'].encode('utf-8') + b'\x00' + e['romaji'].encode('utf-8') + b'\x00'
    offset = len(string_pool)
    str_offsets.append(offset)
    string_pool.extend(str_bytes)

with open(OUTPUT_BIN, 'wb') as f:
    f.write(struct.pack('<4sIII', b'NLRD', len(entries), header_size, strings_offset))
    for i, e in enumerate(entries):
        f.write(struct.pack('<HBB I', e['id'], e['slot_mask'], e['heroine_mask'], str_offsets[i]))
    f.write(string_pool)

file_size = os.path.getsize(OUTPUT_BIN)
print(f"Generated {OUTPUT_BIN} successfully! File size: {file_size} bytes ({file_size / 1024:.2f} KB)")
