/* Extended Module Player
 * Copyright (C) 1996-2013 Claudio Matsuoka and Hipolito Carraro Jr
 *
 * This file is part of the Extended Module Player and is distributed
 * under the terms of the GNU Lesser General Public License. See COPYING.LIB
 * for more information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#ifdef __native_client__
#include <sys/syslimits.h>
#else
#include <limits.h>
#endif

#ifndef DECRUNCH_MAX
#define DECRUNCH_MAX 5 /* don't recurse more than this */
#endif

#if !defined(HAVE_POPEN) && defined(WIN32) && DECRUNCH_MAX > 0
#include "../win32/ptpopen.h"
#endif

#include "format.h"
#include "list.h"
#include "md5.h"
#include "hio.h"
#include "extras.h"

#ifndef NO_COMPOSITE_LOADER
extern struct format_loader *format_loader[];
#endif

void load_prologue(struct context_data *);
void load_epilogue(struct context_data *);
int prepare_scan(struct context_data *);


struct tmpfilename {
	char *name;
	struct list_head list;
};

#define BUFLEN 16384

static void set_md5sum(HIO_HANDLE *f, unsigned char *digest)
{
	unsigned char buf[BUFLEN];
	MD5_CTX ctx;
	int bytes_read;
	struct stat st;

	if (hio_stat(f, &st) < 0)
		return;

	if (st.st_size <= 0) {
		memset(digest, 0, 16);
		return;
	}

	hio_seek(f, 0, SEEK_SET);

	MD5Init(&ctx);
	while ((bytes_read = hio_read(buf, 1, BUFLEN, f)) > 0) {
		MD5Update(&ctx, buf, bytes_read);
	}
	MD5Final(digest, &ctx);
}

#if DECRUNCH_MAX > 0
int decrunch_arc	(FILE *, FILE *);
int decrunch_arcfs	(FILE *, FILE *);
int decrunch_sqsh	(FILE *, FILE *);
int decrunch_pp		(FILE *, FILE *);
int decrunch_mmcmp	(FILE *, FILE *);
int decrunch_muse	(FILE *, FILE *);
int decrunch_lzx	(FILE *, FILE *);
int decrunch_oxm	(FILE *, FILE *);
int decrunch_xfd	(FILE *, FILE *);
int decrunch_s404	(FILE *, FILE *);
int decrunch_zip	(FILE *, FILE *);
int decrunch_gzip	(FILE *, FILE *);
int decrunch_compress	(FILE *, FILE *);
int decrunch_bzip2	(FILE *, FILE *);
int decrunch_xz		(FILE *, FILE *);
int decrunch_lha	(FILE *, FILE *);
/* int decrunch_zoo	(FILE *, FILE *); */
int test_oxm		(FILE *);
char *test_xfd		(unsigned char *, int);

enum {
	BUILTIN_PP = 0x01,
	BUILTIN_SQSH,
	BUILTIN_MMCMP,
	BUILTIN_ARC,
	BUILTIN_ARCFS,
	BUILTIN_S404,
	BUILTIN_OXM,
	BUILTIN_XFD,
	BUILTIN_MUSE,
	BUILTIN_LZX,
	BUILTIN_ZIP,
	BUILTIN_GZIP,
	BUILTIN_COMPRESS,
	BUILTIN_BZIP2,
	BUILTIN_XZ,
	BUILTIN_LHA,
	/* BUILTIN_ZOO, */
};


#if defined __EMX__ || defined WIN32
#define REDIR_STDERR "2>NUL"
#elif defined unix || defined __unix__
#define REDIR_STDERR "2>/dev/null"
#else
#define REDIR_STDERR
#endif

