#!/bin/bash

check () {

    #
    # We can't easily determine whether a dm-cache device was set up by
    # zodcache, so don't automatically include this module in the initramfs.
    #
    # Edit /etc/dracut.conf.d/50-zodcache.conf to enable zodcache in the
    # initramfs.
    #

    return 255
}

depends () {
    return 0
}

installkernel () {
    hostonly='' instmods dm_cache_smq
}

install () {
    inst /usr/sbin/zcstart
    inst_rules 69-zodcache.rules
}
