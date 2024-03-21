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
    R"(System V Shared Memory Dump

    Usage: 
      svsmdump [options] SHMID

    Options:
      -h --help             Show this screen.
      --skip=BYTES          Start reading BYTES after the start of the segment. [default: 0]
      --count=BYTES         Stop reading after BYTES, or 0 for the remainder. [default: 0]
      --render=STRING       One of 'raw', 'int', 'str'. [default: raw]

)";

static docopt::Options d;

int main (int argc, char **argv, char **envp)
{
    d = docopt::docopt (USAGE, {argv+1, argv+argc});
    int64_t skip{d["--skip"].asLong ()};
    int64_t count{d["--count"].asLong ()};
    int64_t shmid{d["SHMID"].asLong ()};
    
    struct shmid_ds shm0;
    dieunless (shmctl (shmid, IPC_STAT, &shm0) == 0);
    
    dieunless ((skip + count) <= shm0.shm_segsz);
    if (count == 0) {
	count = shm0.shm_segsz - skip;
    }

    char *pbase{nullptr};
    dieunless ((char *)-1 != (pbase = (char *)shmat (shmid, nullptr, SHM_RDONLY)));
    
    if (!d["--render"].asString ().compare ("raw")) {
	int64_t nbytes = 0;
	while (nbytes < count) {
	    ssize_t ret = write (STDOUT_FILENO, pbase + skip + nbytes, count - nbytes);
	    if ((ret < 0) && (errno == EAGAIN)) {
		continue;
	    }
	    dieunless (ret != -1);
	    nbytes += ret;
	}
    } else if (!d["--render"].asString ().compare ("int")) {
	dieunless (count <= 8);
	int64_t val{0};
	memcpy (&val, pbase + skip, count);
	printf ("%ld\n", val);
    } else if (!d["--render"].asString ().compare ("str")) {
	printf ("%s\n", pbase + skip);
    } else {
	fprintf (stderr, "Unsupported renderer '%s'\n", d["--render"].asString ().c_str ());
    }

    return 0;
}
