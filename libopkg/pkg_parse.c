/* pkg_parse.c - the opkg package management system

   Steven M. Ayer
   
   Copyright (C) 2002 Compaq Computer Corporation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
*/

#include "includes.h"
#include <errno.h>
#include <ctype.h>
   
#include "pkg.h"
#include "opkg_utils.h"
#include "pkg_parse.h"
#include "libbb/libbb.h"

static int
is_field(char *type, const char *line)
{
	if (!strncmp(line, type, strlen(type)))
		return 1;
	return 0;
}

static char *
parse_simple(char *type, const char *line)
{
	return trim_xstrdup(line + strlen(type) + 1);
}

/*
 * Parse a comma separated string into an array.
 */
static char **
parse_comma_separated(const char *raw, int *count)
{
	char **depends = NULL;
	const char *start, *end;
	int line_count = 0;

	/* skip past the "Field:" marker */
	while (*raw && *raw != ':')
		raw++;
	raw++;

	if (line_is_blank(raw)) {
		*count = line_count;
		return NULL;
	}

	while (*raw) {
		depends = xrealloc(depends, sizeof(char *) * (line_count + 1));
	
		while (isspace(*raw))
			raw++;

		start = raw;
		while (*raw != ',' && *raw)
			raw++;
		end = raw;

		while (end > start && isspace(*end))
			end--;

		depends[line_count] = xstrndup(start, end-start);

        	line_count++;
		if (*raw == ',')
		    raw++;
	}

	*count = line_count;
	return depends;
}

static void
parse_status(pkg_t *pkg, const char *sstr)
{
	char sw_str[64], sf_str[64], ss_str[64];

	if (sscanf(sstr, "Status: %63s %63s %63s",
				sw_str, sf_str, ss_str) != 3) {
		fprintf(stderr, "%s: failed to parse Status line for %s\n",
				__FUNCTION__, pkg->name);
		return;
	}

	pkg->state_want = pkg_state_want_from_str(sw_str);
	pkg->state_flag = pkg_state_flag_from_str(sf_str);
	pkg->state_status = pkg_state_status_from_str(ss_str);
}

static void
parse_conffiles(pkg_t *pkg, const char *cstr)
{
	char file_name[1024], md5sum[35];

	if (sscanf(cstr, "%1023s %34s", file_name, md5sum) != 2) {
		fprintf(stderr, "%s: failed to parse Conffiles line for %s\n",
				__FUNCTION__, pkg->name);
		return;
	}

	conffile_list_append(&pkg->conffiles, file_name, md5sum);
}

int
parse_version(pkg_t *pkg, const char *vstr)
{
	char *colon;

	if (strncmp(vstr, "Version:", 8) == 0)
		vstr += 8;

	while (*vstr && isspace(*vstr))
		vstr++;

	colon = strchr(vstr, ':');
	if (colon) {
		errno = 0;
		pkg->epoch = strtoul(vstr, NULL, 10);
		if (errno) {
			fprintf(stderr, "%s: %s: invalid epoch: %s\n",
				__FUNCTION__, pkg->name, strerror(errno));
		}
		vstr = ++colon;
	} else {
		pkg->epoch= 0;
	}

	pkg->version= xstrdup(vstr);
	pkg->revision = strrchr(pkg->version,'-');

	if (pkg->revision)
		*pkg->revision++ = '\0';

	return 0;
}

