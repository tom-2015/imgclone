# imgclone
This program can be used to create a backup of your Raspberry Pi SD card while it's active. It does this in the same way as the official piclone program (https://github.com/raspberrypi-ui/piclone).
Main differences are:
* Command line based, so it can be used to automatically create backups
* It creates a backup to a disk image file (.img). So you can create multiple backups on 1 external SD card, USB drive, USB hard disk or even a network shared drive.
* Result backup will only be a few percent larger than the used disk space of your Raspberry SD card!

# Installation
On the raspberry you open a terminal window and type following commands:
* `sudo apt-get update`
* `sudo apt-get install gcc make git`
* `git clone https://github.com/tom-2015/imgclone.git`
* `cd imgclone`
* `make`
* `sudo make install`

# Usage
Attach external storage device (USB, Hard disk or mount a Samba share). cd to the drive:
* `cd /media/pi/<external drive>`
Start backup:
* `imgclone -d mybackup.img`

To compress the image AFTER it is made use the -gzip or -bzip2 arguments.
Show copy progress with the -p argument.

# backup to network drive
Make sure you have a NAS or other Samba shared drive in your network, then just mount it:
* `mkdir /tmp/backup`
* `sudo mount -t cifs //<share_drive_ip>/<share_folder_name> /tmp/backup`
* `imgclone -d /tmp/backup/mybackup.img`

# restore backup
You can use standard procedure (dd or win32diskimager) to write the .img file to a (new) SD card.
Insert to your Raspberry and start it! Run sudo raspi-config to expand the file system of the root partition to fill the entire SD card.
See https://www.raspberrypi.org/documentation/installation/installing-images/README.md