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
        time.sleep(4)
    
    def tearDown(self):
        g.log.info("Inside teardown")
        blocknames = get_block_list(self.mnode, self.volname)
        if blocknames:
            for blockname in blocknames:
                ret, out, err = block_delete(self.mnode, self.volname, blockname, "yes")
                self.assertEqual(0, ret, "Block could not be deleted")
    
    
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
    
    
    def test_b_block_delete_unlink_storage(self):
        """Delete the block with unkink-storage set to no
        """

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
        #Get the GBID of the block
        gbid = get_block_gbid(self.mnode, self.volname, blockname)
        #Get the brick paths for the volume 
        brick_paths = get_all_bricks(self.mnode, self.volname)
        # Delete the block with unlink-storage=no
        ret, out, err = block_delete(self.mnode, self.volname, blockname, "no")
        self.assertEqual(ret, 0, "Block could not be deleted")
        block_list = get_block_list(self.mnode, self.volname)
        self.assertIsNone(block_list, "Block list is not empty while it was expected to be")
        
        cmd = ("ls %s/block-store/ | grep %s" % ( brick_paths[0].split(':')[1], gbid))
        ret, out, err = g.run(brick_paths[0].split(':')[0], cmd)
        self.assertEqual(ret, 0, "Block file deleted but it wasn't supposed to be")


    @classmethod
    def tearDownClass(self):
        
        # stopping the volume and Cleaning up the volume
        ret = self.cleanup_volume()
        if not ret:
            raise ExecutionError("Failed to Cleanup the Volume %s"
                                 % self.volname)
        g.log.info("Volume deleted successfully : %s", self.volname)
        GlusterBaseClass.tearDownClass.im_func(self)

    
