#include <unistd.h>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <docopt.h>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <openssl/sha.h>
#include <sstream>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <sys/time.h>
#include <vector>
#include <signal.h>
#include <random>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>


inline void __dieunless (const char *msg, const char *file, int line) { fprintf (stderr, "[%s:%d] Assertion '%s' failed.\n", file, line, msg); abort (); }

#define dieunless(EX) ((EX) || (__dieunless (#EX, __FILE__, __LINE__),false))

using namespace std;

static const char USAGE[] =
    R"(System V Shared Memory Create

    Usage: 
      svsmcreate [options] KEY SIZE_BYTES

    Options:
      -h --help             Show this screen.

)";

static docopt::Options d;

int main (int argc, char **argv, char **envp)
{
    d = docopt::docopt (USAGE, {argv+1, argv+argc});

    int64_t sz{d["--size"].asLong ()};
    key_t key0 = std::stoul (d["KEY"].asString (), nullptr, 16);
    int shmid = shmget (key0, sz, IPC_CREAT | IPC_EXCL | 0666);
    dieunless (shmid > 0);
    cout << shmid << "\n";
    return 0;
}
