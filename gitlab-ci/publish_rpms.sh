#!/usr/bin/env bash

kinit stci@CERN.CH -k -t /stci.krb5/stci.keytab

export_rpms() {   # args: source targetarch
    EXPORT_DIR=/eos/project/s/storage-ci/www/samba-next/$2
    echo "Publishing from $1 in location: ${EXPORT_DIR}"
    mkdir -p ${EXPORT_DIR}/SRPMS ${EXPORT_DIR}/x86_64
    cp $1/*src.rpm ${EXPORT_DIR}/SRPMS/
    cp $1/*x86_64.rpm ${EXPORT_DIR}/x86_64/
    cp $1/*noarch.rpm ${EXPORT_DIR}/noarch/
    createrepo -q ${EXPORT_DIR}/
}

#export_rpms RPMS_cc7 el-7
export_rpms RPMS el-8
