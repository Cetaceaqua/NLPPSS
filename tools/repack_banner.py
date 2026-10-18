import os
import sys
import shutil
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
RES_DIR = os.path.join(REPO_ROOT, "res")

# Support both Windows and WSL
if sys.platform == "win32":
    TOOLS_DIR = r"D:\Files\NLPP_CHS\3DS Toolkit\tools"
else:
    TOOLS_DIR = "/mnt/d/Files/NLPP_CHS/3DS Toolkit/tools"

TXOBTOOL = os.path.join(TOOLS_DIR, "txobtool.exe")
THREEDSTOOL = os.path.join(TOOLS_DIR, "3dstool.exe")

BANNER_PNG = os.path.join(RES_DIR, "banner.png")
BANNER_BNR = os.path.join(RES_DIR, "NLPPSS.bnr")
ICON_PNG = os.path.join(RES_DIR, "icon.png")
SMDH_FILE = os.path.join(REPO_ROOT, "NLPPSS.smdh")

def repack_banner(force=False):
    if not os.path.exists(BANNER_PNG):
        print("res/banner.png not found, skipping banner repack.")
        return

    if not force and os.path.exists(BANNER_BNR):
        if os.path.getmtime(BANNER_BNR) >= os.path.getmtime(BANNER_PNG):
            print("NLPPSS.bnr is up to date.")
            return

    if not os.path.exists(TXOBTOOL) or not os.path.exists(THREEDSTOOL):
        print(f"3dstool or txobtool not found in {TOOLS_DIR}, skipping banner repack.")
        return

    temp_dir = os.path.join(REPO_ROOT, "build", "banner_temp")
    if os.path.exists(temp_dir):
        shutil.rmtree(temp_dir, ignore_errors=True)
    os.makedirs(temp_dir, exist_ok=True)

    # 1. Extract existing BNR template
    cmd_extract = [THREEDSTOOL, "-xvtf", "banner", BANNER_BNR, "--banner-dir", temp_dir]
    r = subprocess.run(cmd_extract, capture_output=True, text=True)
    if r.returncode != 0:
        print("Failed to extract banner:", r.stderr)
        return

    # 2. Extract textures from banner0.bcmdl
    banner0_dir = os.path.join(temp_dir, "banner0")
    bcmdl_file = os.path.join(temp_dir, "banner0.bcmdl")
    cmd_tx_export = [TXOBTOOL, "-evfd", bcmdl_file, banner0_dir]
    r = subprocess.run(cmd_tx_export, capture_output=True, text=True)
    if r.returncode != 0:
        print("Failed to export bcmdl textures:", r.stderr)
        return

    # 3. Overwrite COMMON1.png with new banner.png
    dest_png = os.path.join(banner0_dir, "COMMON1.png")
    shutil.copyfile(BANNER_PNG, dest_png)

    # 4. Import new texture into banner0.bcmdl
    cmd_tx_import = [TXOBTOOL, "-ivfd", bcmdl_file, banner0_dir]
    r = subprocess.run(cmd_tx_import, capture_output=True, text=True)
    if r.returncode != 0:
        print("Failed to import bcmdl textures:", r.stderr)
        return

    # 5. Repack into NLPPSS.bnr
    cmd_repack = [THREEDSTOOL, "-cvtf", "banner", BANNER_BNR, "--banner-dir", temp_dir]
    r = subprocess.run(cmd_repack, capture_output=True, text=True)
    if r.returncode != 0:
        print("Failed to repack banner.bnr:", r.stderr)
        return

    # Cleanup temp
    shutil.rmtree(temp_dir, ignore_errors=True)
    print("Repacked NLPPSS.bnr successfully!")

if __name__ == "__main__":
    force_flag = "--force" in sys.argv
    repack_banner(force=force_flag)
