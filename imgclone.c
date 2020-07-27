/*
Copyright (c) 2018 Raspberry Pi (Trading) Ltd.
All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

This program was adjusted to be able to clone the Raspberry Pi SD card to a .img file

see:
https://github.com/tom-2015/imgclone.git

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>

/*---------------------------------------------------------------------------*/
/* Variable and macro definitions */
/*---------------------------------------------------------------------------*/

/* struct to store partition data */

#define MAXPART 9

typedef struct
{
    int pnum;
    long long int start;
    long long int end;
    char ptype[10];
    char ftype[20];
    char flags[10];
} partition_t;

partition_t parts[MAXPART];

/*---------------------------------------------------------------------------*/
/* System helpers */

/* Call a system command and read the first string returned */

static int get_string (char *cmd, char *name)
{
    FILE *fp;
    char buf[64];
    int res;

    name[0] = 0;
	printf("%s\n", cmd);
    fp = popen (cmd, "r");
    if (fp == NULL) return 0;
    if (fgets (buf, sizeof (buf) - 1, fp) == NULL)
    {
        pclose (fp);
        return 0;
    }
    else
    {
        pclose (fp);
        res = sscanf (buf, "%s", name);
        if (res != 1) return 0;
        return 1;
    }
}

/* System function with printf formatting */

static int sys_printf (const char * format, ...)
{
    char buffer[256];
	char output[256];
    va_list args;
    FILE *fp;

    va_start (args, format);
    vsprintf (buffer, format, args);
	printf ("%s\n",buffer);
    fp = popen (buffer, "r");
	while (fgets(output, sizeof(output), fp) != NULL) {
		printf("%s", output);
	}
    va_end (args);
    return pclose (fp);
}



/* Get a partition name - format is different on mmcblk from sd */

static char *partition_name (char *device, char *buffer)
{
    if (!strncmp (device, "/dev/mmcblk", 11) || !strncmp(device, "/dev/loop", 9))
        sprintf (buffer, "%sp", device);
    else
        sprintf (buffer, "%s", device);
    return buffer;
}


static void escape_shell_arg(char * dst_buffer, const char * src_buffer){
	while (*src_buffer!='\0'){
		if (*src_buffer=='"' || *src_buffer=='\\'){
			*dst_buffer='\\';
			*dst_buffer++;
		}
		*dst_buffer=*src_buffer;
		src_buffer++;
		dst_buffer++;
	}
	*dst_buffer='\0';
}

