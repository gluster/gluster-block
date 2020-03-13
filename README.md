# gluster-block
gluster-block is a CLI utility, which aims at making Gluster backed block
storage creation and maintenance as simple as possible.

### Demo
------
[![asciicast](https://asciinema.org/a/237565.svg)](https://asciinema.org/a/237565)

### Install
------
<pre>
# git clone https://github.com/gluster/gluster-block.git
# cd gluster-block/

# dnf install gcc autoconf automake make file libtool libuuid-devel json-c-devel glusterfs-api-devel glusterfs-server tcmu-runner targetcli

On Fedora27 and Centos7 [Which use legacy glibc RPC], pass '--enable-tirpc=no' flag at configure time
# ./autogen.sh && ./configure --enable-tirpc=no && make -j install

On Fedora28 and higher [Which use TIRPC], in addition to above, we should also install
# dnf install rpcgen libtirpc-devel

And pass '--enable-tirpc=yes'(default) flag or nothing at configure time
# ./autogen.sh && ./configure && make -j install
</pre>

### Usage
------
**Prerequisites:** *this guide assumes that the following are already present*
- [x] *A block hosting gluster volume with name 'hosting-volume'*
- [x] *Open 24007(for glusterd) 24010(gluster-blockd) 3260(iscsi targets) 111(rpcbind) ports and glusterfs service in your firewall*

<b>Daemon</b>: gluster-blockd runs on all the nodes
```script
# gluster-blockd --help
gluster-blockd (0.4)
usage:
  gluster-blockd [--glfs-lru-count <COUNT>]
                 [--log-level <LOGLEVEL>]
                 [--no-remote-rpc]

commands:
  --glfs-lru-count <COUNT>
        Glfs objects cache capacity [max: 512] [default: 5]
  --log-level <LOGLEVEL>
        Logging severity. Valid options are,
        TRACE, DEBUG, INFO, WARNING, ERROR, CRIT and NONE [default: INFO]
  --no-remote-rpc
        Ignore remote rpc communication, capabilities check and
        other node sanity checks
  --help
        Show this message and exit.
  --version
        Show version info and exit.
```

You can run gluster-blockd as systemd service, note '/etc/sysconfig/gluster-blockd' is the configuration file which gets dynamicaly reloaded on changing any options.
<pre>
# cat /etc/sysconfig/gluster-blockd
# systemctl daemon-reload
# systemctl start gluster-blockd
</pre>

<b>CLI</b>: you can choose to run gluster-block(cli) from any node which has gluster-blockd running
```script
# gluster-block --help
gluster-block (0.4)
usage:
  gluster-block [timeout <seconds>] <command> <volname[/blockname]> [<args>] [--json*]

commands:
  create  <volname/blockname> [ha <count>]
                              [auth <enable|disable>]
                              [prealloc <full|no>]
                              [storage <filename>]
                              [ring-buffer <size-in-MB-units>]
                              [block-size <size-in-Byte-units>]
                              [io-timeout <N-in-Second>]
                              <host1[,host2,...]> [size]
        create block device [defaults: ha 1, auth disable, prealloc full, size in bytes,
                             ring-buffer and block-size default size dependends on kernel,
                             io-timeout 43s]

  list    <volname>
        list available block devices.

  info    <volname/blockname>
        details about block device.

  delete  <volname/blockname> [unlink-storage <yes|no>] [force]
        delete block device.

  modify  <volname/blockname> [auth <enable|disable>] [size <size> [force]]
        modify block device.

  replace <volname/blockname> <old-node> <new-node> [force]
        replace operations.

  reload <volname/blockname> [force]
        reload a block device.

  genconfig <volname[,volume2,volume3,...]> enable-tpg <host>
        generate the block volumes target configuration.

  help
        show this message and exit.

  version
        show version info and exit.

common cli options: (fixed formats)
  timeout <seconds>
        it is the time in seconds that cli can wait for daemon to respond.
        [default: timeout 300]
  --json*
        used to request the output result in json format [default: plain text]
        supported JSON formats: --json|--json-plain|--json-spaced|--json-pretty
```

#### Example:

The hosts involved:

* 192.168.1.11, 192.168.1.12, 192.168.1.13: All nodes run gluster-blockd.service and glusterd.service (three nodes to achieve mutipath for HA)
* 192.168.1.14: Initiator, iSCSI client

Preparation:

* Create a gluster trusted storage pool of the 3 nodes
  192.168.1.11, 192.168.1.12, and 192.168.1.13.
    * Read more about [trusted storage pools](https://gluster.readthedocs.io/en/latest/Administrator%20Guide/Storage%20Pools/).
* Create a block hosting gluster volume called `hosting-volume` on the gluster cluster.
    * Read More on how to [create a gluster volume](https://gluster.readthedocs.io/en/latest/Administrator%20Guide/Setting%20Up%20Volumes/#creating-replicated-volumes).
    * We recommend replica 3 volume with group profile applied on it.
      * Helpful command: `# gluster vol set <block-hosting-volume> group gluster-block`

You can execute gluster-block CLI from any of the 3 nodes where glusterd and gluster-blockd are running.


##### On the Server/Target node[s]
<pre>
Create 1G gluster block storage with name 'block-volume'
<b># gluster-block create hosting-volume/block-volume ha 3 192.168.1.11,192.168.1.12,192.168.1.13 1GiB</b>
IQN: iqn.2016-12.org.gluster-block:aafea465-9167-4880-b37c-2c36db8562ea
PORTAL(S): 192.168.1.11:3260 192.168.1.12:3260 192.168.1.13:3260
RESULT: SUCCESS

Enable Authentication (this can be part of create as well)
<b># gluster-block modify hosting-volume/block-volume auth enable</b>
IQN: iqn.2016-12.org.gluster-block:aafea465-9167-4880-b37c-2c36db8562ea
USERNAME: aafea465-9167-4880-b37c-2c36db8562ea
PASSWORD: 4a5c9b84-3a6d-44b4-9668-c9a6d699a5e9
SUCCESSFUL ON:  192.168.1.11 192.168.1.12 192.168.1.13
RESULT: SUCCESS

<b># gluster-block list hosting-volume</b>
block-volume

<b># gluster-block info hosting-volume/block-volume</b>
NAME: block-volume
VOLUME: hosting-volume
GBID: 6b60c53c-8ce0-4d8d-a42c-5b546bca3d09
SIZE: 1.0 GiB
HA: 3
EXPORTED NODE(S): 192.168.1.11 192.168.1.12 192.168.1.13
</pre>
<b>NOTE:</b> Block targets created using gluster-block utility will use TPG: 1 and LUN: 0.

##### On the Client/Initiator node
<pre>
# dnf install iscsi-initiator-utils device-mapper-multipath
# systemctl start iscsid.service
# systemctl enable iscsid.service
# lsblk (note the available devices)

You can skip configuring multipath, if you choose not to enable mpath.
Below we set mapth in Active/Passive mode; Note currently Active/Active is not supported.
# modprobe dm_multipath
# mpathconf --enable

Please add the below configuration at the end of /etc/multipath.conf file.

For tcmu-runner version < 1.4.0, use:
# LIO iSCSI
devices {
        device {
                vendor "LIO-ORG"
                user_friendly_names "yes" # names like mpatha
                path_grouping_policy "failover" # one path per group
                path_selector "round-robin 0"
                failback immediate
                path_checker "tur"
                prio "const"
                no_path_retry 120
                rr_weight "uniform"
        }
}

For tcmu-runner version >= 1.4.0, use:
# LIO iSCSI
devices {
        device {
                vendor "LIO-ORG"
                user_friendly_names "yes" # names like mpatha
                path_grouping_policy "failover" # one path per group
                hardware_handler "1 alua"
                path_selector "round-robin 0"
                failback immediate
                path_checker "tur"
                prio "alua"
                no_path_retry 120
                rr_weight "uniform"
        }
}

# systemctl restart multipathd
# systemctl enable multipathd

Discovery ...
# iscsiadm -m discovery -t st -p 192.168.1.11

Update Credentials (Skip this step incase if you have not enabled auth)
# iscsiadm -m node -T "iqn.2016-12.org.gluster-block:aafea465-9167-4880-b37c-2c36db8562ea" -o update
 -n node.session.auth.authmethod -v CHAP -n node.session.auth.username -v aafea465-9167-4880-b37c-2c36db8562ea -n node
.session.auth.password -v 4a5c9b84-3a6d-44b4-9668-c9a6d699a5e9

Login ...
# iscsiadm -m node -T "iqn.2016-12.org.gluster-block:aafea465-9167-4880-b37c-2c36db8562ea" -l

# lsblk (note the new devices, let's say sdb, sdc and sdd multipath to mpatha)
# mkfs.xfs /dev/mapper/mpatha
# mount /dev/mapper/mpatha /mnt
</pre>

##### On the Server/Target node[s] to resize the block volume
<pre>
Resizing 1G gluster block volume 'block-volume' to 2G
<b># gluster-block modify hosting-volume/block-volume size 2GiB</b>
IQN: iqn.2016-12.org.gluster-block:aafea465-9167-4880-b37c-2c36db8562ea
SIZE: 2.0 GiB
SUCCESSFUL ON: 192.168.1.11 192.168.1.12 192.168.1.13
RESULT: SUCCESS
</pre>

##### On Initiator side, commands to refresh the device after block volume resizing
<pre>
Rescan the devices
# iscsiadm -m node -R

Rescan the multipath
# multipathd -k'resize map mpatha'

Grow the filesystem
# xfs_growfs  /mnt
</pre>

##### Deleting the block volume
<pre>
On client node
# umount /mnt
# iscsiadm -m node -u

On the server node
<b># gluster-block delete hosting-volume/block-volume</b>
SUCCESSFUL ON: 192.168.1.11 192.168.1.12 192.168.1.13
RESULT: SUCCESS
</pre>

<b>NOTE:</b> gluster-block cannot track iSCSI targets created manually using targetcli.

------

## About Gluster
[Gluster](http://gluster.readthedocs.io/en/latest/) is a well known scale-out distributed storage system, flexible in its design and easy to use. One of its key goals is to provide high availability of data. Gluster is very easy to setup and use. Addition and removal of storage servers from a Gluster cluster is intuitive. These capabilities along with other data services that Gluster provides makes it a reliable software defined storage platform.

We can access glusterfs via [FUSE](https://en.wikipedia.org/wiki/Filesystem_in_Userspace) module. However to perform a single filesystem operation various context switches are required which can often exhibit performance issues. [Libgfapi](http://blog.gluster.org/2014/04/play-with-libgfapi-and-its-python-bindings/) is a userspace library for accessing data in Glusterfs. It can perform I/O on gluster volumes without the FUSE module, kernel VFS layer and hence requires no context switches. It exposes a filesystem like API for accessing gluster volumes. Samba, NFS-Ganesha, QEMU and now the tcmu-runner all use libgfapi to integrate with Gluster.

> A unique distributed storage solution build on traditional filesystems

### How we provide block storage in gluster ?

![untitled diagram](https://cloud.githubusercontent.com/assets/12432241/21478518/235e533c-cb72-11e6-9c5a-e351513a34b7.png)

1. Create a file in the gluster volume (Block Hosting Volume)
2. We expose the file in the gluster volume as tcmu backstore using tcmu-runner, exporting the target file as iSCSI LUN and
3. From the initiator we login to the exported LUN and play with the block device

#### Background
The [SCSI](http://searchstorage.techtarget.com/definition/SCSI) subsystem uses a form of client-server model.  The Client/Initiator request I/O happen through target which is a storage device. The SCSI target subsystem enables a computer node to behave as a SCSI storage device, responding to storage requests by other SCSI initiator nodes.

In simple terms SCSI is a set of standards for physically connecting and transferring data between computers and peripheral devices.

The most common implementation of the SCSI target subsystem is an iSCSIserver, [iSCSI](http://searchstorage.techtarget.com/definition/iSCSI) transports block level data between the iSCSI initiator and the target which resides on the actual storage device. iSCSi protocol wraps up the SCSI commands and sends it over TCP/IP layer. Up on receiving the packets at the other end it disassembles them to form the same SCSI commands, hence on the OS’es it seen as local SCSI device.

> In other words iSCSI is SCSI over TCP/IP.

The [LIO](http://linux-iscsi.org/wiki/LIO) project began with the iSCSI design as its core objective, and created a generic SCSI target subsystem to support iSCSI. LIO is the SCSI target in the Linux kernel. It is entirely kernel code, and allows exported SCSI logical units (LUNs) to be backed by regular files or block devices.

> LIO is Linux IO target, is an implementation of iSCSI target.

[TCM](https://www.kernel.org/doc/Documentation/target/tcmu-design.txt) is another name for LIO, an in-kernel iSCSI target (server). As we know existing TCM targets run in the kernel. TCMU (TCM in Userspace) allows userspace programs to be written which act as iSCSI targets. These enables wider variety of backstores without kernel code. Hence the TCMU userspace-passthrough backstore allows a userspace process to handle requests to a LUN. TCMU utilizes the traditional UIO subsystem, which is designed to allow device driver development in userspace.

> One such backstore with best clustered network storage capabilities is GlusterFS

Any TCMU userspace-passthrough can utilize the TCMU framework handling the messy details of the TCMU interface.
One such passthrough is [Tcmu-runner](https://github.com/open-iscsi/tcmu-runner) (Thanks to Andy Grover). Tcmu-runner has a glusterfs handler that can interact with the backed file in gluster volume over gluster libgfapi interface and can show it as a target (over network).

Some responsibilities of userspace-passthrough include,

Discovering and configuring TCMU UIO devices
waiting for the events on the device and
managing the command ring buffers

[TargetCli](https://github.com/Datera/targetcli) is the general management platform for the LIO/TCM/TCMU. TargetCli with its shell interface is used to configure LIO.
> Think it like a shell which makes life easy in configuring LIO core

------
### How to quickly bringup gluster-block environment locally ?
<pre>
Fedora:
# dnf -y install qemu libvirt libvirt-devel ruby-devel gcc vagrant ansible

CentOS:
# yum -y install qemu libvirt libvirt-devel ruby-devel gcc qemu-kvm ansible

Note: Please download and install vagrant package for CentOS from:
https://www.vagrantup.com/downloads.html


Start and enable libvirtd service
# systemctl start libvirtd
# systemctl enable libvirtd

Now install vagrant libvirt plugin
# vagrant plugin install vagrant-libvirt

Make sure you are in gluster-block root directory
# vagrant up
</pre>

### Managing the vagrant Vm's
<pre>

To check VMs status
# vagrant status

To ssh and get access to a VM
# vagrant ssh {name}

To stop the VMs
# vagrant halt

To destroy the VMs
# vagrant destroy

Check more commands with
# vagrant list-commands
</pre>

------

## License
gluster-block is licensed to you under your choice of the GNU Lesser General Public License, version 3 or any later version ([LGPLv3](https://opensource.org/licenses/lgpl-3.0.html) or later), or the GNU General Public License, version 2 ([GPLv2](https://opensource.org/licenses/GPL-2.0)), in all cases as published by the Free Software Foundation.

## Maintainers
See [MAINTAINERS](https://github.com/gluster/gluster-block/blob/master/MAINTAINERS.md) file

## Community
* Please join our [mailing list](https://lists.gluster.org/mailman/listinfo/gluster-devel)
* To ask a question or start a discussion, you can also raise an [issue](https://github.com/gluster/gluster-block/issues)
* IRC: #gluster-devel on Freenode
