# syntax=docker/dockerfile:1

# The VirtualBox layer over the runtime image, converted into a bootable disk
# by d2vm (scripts/build_vm_image.sh). d2vm adds the kernel, systemd and a
# bootloader itself; this layer adds only what a machine someone logs into
# needs and a container does not.
ARG PEREDUR_IMAGE=peredur:local
FROM ${PEREDUR_IMAGE}

USER root

# Empty leaves the sources alone. A mirror set here stays in the VM's sources,
# since d2vm's own kernel install runs after this layer and reads them too.
ARG UBUNTU_MIRROR=
RUN if [ -n "${UBUNTU_MIRROR}" ]; then \
        sed -i -E "s#^URIs: http://(archive|security)\.ubuntu\.com/ubuntu/?\$#URIs: ${UBUNTU_MIRROR%/}/#" \
            /etc/apt/sources.list.d/ubuntu.sources; \
    fi

# sshd is how output leaves the VM: the container's bind mount has no
# equivalent short of VirtualBox guest additions, which need a kernel module
# build inside the guest. The runtime image ships no editor or pager either.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        less \
        libpam-systemd \
        nano \
        openssh-server \
        sudo \
    && rm -rf /var/lib/apt/lists/*

# A client's LANG and LC_* would otherwise be accepted into sessions on a guest
# with no locales generated, and every command would warn that it cannot set
# them.
RUN sed -i 's/^AcceptEnv/#&/' /etc/ssh/sshd_config

# initramfs-tools records the swap of whatever machine builds the initramfs as
# the resume device, which here is d2vm's build host, and the guest then waits
# 30 s at every boot for a device it does not have.
RUN mkdir -p /etc/initramfs-tools/conf.d \
    && echo 'RESUME=none' > /etc/initramfs-tools/conf.d/resume

# Image ENV is container configuration, not a file, and does not survive the
# conversion. RUN sees it as its own environment, so writing it out here keeps
# the runtime stage's ENV block the one place these paths are set. pam_env
# reads /etc/environment for console and ssh sessions alike, including a
# non-interactive `ssh vm peredur ...`, which a profile.d script would miss.
RUN env | grep -E '^(PATH|PEREDUR_[A-Z_]+)=' > /etc/environment

# The password is public by design, being written in docs/vm.md; the forwarded
# ssh port binds to the host's loopback only.
RUN usermod --shell /bin/bash --append --groups sudo peredur \
    && echo 'peredur:peredur' | chpasswd

RUN mkdir -p /etc/systemd/system/getty@tty1.service.d \
    && printf '%s\n' '[Service]' 'ExecStart=' \
        'ExecStart=-/sbin/agetty --autologin peredur --noclear %I $TERM' \
        > /etc/systemd/system/getty@tty1.service.d/autologin.conf

# Host keys generated at package install would be shared by every VM imported
# from one image, so they are deleted and each VM makes its own at first boot.
RUN rm -f /etc/ssh/ssh_host_* \
    && printf '%s\n' '[Unit]' 'Description=Generate missing SSH host keys' \
        'Before=ssh.service' '' \
        '[Service]' 'Type=oneshot' 'ExecStart=/usr/bin/ssh-keygen -A' '' \
        '[Install]' 'WantedBy=multi-user.target' \
        > /etc/systemd/system/ssh-host-keys.service \
    && ln -s /etc/systemd/system/ssh-host-keys.service \
        /etc/systemd/system/multi-user.target.wants/ssh-host-keys.service

# systemd generates a machine id on first boot when the file is empty.
RUN : > /etc/machine-id

COPY motd /etc/motd
