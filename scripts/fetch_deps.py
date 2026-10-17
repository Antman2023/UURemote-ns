"""Download pinned source archives; no system-wide installs or git submodules."""
import concurrent.futures
import hashlib
import io
import json
import pathlib
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
def fetch(entry):
    name, repo, ref = entry['name'], entry['repo'], entry['ref']
    target = ROOT / 'external' / entry['path']
    if (target / '.fetched').exists() and (target / '.fetched').read_text().splitlines()[0] == ref:
        return
    data = urllib.request.urlopen(f'https://codeload.github.com/{repo}/tar.gz/{ref}', timeout=120).read()
    if hashlib.sha256(data).hexdigest() != entry['sha256']:
        raise ValueError('Source archive SHA256 mismatch: '+name)
    target.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        for member in archive:
            parts = pathlib.PurePosixPath(member.name).parts[1:]
            if not parts:
                continue
            member.name = str(pathlib.PurePosixPath(*parts))
            archive.extract(member, target, filter='data')
    (target / '.fetched').write_text(ref + '\n' + hashlib.sha256(data).hexdigest())
    print('Fetched', name, ref, flush=True)

def main():
    dependencies = json.loads((ROOT/'scripts/sources.lock.json').read_text())
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        # Parents must finish before children extract into their directories.
        list(pool.map(fetch, [d for d in dependencies if '/' not in d['path']]))
        list(pool.map(fetch, [d for d in dependencies if '/' in d['path']]))

if __name__ == '__main__':
    main()
