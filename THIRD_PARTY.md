# Third-party software

Application code is licensed under GPL-3.0-or-later; see LICENSE. Linked dependencies retain their own licenses. The supplied devkitPro FFmpeg build is GPL-enabled. Exact source archive revisions and hashes are recorded in scripts/sources.lock.json; SDK package versions and hashes are in scripts/sdk-windows.lock.json.

| Dependency | Upstream | License |
| --- | --- | --- |
| libnx | https://github.com/switchbrew/libnx | ISC |
| SDL2 / SDL2_ttf | https://github.com/libsdl-org | zlib |
| FFmpeg (Horizon/NVTEGRA build) | https://github.com/devkitPro/pacman-packages/tree/master/switch/ffmpeg | GPL-2.0-or-later build |
| libdatachannel / libjuice | https://github.com/paullouisageneau | MPL-2.0 |
| Mbed TLS | https://github.com/Mbed-TLS/mbedtls | Apache-2.0 OR GPL-2.0-or-later |
| usrsctp | https://github.com/sctplab/usrsctp | BSD-3-Clause |
| libSRTP | https://github.com/cisco/libsrtp | BSD-3-Clause |
| plog | https://github.com/SergiusTheBest/plog | MIT |
| nlohmann/json | https://github.com/nlohmann/json | MIT |
| QR Code generator | https://github.com/nayuki/QR-Code-generator | MIT |
| Opus | https://opus-codec.org | BSD-3-Clause |
| libcurl | https://curl.se | curl license |
| FreeType / HarfBuzz | https://freetype.org / https://harfbuzz.github.io | FTL or GPL / MIT |
| Mesa / libdrm | https://www.mesa3d.org / https://gitlab.freedesktop.org/mesa/drm | MIT and notices in source |
| dav1d / libpng / zlib / bzip2 | devkitPro portlibs package sources | BSD-2-Clause / libpng / Zlib / bzip2 license |

The native client protocol is ported from the user-provided local UURemote-web reference. No original UU binaries, APK, or signing constant are committed to this repository. UU names, services, and protocol belong to their respective owners. This is an unofficial client.
