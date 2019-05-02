<!--
This issue template is meant mainly for bug reports and feature requests.

If you are looking for help, you can reach out to us by sending an email
to our mailing list gluster-users@gluster.org or gluster-devel@gluster.org

You can still use an issue to ask for help. In this case you should remove
the template contents or use it as a guideline for providing information
for debugging.
-->

### Kind of issue

> Uncomment only one of these:
>
> Bug
> Feature request

### Observed behavior


### Expected/desired behavior


### Details on how to reproduce (minimal and precise)


### Logs and Information about the environment:

- gluster-block version used (e.g. v0.4 or master):
- other dependencies version
  - glusterfs
  - tcmu-runner
  - targetcli
  - python-rtslib
  (if rpms, '$rpm -qa | grep -e gluster -e tcmu -e targetcli -e rtslib')
  - OS and kernel version
- logs:
  - from all target/server nodes:
    - /var/log/gluster-block/ directory
    - /etc/target/saveconfig.json file
    - $ targetcli ls
  - from initiator node: (only needed if the issue is from client side)
    - $ multipath -ll
    - $ lsblk
    - /etc/multipath.conf file


### Other useful information


