"""Extract the pinned Windows devkitPro SDK inside this workspace (Python 3.14+)."""
import concurrent.futures
import hashlib
import json
import pathlib
import sys
import tarfile
import time
import urllib.request
from urllib.error import HTTPError, URLError

ROOT = pathlib.Path(__file__).resolve().parents[1]

def download(package):
    request = urllib.request.Request(package['url'], headers={'User-Agent': 'pacman/7.0.0'})
    for attempt in range(1, 4):
        try:
            with urllib.request.urlopen(request, timeout=180) as response:
                return response.read()
        except (URLError, TimeoutError) as error:
            if isinstance(error, HTTPError) and error.code not in (403, 408, 429) and not 500 <= error.code < 600:
                raise
            if attempt == 3:
                raise
            print(f"Retrying SDK download for {package['name']} ({attempt + 1}/3)", flush=True)
            time.sleep(2 ** attempt)

def cached_archive(package, cache):
    filename = package['url'].split('/')[-1].split('?')[0]
    path = cache/filename
    if not path.exists():
        path.write_bytes(download(package))
    if hashlib.sha256(path.read_bytes()).hexdigest() != package['sha256']:
        raise ValueError('SDK SHA256 mismatch: '+package['name'])
    return path

if __name__ == '__main__':
    if sys.version_info < (3,14):
        raise SystemExit('Python 3.14+ is required for .tar.zst support; alternatively install devkitPro normally.')
    if sys.platform != 'win32':
        raise SystemExit('This lock is for Windows. On Linux/macOS use the official devkitPro packages.')
    packages = json.loads((ROOT/'scripts/sdk-windows.lock.json').read_text())
    cache = ROOT/'.tools/cache'
    cache.mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        archives = list(pool.map(lambda package: cached_archive(package, cache), packages))

    # Archives share directories. Windows path resolution can race concurrent creation.
    target = ROOT/'.tools/sdk'
    target.mkdir(parents=True, exist_ok=True)
    for package, path in zip(packages, archives, strict=True):
        with tarfile.open(path, 'r:*') as archive:
            archive.extractall(target, filter='data')
        print('SDK:', package['name'], package['version'], flush=True)
    print('Set DEVKITPRO to', target/'opt/devkitpro')
