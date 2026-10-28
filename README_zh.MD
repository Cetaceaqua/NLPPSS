# NLPPSS
NLPPSS - NEWラブプラス+ Save Studio

<p align="left">
<a href="http://www.gnu.org/copyleft/gpl.html"><img alt="license" src="https://img.shields.io/badge/license-GPL%20V3-blue.svg?style=flat"/></a>
<a href="https://github.com/Cetaceaqua/NLPPSS/releases/latest"><img alt="release" src="https://img.shields.io/badge/release-v1.3.3-brightgreen.svg?style=flat"/></a>
<a href="https://github.com/Cetaceaqua/NLPPSS"><img alt="platform" src="https://img.shields.io/badge/platform-Nintendo%203DS-red.svg?style=flat"/></a>
</p>

<p align="center">
  <img src="doc/Title.png" alt="NEW LovePlus+ Save Studio" width="600" />
</p>

<h4 align="right">
  简体中文 | <a href="README.MD">English</a>
</h4>

<p align="center">
  <img src="doc/Preview.png" alt="NLPPSS Preview" width="360" />
</p>

## 功能特性

* 支持读取和修改 3DS 数字版和实体版存档。
* 支持自动备份、手动备份和恢复存档。
* 支持复制和清空存档槽位数据。
* 支持拼音、假名、英文、数字和符号内置键盘，可以输入中文。
* 支持浅色和深色界面主题。
* 支持多语言用户界面，提供简体中文、英文和繁体中文。
* 支持多种存档修改功能：
  - 修改姓氏、名字和通讯昵称（HN）。
  - 修改生日、血型、出身地和自称（咱/我）。
  - 修改姓氏读音、名字读音、后缀读音、预设读音和特殊读音，快捷搜索所有可用读音。
  - 修改发长、发型和发色，提供图片实时预览；支持自定义 RGB 与 HEX 发色并自动匹配发丝高光。
  - 修改总天数与上线天数。
  - 修改运动、知识、感性、魅力属性值。
  - 修改金钱和 TODO 点数。
  - 修改等级称号。
  - 移除相机滤镜。

---

## 下载与安装

请前往 [GitHub Releases](https://github.com/Cetaceaqua/NLPPSS/releases) 下载最新版本，提供多种格式：

| 文件格式 | 适用场景 | 安装与使用方法 |
| :--- | :--- | :--- |
| **`.cia`** | 3DS | 使用 FBI 等安装工具安装。 |
| **`.3dsx`** | 3DS | 将文件放在 SD 卡 `/3ds/NLPPSS/NLPPSS.3dsx`，使用 Homebrew Launcher 启动。 |
| **`.cci`** | 模拟器 | 直接在 Azahar 模拟器中运行。 |

---

## 编译构建

1. 安装 [devkitPro](https://devkitpro.org/)（包含 `devkitARM`、`libctru`、`citro2d`、`citro3d`）。
2. 安装所需依赖：
   ```bash
   dkp-pacman -S 3ds-curl 3ds-mbedtls 3ds-zlib
   ```
3. 安装 Python 3 与 `gettext` 工具链。
4. 克隆仓库并编译：
   ```bash
   git clone https://github.com/Cetaceaqua/NLPPSS.git
   cd NLPPSS
   make all_formats
   ```

---

## 许可证

本项目基于 **GNU General Public License v3.0** 许可证分发。详细内容请参阅 [LICENSE](./LICENSE)。

**Copyright © 2026 Cetaceaqua.**

**Copyright © 2024 - 2026 R-YaTian.**

---

## 致谢

* **NLPPSS**
  - [Cetaceaqua](https://github.com/Cetaceaqua)：开发与逆向工程
  - 社区测试者：（排序不分先后）
    - 渲酱
    - 闪光
    - zhizhi0w0
    - Check
    - 佚名
* **TYSS**
  - [R-YaTian](https://github.com/R-YaTian)：主要开发者，项目所有者
  - [J-D-K](https://github.com/J-D-K) 提供原始的 [JKSM](https://github.com/J-D-K/JKSM) 版本
  - [TuxSH](https://github.com/tuxsh) 提供 [TWLSaveTool](https://github.com/TuxSH/TWLSaveTool)，从中获取了 SPI 相关代码
  - 所有 [Checkpoint](https://github.com/BernardoGiordano/Checkpoint) 和 [PKSM](https://github.com/FlagBrew/PKSM) 贡献者，某些代码实现已移植到 TYSS
  - [WinterMute](https://github.com/WinterMute)、[fincs](https://github.com/fincs) 和 [devkitPro](https://devkitpro.org/) 贡献者提供 devkitARM 和 [libctru](https://github.com/devkitPro/libctru)
  - [Forget](https://github.com/James-Makoto) 进行测试并制作 TYSS 标志/图标
  - [FlagBrew](https://github.com/FlagBrew) 提供原始的 [Sharkive 金手指数据库](https://github.com/FlagBrew/Sharkive)
  - [天地人](https://github.com/realkkk)：[Sharkive 简中金手指数据库](https://github.com/R-YaTian/Sharkive) 主要翻译者
  - [Ryuzaki-MrL](https://github.com/Ryuzaki-MrL) 提供 [Cthulhu](https://github.com/Ryuzaki-MrL/Cthulhu) 自制软件
