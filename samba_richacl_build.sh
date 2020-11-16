#!/bin/bash


# easiest way to install the richacl module for, say, samba-4.9.1
# . obtain (yumdownloader --source samba-4.9.1) the samba srpm, and install it (rpm -i samba-4.9.1.src.rpm)
# . samba_richacl_build.sh samba-4.9.1 srpm, note the version of the srpm created
# . rpm -i ~/rpmbuild/SRPMS/samba_richacl-4.9.1-X.src.rpm   <--- X == version
# . rpmbuild -bb ~/rpmbuild/SPECS/samba_richacl.spec
# . rpm -i the newly created rpm


sambaver=${1-samba}		# e.g. samba-4.9.1-6.el7, samba-4.12.10 for "tgz" build
dir=$(dirname $0)

sambaspec=~/rpmbuild/SPECS/samba.spec

build_from_tgz() {      # download samba.tar.gz and create srpm

set -x
    # decent place to find seminal samba spec files in fedora, e.g.
    # https://dl.fedoraproject.org/pub/fedora/linux/development/rawhide/Everything/source/tree/Packages/s/samba-4.12-10.fc34.src.rpm
    sver=${sambaver##samba-}
    iniSpec="$3"
    [[ -n "$3" ]] || { 
        case $sver in 
            4.11.*)  iniSpec=samba.spec.4.11.x ;;
            4.12.*)  iniSpec=samba.spec.4.12 ;;
            *) echo Error, need initial spec file; exit 1 
                ;;
        esac
    }

    cp $iniSpec $sambaspec || { echo Error, failed to copy initial spec file; exit 1; }


    sed -i -r \
            -e "/(global|define) samba_version .*/s//\1 samba_version ${sver}/" \
            -e"/xzcat/s//zcat/"  \
            -e "/widelinks./d" \
            -e "/dnsresolver./d" \
            -e "/exclude .*mandir.*vfs_ceph/d" \
            -e "/gp_scripts_ext./d" \
            $sambaspec

    sed -i  \
            -e "/{_make_verbose}/s//{?_make_verbose}/" \
            $sambaspec

    #sed -i -r \
    #        -e "/_bindir.\/winexe/a%{_mandir}/man1/winexe.1.gz\\n" \
    #        $sambaspec


    [[ $sver < 4.13.0 ]] && {
        sed -i -e '/global required_mit_krb5 .*/s//global required_mit_krb5 1.17/' $sambaspec
        sed -i -e '/global ldb_version .*/s//global ldb_version 2.1.4/' $sambaspec
    }



    # download the sources and build srpm
    rpmbuild --undefine=_disable_source_fetch -bs $sambaspec || exit
    echo now run:  rpmbuild -bb $sambaspec

    exit 0
}








[[ -n "$2" ]] && {
    case $2 in 
        srpm)           # create the source rpm from files in cwd
            cp $dir/vfs_richacl.c $0 ~/rpmbuild/SOURCES
            cp $dir/samba_richacl.spec ~/rpmbuild/SPECS

            rpmbuild -bs ~/rpmbuild/SPECS/samba_richacl.spec
            exit;;

        tgz)            # download samba.tar.gz and create srpm, need seminal spec file
            # e.g.: $0 samba-4.12.10 tgz ./samba.initial.spec
            build_from_tgz $*
            exit;;

        *)
            echo "'$2' invalid"
            exit 1;;
    esac
}



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
