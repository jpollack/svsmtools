#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <unistd.h>
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
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
#include <unordered_map>
#include <memory>

inline void __dieunless (const char *msg, const char *file, int line) { fprintf (stderr, "[%s:%d] Assertion '%s' failed.\n", file, line, msg); abort (); }

#define dieunless(EX) ((EX) || (__dieunless (#EX, __FILE__, __LINE__),false))

using namespace std;

struct mydata
{
    unordered_map<string,pair<int, struct shmid_ds>> segs;
    unordered_map<string, unique_ptr<struct stat>> filstats;
    void init (void);
};


void mydata::init (void)
{
    struct shmid_ds dummy;
    int maxShmid = shmctl (0, SHM_INFO, &dummy);
    dieunless (maxShmid >= 0);

    this->segs.clear ();
    this->filstats.clear ();

    for (int ii=0; ii<=maxShmid; ii++) {

	struct shmid_ds seg0;
	int ret = shmctl (ii, SHM_STAT_ANY, &seg0);
	if (ret < 0) continue;
	if (!seg0.shm_perm.__key) continue;

	char buf[16];
	snprintf (buf, 16, "0x%08x", seg0.shm_perm.__key);
	this->segs[string (buf)] = {ret, seg0};

	auto p = make_unique<struct stat>();
	memset (p.get (), 0, sizeof(struct stat));
	p->st_uid = seg0.shm_perm.uid;
	p->st_gid = seg0.shm_perm.gid;
	p->st_mode = S_IFREG | 0644;
	p->st_nlink = 1;
	p->st_size = seg0.shm_segsz;
	this->filstats[string (buf)] = move (p);
    }
}

static int do_getattr (const char *path, struct stat *st, struct fuse_file_info *fi)
{
    int ret = -1;

    struct fuse_context *ctx = fuse_get_context ();
    mydata *that = (mydata *) ctx->private_data;

    if (!strcmp(path, "/"))
    {
	st->st_uid = getuid();
	st->st_gid = getgid();
	st->st_atime = time( NULL );
	st->st_mtime = st->st_atime;
	st->st_mode = S_IFDIR | 0755;
	st->st_nlink = 2;
	ret = 0;
    }
    else if (that->filstats.count (path+1))
    {
	memcpy (st, that->filstats[path+1].get (), sizeof(struct stat));
	ret = 0;
    } else {
	ret = -ENOENT;
    }

    return ret;
}

static void *do_init (struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    static mydata d0;
    d0.init ();
    return &d0;
}

static void do_destroy (void *udata)
{
    (void) udata;
}


static int do_truncate (const char *path, off_t size, struct fuse_file_info *fi)
{
    struct fuse_context *ctx = fuse_get_context ();
    mydata *that = (mydata *) ctx->private_data;

    if (that->segs.count (path+1)) {  // Can't overwrite
	return -EPERM;
    }

    if (!that->filstats.count (path+1)) { // Create wasn't called.
	return -ENOENT;
    }

    key_t key0 = std::stoul (string (path+1), nullptr, 16);
    int shmid = shmget (key0, size, IPC_CREAT | IPC_EXCL | 0666);

    struct shmid_ds seg0;
    dieunless (shmctl (shmid, IPC_STAT, &seg0) == 0);

    that->segs[string (path+1)] = {shmid, seg0};

    const auto& p = that->filstats[string (path+1)];
    p->st_uid = seg0.shm_perm.uid;
    p->st_gid = seg0.shm_perm.gid;
    p->st_mode = S_IFREG | 0644;
    p->st_nlink = 1;
    p->st_size = seg0.shm_segsz;

    return 0;
}
static int do_fallocate (const char *path, int mode, off_t offset, off_t length, struct fuse_file_info *fi)
{
    struct fuse_context *ctx = fuse_get_context ();
    mydata *that = (mydata *) ctx->private_data;

    if (mode != 0) {
	return -ENOSYS;
    }

    return do_truncate (path, offset + length, fi);
}

