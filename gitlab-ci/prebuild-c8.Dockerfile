FROM gitlab-registry.cern.ch/linuxsupport/c8-base

LABEL maintainer="Fabio Luchetti, faluchet@cern.ch, CERN 2021"

WORKDIR /builds/dss/samba-richacl/

# If the working directory is a not the top-level dir of a git repo OR git remote is not set to the EOS repo url.
# On Gitlab CI, the test won't (and don't have to) pass.
RUN dnf install --nogpg -y git && dnf clean all \
    && if [[ $(git rev-parse --git-dir) != .git ]] || [[ $(git config --get remote.origin.url) != *gitlab.cern.ch/dss/samba-richacl.git ]]; \
        then git clone https://gitlab.cern.ch/dss/samba-richacl.git . ; fi

COPY *8.repo /etc/yum.repos.d

RUN dnf install -y epel-release \
    && dnf install --nogpg -y $(cat ./dnf-install-list) \
    && dnf install -y ccache moreutils \
    && dnf clean all
# install moreutils just for 'ts', nice to benchmark the build time.
# cleaning yum cache should reduce image size.

ENTRYPOINT /bin/bash
