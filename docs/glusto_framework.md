# Gluster-block glusto test framework 

Glusto is a test automation framework for a distributed systems like glusterfs.
Glusto-tests repo provides libraries for automating test cases for glusterfs, gluster-block and other related projects.

The aim of this document is to help in getting started with automating test cases for gluster-block. Therefore, this includes very basic test cases.

## Installing

To automate test cases using glusto, we need to install glusto and other libraries/modules specific to glusterfs and gluster-block.
Follow this link for the setup: https://github.com/gluster/glusto-tests

## Running the tests

Before writing gluster-block test cases, create 3-4 virtual machines which will serve as clients and servers.
Edit the config file tests/gluster_tests_config.yml inside glusto-tests repo with the information of the allocated machines.
We can start writing the test cases now.

The test libraries for block are under glustolibs-gluster package which we have already installed. Import these and other required libraries for writing test cases.
Here are a few examples of how test cases can be automated for gluster-block.

### Few Examples

@runs_on decorator is used to define the volume types for test cases. Multiple volume types can be given at the same time.

In the below example, the setUpClass method creates a glusterfs volume of type repicated using the information from the config file.
For this example I am assuming that glusterd and gluster-blockd are already started on the servers but they can be started/restarted in the test cases too as you can see below. 

```
@runs_on([['replicated'],['glusterfs']])
class TestBlockOperationCreate(GlusterBlockBaseClass):



    @classmethod
    def setUpClass(cls):

        GlusterBlockBaseClass.setUpClass.im_func(cls)
        # check whether peers are in connected state
        ret = cls.validate_peers_are_connected()
        if not ret:
            raise ExecutionError("Peers are not in connected state")

        ret = setup_volume(cls.mnode, cls.all_servers_info, cls.volume)
        if not ret:
            raise ExecutionError("Failed to setup volume %s" % cls.volname)
        g.log.info("Volume %s has been setuo successfully", cls.volname)

        cmd = 'gluster vol set %s group gluster-block' % cls.volname
        ret, out, err = g.run(cls.mnode, cmd)
        if ret!=0:
            raise ExecutionError("Failed to set group option for gluster-block on %s" % cls.volname)

        for server in cls.servers:
            ret, out, err= g.run(server, "systemctl restart gluster-blockd")
            if ret!=0:
                raise ExecutionError("Failed to restart gluster-blockd on %s" % server)
            g.log.info("gluster-blockd started successfully on %s" % server)
        time.sleep(2)
```

### Create a block

Manual

```
gluster-block create block-test/block_testing_1 ha 3 192.168.1.11,192.168.1.12,192.168.1.13 1GiB
```

Automated
```
    def test_c_create_field_ha_3(self):
        gluster_block_args_info = {
                'ha': 3,
                'auth': None,
                'prealloc': None,
                'storage': None,
                'ring-buffer': None
            }
        blockname  = 'block_testing_1'
        servers = random.sample(self.servers_ips, 3)
        size = '1GiB'
        ret = setup_block(self.mnode, self.volname, blockname, servers, size, **gluster_block_args_info)
        self.assertTrue(ret, "All blocks could not be created successfully")
        #Check if gluster-block list operations returns the block name
        ret = if_block_exists(self.mnode, self.volname, blockname)
        self.assertTrue(ret, "Block exists on volume %s" % self.volname)
        ret = validate_block_info(self.mnode, self.volname, blockname, servers,"1.0 GiB",ha=3)
        self.assertTrue(ret, "Block information validated")
```

### Delete a block

This test case first creates 10 blocks and then deletes them.

```
    def test_a_block_delete(self):
        """Create 10 blocks in a loop on same volume and delete them 
        in a loop.
        """
        gluster_block_args_info = {
                'ha': 3,
                'auth': None,
                'prealloc': None,
                'storage': None,
                'ring-buffer': None
            }
        
        for i in range(1,10):
            blockname  = 'block_testing_%d' % i
            servers = random.sample(self.servers_ips, 3)
            size = '1GiB'
            ret = setup_block(self.mnode, self.volname, blockname, servers, size, **gluster_block_args_info)
            self.assertTrue(ret, "All blocks could not be created successfully")

        #Delete all the blocks
        for blockname in get_block_list(self.mnode, self.volname):
            ret, out, err = block_delete(self.mnode, self.volname, blockname)
            self.assertEqual(ret, 0, "Block could not be deleted")

        ret = get_block_list(self.mnode, self.volname)
        self.assertIsNone(ret, "Block list is not empty while it was expected to be")
```
### Modify block 

This test case first enables authentication.

```
    def test_modify_auth_enable(self):
        gluster_block_args_info = {
                'ha': 3,
                'auth': None,
                'prealloc': None,
                'storage': None,
                'ring-buffer': None
            }
        blockname  = 'block_testing_1'
        servers = random.sample(self.servers_ips, 3)
        size = '2GiB'
        ret = setup_block(self.mnode, self.volname, blockname, servers, size, **gluster_block_args_info)
        self.assertTrue(ret, "All blocks could not be created successfully")

        # Modify the block with auth = enable
        ret, out, err = block_modify(self.mnode, self.volname, blockname, "enable")
        self.assertEqual(ret, 0, "Block could not be modified")
        block_info_dict = get_block_info(self.mnode, self.volname, blockname)
        password_len = len(block_info_dict.get('PASSWORD'))
        self.assertTrue(password_len > 0, "Block information could not be validated")

```

### Initiator/client side test cases

The functions for initiator side operations are included in GlusterBlockBaseClass. The main ones are:
For block discovery: discover_blocks_on_clients
For block login:  login_to_iqn_on_clients
Finding the multipath of form /dev/mapper/mpatha: get_mpath_of_iqn_on_clients
Mounting the block: mount_blocks

There are a few more methods inside GlusterBlockBaseClass which are used by the above methods.

Suppose we have created a block block_testing_1 with ha=3 192.168.1.11,192.168.1.12,192.168.1.13 and size 1GiB with auth=enabled. The below example with show how to mount this block on client.
We can perform I/O operation on the block after doing the mount.

```
    def test_mount_block_on_client(self):

        blockname  = 'block_testing_1'
	# Discover the block
        ret = self.discover_blocks_on_clients(blockname)
        self.assertTrue(ret, "Block could not be discovered on client")

	# Login inside the client
	ret = self.login_to_iqn_on_clients(blockname)
	self.assertTrue(ret, "Block could not be discovered on client")
	
	# Get multipath(/dev/mapper/mpath*) for doing a mount 
	ret = self.get_mpath_of_iqn_on_clients(blockname)
	self.assertTrue(ret, "Multipath location not found")
	
	# Mount the block(/dev/mapper/mapath*) at /mnt/block_testing_1
	ret = self.mount_blocks(blockname)
	self.assertTrue(ret, "Block could not be mounted on client")

```

### tearDownClass

```
   @classmethod
    def tearDownClass(self):
       
        # Umount the block, logout the block, delete the block, delete the volume
        ret = self.tearDownClass(umount_blocks=True, cleanup_blocks=True, cleanup_vol=True, unlink_storage="no")
        if not ret:
            raise ExecutionError("Failed to clean up the block")
        g.log.info("Block block_testing_1 cleaned successfully" )

```
