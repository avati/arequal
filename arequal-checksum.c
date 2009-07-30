/*
  Copyright (c) 2009 Z RESEARCH, Inc. <http://www.zresearch.com/>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ftw.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <libgen.h>
#include <stdint.h>
#include <alloca.h>
#include <dirent.h>


int debug = 0;

#define DBG(fmt ...) do {			\
		if (debug) {			\
			fprintf (stderr, "D "); \
			fprintf (stderr, fmt);	\
		}				\
	} while (0)


static inline int roof(int a, int b)
{
	return ((((a)+(b)-1)/((b)?(b):1))*(b));
}


/* All this runs in single thread, hence using 'global' variables */

unsigned long long     avg_uid_file    = 0;
unsigned long long     avg_uid_dir     = 0;
unsigned long long     avg_uid_symlink = 0;
unsigned long long     avg_uid_other   = 0;

unsigned long long     avg_gid_file    = 0;
unsigned long long     avg_gid_dir     = 0;
unsigned long long     avg_gid_symlink = 0;
unsigned long long     avg_gid_other   = 0;

unsigned long long     avg_mode_file    = 0;
unsigned long long     avg_mode_dir     = 0;
unsigned long long     avg_mode_symlink = 0;
unsigned long long     avg_mode_other   = 0;

unsigned long long global_ctime_checksum = 0;


unsigned long long      count_dir        = 0;
unsigned long long      count_file       = 0;
unsigned long long      count_symlink    = 0;
unsigned long long      count_other      = 0;


unsigned long long      checksum_file1   = 0;
unsigned long long      checksum_file2   = 0;
unsigned long long      checksum_dir     = 0;
unsigned long long      checksum_symlink = 0;
unsigned long long      checksum_other   = 0;


unsigned long long
checksum_path (const char *path)
{
	unsigned long long   csum = 0;
	unsigned long long  *nums = 0;
	int                  len = 0;
	int                  cnt = 0;

	len = roof (strlen (path), sizeof (csum));
	cnt = len / sizeof (csum);

	nums = alloca (len);
	memset (nums, 0, len);
	strcpy ((char *)nums, path);

	while (cnt) {
		csum ^= *nums;
		nums++;
		cnt--;
	}

	return csum;
}


int
checksum_md5 (const char *path, const struct stat *sb)
{
        uint64_t    this_data_checksum = 0;
	FILE       *filep              = NULL;
	char        cmd[4096 + 64]     = {0,};
	char        strvalue[17]       = {0,};
	int         ret                = -1;


        sprintf (cmd, "md5sum '%s'", path);

        filep = popen (cmd, "r");
        if (!filep) {
		perror (path);
		goto out;
        }

        if (fread (strvalue, sizeof (char), 16, filep) != 16) {
		fprintf (stderr, "%s: short read\n", path);
		goto out;
	}

        this_data_checksum = strtoull (strvalue, NULL, 16);
        if (-1 == this_data_checksum) {
                fprintf (stderr, "%s: %s\n", strvalue, strerror (errno));
		goto out;
        }
        checksum_file1 ^= this_data_checksum;

        if (fread (strvalue, sizeof (char), 16, filep) != 16) {
		fprintf (stderr, "%s: short read\n", path);
		goto out;
	}

        this_data_checksum = strtoull (strvalue, NULL, 16);
        if (-1 == this_data_checksum) {
                fprintf (stderr, "%s: %s\n", strvalue, strerror (errno));
		goto out;
        }
        checksum_file2 ^= this_data_checksum;

	ret = 0;
out:
	if (filep)
		pclose (filep);

        return ret;
}


int
checksum_filenames (const char *path, const struct stat *sb)
{
	DIR                *dirp = NULL;
	struct dirent      *entry = NULL;
	unsigned long long  csum = 0;

	dirp = opendir (path);
	if (!dirp) {
		perror (path);
		goto out;
	}

	errno = 0;
	while ((entry = readdir (dirp))) {
		csum = checksum_path (entry->d_name);
		checksum_dir ^= csum;
	}

	if (errno) {
		perror (path);
		goto out;
	}

out:
	if (dirp)
		closedir (dirp);

	return 0;
}


int
process_file (const char *path, const struct stat *sb)
{
	int    ret = 0;

	count_file++;

	avg_uid_file ^= sb->st_uid;
	avg_gid_file ^= sb->st_gid;
	avg_mode_file ^= sb->st_mode;

	ret = checksum_md5 (path, sb);

	return ret;
}


