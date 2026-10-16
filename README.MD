# NLPP Save Studio (NEWラブプラス+ Save Studio)
Dedicated Save Editor for *NEW LovePlus+* on Nintendo 3DS (`00040000000F4E00`).

## Overview
**NLPP Save Studio (NLPPSS)** is a dedicated Nintendo 3DS homebrew save editor tailored specifically for *NEW LovePlus+* (NEWラブプラス+ / `00040000000F4E00`).

Based on the mature native 3DS homebrew stack and grounded in the reverse engineering master specification (`SAVE_REVERSE_ENGINEERING_MASTER.md`), it provides real-time dual-screen monitoring, touch controls, and direct live save archive editing for both physical GameCards (`CTR-P-AKNJ`) and digital eShop SD installations (`CTR-N-AKNJ`).

---

## Features

1. **Dual Native Live Save Access**
   - Direct access to active cartridge and SD digital save archives.
   - Zero dump prerequisites: seamlessly reads and writes native 3DS CTR filesystem archives (`ARCHIVE_SAVEDATA` and `ARCHIVE_USER_SAVEDATA`).

2. **Zero Corruption Policy & Anti-Rollback Protection**
   - **Pre-commit Automated Backups**: creates full timestamped snapshots in `sdmc:/NLPP_BACKUP/YYYYMMDD_HHMMSS/` before every commit.
   - **Secure Value Reset**: automatically executes `FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE)` on SD digital versions to prevent system anti-save-restore error `00040000`.

3. **Atomic Appellation Triad Synchronizer**
   - Atomically synchronizes girlfriend appellation across:
     1. Primary live record in `savedata9` (`0x281`);
     2. Standby & map models in `savedata3` (`0x6ed + (heroine * 6 + mode) * 770`);
     3. Historical event audit ring buffer in `savedata21` (latest sequence frame).
   - Built-in compact 5,628 reading dictionary (`romfs:/dict/readings.bin`).

4. **Boyfriend Stats & Assets**
   - Four fundamental stats (Fitness, Knowledge, Sensibility, Charm, 0..255) with stepper and one-click max (255).
   - Rich currency (0..999,999 G) and TODO points (0..9,999 points).
   - **Permanent Camera Filter Removal**: fixes the infamous game defect where camera filters cannot be unequipped; resets filter ID to `-1` and clears `EquipMask` bit 2 to restore pure stock camera visuals.

5. **Girlfriend Appearance & 20-Color Official Dyeing System**
   - Affection level (0..255) with one-click restoration to 255 (resolves cold war / sulking states).
   - Hair length stages (Short, Middle, Long) mapped to heroine-specific ladders.
   - 17 official hairstyles with affinity protection.
   - **Full 20-Color Hair Palette**: Color ID (0..19) + RGB triad + active state register (`0x54C = 0x00`), including official challenge shades (Pink, Blue, Purple, Orange, Green, Cyan) and one-click default reset.
   - Personality 2D coordinates (X, Y, Z).

6. **Timeline & 23-Tier Couple Titles**
   - Timeline Days (`%d日` on file select card) and active Dating Days (triggers 100-day / anniversary dialogues).
   - Reset Max Absence to 0 to prevent absence penalties.
   - Jump directly to Lv. 23 "最愛のふたり" (Most Beloved Couple) and unlock all 23 Gold Medals.

7. **Safe 513 Wardrobe & 1,317 Master Gallery Unlock**
   - **Strict 513-Item Boundary Guard**: sets counter to 513, enables bitmasks 0..512, strictly zeros out items 513..559 to prevent out-of-bounds pointer crashes, and clears NEW indicators.
   - **1,317 Master Gallery Elements**: lights up Dreams (60), After-Dreams (60), Memories (243), Trip CGs (123), Contests (255), and Specials (576) while strictly preserving authentic trip replay logs (`0x0412~0x3A59`).

---

## Controls

| Buttons | Action |
| :--- | :--- |
| **`[L]` / `[R]`** | Cycle between 7 editing tabs |
| **`D-Pad [Up]` / `[Down]`** | Navigate items in current tab |
| **`D-Pad [Left]` / `[Right]`** | Adjust selected parameter value |
| **`[A]`** | Confirm / Step / Launch Software Keyboard (`swkbd`) |
| **`[X]`** | One-Click MAX / Extreme values for active tab |
| **`[Y]`** | Quick manual snapshot backup to `sdmc:/NLPP_BACKUP/` |
| **`[START]`** | Commit changes to 3DS save archive |
| **Touch Screen** | Direct tap for tabs, item rows, and action triggers |

---

## Building

Requires devkitARM and libctru:

```bash
make -j$(nproc)
```

Outputs:
- `NLPPSS.3dsx`
- `NLPPSS.smdh`
