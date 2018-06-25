
from glusto.core import Glusto as g
from glustolibs.gluster.exceptions import ConfigError, ExecutionError
from glustolibs.gluster.gluster_base_class import GlusterBaseClass, runs_on, GlusterBlockBaseClass
from glustolibs.gluster.volume_ops import (volume_create, volume_start,
                                           volume_stop, volume_delete,
                                           get_volume_list, get_volume_info)
from glustolibs.gluster.volume_libs import (setup_volume, cleanup_volume)
from glustolibs.gluster.peer_ops import (peer_probe, peer_detach)
from glustolibs.gluster.lib_utils import form_bricks_list
from glustolibs.gluster.block_libs import if_block_exists, validate_block_info, setup_block, get_block_list, get_block_gbid, get_block_info
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
        g.log.info("Volume %s has been setup successfully", cls.volname)

        cmd = 'gluster vol set %s group gluster-block' % cls.volname
        ret, out, err = g.run(cls.mnode, cmd)
        if ret!=0:
            raise ExecutionError("Failed to set group option for gluster-block on %s" % cls.volname)

        for server in cls.servers:
            ret, out, err = g.run(server, "systemctl restart gluster-blockd")
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

        #Modify the block with auth = disable
        ret, out, err = block_modify(self.mnode, self.volname, blockname, "disable")
        self.assertEqual(ret, 0, "Block could not be modified")
        block_info_dict = get_block_info(self.mnode, self.volname, blockname)
        password_len = len(block_info_dict.get('PASSWORD'))
        self.assertTrue(password_len == 0, "Block information could not be validated")

    @classmethod
    def tearDownClass(self):
        
        # stopping the volume and Cleaning up the volume
        ret = self.cleanup_volume()
        if not ret:
            raise ExecutionError("Failed to Cleanup the Volume %s"
                                 % self.volname)
        g.log.info("Volume deleted successfully : %s", self.volname)
        GlusterBaseClass.tearDownClass.im_func(self)


