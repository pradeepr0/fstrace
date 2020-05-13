/**
 * A passthrough FUSE filesystem that traces `open` and `create` calls
 * adapted from the libfuse example fusexmp_fh.c
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FUSE_USE_VERSION 26
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <set>
#include <string>

extern "C" {
#include <ulockmgr.h>
}

#include "sysutil.hxx"

class IntrofsState {
public:
  IntrofsState(pid_t delegate_pid_, const char* logfilename, const char* mount_point_)
      : delegate_pid(delegate_pid_),
        logfp(open_file(logfilename, "w")),
        mount_point(mount_point_) {}

  ~IntrofsState() { fclose(logfp); }

  const pid_t delegate_pid;
  FILE* logfp;
  const std::string mount_point;
  std::set<std::string> links;
  std::set<std::string> ifiles;
  std::set<std::string> ofiles;
};

static int log_printf(const char* format, ...) noexcept {
  struct fuse_context* context = fuse_get_context();
  IntrofsState* state = static_cast<IntrofsState*>(context->private_data);
  FILE* logfp = state->logfp;

  va_list args;
  va_start(args, format);
  int n = vfprintf(logfp, format, args);
  va_end(args);

  return n;
}

//
// FUSE delegate definitions
//

static void* introfs_init(struct fuse_conn_info* conn) noexcept {
  IntrofsState* pstate = static_cast<IntrofsState*>(fuse_get_context()->private_data);
  sigqueue(pstate->delegate_pid, SIGUSR2, sigval());
  return pstate;
}

static void introfs_destroy(void* pdata) noexcept {
  try {
    IntrofsState* pstate = static_cast<IntrofsState*>(pdata);
    for (const auto& path : pstate->links) {
      log_printf("L\t%s\n", path.c_str());
    }
    for (const auto& path : pstate->ifiles) {
      log_printf("R\t%s\n", path.c_str());
    }
    for (const auto& path : pstate->ofiles) {
      log_printf("W\t%s\n", path.c_str());
    }
  } catch (...) {
  }
}

static int introfs_getattr(const char* path, struct stat* stbuf) {
  int res;

  res = lstat(path, stbuf);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_fgetattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
  int res;

  (void)path;

  res = fstat(fi->fh, stbuf);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_access(const char* path, int mask) {
  int res;

  res = access(path, mask);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_readlink(const char* path, char* buf, size_t size) {
  int res;

  auto* context = fuse_get_context();
  auto* pstate = static_cast<IntrofsState*>(context->private_data);

  pstate->links.insert(path);
  res = readlink(path, buf, size - 1);
  if (res == -1) return -errno;


  std::string redirected;
  if (buf[0] == '/') {  // absolute link
    redirected = pstate->mount_point + std::string(buf, res);
  } else {  // relative link
    const auto& dirname = [](const std::string& path) { return path.substr(0, path.rfind('/')); };
    redirected = pstate->mount_point + dirname(path).c_str() + "/" + std::string(buf, res);
  }

  if (size < (redirected.size() + 1)) {
    errno = ENAMETOOLONG;
    return -1;
  }
  memcpy(buf, redirected.c_str(), redirected.size());
  buf[redirected.size()] = '\0';

  return 0;
}

static int introfs_opendir(const char* path, struct fuse_file_info* fi) {
  DIR* dp = opendir(path);
  if (dp == nullptr) return -errno;

  fi->fh = (unsigned long)dp;
  return 0;
}

static int introfs_readdir(
    const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi) {
  DIR* dp = (DIR*)fi->fh;
  struct dirent* de;

  (void)path;
  seekdir(dp, offset);
  while ((de = readdir(dp)) != nullptr) {
    struct stat st = {};
    st.st_ino = de->d_ino;
    st.st_mode = de->d_type << 12;

    if (filler(buf, de->d_name, &st, telldir(dp))) break;
  }

  return 0;
}

static int introfs_releasedir(const char* path, struct fuse_file_info* fi) {
  DIR* dp = (DIR*)fi->fh;
  (void)path;
  closedir(dp);
  return 0;
}

static int introfs_mknod(const char* path, mode_t mode, dev_t rdev) {
  int res;

  if (S_ISFIFO(mode))
    res = mkfifo(path, mode);
  else
    res = mknod(path, mode, rdev);

  if (res == -1) return -errno;

  return 0;
}

static int introfs_mkdir(const char* path, mode_t mode) {
  int res;

  res = mkdir(path, mode);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_unlink(const char* path) {
  int res;

  res = unlink(path);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_rmdir(const char* path) {
  int res;

  res = rmdir(path);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_symlink(const char* from, const char* to) {
  int res;

  res = symlink(from, to);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_rename(const char* from, const char* to) {
  int res;

  res = rename(from, to);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_link(const char* from, const char* to) {
  int res;

  res = link(from, to);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_chmod(const char* path, mode_t mode) {
  int res;

  res = chmod(path, mode);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_chown(const char* path, uid_t uid, gid_t gid) {
  int res;

  res = lchown(path, uid, gid);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_truncate(const char* path, off_t size) {
  int res;

  res = truncate(path, size);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_ftruncate(const char* path, off_t size, struct fuse_file_info* fi) {
  int res;

  (void)path;

  res = ftruncate(fi->fh, size);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_utimens(const char* path, const struct timespec ts[2]) {
  int res;
  struct timeval tv[2];

  tv[0].tv_sec = ts[0].tv_sec;
  tv[0].tv_usec = ts[0].tv_nsec / 1000;
  tv[1].tv_sec = ts[1].tv_sec;
  tv[1].tv_usec = ts[1].tv_nsec / 1000;

  res = utimes(path, tv);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
  int fd;

  fd = open(path, fi->flags, mode);
  if (fd == -1) return -errno;

  try {
    auto* context = fuse_get_context();
    auto* pstate = static_cast<IntrofsState*>(context->private_data);
    pstate->ofiles.emplace(path);
  } catch (...) {
    return -1;
  }

  fi->fh = fd;
  return 0;
}

static int introfs_open(const char* path, struct fuse_file_info* fi) noexcept {
  int fd = open(path, fi->flags);
  if (fd == -1) return -errno;

  try {
    auto* context = fuse_get_context();
    auto* pstate = static_cast<IntrofsState*>(context->private_data);

    if (fi->flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) {
      pstate->ofiles.emplace(path);
    } else {
      pstate->ifiles.emplace(path);
    }

  } catch (...) {
    return -1;
  }

  fi->fh = fd;
  return 0;
}

static int introfs_read(
    const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
  int res;

  (void)path;
  res = pread(fi->fh, buf, size, offset);
  if (res == -1) res = -errno;

  return res;
}

static int introfs_write(
    const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
  int res;

  (void)path;
  res = pwrite(fi->fh, buf, size, offset);
  if (res == -1) res = -errno;

  return res;
}

static int introfs_statfs(const char* path, struct statvfs* stbuf) {
  int res;

  res = statvfs(path, stbuf);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_flush(const char* path, struct fuse_file_info* fi) {
  int res;

  (void)path;
  /* This is called from every close on an open file, so call the
     close on the underlying filesystem.  But since flush may be
     called multiple times for an open file, this must not really
     close the file.  This is important if used on a network
     filesystem like NFS which flush the data/metadata on close() */
  res = close(dup(fi->fh));
  if (res == -1) return -errno;

  return 0;
}

