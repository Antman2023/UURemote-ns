"""Extract the pinned Windows devkitPro SDK inside this workspace (Python 3.14+)."""
import concurrent.futures
import hashlib
import json
import pathlib
import sys
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
def install(package):
    cache = ROOT/'.tools/cache'
    cache.mkdir(parents=True, exist_ok=True)
    filename = package['url'].split('/')[-1].split('?')[0]
    path = cache/filename
    if not path.exists():
        request = urllib.request.Request(package['url'], headers={'User-Agent':'pacman/7.0.0'})
        path.write_bytes(urllib.request.urlopen(request, timeout=180).read())
    if hashlib.sha256(path.read_bytes()).hexdigest() != package['sha256']:
        raise ValueError('SDK SHA256 mismatch: '+package['name'])
    target = ROOT/'.tools/sdk'
    target.mkdir(parents=True, exist_ok=True)
    with tarfile.open(path, 'r:*') as archive:
        archive.extractall(target, filter='data')
    print('SDK:', package['name'], package['version'], flush=True)

if __name__ == '__main__':
    if sys.version_info < (3,14):
        raise SystemExit('Python 3.14+ is required for .tar.zst support; alternatively install devkitPro normally.')
    if sys.platform != 'win32':
        raise SystemExit('This lock is for Windows. On Linux/macOS use the official devkitPro packages.')
    packages = json.loads((ROOT/'scripts/sdk-windows.lock.json').read_text())
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(install, packages))
    print('Set DEVKITPRO to',ROOT/'.tools/sdk/opt/devkitpro')