static int decrunch(struct list_head *head, FILE **f, char **s, int ttl)
{
    unsigned char b[1024];
    char *cmd;
    FILE *t;
    int fd, builtin, res;
    char *temp2, tmp[PATH_MAX];
    struct tmpfilename *temp;
    int headersize;

    cmd = NULL;
    builtin = res = 0;

    if (get_temp_dir(tmp, sizeof(tmp)) < 0)
	return 0;

    strncat(tmp, "xmp_XXXXXX", sizeof(tmp));

    fseek(*f, 0, SEEK_SET);
    if ((headersize = fread(b, 1, 1024, *f)) < 100)	/* minimum valid file size */
	return 0;

#if defined __AMIGA__ && !defined __AROS__
    if (test_xfd(b, 1024)) {
	builtin = BUILTIN_XFD;
    } else
#endif

    if (b[0] == 'P' && b[1] == 'K' &&
	((b[2] == 3 && b[3] == 4) || (b[2] == '0' && b[3] == '0' &&
	b[4] == 'P' && b[5] == 'K' && b[6] == 3 && b[7] == 4))) {

	/* Zip */
	builtin = BUILTIN_ZIP;
    } else if (b[2] == '-' && b[3] == 'l' && b[4] == 'h') {
	/* LHa */
	builtin = BUILTIN_LHA;
    } else if (b[0] == 31 && b[1] == 139) {
	/* gzip */
	builtin = BUILTIN_GZIP;
    } else if (b[0] == 'B' && b[1] == 'Z' && b[2] == 'h') {
	/* bzip2 */
	builtin = BUILTIN_BZIP2;
    } else if (b[0] == 0xfd && b[3] == 'X' && b[4] == 'Z' && b[5] == 0x00) {
	/* xz */
	builtin = BUILTIN_XZ;
#if 0
    } else if (b[0] == 'Z' && b[1] == 'O' && b[2] == 'O' && b[3] == ' ') {
	/* zoo */
	builtin = BUILTIN_ZOO;
#endif
    } else if (b[0] == 'M' && b[1] == 'O' && b[2] == '3') {
	/* MO3 */
	cmd = "unmo3 -s \"%s\" STDOUT";
    } else if (b[0] == 31 && b[1] == 157) {
	/* compress */
	builtin = BUILTIN_COMPRESS;
    } else if (memcmp(b, "PP20", 4) == 0) {
	/* PowerPack */
	builtin = BUILTIN_PP;
    } else if (memcmp(b, "XPKF", 4) == 0 && memcmp(b + 8, "SQSH", 4) == 0) {
	/* SQSH */
	builtin = BUILTIN_SQSH;
    } else if (!memcmp(b, "Archive\0", 8)) {
	/* ArcFS */
	builtin = BUILTIN_ARCFS;
    } else if (memcmp(b, "ziRCONia", 8) == 0) {
	/* MMCMP */
	builtin = BUILTIN_MMCMP;
    } else if (memcmp(b, "MUSE", 4) == 0 && readmem32b(b + 4) == 0xdeadbeaf) {
	/* J2B MUSE */
	builtin = BUILTIN_MUSE;
    } else if (memcmp(b, "MUSE", 4) == 0 && readmem32b(b + 4) == 0xdeadbabe) {
	/* MOD2J2B MUSE */
	builtin = BUILTIN_MUSE;
    } else if (memcmp(b, "LZX", 3) == 0) {
	/* LZX */
	builtin = BUILTIN_LZX;
    } else if (memcmp(b, "Rar", 3) == 0) {
	/* rar */
	cmd = "unrar p -inul -xreadme -x*.diz -x*.nfo -x*.txt "
	    "-x*.exe -x*.com \"%s\"";
    } else if (memcmp(b, "S404", 4) == 0) {
	/* Stonecracker */
	builtin = BUILTIN_S404;
    } else if (test_oxm(*f) == 0) {
	/* oggmod */
	builtin = BUILTIN_OXM;
    }

    if (builtin == 0 && cmd == NULL && b[0] == 0x1a) {
	int x = b[1] & 0x7f;
	int i, flag = 0;
	long size;
	
	/* check file name */
	for (i = 0; i < 13; i++) {
	    if (b[2 + i] == 0) {
		if (i == 0)		/* name can't be empty */
		    flag = 1;
		break;
	    }
	    if (!isprint(b[2 + i])) {	/* name must be printable */
		flag = 1;
		break;
	    }
	}

	size = readmem32l(b + 15);	/* max file size is 512KB */
	if (size < 0 || size > 512 * 1024)
		flag = 1;

        if (flag == 0) {
	    if (x >= 1 && x <= 9 && x != 7) {
		/* Arc */
		builtin = BUILTIN_ARC;
	    } else if (x == 0x7f) {
		/* !Spark */
		builtin = BUILTIN_ARC;
	    }
	}
    }

    fseek(*f, 0, SEEK_SET);

    if (builtin == 0 && cmd == NULL)
	return 0;

#if defined ANDROID || defined __native_client__
    /* Don't use external helpers in android */
    if (cmd)
	return 0;
#endif

    D_(D_WARN "Depacking file... ");

    temp = calloc(sizeof (struct tmpfilename), 1);
    if (!temp) {
	D_(D_CRIT "calloc failed");
	return -1;
    }

    temp->name = strdup(tmp);
    if (temp->name == NULL || (fd = mkstemp(temp->name)) < 0) {
	D_(D_CRIT "failed");
	return -1;
    }

    list_add_tail(&temp->list, head);

    if ((t = fdopen(fd, "w+b")) == NULL) {
	D_(D_CRIT "failed");
	return -1;
    }

    if (cmd) {
#define BSIZE 0x4000
	int n;
	char line[1024], buf[BSIZE];
	FILE *p;

	snprintf(line, 1024, cmd, *s);

#ifdef WIN32
	/* Note: The _popen function returns an invalid file opaque, if
	 * used in a Windows program, that will cause the program to hang
	 * indefinitely. _popen works properly in a Console application.
	 * To create a Windows application that redirects input and output,
	 * read the section "Creating a Child Process with Redirected Input
	 * and Output" in the Win32 SDK. -- Mirko 
	 */
	if ((p = popen(line, "rb")) == NULL) {
#else
	/* Linux popen fails with "rb" */
	if ((p = popen(line, "r")) == NULL) {
#endif
	    D_(D_CRIT "failed");
	    fclose(t);
	    return -1;
	}
	while ((n = fread(buf, 1, BSIZE, p)) > 0)
	    fwrite(buf, 1, n, t);
	pclose (p);
    } else {
	switch (builtin) {
	case BUILTIN_PP:    
	    res = decrunch_pp(*f, t);
	    break;
	case BUILTIN_ARC:
	    res = decrunch_arc(*f, t);
	    break;
	case BUILTIN_ARCFS:
	    res = decrunch_arcfs(*f, t);
	    break;
	case BUILTIN_SQSH:    
	    res = decrunch_sqsh(*f, t);
	    break;
	case BUILTIN_MMCMP:    
	    res = decrunch_mmcmp(*f, t);
	    break;
	case BUILTIN_MUSE:    
	    res = decrunch_muse(*f, t);
	    break;
	case BUILTIN_LZX:    
	    res = decrunch_lzx(*f, t);
	    break;
	case BUILTIN_S404:
	    res = decrunch_s404(*f, t);
	    break;
	case BUILTIN_ZIP:
	    res = decrunch_zip(*f, t);
	    break;
	case BUILTIN_GZIP:
	    res = decrunch_gzip(*f, t);
	    break;
	case BUILTIN_COMPRESS:
	    res = decrunch_compress(*f, t);
	    break;
	case BUILTIN_BZIP2:
	    res = decrunch_bzip2(*f, t);
	    break;
	case BUILTIN_XZ:
	    res = decrunch_xz(*f, t);
	    break;
	case BUILTIN_LHA:
	    res = decrunch_lha(*f, t);
	    break;
#if 0
	case BUILTIN_ZOO:
	    res = decrunch_zoo(*f, t);
	    break;
#endif
	case BUILTIN_OXM:
	    res = decrunch_oxm(*f, t);
	    break;
#ifdef AMIGA
	case BUILTIN_XFD:
	    res = decrunch_xfd(*f, t);
	    break;
#endif
	}
    }

    if (res < 0) {
	D_(D_CRIT "failed");
	fclose(t);
	return -1;
    }

    D_(D_INFO "done");

    fclose(*f);
    *f = t;
 
    if (!--ttl) {
	    return -1;
    }
    
    temp2 = strdup(temp->name);
    res = decrunch(head, f, &temp->name, ttl);
    free(temp2);
    /* Mirko: temp is now deallocated in unlink_tempfiles()
     * not a problem, since unlink_tempfiles() is called after decrunch
     * in loader routines
     *
     * free(temp);
     */

    return res;
}
#endif

#ifndef NO_COMPOSITE_LOADER
/*
 * Windows doesn't allow you to unlink an open file, so we changed the
 * temp file cleanup system to remove temporary files after we close it
 */
static void unlink_tempfiles(struct list_head *head)
{
	struct tmpfilename *li;
	struct list_head *tmp;

	/* can't use list_for_each when freeing the node! */
	for (tmp = head->next; tmp != head; ) {
		li = list_entry(tmp, struct tmpfilename, list);
		D_(D_INFO "unlink tmpfile %s", li->name);
		unlink(li->name);
		free(li->name);
		list_del(&li->list);
		tmp = tmp->next;
		free(li);
	}
}

int xmp_test_module(char *path, struct xmp_test_info *info)
{
	HIO_HANDLE *h;
	struct stat st;
	char buf[XMP_NAME_SIZE];
	int i;
  
	struct list_head tmpfiles_list;
  
	int ret = -XMP_ERROR_FORMAT;;

	if (stat(path, &st) < 0)
		return -XMP_ERROR_SYSTEM;

#ifndef _MSC_VER
	if (S_ISDIR(st.st_mode)) {
		errno = EISDIR;
		return -XMP_ERROR_SYSTEM;
	}
#endif

	if ((h = hio_open_file(path, "rb")) == NULL)
		return -XMP_ERROR_SYSTEM;

	INIT_LIST_HEAD(&tmpfiles_list);

#if DECRUNCH_MAX > 0
	if (decrunch(&tmpfiles_list, &h->f, &path, DECRUNCH_MAX) < 0) {
		ret = -XMP_ERROR_DEPACK;
		goto err;
	}
#endif

	if (hio_stat(h, &st) < 0) {/* get size after decrunch */
		ret = -XMP_ERROR_DEPACK;
		goto err;
	}

	if (st.st_size < 256) {		/* set minimum valid module size */
		ret = -XMP_ERROR_FORMAT;
		goto err;
	}

	if (info != NULL) {
		*info->name = 0;	/* reset name prior to testing */
		*info->type = 0;	/* reset type prior to testing */
	}

	for (i = 0; format_loader[i] != NULL; i++) {
		fseek(h->f, 0, SEEK_SET);
		if (format_loader[i]->test(h, buf, 0) == 0) {
			int is_prowizard = 0;
#ifndef NO_PROWIZARD
			if (strcmp(format_loader[i]->name, "prowizard") == 0) {
				fseek(h->f, 0, SEEK_SET);
				pw_test_format(h->f, buf, 0, info);
				is_prowizard = 1;
			}
#endif
			fclose(h->f);

			unlink_tempfiles(&tmpfiles_list);
			if (info != NULL && !is_prowizard) {
				strncpy(info->name, buf, XMP_NAME_SIZE);
				strncpy(info->type, format_loader[i]->name,
							XMP_NAME_SIZE);
			}
			return 0;
		}
	}

    err:
	hio_close(h);
	unlink_tempfiles(&tmpfiles_list);
	return ret;
}

static char *get_dirname(char *name)
{
	char *div, *dirname;
	int len;

	if ((div = strrchr(name, '/'))) {
		len = div - name + 1;
		dirname = malloc(len + 1);
		if (dirname != NULL) {
			memcpy(dirname, name, len);
			dirname[len] = 0;
		}
	} else {
		dirname = strdup("");
	}

	return dirname;
}

static char *get_basename(char *name)
{
	char *div, *basename;

	if ((div = strrchr(name, '/'))) {
		basename = strdup(div + 1);
	} else {
		basename = strdup(name);
	}

	return basename;
}

int xmp_load_module(xmp_context opaque, char *path)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct module_data *m = &ctx->m;
	HIO_HANDLE *h;
	struct stat st;
	struct list_head tmpfiles_list;
	int test_result, load_result;
	int i, ret;

	D_(D_WARN "path = %s", path);

	if (stat(path, &st) < 0)
		return -XMP_ERROR_SYSTEM;

#ifndef _MSC_VER
	if (S_ISDIR(st.st_mode)) {
		errno = EISDIR;
		return -XMP_ERROR_SYSTEM;
	}
#endif

	if ((h = hio_open_file(path, "rb")) == NULL)
		return -XMP_ERROR_SYSTEM;

	INIT_LIST_HEAD(&tmpfiles_list);

#if DECRUNCH_MAX > 0
	D_(D_INFO "decrunch");
	if (decrunch(&tmpfiles_list, &h->f, &path, DECRUNCH_MAX) < 0)
		goto err_depack;
#endif

	if (hio_stat(h, &st) < 0)
		goto err_depack;

	if (st.st_size < 256) {			/* get size after decrunch */
		hio_close(h);
		unlink_tempfiles(&tmpfiles_list);
		return -XMP_ERROR_FORMAT;
	}

	if (ctx->state > XMP_STATE_UNLOADED)
		xmp_release_module(opaque);

	m->dirname = get_dirname(path);
	if (m->dirname == NULL)
		return -XMP_ERROR_SYSTEM;

	m->basename = get_basename(path);
	if (m->basename == NULL)
		return -XMP_ERROR_SYSTEM;

	m->filename = path;	/* For ALM, SSMT, etc */
	m->size = st.st_size;

	load_prologue(ctx);

	D_(D_WARN "load");
	test_result = load_result = -1;
	for (i = 0; format_loader[i] != NULL; i++) {
		hio_seek(h, 0, SEEK_SET);
		test_result = format_loader[i]->test(h, NULL, 0);
		if (test_result == 0) {
			hio_seek(h, 0, SEEK_SET);
			D_(D_WARN "load format: %s", format_loader[i]->name);
			load_result = format_loader[i]->loader(m, h, 0);
			break;
		}
	}

	set_md5sum(h, m->md5);

	hio_close(h);
	unlink_tempfiles(&tmpfiles_list);

	if (test_result < 0) {
		free(m->basename);
		free(m->dirname);
		return -XMP_ERROR_FORMAT;
	}

	if (load_result < 0) {
		xmp_release_module(opaque);
		return -XMP_ERROR_LOAD;
	}

	str_adj(m->mod.name);
	if (!*m->mod.name) {
		strncpy(m->mod.name, m->basename, XMP_NAME_SIZE);
	}

	load_epilogue(ctx);

	ret = prepare_scan(ctx);
	if (ret < 0)
		return ret;

	scan_sequences(ctx);

	ctx->state = XMP_STATE_LOADED;

	return 0;

    err_depack:
	hio_close(h);
	unlink_tempfiles(&tmpfiles_list);
	return -XMP_ERROR_DEPACK;
}
#endif // NO_COMPOSITE_LOADER

