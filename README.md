# intromake
An introspecting `make` that records I/O activity

`intromake` sets up a FUSE mirror of the directory it is invoked in.
It then spawns a make in the mirrored FUSE filesystem. The FUSE layer
allows intercepting file accesses. This allows us to examine what files
were required to compile a project.