int
process_dir (const char *path, const struct stat *sb)
{
	unsigned long long csum = 0;

	count_dir++;

	avg_uid_dir ^= sb->st_uid;
	avg_gid_dir ^= sb->st_gid;
	avg_mode_dir ^= sb->st_mode;

	csum = checksum_filenames (path, sb);

	checksum_dir ^= csum;

	return 0;
}


int
process_symlink (const char *path, const struct stat *sb)
{
	int                ret = 0;
	char               buf[4096] = {0, };
	unsigned long long csum = 0;

	count_symlink++;

	avg_uid_symlink ^= sb->st_uid;
	avg_gid_symlink ^= sb->st_gid;
	avg_mode_symlink ^= sb->st_mode;

	ret = readlink (path, buf, 4096);
	if (ret < 0) {
		perror (path);
		goto out;
	}

	DBG ("readlink (%s) => %s\n", path, buf);

	csum = checksum_path (buf);

	DBG ("checksum_path (%s) => %llx\n", buf, csum);

	checksum_symlink ^= csum;

	ret = 0;
out:
	return ret;
}


int
process_other (const char *path, const struct stat *sb)
{
	count_other++;

	avg_uid_other ^= sb->st_uid;
	avg_gid_other ^= sb->st_gid;
	avg_mode_other ^= sb->st_mode;

	checksum_other ^= sb->st_rdev;

	return 0;
}


int
process_entry (const char *path, const struct stat *sb,
	       int typeflag, struct FTW *ftwbuf)
{
	int ret = 0;

	DBG ("processing entry %s\n", path);

	switch ((S_IFMT & sb->st_mode)) {
	case S_IFDIR:
		ret = process_dir (path, sb);
		break;
	case S_IFREG:
		ret = process_file (path, sb);
		break;
	case S_IFLNK:
		ret = process_symlink (path, sb);
		break;
	default:
		ret = process_other (path, sb);
		break;
	}

        return ret;
}


int
display_counts (FILE *fp)
{
	fprintf (fp, "\n");
	fprintf (fp, "Entry counts\n");
	fprintf (fp, "Regular files   : %lld\n", count_file);
	fprintf (fp, "Directories     : %lld\n", count_dir);
	fprintf (fp, "Symbolic links  : %lld\n", count_symlink);
	fprintf (fp, "Other           : %lld\n", count_other);
	fprintf (fp, "Total           : %lld\n",
		 (count_file + count_dir + count_symlink + count_other));

	return 0;
}


int
display_checksums (FILE *fp)
{
	fprintf (fp, "\n");
	fprintf (fp, "Checksums\n");
	fprintf (fp, "Regular files   : %llx%llx\n", checksum_file1, checksum_file2);
	fprintf (fp, "Directories     : %llx\n", checksum_dir);
	fprintf (fp, "Symbolic links  : %llx\n", checksum_symlink);
	fprintf (fp, "Other           : %llx\n", checksum_other);
	fprintf (fp, "Total           : %llx\n",
		 (checksum_file1 ^ checksum_file2 ^ checksum_dir ^ checksum_symlink ^ checksum_other));

	return 0;
}


int
display_metadata (FILE *fp)
{
	fprintf (fp, "\n");
	fprintf (fp, "Metadata checksums\n");
	fprintf (fp, "Regular files   : %llx\n",
		 (avg_uid_file + 13) * (avg_gid_file + 11) * (avg_mode_file + 7));
	fprintf (fp, "Directories     : %llx\n",
		 (avg_uid_dir + 13) * (avg_gid_dir + 11) * (avg_mode_dir + 7));
	fprintf (fp, "Symbolic links  : %llx\n",
		 (avg_uid_symlink + 13) * (avg_gid_symlink + 11) * (avg_mode_symlink + 7));
	fprintf (fp, "Other           : %llx\n",
		 (avg_uid_other + 13) * (avg_gid_other + 11) * (avg_mode_other + 7));

	return 0;
}

int
display_stats (FILE *fp)
{
	display_counts (fp);

	display_metadata (fp);

	display_checksums (fp);

	return 0;
}


int
main(int argc, char *argv[])
{
	int  ret = 0;

	if (argc != 2)  {
		fprintf (stderr, "Usage: %s <directory>\n",
                         argv[0]);
		return -1;
	}

	ret = nftw (argv[1], process_entry, 30, FTW_PHYS|FTW_MOUNT);
	if (ret != 0) {
		fprintf (stderr, "ftw (%s) returned %d (%s), terminating\n",
			 argv[1], ret, strerror (errno));
		return 1;
	}

	display_stats (stdout);

	return 0;
}
