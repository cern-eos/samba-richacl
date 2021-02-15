# give a chance to specify a specific version instead of the installed samba version, e.g. for src package
# as in rpmbuild -bs --define "smbversion 0.0.0" ~/rpmbuild/SPECS/samba_richacl.spec 
%if 0%{?smbversion:1}
%global samba_version %{smbversion}
%else
%define xsmbvers %( x=$(ls -d %{_builddir}/samba-[0-9]*[0-9]/source3 2>/dev/null|head -1); x=${x#*samba-}; echo ${x%%/source3} )
%if "x%{?xsmbvers:}" != "x"
%global samba_version %{xsmbvers}
%else
# samba not installed, no problem if this is an arc package build
%global samba_version 0.0.0
%endif
%endif

%global samba_builddir samba-%{samba_version}
%if 0
%{echo: samba_version %{samba_version}}
%endif

Name:           samba_richacl
Version:        %{samba_version}
Release:        2

%if 0%{?rhel}
Epoch:          0
%else
Epoch:          2
%endif

%if 0%{?epoch} > 0
%define samba_depver %{epoch}:%{samba_version}
%else
%define samba_depver %{samba_version}
%endif

Summary:        Samba VFS module implementing "richacl" ACL support
License:        GPLv3+ and LGPLv3+
URL:            http://www.samba.org/

Source0:        vfs_richacl.c
Source1:	samba_richacl_build.sh

Requires(post): systemd
Requires(preun): systemd
Requires(postun): systemd

Requires(pre): %{name}-common = %{samba_depver}
Requires: %{name}-lib = %{samba_depver}

Requires: librichacl

BuildRequires: sed
BuildRequires: librichacl-devel


%description
Samba is the standard Windows interoperability suite of programs for Linux and
Unix. samba_richacl is a module supporting RICHACLs as Windows ACLs

To build this package you need to install the src rpm and run the samba_richacl_build.sh
script in the SOURCES directory (this builds parts of Samba and the smb_richacl-lib rpm)

for example:

$ rpm -i samba_richacl-0.0.0-0.src.rpm
# specifying the desired samba version helps when several are available:
$ sh ~/rpmbuild/SOURCES/samba_richacl_build.sh [ samba-4.9.1 ]

To enable for a share in samba, add the following to smb.conf for the share in question:
    vfs objects = richacl
    nfs4: acedup = merge

%package lib
Summary: vfs library
%description lib
The lib package implements richacl functionality in samba.

To enable for a share in samba, add the following to smb.conf for the share in question:
    vfs objects = richacl
    nfs4: acedup = merge


%prep
echo SOURCE0 %{SOURCE0}
echo _builddir %{_builddir}
%define samba_mod %{samba_builddir}/source3/modules
%define wscript_build %{samba_mod}/wscript_build
echo wscript_build %{wscript_build}

echo cp %{SOURCE0} %{samba_mod}
cp %{SOURCE0} %{samba_mod}
sed -i -e '/vfs_richacl_begin/,/vfs_richacl_end/d' %{wscript_build}
cat >> %{wscript_build} <<EOFwscript
#vfs_richacl_begin
#import pdb
#pdb.set_trace()
if 'vfs_richacl' not in bld.env['shared_modules']: bld.env['shared_modules'].append('vfs_richacl')
if hasattr(bld.env, '_get_list_value_for_modification'): bld.env._get_list_value_for_modification('LINKFLAGS').append('-lrichacl')
else: bld.env['LINKFLAGS'].append('-lrichacl')
bld.SAMBA3_MODULE('vfs_richacl', subsystem='vfs', source='vfs_richacl.c', deps='NFS4_ACLS', init_function='vfs_richacl_init',
internal_module=bld.SAMBA3_IS_STATIC_MODULE('vfs_richacl'), enabled=bld.SAMBA3_IS_ENABLED_MODULE('vfs_richacl'), cflags='-lrichacl')
#vfs_richacl_end
EOFwscript

%build

%define samba_mod %{samba_builddir}/source3/modules
pwd
echo samba_mod %{samba_mod}
cd %{samba_mod}/../..
pwd
make reconfigure || echo "samba make reconfigure error ignored"
./buildtools/bin/waf --targets=vfs_richacl



%install
rm -rf %{buildroot}

echo _libdir %{_libdir}

%define samba_mod %{samba_builddir}/source3/modules
install -D -m 0755 %{samba_mod}/../../bin/default/source3/modules/libvfs_module_richacl.so %{buildroot}/usr/lib64/samba/vfs/richacl.so


%files lib
%defattr(-,root,root,-)
/usr/lib64/samba/vfs/richacl.so

%post

%preun

%postun

%pre lib
: getent group printadmin >/dev/null || groupadd -r printadmin || :

%post lib
: /sbin/ldconfig

%postun lib

%clean
rm -rf %{buildroot}
