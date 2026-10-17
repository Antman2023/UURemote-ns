"""Cross-compile the real socket headers; accept Horizon and reject the old layout."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get('DEVKITPRO', ROOT / '.tools/sdk/opt/devkitpro'))
SUFFIX = '.exe' if os.name == 'nt' else ''

with tempfile.TemporaryDirectory(prefix='uu-sctp-abi-') as temporary:
    directory = Path(temporary)
    source = directory / 'probe.c'
    source.write_text('#include "usrsctp.h"\n#include "switch_compat/sctp_layout.h"\n')
    includes = [
        '-I' + str(ROOT / 'include'),
        '-I' + str(ROOT / 'external/libdatachannel/deps/usrsctp/usrsctplib'),
        '-isystem', str(SDK / 'libnx/include'),
        '-isystem', str(SDK / 'portlibs/switch/include'),
        '-DHAVE_SCONN_LEN', '-march=armv8-a+crc+crypto', '-mtp=soft',
    ]
    for language, executable, standard in [('c', 'gcc', 'gnu11'), ('c++', 'g++', 'gnu++17')]:
        compiler = SDK / 'devkitA64/bin' / ('aarch64-none-elf-' + executable + SUFFIX)
        if not compiler.is_file():
            raise SystemExit('Set DEVKITPRO to a devkitA64 SDK and fetch/patch dependencies first.')
        common = [str(compiler), '-x', language, '-std=' + standard, *includes,
                  '-c', str(source), '-o', str(directory / 'probe.o')]
        correct = subprocess.run([*common, '-D__SWITCH__'], capture_output=True,
                                 encoding='utf-8', errors='replace')
        if correct.returncode:
            raise SystemExit(correct.stderr)
        # Removing the Switch OS macro selects exactly the old sockaddr_conn branch,
        # while the actual Horizon SDK still supplies the native sockaddr definition.
        obsolete = subprocess.run([*common, '-U__SWITCH__'], capture_output=True,
                                  encoding='utf-8', errors='replace')
        if not obsolete.returncode or 'sconn_len' not in obsolete.stderr:
            raise SystemExit('The ABI guard failed to reject the pre-fix SCTP address layout.')
print('SCTP ABI contract passed: C and C++ accept Horizon and reject the legacy layout.')