static int do_unlink (const char *path)
{
    struct fuse_context *ctx = fuse_get_context ();
    mydata *that = (mydata *) ctx->private_data;

    if (!that->filstats.count (path+1) && !that->segs.count (path+1)) {
	return -ENOENT;
    }

    if (that->filstats.count (path + 1)) {
	that->filstats.erase (path + 1);
    }

    if (that->segs.count (path+1)) {
	dieunless (0 == shmctl (that->segs[path+1].first, IPC_RMID, nullptr));
	that->segs.erase (path + 1);
    }

    return 0;
}

static int do_write (const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct fuse_context *ctx = fuse_get_context ();
    mydata *that = (mydata *) ctx->private_data;

    if (!that->segs.count (path+1)) {
	return -ENOENT;
    }

    int shmid = that->segs[path+1].first;
    size_t shmsz = that->segs[path+1].second.shm_segsz;
    char *pbase{nullptr};

    dieunless ((char *)-1 != (pbase = (char *)shmat (shmid, nullptr, 0)));
    if ((shmsz - offset) < size) {
	size = shmsz - offset;
    }
    memcpy (pbase + offset, buf, size);
    shmdt (pbase);
    return size;
}

static int do_create (const char *path, mode_t mode, struct fuse_file_info *fi)
{
    struct fuse_context *ctx = fuse_get_context ();
    mydata *that = (mydata *) ctx->private_data;

    if (that->filstats.count (path+1)) {
	return -EPERM;  // Can't overwrite
    }

    if ((path[1] != '0') || (path[2] != 'x')) { // Don't allow creating non-hex
	return -EPERM;
    }

    key_t key0 = std::stoul (string (path+1), nullptr, 16);
    char buf[16];
    snprintf (buf, 16, "0x%08x", key0);

    if (strcmp (buf, path+1)) {
	return -EPERM;
    }

    auto p = make_unique<struct stat>();
    memset (p.get (), 0, sizeof(struct stat));
    p->st_mode = S_IFREG | mode;
    p->st_nlink = 1;
    that->filstats[string (path+1)] = move (p);

    return 0;
}

static int do_readdir( const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags fl)
{
    if (strcmp (path, "/")) {
	return -ENOENT;
    }

    struct fuse_context *ctx = fuse_get_context ();
    mydata *that = (mydata *) ctx->private_data;

    filler (buffer, ".", NULL, 0, (fuse_fill_dir_flags) 0);
    filler (buffer, "..", NULL, 0, (fuse_fill_dir_flags) 0);

    for (const auto& [k, v] : that->filstats) {
	filler (buffer, k.c_str (), v.get (), 0, (fuse_fill_dir_flags) 0);
    }

    return 0;
}

static int do_read( const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi )
{
    struct fuse_context *ctx = fuse_get_context ();
    mydata *that = (mydata *) ctx->private_data;

    if (!that->segs.count (path+1)) {
	return -1;
    }
    int shmid = that->segs[path+1].first;
    size_t shmsz = that->segs[path+1].second.shm_segsz;
    char *pbase{nullptr};

    dieunless ((char *)-1 != (pbase = (char *)shmat (shmid, nullptr, SHM_RDONLY)));
    if ((shmsz - offset) < size) {
	size = shmsz - offset;
    }
    memcpy (buffer, pbase + offset, size);
    shmdt (pbase);
    return size;
}

static struct fuse_operations operations = {
    .getattr	= do_getattr,
    .unlink	= do_unlink,
    .truncate	= do_truncate,
    .read	= do_read,
    .write	= do_write,
    .readdir	= do_readdir,
    .init	= do_init,
    .destroy	= do_destroy,
    .create	= do_create,
    .fallocate	= do_fallocate,
};

int main (int argc, char **argv, char **envp)
{
    return fuse_main (argc, argv, &operations, NULL);
}