static int
pkg_parse_line(pkg_t *pkg, const char *line, uint mask)
{
	/* these flags are a bit hackish... */
	static int reading_conffiles = 0, reading_description = 0;
	int ret = 0;

	switch (*line) {
	case 'A':
		if ((mask & PFM_ARCHITECTURE ) && is_field("Architecture", line))
			pkg->architecture = parse_simple("Architecture", line);
		else if ((mask & PFM_AUTO_INSTALLED) && is_field("Auto-Installed", line)) {
			char *tmp = parse_simple("Auto-Installed", line);
			if (strcmp(tmp, "yes") == 0)
			    pkg->auto_installed = 1;
			free(tmp);
		}
		break;

	case 'C':
		if ((mask & PFM_CONFFILES) && is_field("Conffiles", line)) {
			reading_conffiles = 1;
			reading_description = 0;
			goto dont_reset_flags;
	    	}
		else if ((mask & PFM_CONFLICTS) && is_field("Conflicts", line))
			pkg->conflicts_str = parse_comma_separated(line, &pkg->conflicts_count);
		break;

	case 'D':
		if ((mask & PFM_DESCRIPTION) && is_field("Description", line)) {
			pkg->description = parse_simple("Description", line);
			reading_conffiles = 0;
			reading_description = 1;
			goto dont_reset_flags;
		} else if ((mask & PFM_DEPENDS) && is_field("Depends", line))
			pkg->depends_str = parse_comma_separated(line, &pkg->depends_count);
		break;

	case 'E':
		if((mask & PFM_ESSENTIAL) && is_field("Essential", line)) {
			char *tmp = parse_simple("Essential", line);
			if (strcmp(tmp, "yes") == 0)
				pkg->essential = 1;
			free(tmp);
		}
		break;

	case 'F':
		if((mask & PFM_FILENAME) && is_field("Filename", line))
			pkg->filename = parse_simple("Filename", line);
		break;

	case 'I':
		if ((mask && PFM_INSTALLED_SIZE) && is_field("Installed-Size", line)) {
			char *tmp = parse_simple("Installed-Size", line);
			pkg->installed_size = (strtoul(tmp, NULL, 0)+1023)/1024;
			free (tmp);
		} else if ((mask && PFM_INSTALLED_TIME) && is_field("Installed-Time", line)) {
			char *tmp = parse_simple("Installed-Time", line);
			pkg->installed_time = strtoul(tmp, NULL, 0);
			free (tmp);
		}	    
		break;

	case 'M':
		if (mask && PFM_MD5SUM) {
			if (is_field("MD5sum:", line))
				pkg->md5sum = parse_simple("MD5sum", line);
			/* The old opkg wrote out status files with the wrong
			* case for MD5sum, let's parse it either way */
			else if (is_field("MD5Sum:", line))
				pkg->md5sum = parse_simple("MD5Sum", line);
		} else if((mask & PFM_MAINTAINER) && is_field("Maintainer", line))
			pkg->maintainer = parse_simple("Maintainer", line);
		break;

	case 'P':
		if ((mask & PFM_PACKAGE) && is_field("Package", line)) 
			pkg->name = parse_simple("Package", line);
		else if ((mask & PFM_PRIORITY) && is_field("Priority", line))
			pkg->priority = parse_simple("Priority", line);
		else if ((mask & PFM_PROVIDES) && is_field("Provides", line))
			pkg->provides_str = parse_comma_separated(line, &pkg->provides_count);
		else if ((mask & PFM_PRE_DEPENDS) && is_field("Pre-Depends", line))
			pkg->pre_depends_str = parse_comma_separated(line, &pkg->pre_depends_count);
		break;

	case 'R':
		if ((mask & PFM_RECOMMENDS) && is_field("Recommends", line))
			pkg->recommends_str = parse_comma_separated(line, &pkg->recommends_count);
		else if ((mask & PFM_REPLACES) && is_field("Replaces", line))
			pkg->replaces_str = parse_comma_separated(line, &pkg->replaces_count);

		break;

	case 'S':
		if ((mask & PFM_SECTION) && is_field("Section", line))
			pkg->section = parse_simple("Section", line);
#ifdef HAVE_SHA256
		else if ((mask & PFM_SHA256SUM) && is_field("SHA256sum", line))
			pkg->sha256sum = parse_simple("SHA256sum", line);
#endif
		else if ((mask & PFM_SIZE) && is_field("Size", line)) {
			char *tmp = parse_simple("Size", line);
			pkg->size = (strtoul(tmp, NULL, 0)+1023)/1024;
			free (tmp);
		} else if ((mask & PFM_SOURCE) && is_field("Source", line))
			pkg->source = parse_simple("Source", line);
		else if ((mask & PFM_STATUS) && is_field("Status", line))
			parse_status(pkg, line);
		else if ((mask & PFM_SUGGESTS) && is_field("Suggests", line))
			pkg->suggests_str = parse_comma_separated(line, &pkg->suggests_count);
		break;

	case 'T':
		if ((mask & PFM_TAGS) && is_field("Tags", line))
			pkg->tags = parse_simple("Tags", line);
		break;

	case 'V':
		if ((mask & PFM_VERSION) && is_field("Version", line))
			parse_version(pkg, line);
		break;

	case ' ':
		if ((mask & PFM_DESCRIPTION) && reading_description) {
			pkg->description = xrealloc(pkg->description,
						strlen(pkg->description)
						+ 1 + strlen(line) + 1);
			strcat(pkg->description, "\n");
			strcat(pkg->description, (line));
			goto dont_reset_flags;
		} else if ((mask && PFM_CONFFILES) && reading_conffiles) {
			parse_conffiles(pkg, line);
			goto dont_reset_flags;
		}

		/* FALLTHROUGH */
	default:
		/* For package lists, signifies end of package. */
		if(line_is_blank(line)) {
			ret = 1;
			break;
		}
	}

	reading_description = 0;
	reading_conffiles = 0;

dont_reset_flags:

	return ret;
}

