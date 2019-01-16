#!/bin/bash

sambaspec=~/rpmbuild/SPECS/samba.spec
sambabuild=$(ls -d ~/rpmbuild/BUILD/samba-[0-9.]*[0-9] 2>/dev/null)
if [[ ! -d $sambabuild ]]; then
  cd /tmp
  yumdownloader --source samba
  
  rpm -i samba-*.src.rpm
  yum-builddep -y $sambaspec

  if false; then		# brute force: full, lengthy build
    rpmbuild -bc $sambaspec
  else
    # an alternative to the full "rpmbuild ~/rpmbuild/SPECS/samba.spec"  
    rpmbuild -bp $sambaspec
    sambabuild=$(ls -d ~/rpmbuild/BUILD/samba-[0-9.]*[0-9])
    sed -i -e '/^all:/ a \\t@echo not running make \nall2:' $sambabuild/Makefile	# stop after configure
    rpmbuild -bc --short-circuit $sambaspec || exit			# do the configure without build
  fi
  
fi

rpmbuild -bb ~/rpmbuild/SPECS/samba_richacl.spec