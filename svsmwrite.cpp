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
    R"(System V Shared Memory Write

    Usage: 
      svsmwrite [options] SHMID

    Options:
      -h --help             Show this screen.
      --seek=BYTES          Start writing BYTES after the start of the segment. [default: 0]
      --count=BYTES         Stop reading after BYTES, or 0 for the remainder. [default: 0]

)";

static docopt::Options d;

int main (int argc, char **argv, char **envp)
{
    d = docopt::docopt (USAGE, {argv+1, argv+argc});
    int64_t seek{d["--seek"].asLong ()};
    int64_t count{d["--count"].asLong ()};
    int64_t shmid{d["SHMID"].asLong ()};

    struct shmid_ds shm0;
    dieunless (shmctl (shmid, IPC_STAT, &shm0) == 0);
    
    dieunless ((seek + count) <= shm0.shm_segsz);
    if (count == 0) {
	count = shm0.shm_segsz - seek;
    }

    char *pbase{nullptr};
    dieunless ((char *)-1 != (pbase = (char *)shmat (shmid, nullptr, 0)));

    int64_t nbytes = 0;
    while (nbytes < count) {
	ssize_t ret = read (STDIN_FILENO, pbase + seek + nbytes, count - nbytes);
	if ((ret < 0) && (errno == EAGAIN)) {
	    continue;
	}
	dieunless (ret != -1);
	nbytes += ret;
    }
    
    dieunless (-1 != shmdt (pbase));

    return 0;
}
