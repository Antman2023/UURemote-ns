"""Install the pinned Windows native debug toolchain locally (Python 3.14+)."""
import concurrent.futures
import hashlib
import json
from pathlib import Path
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DESTINATION = ROOT / '.tools/native-sdk'

def install(package):
    cache = DESTINATION / 'cache'
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / package['url'].rsplit('/', 1)[1]
    if not path.exists():
        request = urllib.request.Request(package['url'], headers={'User-Agent': 'UURemote-native-build'})
        temporary = path.with_suffix('.partial')
        with urllib.request.urlopen(request, timeout=60) as response, temporary.open('wb') as output:
            while block := response.read(1024 * 1024):
                output.write(block)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != package['sha256']:
            raise ValueError('Native SDK checksum mismatch: ' + package['name'])
        temporary.replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != package['sha256']:
        raise ValueError('Native SDK checksum mismatch: ' + package['name'])
    with tarfile.open(path, 'r:*') as archive:
        archive.extractall(DESTINATION, members=[m for m in archive if m.name.startswith('mingw64/')], filter='data')
    print('Native SDK:', package['name'], package['version'], flush=True)

if __name__ == '__main__':
    if sys.platform != 'win32' or sys.version_info < (3, 14):
        raise SystemExit('The native Windows SDK requires Windows and Python 3.14+.')
    packages = json.loads((ROOT / 'scripts/sdk-native-windows.lock.json').read_text(encoding='utf-8'))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(install, packages))
    print('Native SDK ready:', DESTINATION / 'mingw64')
