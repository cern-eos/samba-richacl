## Samba-RichACL VFS library

This repository holds the Samba VFS plugin to support **Rich ACLs** in EOS.

The credits for the code go to the original authors as mentioned in the source file,
as well as to Rainer Toebbicke for the adaptations to EOS.

The plugin can be enabled in a Samba setup by adding the following to `smb.conf`:

```
    vfs objects = richacl
    nfs4: acedup = merge
```

The `build-samba.txt` file illustrates how to build Samba and the RichACL module when prerequisites are not
easily met and are complicated to build. The exact packages that were required may change, of course, the
rpms built here are those required when building Samba 4.13.5 on vanilla CentOS 8. The compilation with
a recent 4.14 has not yet been attempted and it likely breaks.

The strategy was:
* download a modern samba.src.rpm
* run yum-buildep to find out what packages were not easily resolved from repositories
* find and download their src.spms somewhere (usually Fedora)
* find and download their pre-requisites (may be a recursive process)
* build them all


### License

EOS - The CERN Disk Storage System

Copyright (C) 2021 CERN/Switzerland

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see http://www.gnu.org/licenses/.
