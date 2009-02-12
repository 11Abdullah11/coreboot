/*
 * lar - lightweight archiver
 *
 * Copyright (C) 2006-2007 coresystems GmbH
 * (Written by Stefan Reinauer <stepan@coresystems.de> for coresystems GmbH)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA, 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "lar.h"
#include "lib.h"

#define MAX_PATH 1024

static struct file *files = NULL;

/**
 * The default "compress impossible" hook to call when no other compression
 * is used
 */
void compress_impossible(char *in, int in_len, char *out, int *out_len)
{
	fprintf(stderr,
		"The selected compression algorithm wasn't compiled in.\n");
	exit(1);
}

/**
 * The default "compress" hook to call when no other compression is used
 */
void do_no_compress(char *in, int in_len, char *out, int *out_len)
{
	memcpy(out, in, in_len);
	out_len[0] = in_len;
}

/**
 * The zeroes "compress" hook
 * Leave one byte for calculating the checksum.
 */
void do_zeroes_compress(char *in, int in_len, char *out, int *out_len)
{
	out[0] = 0;
	out_len[0] = 1;
}

/**
 * The zeroes "uncompress" hook
 */

void do_zeroes_uncompress(char *dst, int dst_len, char *src, int src_len)
{
	memset(dst, 0, dst_len);
}

/**
 * The default "uncompress" hook to call when no other compression is used
 */

void do_no_uncompress(char *dst, int dst_len, char *src, int src_len)
{
	if (dst_len == src_len)
		memcpy(dst, src, dst_len);
	else
	{
		fprintf(stderr, "%s: src_len(%d)!=dst_len(%d)\n",
			__func__, src_len, dst_len);
		exit(1);
	}
}

/**
 * The default "uncompress" hook to call when no other compression is used
 */
void uncompress_impossible(char *dst, int dst_len, char *src, int src_len)
{
	fprintf(stderr,
		"Cannot uncompress data (algorithm not compiled in).\n");
	exit(1);
}


/**
 * Create a new directory including any missing parent directories.
 *
 * NOTE: This function does not do complete path resolution as described in
 * Linux path_resolution(2) and hence will fail for complex paths:
 *
 * e.g.: mkdirp_below("subdir", "subdir/../subdir/x", 0777);
 *
 * This call should create subdir/x, but since subdir/.. is outside subdir,
 * the function returns an error.
 *
 * @param parent Return an error if a new directory would be created outside
 * this directory. Pass "/" to allow new directories to be created anywhere.
 * @param dirpath The new directory that should be created, the path can be
 * either absolute or relative to the current working directory. (It is not
 * relative to parent.)
 * @param mode Permissions to use for newly created directories.
 */
int mkdirp_below(const char *parent, const char *dirpath, mode_t mode)
{
	int ret = -1;
	size_t dirsep, parlen, sublen;
	char c, *r, *path = NULL, *subdir, rpar[PATH_MAX], rsub[PATH_MAX];

	if (!dirpath) {
		fprintf(stderr, "mkdirp_below: No new directory specified\n");
		goto done;
	}

	path = strdup(dirpath);
	if (!path) {
		perror("Duplicate new directory failed:");
		goto done;
	}

	if (NULL == realpath(parent, rpar)) {
		fprintf(stderr, "realpath(%s) failed: %s\n", parent,
			strerror(errno));
		goto done;
	}
	parlen = strlen(rpar);

	for (subdir = path, dirsep = 0; subdir[dirsep]; subdir += dirsep) {
		dirsep = strcspn(subdir, "/\\");
		if (!dirsep) {
			subdir++;
			continue;
		}

		c = subdir[dirsep];
		subdir[dirsep] = 0;
		r = realpath(path, rsub);
		sublen = strlen(rsub);
		if (NULL == r) {
			if(ENOENT != errno) {
				fprintf(stderr, "realpath(%s) failed: %s\n",
					path, strerror(errno));
				goto done;
			}
		} else if (sublen < parlen || strncmp(rpar, rsub, parlen)) {
			fprintf(stderr, "Abort: %s is outside %s\n", dirpath,
				parent);
			goto done;
		}
		if(-1 == mkdir(path, mode) && EEXIST != errno) {
			fprintf(stderr, "mkdir(%s): %s\n", path,
				strerror(errno));
			goto done;
		}
		subdir[dirsep] = c;
	}
	ret = 0;

done:
	if (path)
		free(path);
	return ret;
}

