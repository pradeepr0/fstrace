fstrace
========

A tool for intercepting and logging files accessed by a process.

`fstrace` sets up a FUSE (Filesystem in user space) mirror of the entire
rooted filesystem at `/`. It then spawns the specified command with in the
mirrored FUSE filesystem. As a result, relative file paths accessed by the
(sub-)command go through the mirrored FUSE filesystem and can be
intercepted and logged.


Building from source
--------------------
Install required libraries:
```sh
sudo apt-get install libfuse-dev
```
Build from sources:
```sh
# from within the cloned repository folder
mkdir bin
make
```
You will also need to enable `root` to access user created filesystems: edit
`/etc/fuse.conf` and uncomment the line `user_allow_other`.


Usage
-----
```sh
fstrace <cmd> [<args>...]
```

## Example:
```sh
fstrace touch sample_file.txt
```
When the command executes, the FUSE filesystem is mounted on
`/home/lyft/__introfs__`. The file access log is written to
`/home/lyft/__introfs__.log` when the command completes.

The file access log contains two columns separated by a TAB character, as below:
```tsv
# type	path
L	/symlink/accessed/by/command
R	/path/to/file/read/by/command
W	/path/to/file/written/by/command
```

The access log is overwritten by each new invocation of `fstrace`.


Intercepting every file path access
-----------------------------------
`fstrace` intercepts relative path accesses only. This means that if your
command accesses absolute paths (such as `/tmp/data.txt`), those accesses will
not be intercepted and logged. Often, information about the relative paths
accessed by a process is sufficient.

To intercept *all* file accesses, we will need to combine FUSE based tracing
with *chroot* style sandboxing: execute the process as if the FUSE filesystem
at `/home/lyft/__introfs__` is the root filesystem mounted on `/`. A simple
way to do this is to containerize your process with Docker along with
appropriate bind mounts.

For example,
```sh
fstrace docker run -it --rm \
    -u $(id -u):$(id -g) \
    -v /home/lyft/__introfs__/bin:/bin \
    -v /home/lyft/__introfs__/cdrom:/cdrom \
    -v /home/lyft/__introfs__/etc:/etc \
    -v /home/lyft/__introfs__/home:/home \
    -v /home/lyft/__introfs__/lib:/lib \
    -v /home/lyft/__introfs__/lib32:/lib32 \
    -v /home/lyft/__introfs__/lib64:/lib64 \
    -v /home/lyft/__introfs__/libx32:/libx32 \
    -v /home/lyft/__introfs__/media:/media \
    -v /home/lyft/__introfs__/opt:/opt \
    -v /home/lyft/__introfs__/run:/run \
    -v /home/lyft/__introfs__/sbin:/sbin \
    -v /home/lyft/__introfs__/snap:/snap \
    -v /home/lyft/__introfs__/srv:/srv \
    -v /home/lyft/__introfs__/sys:/sys \
    -v /home/lyft/__introfs__/usr:/usr \
    -v /home/lyft/__introfs__/var:/var \
  ubuntu:16.04 \
cat /etc/crontab
```
should leave entries for `/etc/crontab`, `/bin/cat` etc., in the access log at
`/home/lyft/__introfs__.log`.

It is possible to use *chroot* + bind mounts for sandboxing; but containerizing
your process using docker is certainly a friendlier option.


Troubleshooting
---------------
If something goes really wrong and causes `fstrace` to hang, you can explicitly
kill the command launched by `fstrace` and then force unmount the filesystem
with
```sh
umount -lf "$HOME/__introfs__"
```


Disclaimer
----------
I haven't tested the FUSE filesystem implementation to verify that all
operations work as expected. At this point, this is a best effort
implementation.
