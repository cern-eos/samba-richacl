#!/usr/bin/env bash

kinit stci@CERN.CH -k -t /stci.krb5/stci.keytab

export_rpms() {   # args: source targetarch
    EXPORT_DIR=/eos/project/s/storage-ci/www/samba-next/$2
    echo "Publishing from $1 in location: ${EXPORT_DIR}"
    mkdir -p ${EXPORT_DIR}/SRPMS ${EXPORT_DIR}/x86_64
    cp $1/RPMS/*src.rpm ${EXPORT_DIR}/SRPMS/
    cp $1/RPMS/*x86_64.rpm ${EXPORT_DIR}/x86_64/
    createrepo -q ${EXPORT_DIR}/x86_64/
}

export_rpms RPMS_cc7 el-7
export_rpms RPMS_c8  el-8

