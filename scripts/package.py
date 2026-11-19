"""Create an SD-card-ready package from an actual built NRO."""
import hashlib
import pathlib
import re
import shutil
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
version = re.search(r'project\(UURemote-ns VERSION ([\d.]+)',
                    (ROOT/'CMakeLists.txt').read_text(encoding='utf-8')).group(1)
source = ROOT/'build-nx/UURemote-ns.nro'
data = source.read_bytes()
if data[16:20] != b'NRO0':
    raise ValueError('Not a valid NRO executable')
destination = ROOT/'dist/switch/UURemote-ns/UURemote-ns.nro'
destination.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(source, destination)
notices = {
    'libdatachannel-MPL-2.0.txt':'external/libdatachannel/LICENSE',
    'libjuice-MPL-2.0.txt':'external/libdatachannel/deps/libjuice/LICENSE',
    'usrsctp-BSD-3-Clause.txt':'external/libdatachannel/deps/usrsctp/LICENSE.md',
    'libsrtp-BSD-3-Clause.txt':'external/libdatachannel/deps/libsrtp/LICENSE',
    'plog-MIT.txt':'external/libdatachannel/deps/plog/LICENSE',
    'json-MIT.txt':'external/json/LICENSE.MIT',
    'qrcodegen-MIT.txt':'external/qrcodegen/Readme.markdown',
    'mbedtls-Apache-2.0.txt':'external/mbedtls/LICENSE',
    'curl.txt':'external/curl/COPYING',
    'NotoSansCJK-OFL.txt':'romfs/fonts/OFL-NotoSansCJK.txt',
}
digest=hashlib.sha256(data).hexdigest()
archive = ROOT/f'dist/UURemote-ns-{version}-switch.zip'
with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as z:
    z.write(destination, destination.relative_to(ROOT/'dist'))
    for name in ('LICENSE', 'THIRD_PARTY.md'):
        z.write(ROOT/name, name)
    for name, path in notices.items():
        notice = ROOT/path
        if notice.is_file():
            z.write(notice, 'licenses/'+name)
(ROOT/'dist/SHA256SUMS').write_text(
    f'{digest}  UURemote-ns.nro\n'
    f'{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n'
)
print(archive)
print('NRO SHA256:',digest)
