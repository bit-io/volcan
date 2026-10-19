import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_shim import extract

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

INCLUDES = """#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <chrono>
#include <mutex>
#include <thread>
#include <algorithm>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <unistd.h>
"""

def region():
    cpp = extract(os.path.join(ROOT, "lib", "shim.hcs"))
    return cpp[cpp.index("// BEGIN-STANDALONE"):cpp.index("// END-STANDALONE")]

def source():
    """Kompletny plik .cpp: bufory + blok standalone w extern "C"."""
    return INCLUDES + 'namespace { std::string g_buf0; std::string g_buf1; }\nextern "C" {\n' + region() + "\n}\n"
