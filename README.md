intromake
=========

An introspecting `make` that records I/O activity

`intromake` sets up a `FUSE` (Filesystem in user space) mirror of the
directory it is invoked in. It then spawns a make in the mirrored `FUSE`
filesystem. The `FUSE` layer allows intercepting file accesses. This
inturn allows us to examine what files were required to compile a project.

Overall `intromake` is useful for extracting header file dependencies,
source generator tool dependencies etc.