static int module_load(HIO_HANDLE* h, struct context_data *ctx, const struct format_loader* format)
{
	struct module_data *m = &ctx->m;

	hio_seek(h, 0, SEEK_SET);
	if (format->loader(m, h, 0) < 0)
	  return -XMP_ERROR_LOAD;

	str_adj(m->mod.name);
	str_adj(m->mod.author);

	load_epilogue(ctx);

	if (prepare_scan(ctx) < 0)
	  return -XMP_ERROR_LOAD;

	scan_sequences(ctx);
	ctx->state = XMP_STATE_LOADED;
	set_md5sum(h, m->md5);
	return 0;
}

int xmp_load_typed_module_from_memory(xmp_context opaque, void *mem, long size, const struct format_loader* format)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct module_data *m = &ctx->m;
	HIO_HANDLE *h;
	int ret;

	if ((h = hio_open_mem(mem, size)) == NULL)
		return -XMP_ERROR_SYSTEM;

	m->filename = NULL;
	m->basename = NULL;
	m->size = size;

	if (ctx->state > XMP_STATE_UNLOADED)
		xmp_release_module(opaque);

	load_prologue(ctx);

	D_(D_WARN "load");
	ret = -XMP_ERROR_FORMAT;
	if (format->test(h, NULL, 0) == 0) {
	  if ((ret = module_load(h, ctx, format)))
	    xmp_release_module(opaque);
	}
	hio_close(h);
  return ret;
}

