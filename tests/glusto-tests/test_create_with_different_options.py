#  Copyright (C) 2018  Red Hat, Inc. <http://www.redhat.com>
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

from glusto.core import Glusto as g
from glustolibs.gluster.exceptions import ConfigError, ExecutionError
from glustolibs.gluster.gluster_base_class import GlusterBaseClass, runs_on, GlusterBlockBaseClass
from glustolibs.gluster.volume_ops import (volume_create, volume_start,
                                           volume_stop, volume_delete,
                                           get_volume_list, get_volume_info)
from glustolibs.gluster.volume_libs import (setup_volume, cleanup_volume)
from glustolibs.gluster.peer_ops import (peer_probe, peer_detach)
from glustolibs.gluster.lib_utils import form_bricks_list
from glustolibs.gluster.block_libs import if_block_exists, validate_block_info, setup_block, get_block_list, get_block_gbid
from glustolibs.gluster.block_ops import block_modify, block_delete
from glustolibs.gluster.brick_libs import get_all_bricks
import time, random, pytest


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
    
    def tearDown(self):
        g.log.info("Inside teardown")
        blocknames = get_block_list(self.mnode, self.volname)
        if blocknames:
            for blockname in blocknames:
                ret, out, err = block_delete(self.mnode, self.volname, blockname, "yes"  )
                self.assertEqual(0, ret, "Block could not be deleted")
    
    
    def test_a_create_field_ha_1(self):
        gluster_block_args_info = {
                'ha': 1,
                'auth': None,
                'prealloc': None,
                'storage': None,
                'ring-buffer': None
            }
        blockname  = 'block_testing_1'
        servers = random.sample(self.servers_ips, 1)
        size = '1GiB'
        ret = setup_block(self.mnode, self.volname, blockname, servers, size, **gluster_block_args_info)
        self.assertTrue(ret, "All blocks could not be created successfully")
        #Check if gluster-block list operations returns the block name
        ret = if_block_exists(self.mnode, self.volname, blockname)
        self.assertTrue(ret, "Block exists on volume %s" % self.volname)
        ret = validate_block_info(self.mnode, self.volname, blockname, servers,"1.0 GiB",ha=1)
        self.assertTrue(ret, "Block information validated")
        

    def test_b_create_field_ha_2(self):
        gluster_block_args_info = {
                'ha': 2,
                'auth': None,
                'prealloc': None,
                'storage': None,
                'ring-buffer': None
            }
        blockname  = 'block_testing_1'
        servers = random.sample(self.servers_ips, 2)
        size = '1GiB'
        ret = setup_block(self.mnode, self.volname, blockname, servers, size, **gluster_block_args_info)
        self.assertTrue(ret, "All blocks could not be created successfully")
        #Check if gluster-block list operations returns the block name
        ret = if_block_exists(self.mnode, self.volname, blockname)
        self.assertTrue(ret, "Block exists on volume %s" % self.volname)
        ret = validate_block_info(self.mnode, self.volname, blockname, servers,"1.0 GiB",ha=2)
        self.assertTrue(ret, "Block information validated")


    
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

    def test_d_create_field_auth_enable(self):
        gluster_block_args_info = {
                'ha': 3,
                'auth': 'enable',
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

    
    def test_e_create_field_size_1024(self):
        # ha =3
        gluster_block_args_info = {
                'ha': 3,
                'auth': None,
                'prealloc': None,
                'storage': None,
                'ring-buffer': None
            }
        blockname  = 'block_testing_1'
        servers = random.sample(self.servers_ips, 3)
        size = '1024'
        ret = setup_block(self.mnode, self.volname, blockname, servers, size, **gluster_block_args_info)
        self.assertTrue(ret, "All blocks could not be created successfully")
        #Check if gluster-block list operations returns the block name
        ret = if_block_exists(self.mnode, self.volname, blockname)
        self.assertTrue(ret, "Block exists on volume %s" % self.volname)
        ret = validate_block_info(self.mnode, self.volname, blockname, servers,"1.0 KiB",ha=3)
        self.assertTrue(ret, "Block information validated")
    
    def test_f_create_field_size_1K(self):
        # ha =3
        gluster_block_args_info = {
                'ha': 3,
                'auth': None,
                'prealloc': None,
                'storage': None,
                'ring-buffer': None
            }
        blockname  = 'block_testing_1'
        servers = random.sample(self.servers_ips, 3)
        size = '1K'
        ret = setup_block(self.mnode, self.volname, blockname, servers, size, **gluster_block_args_info)
        self.assertTrue(ret, "All blocks could not be created successfully")
        #Check if gluster-block list operations returns the block name
        ret = if_block_exists(self.mnode, self.volname, blockname)
        self.assertTrue(ret, "Block exists on volume %s" % self.volname)
        ret = validate_block_info(self.mnode, self.volname, blockname, servers,"1.0 KiB",ha=3)
        self.assertTrue(ret, "Block information validated")
    
    def test_g_create_field_missing_parameter(self):
        gluster_block_args_info = {
                'ha': 3,
                'auth': None,
                'prealloc': None,
                'storage': None,
                'ring-buffer': None
            }
        blockname  = 'block_testing_1'
        servers = random.sample(self.servers_ips, 3)
        size = ''
        ret = setup_block(self.mnode, self.volname, blockname, servers, size, **gluster_block_args_info)
        self.assertFalse(ret, "Block creation succedded while it wasn't supposed to be")

    def test_h_create_field_non_matching_hosts_and_HA(self):
        gluster_block_args_info = {
                'ha': 3,
                'auth': None,
                'prealloc': None,
                'storage': None,
                'ring-buffer': None
            }
        blockname  = 'block_testing_1'
        servers = random.sample(self.servers_ips, 2)
        size = '1GiB'
        ret = setup_block(self.mnode, self.volname, blockname, servers, size, **gluster_block_args_info)
        self.assertFalse(ret, "Block creation succedded while it wasn't supposed to be")

   
    def test_i_create_field_non_matching_hosts_and_HA(self):
        gluster_block_args_info = {
                'ha': 2,
                'auth': None,
                'prealloc': None,
                'storage': None,
                'ring-buffer': None
            }
        blockname  = 'block_testing_1'
        servers = random.sample(self.servers_ips, 1)
        size = '1GiB'
        ret = setup_block(self.mnode, self.volname, blockname, servers, size, **gluster_block_args_info)
        self.assertFalse(ret, "Block creation succedded while it wasn't supposed to be")

        
    
    def test_j_create_field_pass_invalid_value(self):
        gluster_block_args_info = {
                'ha': 2,
                'auth': 'yes-no',
                'prealloc': None,
                'storage': None,
                'ring-buffer': None
            }
        blockname  = 'block_testing_1'
        servers = random.sample(self.servers_ips, 1)
        size = '1GiB'
        ret = setup_block(self.mnode, self.volname, blockname, servers, size, **gluster_block_args_info)
        self.assertFalse(ret, "Block creation succedded while it wasn't supposed to be")
    
    
    def test_k_create_field_unlink_storage(self):
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
        gbid = get_block_gbid(self.mnode, self.volname, blockname)
        brick_paths = get_all_bricks(self.mnode, self.volname)
        cmd = ("ls -l %s/block-store/ | grep %s | cut -d ' ' -f2" % ( brick_paths[0].split(':')[1], gbid))
        ret, out, err = g.run(brick_paths[0].split(':')[0], cmd)
        self.assertEqual(ret, 0, "Command failed to execute")
        hard_link_count = int(out)
        ret, out, err = block_delete(self.mnode, self.volname, blockname, "no")
        self.assertEqual(ret, 0, "Block could not be deleted")
        cmd = ("ls %s/block-store/ | grep %s" % (brick_paths[0].split(':')[1], gbid))
        ret, out, err = g.run(brick_paths[0].split(':')[0], cmd)
        self.assertEqual(ret, 0, "Block file deleted but it wasn't supposed to be")
        ret = setup_block(self.mnode, self.volname, blockname, servers, "", storage=gbid, ha=2)
        self.assertTrue(ret, "Block created successfully")
        cmd = ("ls -l %s/block-store/ | grep %s | cut -d ' ' -f2" % ( brick_paths[0].split(':')[1], gbid))
        ret, out, err = g.run(brick_paths[0].split(':')[0], cmd)
        self.assertEqual(ret, 0, "Command failed to execute")
        expected_hard_link = hard_link_count+1
        self.assertEqual(int(out), expected_hard_link , "Hard link count not increased by 1")
    
    @classmethod
    def tearDownClass(self):
        
        # stopping the volume and Cleaning up the volume
        ret = self.cleanup_volume()
        if not ret:
            raise ExecutionError("Failed to Cleanup the Volume %s"
                                 % self.volname)
        g.log.info("Volume deleted successfully : %s", self.volname)
        

        GlusterBaseClass.tearDownClass.im_func(self)


