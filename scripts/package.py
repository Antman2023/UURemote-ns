"""Create an SD-card-ready package from an actual built NRO."""
import hashlib
import pathlib
import shutil
import zipfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
source = ROOT/'build-nx/UURemote-ns.nro'
data = source.read_bytes()
if data[16:20] != b'NRO0':
    raise ValueError('Not a valid NRO executable')
destination = ROOT/'dist/switch/UURemote-ns/UURemote-ns.nro'
destination.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(source, destination)
shutil.copy2(ROOT/'README.md', ROOT/'dist/README.md')
shutil.copy2(ROOT/'THIRD_PARTY.md', ROOT/'dist/THIRD_PARTY.md')
shutil.copy2(ROOT/'LICENSE', ROOT/'dist/LICENSE')
shutil.copytree(ROOT/'docs', ROOT/'dist/docs', dirs_exist_ok=True)
notices = {
    'libdatachannel-MPL-2.0.txt':'external/libdatachannel/LICENSE',
    'libjuice-MPL-2.0.txt':'external/libdatachannel/deps/libjuice/LICENSE',
    'usrsctp-BSD-3-Clause.txt':'external/libdatachannel/deps/usrsctp/LICENSE.md',
    'libsrtp-BSD-3-Clause.txt':'external/libdatachannel/deps/libsrtp/LICENSE',
    'plog-MIT.txt':'external/libdatachannel/deps/plog/LICENSE',
    'json-MIT.txt':'external/json/LICENSE.MIT',
    'qrcodegen-MIT.txt':'external/qrcodegen/Readme.markdown',
    'mbedtls-Apache-2.0.txt':'external/mbedtls/LICENSE',
}
notice_dir = ROOT/'dist/licenses'
notice_dir.mkdir(parents=True, exist_ok=True)
for name,path in notices.items():
    if (ROOT/path).is_file():
        shutil.copy2(ROOT/path,notice_dir/name)
digest=hashlib.sha256(data).hexdigest()
(ROOT/'dist/SHA256SUMS').write_text(f'{digest}  switch/UURemote-ns/UURemote-ns.nro\n')
archive = ROOT/'dist/UURemote-ns-0.1.0-switch.zip'
with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as z:
    paths = [destination,ROOT/'dist/README.md',ROOT/'dist/THIRD_PARTY.md',ROOT/'dist/LICENSE',ROOT/'dist/SHA256SUMS']
    paths += list((ROOT/'dist/docs').rglob('*')) + list(notice_dir.glob('*'))
    for path in paths:
        if not path.is_file(): continue
        z.write(path,path.relative_to(ROOT/'dist'))
(ROOT/'dist/SHA256SUMS').write_text(
    f'{digest}  UURemote-ns.nro\n'
    f'{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n'
)
print(archive)
print('NRO SHA256:',digest)
