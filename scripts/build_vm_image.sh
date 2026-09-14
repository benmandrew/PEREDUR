#!/usr/bin/env bash
# Build a VirtualBox appliance (.ova) from a PEREDUR Docker image.
#
#     scripts/build_vm_image.sh <peredur-image> [output-dir]
#
# Layers docker/vm/Dockerfile over the image, converts the result to a disk with
# d2vm, and wraps the disk in an appliance with VBoxManage. Writes
# <output-dir>/peredur.vdi and <output-dir>/peredur.ova (default build-vm/).
#
# VM_DISK_SIZE (20G), VM_MEMORY_MB (8192) and VM_CPUS (4) size the appliance;
# the disk is dynamically allocated, so its size is a ceiling rather than a cost.
# UBUNTU_MIRROR replaces archive.ubuntu.com and security.ubuntu.com in the
# image's apt sources, for a network where Canonical's own hosts are slow or
# unreachable; d2vm installs the kernel through those sources.
set -euo pipefail

d2vm_image=linkacloud/d2vm:v0.4.0

image=${1:?usage: $0 <peredur-image> [output-dir]}
out_dir=${2:-build-vm}
disk_size=${VM_DISK_SIZE:-20G}
memory_mb=${VM_MEMORY_MB:-8192}
cpus=${VM_CPUS:-4}

repo_root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$out_dir"
out_dir=$(cd "$out_dir" && pwd)
vm_layer=peredur-vm:build-$$

for tool in docker VBoxManage; do
    command -v "$tool" >/dev/null || { echo "$tool not found on PATH" >&2; exit 1; }
done

docker build --build-arg PEREDUR_IMAGE="$image" --build-arg UBUNTU_MIRROR="${UBUNTU_MIRROR:-}" \
    -t "$vm_layer" "$repo_root/docker/vm"

# d2vm drives the host's daemon through the socket and needs loop devices to
# partition the disk, hence --privileged. It writes as root, so the files are
# handed back to the invoking user afterwards. amd64 is stated rather than left
# to d2vm's default: it is the one platform VirtualBox runs Ubuntu guests on
# across its hosts, and an arm64 PEREDUR image would otherwise fail late. The
# hostname needs its own hosts entry, Docker replacing the image's /etc/hosts
# with one of its own, or sudo fails to resolve it.
rm -f "$out_dir/peredur.vdi" "$out_dir/peredur.ova"
docker run --rm --privileged \
    -v /var/run/docker.sock:/var/run/docker.sock \
    -v "$out_dir:/out" -w /out \
    "$d2vm_image" convert "$vm_layer" \
    --output /out/peredur.vdi --size "$disk_size" --platform linux/amd64 \
    --hostname peredur --add-host peredur:127.0.1.1
docker run --rm -v "$out_dir:/out" --entrypoint chown "$d2vm_image" \
    "$(id -u):$(id -g)" /out/peredur.vdi
docker image rm "$vm_layer" >/dev/null

# The appliance is assembled in a throwaway VirtualBox registration, removed on
# exit whatever happens. unregistervm is deliberately not given --delete, which
# would delete the attached disk too.
vm_name=peredur-vm-build-$$
base_dir=$(mktemp -d)
cleanup() {
    VBoxManage unregistervm "$vm_name" >/dev/null 2>&1 || true
    VBoxManage closemedium disk "$out_dir/peredur.vdi" >/dev/null 2>&1 || true
    rm -rf "$base_dir"
}
trap cleanup EXIT

VBoxManage createvm --name "$vm_name" --ostype Ubuntu_64 --basefolder "$base_dir" --register
VBoxManage modifyvm "$vm_name" \
    --memory "$memory_mb" --cpus "$cpus" --firmware bios \
    --graphicscontroller vmsvga --audio none \
    --nic1 nat --natpf1 "ssh,tcp,127.0.0.1,2222,,22"
VBoxManage storagectl "$vm_name" --name SATA --add sata --controller IntelAhci --portcount 1
VBoxManage storageattach "$vm_name" --storagectl SATA --port 0 --device 0 \
    --type hdd --medium "$out_dir/peredur.vdi"
VBoxManage export "$vm_name" --output "$out_dir/peredur.ova" \
    --vsys 0 --vmname peredur --product peredur \
    --description "PEREDUR from $image"
# VBoxManage writes the appliance 0600, which is wrong for a file made to share.
chmod 644 "$out_dir/peredur.ova"

echo "Wrote $out_dir/peredur.ova"
