---
layout: post
title: "Installing Arch Linux on a Raspberry Pi"
---

{% include JB/setup %}

This tutorial is for people who are running *NIX or OS X. It is not for absolute beginners, as it does not hold your hand throughout.

### Getting the base image on an SD card and logging in

0. Get an SD card. Any one will do, really, but if you want any decent speed, get a class 10 card.
1. Download the zipped image file from [here](http://archlinuxarm.org/platforms/armv6/raspberry-pi) - it's in the "Installation" tab - and then unzip it to get the .img. Careful, as together the files sum to about 2.3GB.
2. Run `sudo dd if=/path/to/ArchLinux.img of=/block/device/of/SD/card bs=1m conv=notrunc`.
  * NOTE: MAKE SURE YOU ARE OKAY WITH THE CONTENTS BEING IRREVERSIBLY OVERWRITTEN.
  * NOTE: If you get an error about a bad block size, change `1m` to `1M` - this is a difference between BSD and GNU/Linux `dd`
  * NOTE: You will not have a progress bar unless you use a utility like `pv`. Just wait. Depending on the speed, it could take 15 minutes.
3. Put the SD card into the Pi, along with a blank (or wipable) USB drive in the top USB slot.
  * NOTE: MAKE SURE YOU ARE OKAY WITH THE CONTENTS BEING IRREVERSIBLY OVERWRITTEN.
4. Boot the Pi with the HDMI cable connected to a monitor or TV.
5. Login with username `root` and password `root`.

![](/assets/img/blog/arch_pi/pi_time.jpg)

### Getting the base image on a USB drive and expanding the partition

0. Put the SD card and USB drive into the Pi and boot it up.
1. Login with `root`/`root`.
2. Run `lsblk` to see the available block devices. I see something like this:
  * <pre>
  NAME        MAJ:MIN RM  SIZE RO TYPE MOUNTPOINT
  sda           8:0    1 57.7G  0 disk
  |-sda1        8:1    1   90M  0 part
  |-sda2        8:2    1    1k  0 part
  `-sda5        8:5    1  1.7G  0 part
  mmcblk0     179:0    0  1.9G  0 disk
  |-mmcblk0p1 179:1    0   90M  0 part /boot
  |-mmcblk0p2 179:2    0    1K  0 part
  `-mmcblk0p5 179:5    0  1.7G  0 part /</pre>
  * NOTE: Your `/dev/sda` may have completely different partitioning. That's totally fine.
  * NOTE: `/dev/sda` is the USB drive (with high capacity) and `/dev/mmcblk0` is the SD card (with lower capacity).
3. We're going to add a new Linux partition. Run `cfdisk /dev/sda`.
4. Type `d` or select the `Delete` button at the bottom of the screen, deleting all of the partitions until you see just one line that says `Pri/Log     Free Space                             61918.16` (or something similar - the size may vary).
5. Type `n` or select the `New` button at the bottom of the screen to add a new partition. Select `Primary` and use the default size if you want the partition to consume the whole disk.
6. When you are satisfied with your partitioning, select `Write` at the bottom of the screen. It will confirm that you want to destroy data; type `yes`.
  * NOTE: This write is irreversible and will effectively wipe your drive!
7. Run `lsblk` to see your new partitions. Mine looks like:
  * <pre>
  NAME        MAJ:MIN RM  SIZE RO TYPE MOUNTPOINT
  sda           8:0    1 57.7G  0 disk
  `-sda1        8:1    1 57.7G  0 part
  mmcblk0     179:0    0  1.9G  0 disk
  |-mmcblk0p1 179:1    0   90M  0 part /boot
  |-mmcblk0p2 179:2    0    1K  0 part
  `-mmcblk0p5 179:5    0  1.7G  0 part /</pre>
8. Write an `ext4` filesystem to the disk. Run `mkfs.ext4 /dev/sda1` (or whatever your partition is called).
9. Run `mount /dev/sda1 /mnt` to mount your USB drive on the folder `/mnt`.
10. Run `cp -R /{bin,etc,home,lib,opt,root,sbin,srv,usr,var} /mnt/` to copy the necessary files onto your USB drive. You can also use `rsync` if you prefer.
11. Run `nano /boot/cmdline.txt` to open up the boot configuration of the Pi.
12. Change `root=/dev/mmcblk0p5` to `root=/dev/sda1` to change the location of the root filesystem to your USB drive.
13. Reboot and watch the changes take effect! The output of my `lsblk` looks like this:
  * <pre>
  NAME        MAJ:MIN RM  SIZE RO TYPE MOUNTPOINT
  sda           8:0    1 57.7G  0 disk
  `-sda1        8:1    1 57.7G  0 part /
  mmcblk0     179:0    0  1.9G  0 disk
  |-mmcblk0p1 179:1    0   90M  0 part /boot
  |-mmcblk0p2 179:2    0    1K  0 part
  `-mmcblk0p5 179:5    0  1.7G  0 part</pre>
  * NOTE: `/dev/sda1/` now contains the root filesystem.
14. If you're feeling particularly nitpicky, feel free to use `cfdisk` to remove partitions `mmcblk0p2` and `mmcblk0p5` and expand `mmcblk0p1` to use the full disk. This is obviously unnecessary, so is therefore not included in this tutorial.