static int introfs_release(const char* path, struct fuse_file_info* fi) {
  (void)path;
  close(fi->fh);

  return 0;
}

static int introfs_fsync(const char* path, int isdatasync, struct fuse_file_info* fi) {
  int res;
  (void)path;

  (void)isdatasync;
  res = fsync(fi->fh);
  if (res == -1) return -errno;

  return 0;
}

static int introfs_setxattr(
    const char* path, const char* name, const char* value, size_t size, int flags) {
  int res = lsetxattr(path, name, value, size, flags);
  if (res == -1) return -errno;
  return 0;
}

static int introfs_getxattr(const char* path, const char* name, char* value, size_t size) {
  int res = lgetxattr(path, name, value, size);
  if (res == -1) return -errno;
  return res;
}

static int introfs_listxattr(const char* path, char* list, size_t size) {
  int res = llistxattr(path, list, size);
  if (res == -1) return -errno;
  return res;
}

static int introfs_removexattr(const char* path, const char* name) {
  int res = lremovexattr(path, name);
  if (res == -1) return -errno;
  return 0;
}

static int introfs_lock(const char* path, struct fuse_file_info* fi, int cmd, struct flock* lock) {
  (void)path;

  return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner, sizeof(fi->lock_owner));
}

static struct _introfs_operations : public fuse_operations {
  _introfs_operations() {
    this->init = introfs_init;
    this->destroy = introfs_destroy;

    this->getattr = introfs_getattr;
    this->fgetattr = introfs_fgetattr;
    this->access = introfs_access;
    this->readlink = introfs_readlink;
    this->opendir = introfs_opendir;
    this->readdir = introfs_readdir;
    this->releasedir = introfs_releasedir;
    this->mknod = introfs_mknod;
    this->mkdir = introfs_mkdir;
    this->symlink = introfs_symlink;
    this->unlink = introfs_unlink;
    this->rmdir = introfs_rmdir;
    this->rename = introfs_rename;
    this->link = introfs_link;
    this->chmod = introfs_chmod;
    this->chown = introfs_chown;
    this->truncate = introfs_truncate;
    this->ftruncate = introfs_ftruncate;
    this->utimens = introfs_utimens;
    this->create = introfs_create;
    this->open = introfs_open;
    this->read = introfs_read;
    this->write = introfs_write;
    this->statfs = introfs_statfs;
    this->flush = introfs_flush;
    this->release = introfs_release;
    this->fsync = introfs_fsync;
    this->setxattr = introfs_setxattr;
    this->getxattr = introfs_getxattr;
    this->listxattr = introfs_listxattr;
    this->removexattr = introfs_removexattr;
    this->lock = introfs_lock;

    this->flag_nullpath_ok = 0;
  }

} introfs_oper;

