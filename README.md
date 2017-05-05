# gluster-block
gluster-block is a CLI utility, which aims at making gluster backed block
storage creation and maintenance as simple as possible.

## License
gluster-block is licensed to you under your choice of the GNU Lesser General Public License, version 3 or any later version ([LGPLv3](https://opensource.org/licenses/lgpl-3.0.html) or later), or the GNU General Public License, version 2 ([GPLv2](https://opensource.org/licenses/GPL-2.0)), in all cases as published by the Free Software Foundation.

## Gluster
[Gluster](http://gluster.readthedocs.io/en/latest/) is a well known scale-out distributed storage system, flexible in its design and easy to use. One of its key goals is to provide high availability of data. Despite its distributed nature, Gluster is very easy to setup and use. Addition and removal of storage servers from a Gluster cluster is very easy. These capabilities along with other data services that Gluster provides makes it a reliable software defined storage platform.

We can access glusterfs via [FUSE](https://en.wikipedia.org/wiki/Filesystem_in_Userspace) module. However to perform a single filesystem operation various context switches are required which leads to performance issues. [Libgfapi](http://blog.gluster.org/2014/04/play-with-libgfapi-and-its-python-bindings/) is a userspace library for accessing data in Glusterfs. It can perform IO on gluster volumes without the FUSE module, kernel VFS layer and hence requires no context switches. It exposes a filesystem like API for accessing gluster volumes. Samba, NFS-Ganesha, QEMU and now the tcmu-runner all use libgfapi to integrate with Glusterfs.

> A unique distributed storage solution build on traditional filesystems

### How we achieve block storage in gluster ?
======

![untitled diagram](https://cloud.githubusercontent.com/assets/12432241/21478518/235e533c-cb72-11e6-9c5a-e351513a34b7.png)

1. Create a file in the gluster volume
2. We expose the file in the gluster volume as tcmu backstore using tcmu-runner, exporting the target file as iSCSI LUN and
3. From the initiator we login to the exported LUN and play with the block device

#### Background
The [SCSI](http://searchstorage.techtarget.com/definition/SCSI) subsystem uses a sort of client-server model.  The Client/Initiator request IO happen through target which is a storage device. The SCSI target subsystem enables a computer node to behave as a SCSI storage device, responding to storage requests by other SCSI initiator nodes.

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

### Install
------
<pre>
# git clone https://github.com/gluster/gluster-block.git
# cd gluster-block/
# dnf install autoconf automake libtool libuuid-devel json-c-devel glusterfs-api-devel tcmu-runner targetcli (on fedora)
# make -j install
</pre>

### Usage
------
**Prerequisites:** *this guide assume we already have*
- [x] *A gluster volume with name 'block-test'*
- [x] *In all nodes gluster-blockd.service is running*
- [x] *Open 24007(for glusterd) 24006(gluster-blockd) 3260(iscsi targets) 111(rpcbind) ports and glusterfs service in your firewall*

```script
# gluster-block (0.2)
usage:
  gluster-block <command> <volname[/blockname]> [<args>] [--json*]

commands:
  create  <volname/blockname> [ha <count>] [auth enable|disable] <host1[,host2,...]> <size>
        create block device.

  list    <volname>
        list available block devices.

  info    <volname/blockname>
        details about block device.

  delete  <volname/blockname>
        delete block device.

  modify  <volname/blockname> <auth enable|disable>
        modify block device.

  help
        show this message and exit.

  version
        show version info and exit.

supported JSON formats:
  --json|--json-plain|--json-spaced|--json-pretty
```

#### Example:
======
*192.168.1.11, 192.168.1.12, 192.168.1.13: All nodes run gluster-blockd.service and glusterd.service (three nodes to achieve mutipath for HA)<br>
192.168.1.14: Initiator, iSCSI client<br><br>
Execute gluster-block CLI from any of the 3 nodes where glusterd and gluster-blockd are running <br>
Create a gluster volume by pooling 3 nodes (192.168.1.11, 192.168.1.12 and 192.168.1.13) <br>
Read More on how to [create a gluster volume](https://access.redhat.com/documentation/en-US/Red_Hat_Storage/2.1/html/Administration_Guide/sect-User_Guide-Setting_Volumes-Replicated.html)*

<pre>
Create 1G gluster block storage with name 'sample-block'
<b># gluster-block create block-test/sample-block ha 3 192.168.1.11,192.168.1.12,192.168.1.13 1GiB</b>
IQN: iqn.2016-12.org.gluster-block:aafea465-9167-4880-b37c-2c36db8562ea
PORTAL(S): 192.168.1.11:3260 192.168.1.12:3260 192.168.1.13:3260
RESULT: SUCCESS

Enable Authentication (you can do this as part of create as well)
<b># gluster-block modify block-test/sample-block auth enable</b>
IQN: iqn.2016-12.org.gluster-block:aafea465-9167-4880-b37c-2c36db8562ea
USERNAME: aafea465-9167-4880-b37c-2c36db8562ea
PASSWORD: 4a5c9b84-3a6d-44b4-9668-c9a6d699a5e9
SUCCESSFUL ON:  192.168.1.11 192.168.1.12 192.168.1.13
RESULT: SUCCESS

<b># gluster-block list block-test</b>
sample-block

<b># gluster-block info block-test/sample-block</b>
NAME: sample-block
VOLUME: block-test
GBID: 6b60c53c-8ce0-4d8d-a42c-5b546bca3d09
SIZE: 1073741824
HA: 3
BLOCK CONFIG NODE(S): 192.168.1.11 192.168.1.12 192.168.1.13
</pre>
<b>NOTE:</b> Block targets created using gluster-block utility will use TPG: 1 and LUN: 0.

##### On the Initiator machine
<pre>
# dnf install iscsi-initiator-utils
# lsblk (note the available devices)

Make sure you have multipathd running and configured in Active/Passive mode

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

##### Delete the targets
<pre>
On initiator node
# umount /mnt
# iscsiadm -m node -u

On the gluster-block node
<b># gluster-block delete block-test/sample-block</b>
SUCCESSFUL ON: 192.168.1.11 192.168.1.12 192.168.1.13
RESULT: SUCCESS
</pre>

<b>NOTE:</b> gluster-block cannot track iSCSI targets created manually using targetcli.
