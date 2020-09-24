#!/bin/bash


# easiest way to install the richacl module for, say, samba-4.9.1
# . obtain (yumdownloader --source samba-4.9.1) the samba srpm, and install it (rpm -i samba-4.9.1.src.rpm)
# . samba_richacl_build.sh samba-4.9.1 srpm, note the version of the srpm created
# . rpm -i ~/rpmbuild/SRPMS/samba_richacl-4.9.1-X.src.rpm   <--- X == version
# . rpmbuild -bb ~/rpmbuild/SPECS/samba_richacl.spec
# . rpm -i the newly created rpm


sambaver=${1-samba}		# e.g. samba-4.9.1-6.el7
dir=$(dirname $0)

[[ -n "$2" ]] && {
    case $2 in 
        srpm)           # create the source rpm from files in cwd
            cp $dir/vfs_richacl.c $0 ~/rpmbuild/SOURCES
            cp $dir/samba_richacl.spec ~/rpmbuild/SPECS

            rpmbuild -bs ~/rpmbuild/SPECS/samba_richacl.spec
            exit;;

        *)
            echo "'$2' invalid"
            exit 1;;
    esac
}

sambaspec=~/rpmbuild/SPECS/samba.spec

if [[ $sambaver = samba ]]; then
    sambabuild=$(ls -d ~/rpmbuild/BUILD/samba-[0-9.]*[0-9] 2>/dev/null)
    sambaver=${sambabuild##*BUILD/}
else
    sambabuild=~/rpmbuild/BUILD/$sambaver
fi

if [[ ! -d $sambabuild ]]; then
  cd /tmp
  yumdownloader --url --source $sambaver
  yumdownloader --source $sambaver
  
  rpm -i ${sambaver}*.src.rpm
  yum-builddep -y $sambaspec

  if false; then		# brute force: full, lengthy build
    rpmbuild -bc $sambaspec
  else
    # an alternative to the full "rpmbuild ~/rpmbuild/SPECS/samba.spec"  
    rpmbuild -bp $sambaspec
    sed -i -e '/^all:/ a \\t@echo not running make \nall2:' $sambabuild/Makefile	# stop after configure

    # prereqs not caught by yum-builddep
    pre0=()
    # ldb-mess
    case ${sambaver} in
        samba-4.7*) ldbvers=1.2.2;;
        samba-4.8*) ldbvers=1.3.4;;
        samba-4.9*) ldbvers=1.4.2;;
        samba-4.1[0-9]*) pre0+=(python3 lmdb lmdb-devel gpgme-devel); ldbvers=1.5.4;;
    esac

    xx=$(rpm -q pyldb) && [[ "$xx" != pyldb-${ldbvers}* ]] && {
       rpm -e libldb libldb-devel pyldb pyldb-devel --nodeps
    }

    [[ "$xx" = pyldb-${ldbvers}* ]] || {
       yum install -y pyldb-$ldbvers pyldb-devel-$ldbvers libldb-$ldbvers libldb-devel-$ldbvers
    }

    pre=()
    for p in ${pre0[*]} ; do rpm -q $p || pre+=($p);  done
    [[ ${#pre[*]} -gt 0 ]] && yum install -y ${pre[*]}

    rpmbuild -bc --short-circuit $sambaspec || exit			# do the configure without build
  fi
  
fi

rpmbuild -bb ~/rpmbuild/SPECS/samba_richacl.spec
