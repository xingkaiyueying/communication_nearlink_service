"""Compile real S1 sources with mocked platform boundaries. Not a product/device build."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cxx', default='g++')
p.add_argument('--cc', default='gcc')
a = p.parse_args()
here = Path(__file__).resolve().parent
repo = here.parents[2]
profile = repo / 'services/stack/src/cp/bal/iposl'
channel = repo / 'services/service/src/ipshare'
with tempfile.TemporaryDirectory(prefix='p2-s1-') as tmp:
    out = Path(tmp)
    shutil.copytree(here / 'stubs', out, dirs_exist_ok=True)
    for source in [*profile.glob('src/*.[ch]'), profile / 'interface/iposl_profile.h',
                   channel / 'nearlink_ipshare_channel.cpp', channel / 'nearlink_ipshare_channel.h',
                   channel / 'nearlink_ipshare_tun.h', here.parent / 'sleip_probe_packets.h',
                   channel / 'nearlink_ipshare_ipv6.h', channel / 'nearlink_ipshare_ipv6.cpp',
                   repo / 'services/stack/src/cp/bsl/sle/qosm/interface/qosm_trans_channel.h',
                   repo / 'ipc_parcel/parcel/nearlink_ipshare_status.h',
                   repo / 'ipc_parcel/parcel/nearlink_ipshare_status.cpp',
                   repo / 'ipc_parcel/interface/nearlink_service_ipc_interface_code.h']:
        shutil.copyfile(source, out / source.name)
    # Codec is C in the stack, while the probe compiles as C++.
    subprocess.run([a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(out),
                    '-c', str(out/'iposl_codec.c'), '-o', str(out/'codec.o')], check=True)
    for source in sorted(here.glob('*_test.cpp')):
        if source.stem == 'service_test':
            for name in ('nearlink_ipshare_service.cpp', 'nearlink_ipshare_service.h'):
                shutil.copyfile(channel / name, out / name)
        exe = out / (source.stem + '.exe')
        subprocess.run([a.cxx, '-std=c++17', '-pthread', '-I'+str(out), str(source), str(out/'nearlink_ipshare_ipv6.cpp'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        print(source.stem + '=PASS', flush=True)

    production = (profile / 'src/iposl_profile.c').read_text(encoding='utf-8')
    (out / 'profile_send.inc').write_text(production[production.index('/* The caller and CP task'):], encoding='utf-8')
    exe = out / 'profile_send_test.exe'
    subprocess.run([a.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(out),
                    str(here/'profile_send_test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    print('profile_send_test=PASS (production CP send functions; platform/allocator mocked)', flush=True)

    syntax = out / 'syntax'
    syntax.mkdir()
    (syntax / 'nlstk_log.h').write_text('#define NLSTK_LOG_INFO(...) ((void)0)\n#define NLSTK_LOG_WARN(...) ((void)0)\n#define NLSTK_LOG_ERROR(...) ((void)0)\n')
    (syntax / 'cp_worker.h').write_text('#include <stdint.h>\nuint32_t CP_PostTaskBlocked(void (*cb)(void *),void *arg,void (*freeCb)(void *),int timeout);\n')
    includes = sorted({str(p.parent) for p in (repo/'services/stack').rglob('*.h')})
    for name in ('iposl_client.c', 'iposl_server.c', 'iposl_profile.c'):
        subprocess.run([a.cc, '-std=c11', '-fsyntax-only', '-I'+str(syntax),
                        *['-I'+d for d in includes], str(profile/'src'/name)], check=True)
    print('real_stack_headers_c_syntax=PASS (logging/CP worker platform boundary stubbed)', flush=True)

    from probe_syntax import check_probe
    check_probe(a.cxx, repo, syntax / 'probe')
    print('native_probe_host_syntax=PASS (Linux/token SDK boundary stubbed)', flush=True)