#ifndef NO_COMPOSITE_LOADER
static int composite_test(HIO_HANDLE *f, char *t, const int start)
{
  struct format_loader **loader;
  for (loader = format_loader; *loader; ++loader) {
    if (0 == (*loader)->test(f, t, start))
      return 0;
    hio_seek(f, 0, SEEK_SET);
  }
  return -1;
}

static int composite_load(struct module_data *m, HIO_HANDLE *f, const int start)
{
  struct format_loader **loader;
  for (loader = format_loader; *loader; ++loader) {
    if (0 == (*loader)->test(f, 0, start)) {
      hio_seek(f, 0, SEEK_SET);
    	D_(D_WARN "load format: %s", (*loader)->name);
      return (*loader)->loader(m, f, start);
    }
    hio_seek(f, 0, SEEK_SET);
  }
  return -1;
}

const struct format_loader composite_loader = {
  NULL,
  composite_test,
  composite_load
};

int xmp_load_module_from_memory(xmp_context opaque, void *mem, long size)
{
  return xmp_load_typed_module_from_memory(opaque, mem, size, &composite_loader);
}
#endif

void xmp_release_module(xmp_context opaque)
{
	struct context_data *ctx = (struct context_data *)opaque;
	struct module_data *m = &ctx->m;
	struct xmp_module *mod = &m->mod;
	int i;

	if (ctx->state > XMP_STATE_LOADED)
		xmp_end_player(opaque);

	ctx->state = XMP_STATE_UNLOADED;

	D_(D_INFO "Freeing memory");

	release_module_extras(ctx);

	if (mod->xxt) {
		for (i = 0; i < mod->trk; i++) {
			free(mod->xxt[i]);
		}
		free(mod->xxt);
	}

	if (mod->xxp) {
		for (i = 0; i < mod->pat; i++) {
			free(mod->xxp[i]);
		}
		free(mod->xxp);
	}


	if (mod->xxi) {
		for (i = 0; i < mod->ins; i++) {
			free(mod->xxi[i].sub);
			free(mod->xxi[i].extra);
		}
		free(mod->xxi);
	}

	if (mod->xxs) {
		for (i = 0; i < mod->smp; i++) {
			if (mod->xxs[i].data != NULL)
				free(mod->xxs[i].data - 4);
		}
		free(mod->xxs);
	}

	if (m->scan_cnt) {
		for (i = 0; i < mod->len; i++) {
			free(m->scan_cnt[i]);
		}
		free(m->scan_cnt);
	}

	if (m->comment) {
		free(m->comment);
	}

	D_("free dirname/basename");
	free(m->dirname);
	free(m->basename);
}

void xmp_scan_module(xmp_context opaque)
{
	struct context_data *ctx = (struct context_data *)opaque;

	if (ctx->state < XMP_STATE_LOADED)
		return;

	scan_sequences(ctx);
}
