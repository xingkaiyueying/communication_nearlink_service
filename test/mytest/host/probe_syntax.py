"""Syntax-check the single native probe on Windows; Linux/token headers are boundary stubs."""
def check_probe(cc, repo, out):
    import subprocess
    stubs = {
        'sys/socket.h': '''#pragma once
#include <stddef.h>
#include <unistd.h>
typedef unsigned socklen_t;
struct sockaddr { unsigned short sa_family; char sa_data[14]; };
enum { AF_PACKET=17, SOCK_DGRAM=2, SOCK_CLOEXEC=02000000 };
int socket(int,int,int); int bind(int,const struct sockaddr *,socklen_t);
ssize_t sendto(int,const void *,size_t,int,const struct sockaddr *,socklen_t);
ssize_t recvfrom(int,void *,size_t,int,struct sockaddr *,socklen_t *);
''',
        'net/if.h': '#pragma once\nunsigned if_nametoindex(const char *);\n',
        'linux/if_packet.h': '''#pragma once
struct sockaddr_ll { unsigned short sll_family,sll_protocol; int sll_ifindex; unsigned short sll_hatype;
    unsigned char sll_pkttype,sll_halen,sll_addr[8]; };
#define PACKET_OUTGOING 4
''',
        'linux/if_ether.h': '#define ETH_P_ALL 3\n#define ETH_P_IP 0x0800\n#define ETH_P_IPV6 0x86dd\n',
        'arpa/inet.h': '#include <stdint.h>\nuint16_t htons(uint16_t);\n',
        'poll.h': '#define POLLIN 1\nstruct pollfd { int fd; short events,revents; };\nint poll(struct pollfd *,unsigned,int);\n',
        'accesstoken_kit.h': '''namespace OHOS::Security::AccessToken {
struct AccessTokenKit { static int ReloadNativeTokenInfo(); }; }
''',
        'nativetoken_kit.h': '''#include <stdint.h>
struct NativeTokenInfoParams { unsigned permsNum; const char **perms; const char *processName; const char *aplStr; };
uint64_t GetAccessTokenId(NativeTokenInfoParams *);
''',
        'token_setproc.h': '#include <stdint.h>\nint SetSelfTokenID(uint64_t);\n',
    }
    for name, text in stubs.items():
        path = out / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    includes = [out, repo/'test/mytest', repo/'interfaces/inner_api/include',
                repo/'services/stack/src/cp/bal/iposl/interface']
    subprocess.run([cc, '-std=c++17', '-x', 'c++', '-fsyntax-only', *['-I'+str(p) for p in includes],
                    str(repo/'test/mytest/sleip_nearlink_stage1.c')], check=True)