/* clone_to_img
   This function starts clone to img file
	@param src_dev the source device to clone
	@param dst_file the destination disk file to clone to (.IMG)
	@param new_uuid if 1, new uuid will be generated for destination
	@param extra_space add extra free space to the image file for future expansion
*/
int clone_to_img (char * src_dev, char * dst_file, char new_uuid, long long int extra_space)
{
    char buffer[256], res[256], dev[16], uuid[64], puuid[64], npuuid[64], dst_dev[64], src_mnt[64], dst_mnt[64], dst_file_escaped[512];
    int n, p, lbl, uid, puid;
    long long int srcsz, dstsz, stime, total_blocks=0, blocks_available=0, file_size_needed=0,available_free_space=0;
    double prog;
    FILE *fp;
	
	escape_shell_arg(dst_file_escaped, dst_file);

    // get a new partition UUID
    get_string ("uuid | cut -f1 -d-", npuuid);

    // check the source has an msdos partition table
    sprintf (buffer, "parted %s unit s print | tail -n +4 | head -n 1", src_dev);
    fp = popen (buffer, "r");
    if (fp == NULL) return 1;
    if (fgets (buffer, sizeof (buffer) - 1, fp) == NULL)
    {
        pclose (fp);
        fprintf(stderr,"Unable to read source.\n");
        return 2;
    }
    pclose (fp);
    if (strncmp (buffer, "Partition Table: msdos", 22))
    {
        fprintf(stderr,"Non-MSDOS partition table on source.\n");
        return 3;
    }
    
    // prepare temp mount points
    get_string ("mktemp -d", src_mnt);
    get_string ("mktemp -d", dst_mnt);

	printf ("-----------------------------------------------\n");
	printf ("----    READING PARTITIONS               ------\n");
	printf ("-----------------------------------------------\n");
	
    // read in the source partition table
    n = 0;
    sprintf (buffer, "parted %s unit s print | sed '/^ /!d'", src_dev);
    fp = popen (buffer, "r");
    if (fp != NULL)
    {
        while (1)
        {
            if (fgets (buffer, sizeof (buffer) - 1, fp) == NULL) break;
            if (n >= MAXPART)
            {
                pclose (fp);
                fprintf(stderr,"Too many partitions on source.\n");
                return 4;
            }
            
			sscanf (buffer, "%d %llds %llds %*ds %s %s %s", &(parts[n].pnum), &(parts[n].start),
                &(parts[n].end), (char *) &(parts[n].ptype), (char *) &(parts[n].ftype), (char *) &(parts[n].flags));

			printf("Partition %d start: %lld end: %lld ptype:%s ftype:%s flags: %s\n.", n+1, parts[n].start, parts[n].end, parts[n].ptype, parts[n].ftype, parts[n].flags);			
            n++;
			
        }
        pclose (fp);
    }

	//get the needed size of the destination image_file
	//this is the start of the last partition + used space on last partition
	
	// belt-and-braces call to partprobe to make sure devices are found...
	get_string ("partprobe", res);
	
	file_size_needed=parts[n-1].start*(long long int)512; //need at least the start of last partition as image size (blocks * block_size) in bytes
	printf("Last partition starts at %lld bytes.\n", file_size_needed);
	
	//mount last partition to get used disk space
	if (sys_printf ("mount %s%d %s", partition_name (src_dev, dev), parts[n-1].pnum, src_mnt))
	{
		fprintf(stderr,"Could not mount partition ");
		fprintf(stderr,partition_name (src_dev, dev));
		fprintf(stderr,"\n");
		return 5;
	}

	sprintf (buffer, "df %s | tail -n 1 | tr -s \" \" \" \" | cut -d ' ' -f 2", src_mnt);
	get_string (buffer, res);
	sscanf (res, "%lld", &total_blocks);
	sprintf (buffer, "df %s | tail -n 1 | tr -s \" \" \" \" | cut -d ' ' -f 4", src_mnt);
	get_string (buffer, res);
	sscanf (res, "%lld", &blocks_available);
	
	long long int partition_size_used = (total_blocks - blocks_available) * 1024; //blocks used reported by df is not including all reserved space for filesystem, seems better to take the blocks available
	printf("Used size of last partition is %lld bytes.\n", src_mnt, partition_size_used);
	
	if (sys_printf ("umount %s", src_mnt))
	{
		fprintf(stderr,"Could not unmount partition.\n");
		return 6;
	}
	
	file_size_needed+=partition_size_used;
	file_size_needed+=file_size_needed*(long long int)2/(long long int)100; //add 2% extra space
	file_size_needed+=extra_space; //add extra free space if required
	if ((file_size_needed%512)!=0)	file_size_needed+=(long long int)(512-(file_size_needed%512)); //align at 512 byte block size
	printf("Required size for destination image: %lld bytes\n", file_size_needed);

	printf ("-----------------------------------------------\n");
	printf ("----    ALLOCATING SPACE FOR .IMG FILE   ------\n");
	printf ("-----------------------------------------------\n");
	
	//create file
	if (sys_printf("touch \"%s\"", dst_file_escaped)){
		fprintf(stderr,"Could not create destination file %s.\n", dst_file);
		return 24;
	}

	//check if file is on a different disk device
	sprintf (buffer, "df \"%s\" | tail -n 1 | tr -s \" \" \" \" | cut -d ' ' -f 1", dst_file_escaped);
	get_string (buffer, res);	
	printf("%s\n", res);
	if (strncmp(src_dev, buffer, strlen(src_dev))==0){
		fprintf(stderr, "Destination file is located on the disk to clone. Destination file must be on external drive.\n");
		return 25;
	}
	
	//check if there is enough space on the destination disk
	sprintf (buffer, "df --output=avail -B 1 \"%s\" | tail -n 1", dst_file_escaped);
	get_string (buffer, res);
	printf("%s\n", res);
	sscanf (res, "%lld", &available_free_space);
	if (available_free_space < file_size_needed){
		//sys_printf("rm %s", dst_file);
		fprintf(stderr, "Not enough free space to create destination image file %lld free, required %lld bytes.\n", available_free_space, file_size_needed);
		return 26;
	}
	
	//make file size big enough
	if (sys_printf("truncate --size %lld \"%s\"", file_size_needed, dst_file_escaped)){
		fprintf(stderr,"Could not create file large enough on destination disk.\n");
		return 23;
	}

	printf ("-----------------------------------------------\n");
	printf ("----  CREATING DISK DEVICE FOR .IMG FILE ------\n");
	printf ("-----------------------------------------------\n");
	
	//create device, returns the /dev/loopX interface, which is dst_dev
	sprintf(buffer, "losetup --show -f \"%s\"", dst_file_escaped);
	get_string(buffer, dst_dev);
	
	//printf("Unmounting partitions on target\n");
    // unmount any partitions on the target device
    //for (n = 9; n >= 1; n--)
    //{
    //    sys_printf ("umount %s%d", partition_name (dst_dev, dev), n);
    //}

    // wipe the FAT on the target
    if (sys_printf ("dd if=/dev/zero of=%s bs=512 count=1", dst_dev))
    {
        fprintf(stderr,"Could not write to destination device ");
		fprintf(stderr,dst_dev);
		fprintf(stderr,"\n");
        return 7;
    }
	
	printf("Creating FAT on %s.\n", dst_dev);	
    
	// prepare the new FAT
    if (sys_printf ("parted -s %s mklabel msdos", dst_dev))
    {
        fprintf(stderr,"Could not create FAT.\n");
        return 8;
    }	
	
	printf ("-----------------------------------------------\n");
	printf ("----    CREATING PARTITIONS PLEASE WAIT  ------\n");
	printf ("-----------------------------------------------\n");
	
    // recreate the partitions on the target
    for (p = 0; p < n; p++)
    {
        // create the partition
        if (!strcmp (parts[p].ptype, "extended"))
        {
            if (sys_printf ("parted -s %s -- mkpart extended %llds -1s", dst_dev, parts[p].start))
            {
                fprintf(stderr,"Could not create partition.\n");
                return 9;
            }
        }
        else
        {
            if (p == (n - 1))
            {
                if (sys_printf ("parted -s %s -- mkpart %s %s %llds -1s", dst_dev,
                    parts[p].ptype, parts[p].ftype, parts[p].start))
                {
                    fprintf(stderr,"Could not create partition.\n");
                    return 10;
                }
            }
            else
            {
                if (sys_printf ("parted -s %s mkpart %s %s %llds %llds", dst_dev,
                    parts[p].ptype, parts[p].ftype, parts[p].start, parts[p].end))
                {
                    fprintf(stderr,"Could not create partition.\n");
                    return 11;
                }
            }
        }

        // refresh the kernel partion table
        sys_printf ("partprobe");

        // get the UUID
        sprintf (buffer, "lsblk -o name,uuid %s | grep %s%d | tr -s \" \" | cut -d \" \" -f 2", src_dev, partition_name (src_dev, dev) + 5, parts[p].pnum);
        uid = get_string (buffer, uuid);
        if (uid)
        {
            // sanity check the ID
            if (strlen (uuid) == 9)
            {
                if (uuid[4] == '-')
                {
                    // remove the hyphen from the middle of a FAT volume ID
                    uuid[4] = uuid[5];
                    uuid[5] = uuid[6];
                    uuid[6] = uuid[7];
                    uuid[7] = uuid[8];
                    uuid[8] = 0;
                }
                else uid = 0;
            }
            else if (strlen (uuid) == 36)
            {
                // check there are hyphens in the right places in a UUID
                if (uuid[8] != '-') uid = 0;
                if (uuid[13] != '-') uid = 0;
                if (uuid[18] != '-') uid = 0;
                if (uuid[23] != '-') uid = 0;
            }
            else uid = 0;
        }

        // get the label
        sprintf (buffer, "lsblk -o name,label %s | grep %s%d | tr -s \" \" | cut -d \" \" -f 2", src_dev, partition_name (src_dev, dev) + 5, parts[p].pnum);
        lbl = get_string (buffer, res);
        if (!strlen (res)) lbl = 0;

        // get the partition UUID
        sprintf (buffer, "blkid %s | rev | cut -f 2 -d ' ' | rev | cut -f 2 -d \\\"", src_dev);
        puid = get_string (buffer, puuid);
        if (!strlen (puuid)) puid = 0;

        // create file systems
        if (!strncmp (parts[p].ftype, "fat", 3))
        {
            if (uid) sprintf (buffer, "mkfs.fat -F 32 -i %s %s%d", uuid, partition_name (dst_dev, dev), parts[p].pnum);
            else sprintf (buffer, "mkfs.fat -F 32 %s%d", partition_name (dst_dev, dev), parts[p].pnum);

            if (sys_printf (buffer))
            {
                if (uid)
                {
                    // second try just in case the only problem was a corrupt UUID
                    sprintf (buffer, "mkfs.fat -F 32 %s%d", partition_name (dst_dev, dev), parts[p].pnum);
                    if (sys_printf (buffer))
                    {
                        fprintf(stderr, "Could not create file system on uid %s: %s\n", uuid, buffer);
                        return 12;
                    }
                }
                else
                {
					fprintf(stderr, "Could not create file system: %s\n", buffer);
                    return 13;
                }
            }

            if (lbl) sys_printf ("fatlabel %s%d %s", partition_name (dst_dev, dev), parts[p].pnum, res);
        }

        if (!strcmp (parts[p].ftype, "ext4"))
        {
            if (uid) sprintf (buffer, "mkfs.ext4 -F -U %s %s%d", uuid, partition_name (dst_dev, dev), parts[p].pnum);
            else sprintf (buffer, "mkfs.ext4 -F %s%d", partition_name (dst_dev, dev), parts[p].pnum);

            if (sys_printf (buffer))
            {
                if (uid)
                {
                    // second try just in case the only problem was a corrupt UUID
                    sprintf (buffer, "mkfs.ext4 -F %s%d", partition_name (dst_dev, dev), parts[p].pnum);
                    if (sys_printf (buffer))
                    {
                        fprintf(stderr,"Could not create file system.\n");
                        return 14;
                    }
                }
                else
                {
                    fprintf(stderr,"Could not create file system.\n");
                    return 15;
                }
            }

            if (lbl) sys_printf ("e2label %s%d %s", partition_name (dst_dev, dev), parts[p].pnum, res);
        }

        // write the partition UUID
        if (puid) sys_printf ("echo \"x\ni\n0x%s\nr\nw\n\" | fdisk %s", new_uuid ? npuuid : puuid, dst_dev);

        prog = p + 1;
        prog /= n;
        //gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress), prog);
    }

	printf("%d partitions created, now copy files.\n", n);
	
    // do the copy for each partition
    for (p = 0; p < n; p++)
    {
        // don't try to copy extended partitions
        if (strcmp (parts[p].ptype, "extended"))
        {
            printf ("Copying partition %d of %d...\n", p + 1, n);
			printf ("Copy from %s to %s\n", src_mnt, dst_mnt);
			
            // belt-and-braces call to partprobe to make sure devices are found...
            get_string ("partprobe", res);

            // mount partitions
            if (sys_printf ("mount %s%d %s", partition_name (dst_dev, dev), parts[p].pnum, dst_mnt))
            {
                fprintf(stderr,"Could not mount partition.\n");
                return 16;
            }

            if (sys_printf ("mount %s%d %s", partition_name (src_dev, dev), parts[p].pnum, src_mnt))
            {
                fprintf(stderr,"Could not mount partition.\n");
                return 17;
            }

            // check there is enough space...
            sprintf (buffer, "df %s | tail -n 1 | tr -s \" \" \" \" | cut -d ' ' -f 3", src_mnt);
            get_string (buffer, res);
            sscanf (res, "%lld", &srcsz);

            sprintf (buffer, "df %s | tail -n 1 | tr -s \" \" \" \" | cut -d ' ' -f 4", dst_mnt);
            get_string (buffer, res);
            sscanf (res, "%lld", &dstsz);

            if (srcsz >= dstsz)
            {
                sys_printf ("umount %s", dst_mnt);
                sys_printf ("umount %s", src_mnt);
                fprintf(stderr,"Insufficient space. Backup aborted.\n");
                return 18;
            }

			printf ("-----------------------------------------------\n");
			printf ("----    COPYING FILES PLEASE WAIT        ------\n");
			printf ("-----------------------------------------------\n");
            sys_printf ("cp -ax %s/. %s/.", src_mnt, dst_mnt);

            // get the size to be copied
            /*sprintf (buffer, "du -s %s", src_mnt);
            get_string (buffer, res);
            sscanf (res, "%ld", &srcsz);
            if (srcsz < 50000) stime = 1;
            else if (srcsz < 500000) stime = 5;
            else stime = 10;

            // wait for the copy to complete, while updating the progress bar...
            sprintf (buffer, "du -s %s", dst_mnt);
            while (copying)
            {
                get_string (buffer, res);
                sscanf (res, "%ld", &dstsz);
                prog = dstsz;
                prog /= srcsz;
                gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress), prog);
                sleep (stime);
                CANCEL_CHECK;
            }

            gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress), 1.0);*/

            // fix up relevant files if changing partition UUID
            if (puid && new_uuid)
            {
                // relevant files are dst_mnt/etc/fstab and dst_mnt/boot/cmdline.txt
                sys_printf ("if [ -e /%s/etc/fstab ] ; then sed -i s/%s/%s/g /%s/etc/fstab ; fi", dst_mnt, puuid, npuuid, dst_mnt);
                sys_printf ("if [ -e /%s/cmdline.txt ] ; then sed -i s/%s/%s/g /%s/cmdline.txt ; fi", dst_mnt, puuid, npuuid, dst_mnt);
            }

            // unmount partitions
            if (sys_printf ("umount %s", dst_mnt))
            {
                fprintf(stderr,"Could not unmount partition.\n");
                return 19;
            }

            if (sys_printf ("umount %s", src_mnt))
            {
                fprintf(stderr,"Could not unmount partition.\n");
                return 20;
            }

			//if (sys_printf ("umount %s%d", partition_name (dst_dev, dev), parts[p].pnum)){
				
			//}
        }

        // set the flags
        if (!strcmp (parts[p].flags, "lba"))
        {
            if (sys_printf ("parted -s %s set %d lba on", dst_dev, parts[p].pnum))
            {
                fprintf(stderr,"Could not set flags.\n");
                return 21;
            }
        }
        else
        {
            if (sys_printf ("parted -s %s set %d lba off", dst_dev, parts[p].pnum))
            {
                fprintf(stderr,"Could not set flags.\n");
                return 22;
            }
        }
    }
	
	//release the image file
	if (sys_printf("losetup -d %s", dst_dev)){
		fprintf(stderr,"Error releasing device %d.\n", dst_dev);
		return 24;
	}else{
		printf("Backup completed!\n");
	}
	
	//delete partitions from devices
	for (p = 0; p < n; p++){
		sys_printf ("rm %s%d", partition_name (dst_dev, dev), parts[p].pnum);
	}
	
    return 0;
}


