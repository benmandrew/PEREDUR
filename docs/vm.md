# Virtual machine

A VirtualBox *appliance* can be built from the counter Docker image, for a user who wants a virtual machine (VM) rather than a container. It boots the same `/opt/counter` tree the image carries, with the same binaries, solvers and bundled examples. On top of that it adds what a machine someone logs into needs: a kernel, systemd and a Secure Shell (SSH) server. The container itself is covered in [`docker.md`](docker.md), and everything below starts from an image built there.

## Building the appliance

The image has to exist locally first. `docker pull benmandrew/peredur:<sha>` fetches a published one, and a local build as described in [Building the image](docker.md#building-the-image) works the same way when tagged with the short commit sha:

```console
$ docker build --build-arg COUNTER_GIT_COMMIT="$(git rev-parse HEAD)" \
      -t benmandrew/peredur:$(git rev-parse --short HEAD) .
$ scripts/build_vm_image.sh benmandrew/peredur:$(git rev-parse --short HEAD)
```

The script takes the image as its first argument and an output directory as an optional second, defaulting to `build-vm/`. That directory is gitignored with the rest of the build trees. The script writes `counter.vdi` there, the VirtualBox Disk Image (VDI), and `counter.ova`, the appliance packaged as an Open Virtualization Format (OVF) archive.

The build host needs Docker and VirtualBox's `VBoxManage` on `PATH`. It builds for amd64 only. Whoever imports the resulting `.ova` needs VirtualBox alone.

## What the build does

The pipeline has three steps, and the script runs them in order.

**Layering.** [`docker/vm/Dockerfile`](../docker/vm/Dockerfile) builds a layer over the runtime image. It installs `openssh-server`, `sudo`, `less`, `nano` and `libpam-systemd`, sets up the login, and writes the image's environment out to a file. It also stops sshd accepting a client's locale variables and sets `RESUME=none` for initramfs-tools. [Inside the VM](#inside-the-vm) covers what each of those is for.

**Conversion.** [d2vm](https://github.com/linka-cloud/d2vm) converts that image into a bootable disk. It adds a kernel (`linux-image-virtual`), systemd, the syslinux bootloader for a basic input/output system (BIOS), and netplan networking configured by the Dynamic Host Configuration Protocol (DHCP). It runs as a container rather than an installed tool, with `--privileged` and the host's Docker socket mounted in. The socket lets it drive the host's daemon, and the privilege gives it the *loop devices* it partitions the disk through. It writes as root, so the script hands the disk back to the invoking user afterwards. The script also passes d2vm `--add-host counter:127.0.1.1`, because Docker replaces the image's `/etc/hosts` with its own and a hosts entry written in the Dockerfile does not survive. Without it `sudo` printed "unable to resolve host counter".

**Packaging.** `VBoxManage` registers a throwaway VM around the disk and exports it as the appliance. The appliance gets 8192 MB of memory, 4 central processing units (CPUs) and BIOS firmware. Its disk hangs off a Serial AT Attachment (SATA) controller in Advanced Host Controller Interface (AHCI) mode. Its one network adapter uses network address translation (NAT), with a *port forward* from 127.0.0.1:2222 on the host to port 22 in the guest. The registration is removed when the script exits, whether or not the export succeeded.

d2vm is pinned at `linkacloud/d2vm:v0.4.0`. Its `latest` tag is a moving development build, which reported itself as `v0.4.0-dev` when it was tried, so an unpinned build could change under a script that had not.

## Sizing and the mirror

Four environment variables adjust the build.

| Variable | Default | Effect |
|---|---|---|
| `VM_DISK_SIZE` | `20G` | the disk's maximum size |
| `VM_MEMORY_MB` | `8192` | the appliance's memory |
| `VM_CPUS` | `4` | the appliance's CPU count |
| `UBUNTU_MIRROR` | empty | an Ubuntu archive mirror for the image's apt sources |

The disk is *dynamically allocated*, so `VM_DISK_SIZE` is a ceiling rather than a cost. The file grows with what the guest writes, up to that size.

`UBUNTU_MIRROR` replaces `archive.ubuntu.com` and `security.ubuntu.com` in the image's apt sources. The substitution happens in the layering step, ahead of d2vm's kernel install, which reads the same sources. The mirror therefore stays in the VM's sources afterwards, and the guest's own `apt` uses it too. Empty leaves the sources alone.

```console
$ UBUNTU_MIRROR=http://mirrors.ukfast.co.uk/sites/archive.ubuntu.com \
      scripts/build_vm_image.sh benmandrew/peredur:$(git rev-parse --short HEAD)
```

The knob exists because of one build machine's network. On 2026-09-11 Canonical's archive served a small file to that machine at 13 KB/s and then timed out, and `security.ubuntu.com` timed out at 30 s. d2vm's kernel install sat in apt for 36 minutes without finishing. From the same machine, `http://mirrors.ukfast.co.uk/sites/archive.ubuntu.com` served 1.8 MB at 12 MB/s.

## Importing the appliance

VirtualBox imports the `.ova` through File → Import Appliance, or from the command line:

```console
$ VBoxManage import build-vm/counter.ova
```

Memory and CPU count can be changed in the VM's settings before starting it. That resizes one VM without rebuilding the appliance under different `VM_MEMORY_MB` or `VM_CPUS` values.

## Inside the VM

The user is `counter`, password `counter`, in the `sudo` group with bash as its shell. The console on tty1 logs in automatically, and `/etc/motd` prints example commands at login. `passwd` changes the password. `libpam-systemd` registers each login as a `systemd-logind` session.

sshd ignores the client's `LANG` and `LC_*` variables. The guest has no locales generated beyond C.UTF-8, so a client's `en_GB.UTF-8` made every command warn "cannot change locale". `RESUME=none` fixes a boot delay: initramfs-tools recorded the swap of d2vm's build host as the resume device, and the guest waited 30 s at every boot for it. With the setting, boot takes 2.9 s by `systemd-analyze`.

The binaries are on `PATH` and the `COUNTER_*` variables are set. Image `ENV` is container configuration rather than a file, so it does not survive the conversion to a disk. The layering step therefore writes it into `/etc/environment`, which `pam_env` reads for console and SSH sessions alike. That includes a non-interactive `ssh ... counter --version`, which a login-shell profile script would miss.

In the VM `$COUNTER_EXAMPLES` works directly, unlike the container, where a command using it has to be re-entered through `sh -c`. `counter` still requires its `--output-dir` to exist:

```console
$ realize $COUNTER_EXAMPLES/lily02/spec.tlsf
UNREALIZABLE
$ mkdir -p out && counter --input $COUNTER_EXAMPLES/lily02/spec.tlsf --output-dir out --seed 42
```

## Getting output out

SSH is how output leaves the VM, over the forwarded port:

```console
$ ssh -p 2222 counter@127.0.0.1
$ scp -P 2222 -r counter@127.0.0.1:out .
```

The container gets the same result from a bind mount. The VM's equivalent would be VirtualBox *shared folders*, which need *guest additions*, and those build a kernel module inside the guest. An SSH server is one package.

The port forward binds to the host's loopback interface only. Nothing beyond the host can reach port 2222, which is why a password written on this page is acceptable.

Every VM imported from one `.ova` would otherwise share an identity. The layering step deletes the SSH *host keys* from the image, and a oneshot systemd unit regenerates them at each VM's first boot, so no two imports share keys. `/etc/machine-id` is emptied for the same reason, systemd writing a fresh one on first boot.

## Testing

The disk was tested on 2026-09-11. It was booted under QEMU (Quick Emulator) with the Kernel-based Virtual Machine (KVM), on VirtualBox's default hardware: an AHCI disk, an e1000 network adapter and BIOS firmware. It was then checked over SSH on a forwarded port. The checks that passed:

- SSH answered 5 s after power-on.
- The tty1 autologin was active.
- `/etc/environment` supplied `PATH` and the `COUNTER_*` variables to a non-interactive `ssh` command.
- `counter --version` ran, and `realize $COUNTER_EXAMPLES/lily02/spec.tlsf` printed `UNREALIZABLE`.
- A two-generation `counter` run wrote `run.json`, and `scp` copied it out.
- `sudo` worked.
- The SSH host keys and `/etc/machine-id` were generated at first boot.
- The apt sources named the mirror.

`scripts/build_vm_image.sh`, run on that machine with `UBUNTU_MIRROR` set to the mirror above, took 1 min 55 s. The `.ova` it wrote is 171 MB and the `.vdi` 448 MB.

The appliance has not been booted in VirtualBox itself. The build machine runs Ubuntu 22.04.5 with kernel 6.8.0-138 and VirtualBox 6.1.50 from Ubuntu's packages. There VirtualBox intermittently halted VMs with *Guru Meditation* -2708 (`VERR_VMM_SET_JMP_ABORTED_RESUME`) within milliseconds of power-on, at the BIOS reset vector. A freshly created VM with no disk attached halted the same way. The fault is therefore in that host's VirtualBox, and says nothing about the appliance.

## Memory and CPUs

The VM's memory is the cap that `--memory` was in the container. `ltlfilt` has been measured peaking near 3.4GB and `maximal` has taken 19GB on a maximality sweep. The default 8192 MB sits between those two figures, so a maximality sweep wants `VM_MEMORY_MB` raised or the setting changed at import.

The scoring pool sizes itself from the CPUs it can see, and inside the VM that is the count VirtualBox gives the guest. `VM_CPUS` and the import-time setting therefore play the part `--cpus` played in the container.

Most of the layering step replaces something the container runtime supplied without being asked, from the environment variables to the bind mount that carried output out.