static int handle_directory(const char *name, const char *pathname,
				const enum compalgo thisalgo)
{
	int n;
	int ret = -1;
	struct dirent **namelist;

	n = scandir(name, &namelist, 0, alphasort);

	if (n < 0) {
		fprintf(stderr, "Could not enter directory %s\n", name);
	} else {
		while (n--) {
			char fullname[MAX_PATHLEN+1];
			char fullpathname[MAX_PATHLEN+1];
			int len;

			if (strncmp("..", namelist[n]->d_name, 3) &&
			    strncmp(".", namelist[n]->d_name, 2)) {

				len = strlen(name);
				len += (name[len-1]=='/' ? 1 : 0);
				len += strlen(namelist[n]->d_name);

				if (len > MAX_PATHLEN) {
					fprintf(stderr,
						"%s: %s+%s exceeds MAX_PATHLEN.\n",
						__func__, name,
						namelist[n]->d_name);
					return -1;
				}

				strcpy(fullname, name);
				if (name[(strlen(name)) - 1] != '/') {
					strcat(fullname, "/");
				}

				strcat(fullname, namelist[n]->d_name);

				len = strlen(pathname);
				len += (pathname[len-1]=='/'?1:0);
				len += strlen(namelist[n]->d_name);

				if (len > MAX_PATHLEN) {
					fprintf(stderr,
						"%s: %s+%s exceeds MAX_PATHLEN.\n",
						__func__, pathname,
						namelist[n]->d_name);
					return -1;
				}

				strcpy(fullpathname, pathname);
				if (pathname[(strlen(pathname)) - 1] != '/') {
					strcat(fullpathname, "/");
				}

				strcat(fullpathname, namelist[n]->d_name);

				add_files(fullname, fullpathname, thisalgo);
			}
			free(namelist[n]);

		}
		free(namelist);
		ret = 0;
	}

	return ret;
}

/*
 * Add physically existing files to the file list.
 * This function is used when an archive is created or added to.
 */

int add_files(const char *filename, const char * pathname,
		const enum compalgo thisalgo)
{
	struct stat filestat;
	int ret = -1;
	char *c;

	if (verbose())
		printf("%s: %s:%s\n", __func__, filename, pathname);

	if (stat(filename, &filestat) == -1) {
		fprintf(stderr, "Error getting file attributes of %s\n", filename);
		return -1;
	}

	if (S_ISCHR(filestat.st_mode) || S_ISBLK(filestat.st_mode)) {
		fprintf(stderr, "Device files are not supported: %s\n", filename);
	}

	if (S_ISFIFO(filestat.st_mode)) {
		fprintf(stderr, "FIFOs are not supported: %s\n", filename);
	}

	if (S_ISSOCK(filestat.st_mode)) {
		fprintf(stderr, "Sockets are not supported: %s\n", filename);
	}

	if (S_ISLNK(filestat.st_mode)) {
		fprintf(stderr, "Symbolic links are not supported: %s\n", filename);
	}
	/* Is it a directory? */
	if (S_ISDIR(filestat.st_mode)) {
		ret = handle_directory(filename, pathname, thisalgo);
	}
	/* Is it a regular file? */
	if (S_ISREG(filestat.st_mode)) {
		struct file *tmpfile;

		if (verbose())
			printf("... adding %s\n", filename);
		tmpfile = malloc(sizeof(struct file));
		if (!tmpfile) {
			fprintf(stderr, "Out of memory.\n");
			exit(1);
		}

		tmpfile->name = strdup(filename);
		if (!tmpfile->name) {
			fprintf(stderr, "Out of memory.\n");
			exit(1);
		}

		tmpfile->pathname = strdup(pathname);
		if (!tmpfile->pathname) {
			fprintf(stderr, "Out of memory.\n");
			exit(1);
		}

		tmpfile->algo = thisalgo;

		tmpfile->next = files;
		files = tmpfile;
		ret = 0;
	}

	return ret;
}

/*
 * Add files or directories as specified to the file list.
 * This function is used when an archive is listed or extracted.
 */

int add_file_or_directory(const char *name)
{
	struct file *tmpfile;

	tmpfile = malloc(sizeof(struct file));
	if (!tmpfile) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}

	tmpfile->name = strdup(name);
	if (!tmpfile->name) {
		fprintf(stderr, "Out of memory.\n");
		exit(1);
	}

	tmpfile->next = files;
	tmpfile->pathname = NULL;
	tmpfile->algo = ALGO_INVALID;
	files = tmpfile;

	return 0;
}

struct file *get_files(void)
{
	return files;
}

void free_files(void)
{
	struct file *temp;

	while (files) {
		temp = files;
		files = files->next;
		free(temp->name);
		if (temp->pathname!=NULL)
			free(temp->pathname);
		free(temp);
	}
}

#ifdef DEBUG
int list_files(void)
{
	struct file *walk = files;

	printf("File list:\n");
	while (walk) {
		if (temp->pathname==NULL)
			printf("- %s\n", walk->name);
		else
			printf("- %s:%s\n", walk->name, walk->pathname);
		walk = walk->next;
	}
	printf("-----\n");
	return 0;
}
#endif