int
pkg_parse_from_stream_nomalloc(pkg_t *pkg, FILE *fp, uint mask,
						char **buf0, size_t buf0len)
{
	int ret, lineno;
	char *buf, *nl;
	size_t buflen;

	lineno = 1;
	ret = 0;

	buflen = buf0len;
	buf = *buf0;
	buf[0] = '\0';

	while (1) {
		if (fgets(buf, buflen, fp) == NULL) {
			if (ferror(fp)) {
				fprintf(stderr, "%s: fgets: %s\n",
					__FUNCTION__, strerror(errno));
				ret = -1;
			} else if (strlen(*buf0) == buf0len-1) {
				fprintf(stderr, "%s: missing new line character"
						" at end of file!\n",
					__FUNCTION__);
				pkg_parse_line(pkg, *buf0, mask);
			}
			break;
		}

		nl = strchr(buf, '\n');
		if (nl == NULL) {
			if (strlen(buf) < buflen-1) {
				/*
				 * Line could be exactly buflen-1 long and
				 * missing a newline, but we won't know until
				 * fgets fails to read more data.
				 */
				fprintf(stderr, "%s: missing new line character"
						" at end of file!\n",
					__FUNCTION__);
				pkg_parse_line(pkg, *buf0, mask);
				break;
			}
			if (buf0len >= EXCESSIVE_LINE_LEN) {
				fprintf(stderr, "%s: excessively long line at "
					"%d. Corrupt file?\n",
					__FUNCTION__, lineno);
				ret = -1;
				break;
			}

			/*
			 * Realloc and move buf past the data already read.
			 * |<--------------- buf0len ----------------->|
			 * |                     |<------- buflen ---->|
			 * |---------------------|---------------------|
			 * buf0                   buf
			 */
			buflen = buf0len;
			buf0len *= 2;
			*buf0 = xrealloc(*buf0, buf0len);
			buf = *buf0 + buflen -1;

			continue;
		}

		*nl = '\0';

		lineno++;

		if (pkg_parse_line(pkg, *buf0, mask))
			break;

		buf = *buf0;
		buflen = buf0len;
		buf[0] = '\0';
	};

	if (pkg->name == NULL) {
		/* probably just a blank line */
		ret = 1;
	}

	return ret;
}

int
pkg_parse_from_stream(pkg_t *pkg, FILE *fp, uint mask)
{
	int ret;
	char *buf;
	const size_t len = 4096;

	buf = xmalloc(len);
	ret = pkg_parse_from_stream_nomalloc(pkg, fp, mask, &buf, len);
	free(buf);

	return ret;
}
