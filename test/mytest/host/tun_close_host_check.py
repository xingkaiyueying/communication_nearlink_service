"""Execute production TUN close/read ordering with a deterministic blocked poll double."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[3]
source = repo / 'services/service/src/ipshare/nearlink_ipshare_tun.cpp'
production = source.read_text(encoding='utf-8')
before = os.environ.get('P3_TUN_BEFORE_REF')
if before:
    production = subprocess.check_output(['git', '-c', f'safe.directory={repo.as_posix()}', '-C', str(repo),
        'show', before + ':services/service/src/ipshare/nearlink_ipshare_tun.cpp']).decode()
close = production[production.index('void NearlinkIpShareTun::Close()'):production.index('int32_t NearlinkIpShareTun::Write(')]
read = production[production.index('void NearlinkIpShareTun::ReadLoop()'):production.rindex('} // namespace')]
with tempfile.TemporaryDirectory(prefix='p3-tun-close-') as directory:
    root = Path(directory)
    (root / 'nearlink_ipshare_tun.h').write_text(source.with_suffix('.h').read_text(encoding='utf-8'), encoding='utf-8')
    (root / 'check.cpp').write_text(r'''
#include <atomic>
#include <cassert>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#define private public
#include "nearlink_ipshare_tun.h"
#undef private
using ssize_t = long long;
constexpr size_t IP_SHARE_PACKET_MAX=1500;
constexpr short POLLIN=1;
struct pollfd {int fd;short events,revents;};
std::mutex gate;
std::condition_variable cv;
bool entered=false,released=false;
std::atomic_bool stopped{false},closed{false};
std::atomic_int reads{0},callbacks{0};
int poll(pollfd*p,int,int) {
 std::unique_lock<std::mutex> lock(gate);entered=true;cv.notify_all();
 cv.wait(lock,[]{return released;});p->revents=POLLIN;return 1;
}
ssize_t read(int,void*,size_t){++reads;return 1;}
int close(int){closed=true;assert(stopped);return 0;}
void logInfo(const char*text) {if(std::strstr(text,"read loop stopped"))stopped=true;}
template<class... Args> void logInfo(const char*text,Args...){logInfo(text);}
#define HILOGI(...) logInfo(__VA_ARGS__)
#define HILOGE(...) ((void)0)
namespace OHOS::Nearlink {
''' + close + read + r'''
NearlinkIpShareTun::~NearlinkIpShareTun(){Close();}
}
int main(){
 OHOS::Nearlink::NearlinkIpShareTun tun;
 tun.fd_=17;tun.running_=true;tun.callback_=[](const uint8_t*,uint16_t){++callbacks;};
 tun.reader_=std::thread([&]{tun.ReadLoop();});
 {std::unique_lock<std::mutex> lock(gate);cv.wait(lock,[]{return entered;});}
 std::thread cleanup([&]{tun.Close();});
 while(tun.running_.load())std::this_thread::yield();
 {std::lock_guard<std::mutex> lock(gate);released=true;}cv.notify_all();
 cleanup.join();assert(closed && stopped && reads==0 && callbacks==0 && tun.fd_==-1);
}
''', encoding='utf-8')
    executable = root / 'check.exe'
    subprocess.run([sys.argv[1] if len(sys.argv) > 1 else 'g++', '-std=c++17', '-pthread',
        '-I' + str(root), str(root / 'check.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('TUN production close/read race: join before fd release, no post-stop read/callback PASS')