//
// `fstrace` works by using FUSE (filesystem in user space) to setup a new
// filesystem that mirrors the filesystem rooted under the / directory. The new
// filesystem serves content from the original filesystem while keeping track of
// file opens and creates.
//
// When `fstrace` is invoked inside a directory `dir`, it first sets up the
// mirroring-introspecting filesystem and then proceeds to spawn a delegate-tool
// chrooted inside the mirrored copy of `dir`. This allows fstrace to monitor
// the file operations performed by delegate-tool and its subprocesses.
//
// `fstrace` then waits for the spawned delegate-tool to complete and then sets up
// a lazy unmount of the mirroring filesystem with an invocation of
// `fusermount -uz`
//
// Implementation detail:
// ----------------------
//
// The sequence of filesystem setup, make invocation and filesystem unmount
// could possibly have been written as a shell script. However, we need some
// fine grained synchronization that is best achieved in code with some system
// calls. This synchronization is described next.
//
// After delegate-tool is spawned, it needs to pause till the FUSE filesystem is
// setup by the parent `fstrace` process. Otherwise it will begin building
// inside a non-existent directory tree. In code, this is achieved by making the
// spawned delegate-tool process pause util a signal is sent. The parent
// `fstrace` process sends this signal on filesystem init.
//

// fstrace configuration.
// TODO: Maybe load this from a config file in the future.
static struct Configuration {
  const char* mount_point;
  const char* log_filepath;

  Configuration(
      const char* mount_point_ = "/tmp/__introfs__",
      const char* log_filepath_ = "/tmp/__introfs__.log")
      : mount_point(mount_point_), log_filepath(log_filepath_) {}

  std::string get_mirrored_path(const std::string& path) const {
    return std::string(mount_point) + "/" + path;
  }

} config;

void* fuse_ops_thread_func(void* pstate) {
  static char* args[] = {
      const_cast<char*>("introfs"),
      const_cast<char*>(config.mount_point),
      const_cast<char*>("-f"),
  };

  fuse_main(array_size(args), args, &introfs_oper, pstate);
  return nullptr;
}

void setup_fs_and_wait_for_child(const pid_t& child_pid) {
  ensure_mount_point(config.mount_point);

  IntrofsState state(child_pid, config.log_filepath, config.mount_point);

  //
  // spawn a thread to handle FUSE events
  //
  pthread_t fuse_ops_thread;
  int err = pthread_create(&fuse_ops_thread, nullptr, fuse_ops_thread_func, &state);
  if (err != 0) throw SystemException("pthread_create failed", err);

  //
  // Meanwhile wait for the spawned delegate-tool process to complete
  //
  while (true) {
    int wstatus;
    int cid = waitpid(child_pid, &wstatus, WNOHANG | WUNTRACED);

    if (cid == 0)
      /* The waitpid call would have hanged. The child process
       is possibly not executing. try again */
      continue;

    if (cid == -1) continue;

    if (WIFEXITED(wstatus) || WIFSIGNALED(wstatus)) /* The child actually terminated */
      break;
  }

  //
  // Lazy unmount the introfs filesystem
  //
  system((std::string("fusermount -uz ") + config.mount_point).c_str());
}

/*
 * No-op signal handler
 */
void on_user_signal1(int) { return; }

void spawn_delegate_tool(const char* tool_name, char* tool_argv[]) {
  //
  // Prepare to be woken up by the `fstrace` parent process
  // and then pause for a signal
  //
  signal(SIGUSR2, on_user_signal1);
  pause();

  //
  // Change directory to the mirrored copy.
  // chroot to the mount point.
  //
  const auto& curdir = get_current_dir();
  change_dir(config.get_mirrored_path(curdir));

  // Cannot chroot!
  // if (chroot(config.mount_point)) {
  //   perror("chroot failed!");
  //   throw SystemException("chroot failed", errno);
  // }

  //
  // spawn sub process
  //
  if (execvp(tool_name, tool_argv) == -1) {
    throw SystemException("Failed to spawn sub process", errno);
  }
}

//
// This tool should be invoked as:
// ```sh
//  fstrace <cmd> [args...]
// ```
//
int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "USAGE: %s <cmd> [<args>...]\n", argv[0]);
    return -1;
  }

  umask(0);  // TODO: Do we need this?

  const pid_t child_pid = fork();
  if (child_pid > 0) {
    setup_fs_and_wait_for_child(child_pid);
  } else if (child_pid == 0) /* this is the child */ {
    spawn_delegate_tool(argv[1], argv + 1);
  } else {
    throw SystemException("fork() failed", errno);
  }

  return 0;
}
