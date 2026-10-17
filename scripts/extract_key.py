"""Extract the user's APK signing constant into the private build RomFS."""
import argparse
import hashlib
import pathlib
import struct
import zipfile

def extract(apk):
    with zipfile.ZipFile(apk) as z:
        dex = z.read('classes.dex')
    count, offset = struct.unpack_from('<II', dex, 56)
    for i in range(count):
        p = struct.unpack_from('<I', dex, offset + i*4)[0]
        while dex[p] & 128:
            p += 1
        p += 1
        value = dex[p:dex.index(0,p)]
        if len(value) == 24 and hashlib.sha256(value).hexdigest() == '40cb3a32f9f6f90d9962fb9736685bb881979e848b5956649b6dae3cc957528c':
            return value
    raise ValueError('APK does not match the verified UU TV 3.5.0 signing fingerprint')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('apk', type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1]/'romfs/signing.key')
    args = parser.parse_args()
    key = extract(args.apk)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(key)
    print('Signing material extracted (value not printed).')
