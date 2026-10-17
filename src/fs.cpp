/*
 *  This file is part of TYSS.
 *  Copyright (C) 2024-2026 R-YaTian
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <3ds.h>
#include <string>
#include <algorithm>
#include <cstring>
#include <cstdarg>

#include "fs.h"
#include "util.h"
#include "ui.h"
#include "gfx.h"
#include "data.h"
#include "type.h"
#include "cfg.h"
#include "crypto.h"

#define IO_BUFF_SIZE 0x8000

static FS_Archive sdmcArch, saveArch;
static FS_ArchiveID saveMode = (FS_ArchiveID)0;
static std::u16string dataPath;
static Handle fsPxiHandle;
static const u32 pxiPath[5] = { 1,1,3,0,0 };

typedef struct 
{
    FS_Archive srcArch, dstArch;
    std::u16string src, dst;
    uint64_t offset = 0;
    bool commit = false;
    bool isPxi = false;
} cpyArgs;

typedef struct
{
    std::vector<data::titleData>* vect = NULL;
    uint32_t mode = 0;
    fs::BunchType type = fs::BunchType::Bunch_CTR;
} bakArgs;

void fs::createDir(const std::string& path)
{
    FSUSER_CreateDirectory(fs::getSDMCArch(), fsMakePath(PATH_ASCII, path.c_str()), 0);
}

void fs::init()
{
    FSUSER_OpenArchive(&sdmcArch, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));

    createDir("/NLPPSS");
    createDir("/NLPPSS/Saves");
    createDir("/NLPP_BACKUP");
}

void fs::exit()
{
    FSUSER_CloseArchive(sdmcArch);
}

FS_Archive fs::getSDMCArch()
{
    return sdmcArch;
}

FS_Archive fs::getSaveArch()
{
    return saveArch;
}

Handle fs::getPxiHandle()
{
    return fsPxiHandle;
}

void fs::closeSaveArch()
{
    FSUSER_CloseArchive(saveArch);
}

void fs::closePxiSaveArch()
{
    FSPXI_CloseArchive(fsPxiHandle, saveArch);
}

FS_ArchiveID fs::getSaveMode()
{
    return saveMode;
}

bool fs::openArchive(data::titleData& dat, const uint32_t& mode, bool error, FS_Archive& arch)
{
    Result res = -1;
    saveMode = (FS_ArchiveID) mode;

    switch(mode)
    {
        case ARCHIVE_USER_SAVEDATA:
            {
                uint32_t path[3] = {dat.getMedia(), dat.getLow(), dat.getHigh()};
                FS_Path binData = (FS_Path) {PATH_BINARY, 12, path};
                res = FSUSER_OpenArchive(&arch, ARCHIVE_USER_SAVEDATA, binData);
            }
            break;

        case ARCHIVE_SAVEDATA:
            res = FSUSER_OpenArchive(&arch, ARCHIVE_SAVEDATA, fsMakePath(PATH_EMPTY, ""));
            break;

        case ARCHIVE_EXTDATA:
            {
                uint32_t path[] = {MEDIATYPE_SD, dat.getExtData(), 0};
                FS_Path binData = (FS_Path) {PATH_BINARY, 12, path};
                res = FSUSER_OpenArchive(&arch, ARCHIVE_EXTDATA, binData);
            }
            break;

        case ARCHIVE_SYSTEM_SAVEDATA:
            {
                uint32_t path[2] = {MEDIATYPE_NAND, (0x00020000 | dat.getUnique())};
                FS_Path binData = {PATH_BINARY, 8, path};
                res = FSUSER_OpenArchive(&arch, ARCHIVE_SYSTEM_SAVEDATA, binData);
            }
            break;

        case ARCHIVE_BOSS_EXTDATA:
            {
                uint32_t path[3] = {MEDIATYPE_SD, dat.getExtData(), 0};
                FS_Path binData = {PATH_BINARY, 12, path};
                res = FSUSER_OpenArchive(&arch, ARCHIVE_BOSS_EXTDATA, binData);
            }
            break;

        case ARCHIVE_SHARED_EXTDATA:
            {
                uint32_t path[3] = {MEDIATYPE_NAND, dat.getExtData(), 0x00048000};
                FS_Path binPath  = {PATH_BINARY, 0xC, path};
                res = FSUSER_OpenArchive(&arch, ARCHIVE_SHARED_EXTDATA, binPath);
            }
            break;

        case ARCHIVE_NAND_TWL_FS:
            {
                res = FSUSER_OpenArchive(&arch, ARCHIVE_NAND_TWL_FS, fsMakePath(PATH_EMPTY, ""));
                if (R_SUCCEEDED(res))
                {
                    char* saveDir = new char[32];
                    sprintf(saveDir, "/title/%08lx/%08lx/data", (dat.getHigh() & 0x00000FFF) | 0x00030000, dat.getLow());
                    dataPath = util::toUtf16(saveDir);
                    Handle tmp;
                    res = FSUSER_OpenDirectory(&tmp, arch, fsMakePath(PATH_UTF16, dataPath.data()));
                    if (R_FAILED(res))
                        FSUSER_CloseArchive(arch);
                    else
                        FSDIR_Close(tmp);
                    delete[] saveDir;
                }
            }
            break;

        case ARCHIVE_SAVEDATA_AND_CONTENT:
            {
                uint32_t path[4] = {dat.getLow(), dat.getHigh(), dat.getMedia(), 0};
                FS_Path binPath  = {PATH_BINARY, 16, path};
                res = FSPXI_OpenArchive(fsPxiHandle, &arch, ARCHIVE_SAVEDATA_AND_CONTENT, binPath);
                if (R_SUCCEEDED(res))
                {
                    FSPXI_File tmp;
                    res = FSPXI_OpenFile(fsPxiHandle, &tmp, arch, {PATH_BINARY, 20, pxiPath}, FS_OPEN_READ, 0);
                    if (R_FAILED(res))
                        FSPXI_CloseArchive(fsPxiHandle, arch);
                    else
                        FSPXI_CloseFile(fsPxiHandle, tmp);
                }
            }
            break;
    }

    if(R_FAILED(res))
    {
        if(error)
            ui::showMessage(getTxt("无法打开该存档位. 该存档类型可能不存在适用此 title 的数据.\n错误: 0x%08X\nTitle: %s"), (unsigned)res, dat.getTitleUTF8().c_str());
        return false;
    }

    return true;
}

bool fs::openArchive(data::titleData& dat, const uint32_t& mode, bool error)
{
    return openArchive(dat, mode, error, saveArch);
}

void fs::commitData(const uint32_t& mode)
{
    if(mode != ARCHIVE_EXTDATA && mode != ARCHIVE_BOSS_EXTDATA && mode != ARCHIVE_SHARED_EXTDATA && mode != ARCHIVE_NAND_TWL_FS)
    {
        Result res = FSUSER_ControlArchive(saveArch, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
        if(R_FAILED(res))
            ui::showMessage(getTxt("提交存档数据失败!\n错误: 0x%08X"), (unsigned)res);
    }
}

void fs::deleteSv(const uint32_t& mode, const data::titleData& dat)
{
    if(dat.getMedia() != MEDIATYPE_GAME_CARD
        && mode != ARCHIVE_EXTDATA
        && mode != ARCHIVE_BOSS_EXTDATA
        && mode != ARCHIVE_SHARED_EXTDATA
        && mode != ARCHIVE_NAND_TWL_FS)
    {
        Result res = 0;
        u64 in = ((u64)SECUREVALUE_SLOT_SD << 32) | (dat.getUnique() << 8);
        u8 out;

        res = FSUSER_ControlSecureSave(SECURESAVE_ACTION_DELETE, &in, 8, &out, 1);
        if(R_FAILED(res))
            ui::showMessage(getTxt("删除安全值失败.\n错误: 0x%08X"), (unsigned)res);
    }
}

void fs::exportSv(const uint32_t& mode, const std::u16string& _dst, const data::titleData& dat)
{
    if(dat.getMedia() != MEDIATYPE_GAME_CARD
        && mode != ARCHIVE_EXTDATA
        && mode != ARCHIVE_BOSS_EXTDATA
        && mode != ARCHIVE_SHARED_EXTDATA
        && mode != ARCHIVE_NAND_TWL_FS)
    {
        Result res = 0;
        bool exists = false;
        u64 value = 0;
        res = FSUSER_GetSaveDataSecureValue(&exists, &value, SECUREVALUE_SLOT_SD, dat.getUnique(), (u8) (dat.getLow() & 0xFF));
        if(R_SUCCEEDED(res)) {
            if(!exists) return;
            fs::fsfile dst(getSDMCArch(), _dst, FS_OPEN_WRITE | FS_OPEN_CREATE, sizeof(u64));
            dst.write(&value, sizeof(u64));
            dst.close();
        }

        if(R_FAILED(res))
            ui::showMessage(getTxt("获取安全值失败.\n错误: 0x%08X"), (unsigned)res);
    }
}

void fs::importSv(const uint32_t& mode, const std::u16string& _src, const data::titleData& dat)
{
    if(dat.getMedia() != MEDIATYPE_GAME_CARD
        && mode != ARCHIVE_EXTDATA
        && mode != ARCHIVE_BOSS_EXTDATA
        && mode != ARCHIVE_SHARED_EXTDATA
        && mode != ARCHIVE_NAND_TWL_FS)
    {
        if (!fexists(getSDMCArch(), _src)) return; // do nothing if not found
        fs::fsfile src(getSDMCArch(), _src, FS_OPEN_READ);
        u64 value = 0;
        src.read(&value, sizeof(u64));
        src.close();

        Result res = 0;
        res = FSUSER_SetSaveDataSecureValue(value, SECUREVALUE_SLOT_SD, dat.getUnique(), (u8) (dat.getLow() & 0xFF));

        if(R_FAILED(res))
            ui::showMessage(getTxt("导入安全值失败.\n错误: 0x%08X"), (unsigned)res);
    }
}

bool fs::fexists(const FS_Archive& _arch, const std::string& _path)
{
    Handle tmp;
    FS_Path testPath = fsMakePath(PATH_ASCII, _path.c_str());
    Result res = FSUSER_OpenFile(&tmp, _arch, testPath, FS_OPEN_READ, 0);
    FSFILE_Close(tmp);
    return R_SUCCEEDED(res);
}

bool fs::fexists(const FS_Archive& _arch, const std::u16string& _path)
{
    Handle tmp;
    FS_Path testPath = fsMakePath(PATH_UTF16, _path.c_str());
    Result res = FSUSER_OpenFile(&tmp, _arch, testPath, FS_OPEN_READ, 0);
    FSFILE_Close(tmp);
    return R_SUCCEEDED(res);
}

bool fs::fexists(const std::string& path)
{
    return fexists(getSDMCArch(), path);
}

void fs::resetPxiFile(const FS_Archive& _arch)
{
    fs::fsfile pxiFile(_arch, util::toUtf16("/"), FS_OPEN_WRITE);
    size_t size = pxiFile.getSize();
    uint8_t *buffer = new uint8_t[size];
    memset(buffer, 0, size);
    pxiFile.write(buffer, size);
    pxiFile.close();
    delete[] buffer;
    FSPXI_CloseArchive(fsPxiHandle, _arch);
}

void fs::delDirRec(const FS_Archive& _arch, const std::u16string& path)
{
    FS_Path delPath = fsMakePath(PATH_UTF16, path.c_str());
    FSUSER_DeleteDirectoryRecursively(_arch, delPath);
}

void fs::createDir(const FS_Archive& _arch, const std::u16string& _path)
{
    FS_Path createPath = fsMakePath(PATH_UTF16, _path.c_str());
    FSUSER_CreateDirectory(_arch, createPath, 0);
}

void fs::createDirRec(const FS_Archive& _arch, const std::u16string& path)
{
    size_t pos = path.find(L'/', 0) + 1;
    while((pos = path.find(L'/', pos)) != path.npos)
    {
        FSUSER_CreateDirectory(_arch, fsMakePath(PATH_UTF16, path.substr(0, pos).c_str()), 0);
        ++pos;
    }
    FSUSER_CreateDirectory(_arch, fsMakePath(PATH_UTF16, path.c_str()), 0);
}

fs::fsfile::fsfile(const FS_Archive& _arch, const std::string& _path, const uint32_t& openFlags)
{
    if(openFlags & FS_OPEN_CREATE)
    {
        FS_Path delPath = fsMakePath(PATH_ASCII, _path.c_str());
        FSUSER_DeleteFile(_arch, delPath);
    }

    if (_arch != getSDMCArch() && saveMode == ARCHIVE_SAVEDATA_AND_CONTENT)
        error = FSPXI_OpenFile(fsPxiHandle, &fHandle, _arch, {PATH_BINARY, 20, pxiPath}, openFlags, 0);
    else
        error = FSUSER_OpenFile((Handle*) &fHandle, _arch, fsMakePath(PATH_ASCII, _path.c_str()), openFlags, 0);

    if(R_SUCCEEDED(error))
    {
        if (_arch != getSDMCArch() && saveMode == ARCHIVE_SAVEDATA_AND_CONTENT) {
            isPxi = true;
            FSPXI_GetFileSize(fsPxiHandle, fHandle, &fSize);
        } else
            FSFILE_GetSize((Handle) fHandle, &fSize);
        open = true;
    }
}

fs::fsfile::fsfile(const FS_Archive& _arch, const std::string& _path, const uint32_t& openFlags, const uint64_t& crSize)
{
    if(openFlags & FS_OPEN_CREATE)
    {
        FS_Path delPath = fsMakePath(PATH_ASCII, _path.c_str());
        FSUSER_DeleteFile(_arch, delPath);
    }

    if (_arch != getSDMCArch() && saveMode == ARCHIVE_SAVEDATA_AND_CONTENT) {
        FSPXI_CreateFile(fsPxiHandle, _arch, {PATH_BINARY, 20, pxiPath}, 0, crSize);
        error = FSPXI_OpenFile(fsPxiHandle, &fHandle, _arch, {PATH_BINARY, 20, pxiPath}, openFlags, 0);
    } else {
        FSUSER_CreateFile(_arch, fsMakePath(PATH_ASCII, _path.c_str()), 0, crSize);
        error = FSUSER_OpenFile((Handle*) &fHandle, _arch, fsMakePath(PATH_ASCII, _path.c_str()), openFlags, 0);
    }

    if(R_SUCCEEDED(error))
    {
        if (_arch != getSDMCArch() && saveMode == ARCHIVE_SAVEDATA_AND_CONTENT) {
            isPxi = true;
            FSPXI_GetFileSize(fsPxiHandle, fHandle, &fSize);
        } else
            FSFILE_GetSize((Handle) fHandle, &fSize);
        open = true;
    }
}

fs::fsfile::fsfile(const FS_Archive& _arch, const std::u16string& _path, const uint32_t& openFlags)
{
    if(openFlags & FS_OPEN_CREATE)
    {
        FS_Path delPath = fsMakePath(PATH_UTF16, _path.c_str());
        FSUSER_DeleteFile(_arch, delPath);
    }

    if (_arch != getSDMCArch() && saveMode == ARCHIVE_SAVEDATA_AND_CONTENT)
        error = FSPXI_OpenFile(fsPxiHandle, &fHandle, _arch, {PATH_BINARY, 20, pxiPath}, openFlags, 0);
    else
        error = FSUSER_OpenFile((Handle*) &fHandle, _arch, fsMakePath(PATH_UTF16, _path.c_str()), openFlags, 0);

    if(R_SUCCEEDED(error))
    {
        if (_arch != getSDMCArch() && saveMode == ARCHIVE_SAVEDATA_AND_CONTENT) {
            isPxi = true;
            FSPXI_GetFileSize(fsPxiHandle, fHandle, &fSize);
        } else
            FSFILE_GetSize((Handle) fHandle, &fSize);
        open = true;
    }
}

fs::fsfile::fsfile(const FS_Archive& _arch, const std::u16string& _path, const uint32_t& openFlags, const uint64_t& crSize)
{
    if(openFlags & FS_OPEN_CREATE)
    {
        FS_Path delPath = fsMakePath(PATH_UTF16, _path.c_str());
        FSUSER_DeleteFile(_arch, delPath);
    }

    if (_arch != getSDMCArch() && saveMode == ARCHIVE_SAVEDATA_AND_CONTENT) {
        FSPXI_CreateFile(fsPxiHandle, _arch, {PATH_BINARY, 20, pxiPath}, 0, crSize);
        error = FSPXI_OpenFile(fsPxiHandle, &fHandle, _arch, {PATH_BINARY, 20, pxiPath}, openFlags, 0);
    } else {
        FSUSER_CreateFile(_arch, fsMakePath(PATH_UTF16, _path.c_str()), 0, crSize);
        error = FSUSER_OpenFile((Handle*) &fHandle, _arch, fsMakePath(PATH_UTF16, _path.c_str()), openFlags, 0);
    }

    if(R_SUCCEEDED(error))
    {
        if (_arch != getSDMCArch() && saveMode == ARCHIVE_SAVEDATA_AND_CONTENT) {
            isPxi = true;
            FSPXI_GetFileSize(fsPxiHandle, fHandle, &fSize);
        } else
            FSFILE_GetSize((Handle) fHandle, &fSize);
        open = true;
    }
}

fs::fsfile::~fsfile()
{
    if(open)
    {
        if (isPxi)
            FSPXI_CloseFile(fsPxiHandle, fHandle);
        else
            FSFILE_Close((Handle) fHandle);
        open = false;
    }
}

void fs::fsfile::close()
{
    if(open)
    {
        if (isPxi)
            FSPXI_CloseFile(fsPxiHandle, fHandle);
        else
            FSFILE_Close((Handle) fHandle);
        open = false;
    }
}

size_t fs::fsfile::read(void *buf, const uint32_t& max)
{
    uint32_t readOut = 0;
    Result res;

    if (isPxi)
        res = FSPXI_ReadFile(fsPxiHandle, fHandle, &readOut, offset, buf, max);
    else
        res = FSFILE_Read((Handle) fHandle, &readOut, offset, buf, max);

    if(R_FAILED(res))
    {
        if(readOut > max)
            readOut = max;
    }
    offset += readOut;
    return (size_t)readOut;
}

bool fs::fsfile::getLine(char *out, size_t max)
{
    if(offset >= fSize)
        return false;

    memset(out, 0, max);
    unsigned i = 0;
    char byte = 0;
    while(i < max && (byte = getByte()) != '\n')
        *out++ = byte;

    return true;
}

size_t fs::fsfile::write(const void* buf, const uint32_t& size)
{
    uint32_t writeOut = 0;
    if (isPxi)
        FSPXI_WriteFile(fsPxiHandle, fHandle, &writeOut, offset, buf, size, FS_WRITE_FLUSH);
    else
        FSFILE_Write((Handle) fHandle, &writeOut, offset, buf, size, FS_WRITE_FLUSH);
    offset += writeOut;
    return (size_t)writeOut;
}

void fs::fsfile::writef(const char *fmt, ...)
{
    char tmp[512];
    va_list args;
    va_start(args, fmt);
    vsprintf(tmp, fmt, args);
    va_end(args);
    write(tmp, strlen(tmp));
}

uint8_t fs::fsfile::getByte()
{
    uint8_t ret = 0;
    if (isPxi)
        FSPXI_ReadFile(fsPxiHandle, fHandle, NULL, offset, &ret, 1);
    else
        FSFILE_Read((Handle) fHandle, NULL, offset, &ret, 1);
    ++offset;
    return ret;
}

void fs::fsfile::putByte(const uint8_t& put)
{
    if (isPxi)
        FSPXI_WriteFile(fsPxiHandle, fHandle, NULL, offset, &put, 1, FS_WRITE_FLUSH);
    else
        FSFILE_Write((Handle) fHandle, NULL, offset, &put, 1, FS_WRITE_FLUSH);
    ++offset;
}

bool fs::fsfile::eof()
{
    return offset < fSize ? false : true;
}

void fs::fsfile::seek(const int& pos, const uint8_t& seekFrom)
{
    switch(seekFrom)
    {
        case seek_set:
            offset = pos;
            break;

        case seek_cur:
            offset += pos;
            break;

        case seek_end:
            offset = fSize + pos;
            break;
    }
}

struct
{
    bool operator()(const fs::dirItem& a, const fs::dirItem& b)
    {
        if(a.isDir != b.isDir)
            return a.isDir;

        unsigned aLen = a.nameUTF8.length();
        unsigned bLen = b.nameUTF8.length();
        unsigned minLen = std::min(aLen, bLen);
        for(unsigned i = 0; i < minLen; i++)
        {
            int charA = std::tolower(a.nameUTF8[i]), charB = std::tolower(b.nameUTF8[i]);
            if(charA != charB)
                return charA < charB;
        }

        return aLen < bLen;
    }
} sortDirs;

fs::dirList::dirList(const FS_Archive& arch, const std::u16string& p)
{
    dirArch = arch;
    path = p;

    scanItem();
}

fs::dirList::~dirList()
{
    entry.clear();
}

void fs::dirList::scanItem(bool filter)
{
    FSUSER_OpenDirectory(&dirHandle, dirArch, fsMakePath(PATH_UTF16, path.data()));

    uint32_t read = 0;
    do
    {
        FS_DirectoryEntry ent;
        FSDIR_Read(dirHandle, &read, 1, &ent);
        if(read == 1)
        {
            fs::dirItem newEntry = {std::u16string((char16_t *)ent.name),
                                    util::toUtf8(std::u16string((char16_t *)ent.name)),
                                    (ent.attributes & FS_ATTRIBUTE_DIRECTORY) != 0};
            if (!filter)
                entry.push_back(newEntry);
            else if (newEntry.isDir)
                entry.push_back(newEntry);
        }
    }
    while(read > 0);

    FSDIR_Close(dirHandle);

    std::sort(entry.begin(), entry.end(), sortDirs);
}

void fs::dirList::reassign(const FS_Archive& arch, const std::u16string& p, bool filter)
{
    dirArch = arch;
    path = p;

    entry.clear();

    scanItem(filter);
}

void fs::copyFile(const FS_Archive& _srcArch, const std::u16string& _src, const FS_Archive& _dstArch, const std::u16string& _dst, bool commit, bool isPxi, threadInfo *t)
{
    if(t)
        t->status->setStatus(getTxt("正在复制 ") + util::toUtf8(_src) +"...");

    fs::fsfile src(_srcArch, _src, FS_OPEN_READ);

    if (isPxi && saveArch == _dstArch && src.isOpen())
    {
        fs::fsfile tmp(_dstArch, _dst, FS_OPEN_READ);
        if (src.getSize() != tmp.getSize())
        {
            ui::showMessage(getTxt("原始GBAVC存档数据大小不符,无法恢复!"));
            return;
        }
        tmp.close();
    }

    fs::fsfile dst(_dstArch, _dst, FS_OPEN_WRITE, src.getSize());
    if(!src.isOpen() || !dst.isOpen())
        return;

    size_t readIn = 0;
    uint32_t size = 0;
    size = src.getSize() > IO_BUFF_SIZE ? IO_BUFF_SIZE : src.getSize();
    uint8_t *buffer = new uint8_t[size];
    while((readIn = src.read(buffer, size)))
        dst.write(buffer, readIn);

    delete[] buffer;

    if(commit)
    {
        dst.close();
        fs::commitData(fs::getSaveMode());
    }
    if(isPxi) ui::fldRefresh();
}

static void copyFile_t(void *a)
{
    threadInfo *t = (threadInfo *)a;
    cpyArgs *cpy = (cpyArgs *)t->argPtr;
    fs::copyFile(cpy->srcArch, cpy->src, cpy->dstArch, cpy->dst, cpy->commit, cpy->isPxi, t);
    delete cpy;
    t->argPtr = NULL;
    t->drawFunc = NULL;
    t->finished = true;
}

void fs::copyFileThreaded(const FS_Archive& _srcArch, const std::u16string& _src, const FS_Archive& _dstArch, const std::u16string& _dst, bool commit, bool isPxi)
{
    cpyArgs *send = new cpyArgs;
    send->srcArch = _srcArch;
    send->src = _src;
    send->dstArch = _dstArch;
    send->dst = _dst;
    send->commit = commit;
    send->isPxi = isPxi;
    ui::newThread(copyFile_t, send, NULL);
}

void fs::copyDirToDir(const FS_Archive& _srcArch, const std::u16string& _src, const FS_Archive& _dstArch, const std::u16string& _dst, bool commit, threadInfo *t, bool isRecursion)
{
    std::u16string srcPath = _src;
    std::u16string dstPath = _dst;
    if (!isRecursion && saveMode == ARCHIVE_NAND_TWL_FS)
    {
        if (saveArch == _srcArch) srcPath = dataPath + _src;
        else if (saveArch == _dstArch) dstPath = dataPath + _dst;
    }

    fs::dirList srcList(_srcArch, srcPath);
    for(unsigned i = 0; i < srcList.getCount(); i++)
    {
        if(srcList.isDir(i))
        {
            std::u16string newSrc = srcPath + srcList.getItem(i) + util::toUtf16("/");
            std::u16string newDst = dstPath + srcList.getItem(i) + util::toUtf16("/");
            fs::createDir(_dstArch, newDst.substr(0, newDst.length() - 1));
            fs::copyDirToDir(_srcArch, newSrc, _dstArch, newDst, commit, t, true);
        }
        else
        {
            std::u16string fullSrc = srcPath + srcList.getItem(i);
            std::u16string fullDst = dstPath + srcList.getItem(i);
            fs::copyFile(_srcArch, fullSrc, _dstArch, fullDst, commit, false, t);
        }
    }
}

static void copyDirToDir_t(void *a)
{
    threadInfo *t = (threadInfo *)a;
    cpyArgs *cpy = (cpyArgs *)t->argPtr;
    fs::copyDirToDir(cpy->srcArch, cpy->src, cpy->dstArch, cpy->dst, cpy->commit, t);
    delete cpy;

    ui::fldRefresh();

    t->argPtr = NULL;
    t->drawFunc = NULL;
    t->finished = true;
}

void fs::copyDirToDirThreaded(const FS_Archive& _srcArch, const std::u16string& _src, const FS_Archive& _dstArch, const std::u16string& _dst, bool commit)
{
    cpyArgs *send = new cpyArgs;
    send->srcArch = _srcArch;
    send->src = _src;
    send->dstArch = _dstArch;
    send->dst = _dst;
    send->commit = commit;
    ui::newThread(copyDirToDir_t, send, NULL);
}

void fs::backupTitles_t(void *a)
{
    threadInfo *t = (threadInfo *)a;
    bakArgs *args = (bakArgs *)t->argPtr;
    std::vector<data::titleData>& vect = *args->vect;
    uint32_t mode = args->mode;
    BunchType type = args->type;

    for(unsigned i = 0; i < vect.size(); i++)
    {
        if (vect[i].getExtInfos().isDSCard) continue;
        if (vect[i].isAGB()) continue;
        if (type != BunchType::Bunch_TWL && (vect[i].getHigh() & 0x8000) == 0x8000) continue;
        if (type == BunchType::Bunch_TWL && (vect[i].getHigh() & 0x8000) != 0x8000) continue;
        std::string copyStr = getTxt("正在处理 '") + vect[i].getTitleUTF8() + "'...";
        ui::prog->setText(copyStr);
        ui::prog->update(i);

        FS_Archive _arch;
        if(fs::openArchive(vect[i], type == BunchType::Bunch_TWL ? ARCHIVE_NAND_TWL_FS : mode, true, _arch))
        {
            util::createTitleDir(vect[i], mode);
            std::u16string outpath = util::createPath(vect[i], mode) + util::toUtf16(util::getDateString(util::DATE_FMT_YMD));

            std::u16string svOut = outpath + util::toUtf16(".sv");
            if (type != BunchType::Bunch_TWL)
                fs::exportSv(mode, svOut, vect[i]); // export secure value if found

            FSUSER_CreateDirectory(fs::getSDMCArch(), fsMakePath(PATH_UTF16, outpath.c_str()), 0);
            outpath += util::toUtf16("/");
            fs::copyDirToDir(_arch, util::toUtf16("/"), fs::getSDMCArch(), outpath, false, NULL);

            FSUSER_CloseArchive(_arch);
        }
    }

    t->argPtr = NULL;
    t->drawFunc = NULL;
    t->finished = true;
}

void fs::backupTitles(std::vector<data::titleData>& vect, const uint32_t &mode, BunchType type)
{
    ui::prog->setMax(vect.size());
    bakArgs *send = new bakArgs;
    send->vect = &vect;
    send->mode = mode;
    send->type = type;
    ui::newThread(backupTitles_t, send, ui::progressBarDrawFunc, ZIP_THRD_STACK_SIZE);
}