/*---------------------------------------------------------------------------*/
/* Main function */
int main (int argc, char *argv[])
{
	char dst_file[64];
	char src_dev[64];
	char new_uuid=0;
	long long int extra_space=(long long int)512*(long long int)20480; //10MB extra space
	int i;
	
	printf ("----    Raspberry Pi clone to image V1.1    ---\n");
	printf ("-----------------------------------------------\n");
	printf ("---- DO NOT CHANGE FILES ON YOUR SD CARD    ---\n");
	printf ("---- WHILE THE BACKUP PROGRAM IS RUNNING    ---\n");
	printf ("---- THE DESTINATION .IMG FILE MUST BE      ---\n");
	printf ("---- ON AN EXTERNAL STORAGE / NETWORK SHARE ---\n");
	printf ("-----------------------------------------------\n");
	
	sprintf(src_dev, "/dev/mmcblk0");
	dst_file[0]=0;
	
	for (i=1;i<argc;i++){
		if (strcmp(argv[i], "-d")==0){
			i++;
			if (i<argc){
				stpcpy(dst_file,argv[i]);
			}else{
				fprintf(stderr,"Missing file name for -s.\n");
				return 1;
			}
		}else if (strcmp(argv[i], "-s")==0){
			i++;
			if (i<argc){
				stpcpy(argv[i],src_dev);
			}else{
				fprintf(stderr,"Missing file name for -s.\n");
				return 1;
			}
		}else if (strcmp(argv[i], "-u")==0){
			i++;
			if (i<argc){
				if (argv[i][0]!='0') new_uuid=1;
			}else{
				fprintf(stderr,"Missing file name for -s.\n");
				return 1;
			}			
		}else if (strcmp(argv[i], "-x")==0){
			i++;
			if (i<argc){
				sscanf(argv[i], "%lld", &extra_space);
			}else{
				fprintf(stderr,"Missing byte count for -x.\n");
				return 1;
			}
		}else if (strcmp(argv[i], "-?")==0){
			printf("Create a backup of your SD card to an image file.\n");
			printf("Warning! the <destination_file> image file must be located on an external drive, you cannot backup to a file on the SD card!\n");
			printf("Usage:\n");
			printf("imgclone [-u 1] [-s <source_device>] -d <destination_file>\n");
			printf("	-u 1				   optional creates new UUID for the partitions.\n");
			printf("	-s <source_device>     creates a backup of source_device, optional default is /dev/mmcblk0.\n");
			printf("	-d <destination_file>  backup to destination_file.\n");
			printf("    -x <number>            add <number> extra bytes of free space to the last partition.\n");
			return 0;
		}else{
			fprintf(stderr,"Invalid argument ");
			fprintf(stderr,argv[i]);
			fprintf(stderr,"\n");
			return 1;
		}
	}
	
	if (strlen(dst_file)==0){
		fprintf(stderr,"Missing destination file argument (-d <destination_file>).\n");
		return 1;
	}
	

	
	printf("Cloning %s to %s\n", src_dev, dst_file);
	
	return clone_to_img(src_dev, dst_file, new_uuid, extra_space);
}
