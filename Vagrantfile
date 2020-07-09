# -*- mode: ruby -*-
# vi: set ft=ruby :

SERVERS = 3

# All Vagrant configuration is done below. The "2" in Vagrant.configure
# configures the configuration version (we support older styles for
# backwards compatibility). Please don't change it unless you know what
# you're doing.

Vagrant.configure("2") do |config|
  config.vm.box = "centos/7"
  # config.vm.box = "fedora/31-cloud-base"

  config.vm.provider "libvirt" do |lv|
    lv.cpus = "2"
    lv.memory = "2048"
    # Always use system connection instead of QEMU session
    lv.qemu_use_session = false
  end

  # disable default sync dir
  config.vm.synced_folder ".", "/vagrant", disabled: true

  config.vm.define "initiator" do |node|
    node.vm.hostname = "initiator"
  end

  (1..SERVERS).each do |i|
    config.vm.define "server#{i}" do |node|
      node.vm.hostname = "server#{i}"

      # Only execute the Ansible provisioner once,
      # when all the machines are up and ready.
      if i == SERVERS
        node.vm.provision :ansible do |ansible|
          ansible.groups = {
            "servers" => (1..SERVERS).map {|j| "server#{j}"},
            "initiators" => ["initiator"]
          }
          # Disable default limit to connect to all the machines
          ansible.limit = "all"
          ansible.playbook = "ansible/playbook.yml"
        end
      end
    end
  end

end
