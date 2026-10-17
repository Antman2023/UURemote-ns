"""Small, idempotent Horizon compatibility changes to pinned upstream sources."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]/'external/libdatachannel'
def replace(path, before, after):
    file = root/path
    text = file.read_text()
    if after in text:
        return
    if before not in text:
        raise RuntimeError(f'Patch context mismatch: {path}')
    file.write_text(text.replace(before,after))

replace(Path('deps/libjuice/src/socket.h'), '#ifdef __ANDROID__\n#define NO_IFADDRS', '#if defined(__ANDROID__) || defined(__SWITCH__)\n#define NO_IFADDRS')
replace(Path('deps/libjuice/src/random.c'), '#elif defined(_WIN32)', '#elif defined(__SWITCH__)\n#include <switch.h>\nstatic int random_bytes(void *buf, size_t size) { randomGet(buf, size); return 0; }\n#elif defined(_WIN32)')
# Horizon exposes its single active interface via gethostid(), not SIOCGIFCONF.
replace(Path('deps/libjuice/src/udp.c'), '#else // NO_IFADDRS defined\n', '''#elif defined(__SWITCH__)
    if (count > 0) {
        memset(records, 0, sizeof(*records));
        struct sockaddr_in *sa = (struct sockaddr_in *)&records[0].addr;
        sa->sin_family = AF_INET;
        sa->sin_port = htons(port);
        sa->sin_addr.s_addr = gethostid();
        records[0].len = sizeof(*sa);
        return 1;
    }
    return 0;
#else // NO_IFADDRS defined
''')
replace(Path('deps/usrsctp/usrsctplib/user_environment.c'), '#elif defined(SCTP_USE_OPENSSL_RAND)', '''#elif defined(__SWITCH__)
#include <switch.h>
void init_random(void) {}
void read_random(void *buf, size_t size) { randomGet(buf, size); }
void finish_random(void) {}
#elif defined(SCTP_USE_OPENSSL_RAND)''')
replace(Path('src/impl/utils.cpp'), '#include <thread>', '#include <thread>\n#ifdef __SWITCH__\n#include <switch.h>\n#endif')
replace(Path('src/impl/utils.cpp'), '\t// Seed with random device\n\ttry {', '\t// Seed with random device\n#ifdef __SWITCH__\n\tseed.resize(4); randomGet(seed.data(),seed.size()*sizeof(unsigned int));\n#else\n\ttry {')
replace(Path('src/impl/utils.cpp'), '\t// Seed with high-resolution clock', '#endif\n\t// Seed with high-resolution clock')
replace(Path('src/impl/socket.hpp'), '#include <ifaddrs.h>', '#ifndef __SWITCH__\n#include <ifaddrs.h>\n#endif')
# Use the socket-based wakeup path on Horizon; it has no POSIX pipe device.
replace(Path('deps/libjuice/src/conn_poll.c'), '#ifdef _WIN32', '#if defined(_WIN32) || defined(__SWITCH__)')
replace(Path('deps/libjuice/src/conn_poll.c'), '#ifndef _WIN32', '#if !defined(_WIN32) && !defined(__SWITCH__)')
replace(Path('deps/libjuice/src/conn_poll.c'), '#include <string.h>', '#include <string.h>\n#ifdef __SWITCH__\n#define WSAENOTSOCK ENOTSOCK\n#endif')
print('Horizon patches applied.')
