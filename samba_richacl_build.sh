#!/bin/bash

sambaver=${1-samba}		# e.g. samba-4.9.1-6.el7

sambaspec=~/rpmbuild/SPECS/samba.spec

if [[ $sambaver = samba ]]; then
    sambabuild=$(ls -d ~/rpmbuild/BUILD/samba-[0-9.]*[0-9] 2>/dev/null)
    sambaver=${sambabuild##*BUILD/}
else
    sambabuild=~/rpmbuild/BUILD/$sambaver
fi

if [[ ! -d $sambabuild ]]; then
  cd /tmp
  yumdownloader --source $sambaver
  
  rpm -i ${sambaver}.src.rpm
  yum-builddep -y $sambaspec

  if false; then		# brute force: full, lengthy build
    rpmbuild -bc $sambaspec
  else
    # an alternative to the full "rpmbuild ~/rpmbuild/SPECS/samba.spec"  
    rpmbuild -bp $sambaspec
    sed -i -e '/^all:/ a \\t@echo not running make \nall2:' $sambabuild/Makefile	# stop after configure
    rpmbuild -bc --short-circuit $sambaspec || exit			# do the configure without build
  fi
  
fi

rpmbuild -bb ~/rpmbuild/SPECS/samba_richacl.spec
