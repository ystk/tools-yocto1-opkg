/* opkg.c - the opkg  package management system

   Thomas Wood <thomas@openedhand.com>

   Copyright (C) 2008 OpenMoko Inc

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
 */

#include <config.h>
#include <fnmatch.h>

#include "opkg.h"
#include "opkg_conf.h"
#include "args.h"

#include "opkg_install.h"
#include "opkg_configure.h"
#include "opkg_download.h"
#include "opkg_remove.h"
#include "opkg_upgrade.h"

#include "sprintf_alloc.h"
#include "file_util.h"

#include <libbb/libbb.h>

args_t *args;

#define opkg_assert(expr) if (!(expr)) { \
    printf ("opkg: file %s: line %d (%s): Assertation '%s' failed",\
            __FILE__, __LINE__, __PRETTY_FUNCTION__, # expr); abort (); }

#define progress(d, p) d.percentage = p; if (progress_callback) progress_callback (&d, user_data);

/** Private Functions ***/

/**
 * Clone a pkg_t 
 */ 
static opkg_package_t*
pkg_t_to_opkg_package_t (pkg_t *old)
{
  opkg_package_t *new;

  new = opkg_package_new ();

  new->name = xstrdup(old->name);
  new->version = pkg_version_str_alloc (old);
  new->architecture = xstrdup(old->architecture);
  if (old->src)
    new->repository = xstrdup(old->src->name);
  new->description = xstrdup(old->description);
  new->tags = xstrdup(old->tags);

  new->size = old->size;
  new->installed = (old->state_status == SS_INSTALLED);

  return new;
}

static int
opkg_configure_packages(char *pkg_name)
{
  pkg_vec_t *all;
  int i;
  pkg_t *pkg;
  int r, err = 0;

  all = pkg_vec_alloc ();
  pkg_hash_fetch_available (all);

  for (i = 0; i < all->len; i++)
  {
    pkg = all->pkgs[i];

    if (pkg_name && fnmatch (pkg_name, pkg->name, 0))
      continue;

    if (pkg->state_status == SS_UNPACKED)
    {
      r = opkg_configure (pkg);
      if (r == 0)
      {
        pkg->state_status = SS_INSTALLED;
        pkg->parent->state_status = SS_INSTALLED;
        pkg->state_flag &= ~SF_PREFER;
      }
      else
      {
        if (!err)
          err = r;
      }
    }
  }

  pkg_vec_free (all);
  return err;
}

struct _curl_cb_data
{
  opkg_progress_callback_t cb;
  opkg_progress_data_t *progress_data;
  void *user_data;
  int start_range;
  int finish_range;
};

int
curl_progress_cb (struct _curl_cb_data *cb_data,
                  double t,   /* dltotal */
                  double d,   /* dlnow */
                  double ultotal,
                  double ulnow)
{
  int p = (t) ? d * 100 / t : 0;
  static int prev = -1;
  int progress = 0;

  /* prevent the same value being sent twice (can occur due to rounding) */
  if (p == prev)
    return 0;
  prev = p;

  if (t < 1)
    return 0;

  progress = cb_data->start_range + (d / t * ((cb_data->finish_range - cb_data->start_range)));
  cb_data->progress_data->percentage = progress;

  (cb_data->cb)(cb_data->progress_data,
                cb_data->user_data);

  return 0;
}


/*** Public API ***/

opkg_package_t *
opkg_package_new ()
{

  opkg_package_t *p;

  p = xcalloc(1, sizeof (opkg_package_t));

  return p;
}

void
opkg_package_free (opkg_package_t *p)
{
  free (p->name);
  free (p->version);
  free (p->architecture);
  free (p->description);
  free (p->tags);
  free (p->repository);

  free (p);
}

int
opkg_new ()
{
  int err;

  args = xcalloc(1, sizeof (args_t));
  args_init (args);

  err = opkg_conf_init (args);
  if (err)
  {
    args_deinit (args);
    free (args);
    return -1;
  }

  return 0;
}

void
opkg_free (void)
{
#ifdef HAVE_CURL
  opkg_curl_cleanup();
#endif
  opkg_conf_deinit ();
  args_deinit (args);
  free (args);
}

int
opkg_re_read_config_files (void)
{
  /* Unfortunately, the easiest way to re-read the config files right now is to
   * throw away conf and start again */
  opkg_free();
  memset(conf, '\0', sizeof(opkg_conf_t));
  return opkg_new();
}

void
opkg_get_option (char *option, void **value)
{
  int i = 0;
  extern opkg_option_t options[];

  /* look up the option
   * TODO: this would be much better as a hash table
   */
  while (options[i].name)
  {
    if (strcmp (options[i].name, option) != 0)
    {
      i++;
      continue;
    }
  }

  /* get the option */
  switch (options[i].type)
  {
  case OPKG_OPT_TYPE_BOOL:
    *((int *) value) = *((int *) options[i].value);
    return;

  case OPKG_OPT_TYPE_INT:
    *((int *) value) = *((int *) options[i].value);
    return;

  case OPKG_OPT_TYPE_STRING:
    *((char **)value) = xstrdup(options[i].value);
    return;
  }

}

void
opkg_set_option (char *option, void *value)
{
  int i = 0, found = 0;
  extern opkg_option_t options[];

  opkg_assert (option != NULL);
  opkg_assert (value != NULL);

  /* look up the option
   * TODO: this would be much better as a hash table
   */
  while (options[i].name)
  {
    if (strcmp (options[i].name, option) == 0)
    {
      found = 1;
      break;
    }
    i++;
  }

  if (!found)
  {
    /* XXX: Warning: Option not found */
    return;
  }

  /* set the option */
  switch (options[i].type)
  {
  case OPKG_OPT_TYPE_BOOL:
    if (*((int *) value) == 0)
      *((int *)options[i].value) = 0;
    else
      *((int *)options[i].value) = 1;
    return;

  case OPKG_OPT_TYPE_INT:
    *((int *) options[i].value) = *((int *) value);
    return;

  case OPKG_OPT_TYPE_STRING:
    *((char **)options[i].value) = xstrdup(value);
    return;
  }

}

/**
 * @brief libopkg API: Install package
 * @param package_name The name of package in which is going to install
 * @param progress_callback The callback function that report the status to caller. 
 */ 
int
opkg_install_package (const char *package_name, opkg_progress_callback_t progress_callback, void *user_data)
{
  int err;
  char *stripped_filename;
  opkg_progress_data_t pdata;
  pkg_t *old, *new;
  pkg_vec_t *deps, *all;
  int i, ndepends;
  char **unresolved = NULL;

  opkg_assert (package_name != NULL);

  /* ... */
  pkg_info_preinstall_check ();


  /* check to ensure package is not already installed */
  old = pkg_hash_fetch_installed_by_name(package_name);
  if (old)
  {
    /* XXX: Error: Package is already installed. */
    return OPKG_PACKAGE_ALREADY_INSTALLED;
  }

  new = pkg_hash_fetch_best_installation_candidate_by_name(package_name);
  if (!new)
  {
    /* XXX: Error: Could not find package to install */
    return OPKG_PACKAGE_NOT_FOUND;
  }

  new->state_flag |= SF_USER;

  pdata.action = OPKG_INSTALL;
  pdata.package = pkg_t_to_opkg_package_t (new);

  progress (pdata, 0);

  /* find dependancies and download them */
  deps = pkg_vec_alloc ();
  /* this function does not return the original package, so we insert it later */
  ndepends = pkg_hash_fetch_unsatisfied_dependencies (new, deps, &unresolved);
  if (unresolved)
  {
    /* XXX: Error: Could not satisfy dependencies */
    pkg_vec_free (deps);
    return OPKG_DEPENDENCIES_FAILED;
  }

  /* insert the package we are installing so that we download it */
  pkg_vec_insert (deps, new);

  /* download package and dependancies */
  for (i = 0; i < deps->len; i++)
  {
    pkg_t *pkg;
    struct _curl_cb_data cb_data;
    char *url;

    pkg = deps->pkgs[i];
    if (pkg->local_filename)
      continue;

    opkg_package_free (pdata.package);
    pdata.package = pkg_t_to_opkg_package_t (pkg);
    pdata.action = OPKG_DOWNLOAD;

    if (pkg->src == NULL)
    {
      /* XXX: Error: Package not available from any configured src */
      return OPKG_PACKAGE_NOT_AVAILABLE;
    }

    sprintf_alloc(&url, "%s/%s", pkg->src->value, pkg->filename);

    /* Get the filename part, without any directory */
    stripped_filename = strrchr(pkg->filename, '/');
    if ( ! stripped_filename )
        stripped_filename = pkg->filename;

    sprintf_alloc(&pkg->local_filename, "%s/%s", conf->tmp_dir, stripped_filename);

    cb_data.cb = progress_callback;
    cb_data.progress_data = &pdata;
    cb_data.user_data = user_data;
    /* 75% of "install" progress is for downloading */
    cb_data.start_range = 75 * i / deps->len;
    cb_data.finish_range = 75 * (i + 1) / deps->len;

    err = opkg_download(url, pkg->local_filename,
              (curl_progress_func) curl_progress_cb, &cb_data);
    free(url);

    if (err)
    {
      pkg_vec_free (deps);
      opkg_package_free (pdata.package);
      return OPKG_DOWNLOAD_FAILED;
    }

  }
  pkg_vec_free (deps);

  /* clear depenacy checked marks, left by pkg_hash_fetch_unsatisfied_dependencies */
  all = pkg_vec_alloc ();
  pkg_hash_fetch_available (all);
  for (i = 0; i < all->len; i++)
  {
    all->pkgs[i]->parent->dependencies_checked = 0;
  }
  pkg_vec_free (all);


  /* 75% of "install" progress is for downloading */
  opkg_package_free (pdata.package);
  pdata.package = pkg_t_to_opkg_package_t (new);
  pdata.action = OPKG_INSTALL;
  progress (pdata, 75);

  /* unpack the package */
  err = opkg_install_pkg(new, 0);

  if (err)
  {
    opkg_package_free (pdata.package);
    return OPKG_UNKNOWN_ERROR;
  }

  progress (pdata, 75);

  /* run configure scripts, etc. */
  err = opkg_configure_packages (NULL);
  if (err)
  {
    opkg_package_free (pdata.package);
    return OPKG_UNKNOWN_ERROR;
  }

  /* write out status files and file lists */
  opkg_conf_write_status_files ();
  pkg_write_changed_filelists ();

  progress (pdata, 100);
  opkg_package_free (pdata.package);
  return 0;
}

int
opkg_remove_package (const char *package_name, opkg_progress_callback_t progress_callback, void *user_data)
{
  int err;
  pkg_t *pkg = NULL;
  pkg_t *pkg_to_remove;
  opkg_progress_data_t pdata;

  opkg_assert (package_name != NULL);

  pkg_info_preinstall_check ();

  pkg = pkg_hash_fetch_installed_by_name (package_name);

  if (pkg == NULL)
  {
    /* XXX: Error: Package not installed. */
    return OPKG_PACKAGE_NOT_INSTALLED;
  }

  pdata.action = OPKG_REMOVE;
  pdata.package = pkg_t_to_opkg_package_t (pkg);
  progress (pdata, 0);


  if (pkg->state_status == SS_NOT_INSTALLED)
  {
    /* XXX:  Error: Package seems to be not installed (STATUS = NOT_INSTALLED). */
    opkg_package_free (pdata.package);
    return OPKG_PACKAGE_NOT_INSTALLED;
  }
  progress (pdata, 25);

  if (conf->restrict_to_default_dest)
  {
    pkg_to_remove = pkg_hash_fetch_installed_by_name_dest (pkg->name,
                                                           conf->default_dest);
  }
  else
  {
    pkg_to_remove = pkg_hash_fetch_installed_by_name (pkg->name);
  }


  progress (pdata, 75);

  err = opkg_remove_pkg (pkg_to_remove, 0);

  /* write out status files and file lists */
  opkg_conf_write_status_files ();
  pkg_write_changed_filelists ();


  progress (pdata, 100);
  opkg_package_free (pdata.package);
  return (err) ? OPKG_UNKNOWN_ERROR : OPKG_NO_ERROR;
}

int
opkg_upgrade_package (const char *package_name, opkg_progress_callback_t progress_callback, void *user_data)
{
  int err;
  pkg_t *pkg;
  opkg_progress_data_t pdata;

  opkg_assert (package_name != NULL);

  pkg_info_preinstall_check ();

  if (conf->restrict_to_default_dest)
  {
    pkg = pkg_hash_fetch_installed_by_name_dest (package_name,
                                                 conf->default_dest);
    if (pkg == NULL)
    {
      /* XXX: Error: Package not installed in default_dest */
      return OPKG_PACKAGE_NOT_INSTALLED;
    }
  }
  else
  {
    pkg = pkg_hash_fetch_installed_by_name (package_name);
  }

  if (!pkg)
  {
    /* XXX: Error: Package not installed */
    return OPKG_PACKAGE_NOT_INSTALLED;
  }

  pdata.action = OPKG_INSTALL;
  pdata.package = pkg_t_to_opkg_package_t (pkg);
  progress (pdata, 0);

  err = opkg_upgrade_pkg (pkg);
  /* opkg_upgrade_pkg returns the error codes of opkg_install_pkg */
  if (err)
  {
    opkg_package_free (pdata.package);
    return OPKG_UNKNOWN_ERROR;
  }
  progress (pdata, 75);

  err = opkg_configure_packages (NULL);
  if (err) {
    opkg_package_free (pdata.package);  
    return OPKG_UNKNOWN_ERROR;
  }

  /* write out status files and file lists */
  opkg_conf_write_status_files ();
  pkg_write_changed_filelists ();

  progress (pdata, 100);
  opkg_package_free (pdata.package);
  return 0;
}

int
opkg_upgrade_all (opkg_progress_callback_t progress_callback, void *user_data)
{
  pkg_vec_t *installed;
  int err = 0;
  int i;
  pkg_t *pkg;
  opkg_progress_data_t pdata;

  pdata.action = OPKG_INSTALL;
  pdata.package = NULL;

  progress (pdata, 0);

  installed = pkg_vec_alloc ();
  pkg_info_preinstall_check ();

  pkg_hash_fetch_all_installed (installed);
  for (i = 0; i < installed->len; i++)
  {
    pkg = installed->pkgs[i];

    pdata.package = pkg_t_to_opkg_package_t (pkg);
    progress (pdata, 99 * i / installed->len);
    opkg_package_free (pdata.package);

    err += opkg_upgrade_pkg (pkg);
  }
  pkg_vec_free (installed);

  if (err)
    return 1;

  err = opkg_configure_packages (NULL);
  if (err)
    return 1;

  pdata.package = NULL;
  progress (pdata, 100);
  return 0;
}

int
opkg_update_package_lists (opkg_progress_callback_t progress_callback, void *user_data)
{
  char *tmp;
  int err, result = 0;
  char *lists_dir;
  pkg_src_list_elt_t *iter;
  pkg_src_t *src;
  int sources_list_count, sources_done;
  opkg_progress_data_t pdata;

  pdata.action = OPKG_DOWNLOAD;
  pdata.package = NULL;
  progress (pdata, 0);

  sprintf_alloc (&lists_dir, "%s",
                 (conf->restrict_to_default_dest)
                 ? conf->default_dest->lists_dir
                 : conf->lists_dir);

  if (!file_is_dir (lists_dir))
  {
    if (file_exists (lists_dir))
    {
      /* XXX: Error: file exists but is not a directory */
      free (lists_dir);
      return 1;
    }

    err = file_mkdir_hier (lists_dir, 0755);
    if (err)
    {
      /* XXX: Error: failed to create directory */
      free (lists_dir);
      return 1;
    }
  }

  sprintf_alloc(&tmp, "%s/update-XXXXXX", conf->tmp_dir);
  if (mkdtemp (tmp) == NULL) {
    /* XXX: Error: could not create temporary file name */
    free (lists_dir);
    free (tmp);
    return 1;
  }

  /* count the number of sources so we can give some progress updates */
  sources_list_count = 0;
  sources_done = 0;
  list_for_each_entry(iter, &conf->pkg_src_list.head, node)
  {
    sources_list_count++;
  }

  list_for_each_entry(iter, &conf->pkg_src_list.head, node)
  {
    char *url, *list_file_name = NULL;

    src = (pkg_src_t *)iter->data;

    if (src->extra_data)  /* debian style? */
      sprintf_alloc (&url, "%s/%s/%s", src->value, src->extra_data,
                     src->gzip ? "Packages.gz" : "Packages");
    else
      sprintf_alloc (&url, "%s/%s", src->value, src->gzip ? "Packages.gz" : "Packages");

    sprintf_alloc (&list_file_name, "%s/%s", lists_dir, src->name);
    if (src->gzip)
    {
      FILE *in, *out;
      struct _curl_cb_data cb_data;
      char *tmp_file_name = NULL;

      sprintf_alloc (&tmp_file_name, "%s/%s.gz", tmp, src->name);

      /* XXX: Note: downloading url */

      cb_data.cb = progress_callback;
      cb_data.progress_data = &pdata;
      cb_data.user_data = user_data;
      cb_data.start_range = 100 * sources_done / sources_list_count;
      cb_data.finish_range = 100 * (sources_done + 1) / sources_list_count;

      err = opkg_download (url, tmp_file_name, (curl_progress_func) curl_progress_cb, &cb_data);

      if (err == 0)
      {
        /* XXX: Note: Inflating downloaded file */
        in = fopen (tmp_file_name, "r");
        out = fopen (list_file_name, "w");
        if (in && out)
          unzip (in, out);
        else
          err = 1;
        if (in)
          fclose (in);
        if (out)
          fclose (out);
        unlink (tmp_file_name);
      }
      free (tmp_file_name);
    }
    else
      err = opkg_download (url, list_file_name, NULL, NULL);

    if (err)
    {
      /* XXX: Error: download error */
      result = OPKG_DOWNLOAD_FAILED;
    }
    free (url);

#if defined(HAVE_GPGME) || defined(HAVE_OPENSSL)
    if ( conf->check_signature ) {
        char *sig_file_name;
        /* download detached signitures to verify the package lists */
        /* get the url for the sig file */
        if (src->extra_data)  /* debian style? */
            sprintf_alloc (&url, "%s/%s/%s", src->value, src->extra_data,
                    "Packages.sig");
        else
            sprintf_alloc (&url, "%s/%s", src->value, "Packages.sig");

        /* create filename for signature */
        sprintf_alloc (&sig_file_name, "%s/%s.sig", lists_dir, src->name);

        /* make sure there is no existing signature file */
        unlink (sig_file_name);

        err = opkg_download (url, sig_file_name, NULL, NULL);
        if (err)
        {
            /* XXX: Warning: Download failed */
        }
        else
        {
            int err;
            err = opkg_verify_file (list_file_name, sig_file_name);
            if (err == 0)
            {
                /* XXX: Notice: Signature check passed */
            }
            else
            {
                /* XXX: Warning: Signature check failed */
            }
        }
        free (sig_file_name);
        free (list_file_name);
        free (url);
    }
#else
    /* XXX: Note: Signature check for %s skipped because GPG support was not
     * enabled in this build
     */
#endif

    sources_done++;
    progress (pdata, 100 * sources_done / sources_list_count);
  }

  rmdir (tmp);
  free (tmp);
  free (lists_dir);

  /* Now re-read the package lists to update package hash tables. */
  opkg_re_read_config_files ();

  return result;
}


int
opkg_list_packages (opkg_package_callback_t callback, void *user_data)
{
  pkg_vec_t *all;
  int i;

  opkg_assert (callback);

  all = pkg_vec_alloc ();
  pkg_hash_fetch_available (all);
  for (i = 0; i < all->len; i++)
  {
    pkg_t *pkg;
    opkg_package_t *package;

    pkg = all->pkgs[i];

    package = pkg_t_to_opkg_package_t (pkg);
    callback (package, user_data);
    opkg_package_free (package);
  }

  pkg_vec_free (all);

  return 0;
}

int
opkg_list_upgradable_packages (opkg_package_callback_t callback, void *user_data)
{
    struct active_list *head;
    struct active_list *node;
    pkg_t *old=NULL, *new = NULL;
    static opkg_package_t* package=NULL;

    opkg_assert (callback);

    /* ensure all data is valid */
    pkg_info_preinstall_check ();

    head  =  prepare_upgrade_list();
    for (node=active_list_next(head, head); node; active_list_next(head,node)) {
        old = list_entry(node, pkg_t, list);
        new = pkg_hash_fetch_best_installation_candidate_by_name(old->name);
	if (new == NULL)
		continue;
        package = pkg_t_to_opkg_package_t (new);
        callback (package, user_data);
        opkg_package_free (package);
    }
    active_list_head_delete(head);
    return 0;
}

opkg_package_t*
opkg_find_package (const char *name, const char *ver, const char *arch, const char *repo)
{
  pkg_vec_t *all;
  opkg_package_t *package = NULL;
  int i;
#define sstrcmp(x,y) (x && y) ? strcmp (x, y) : 0

  all = pkg_vec_alloc ();
  pkg_hash_fetch_available (all);
  for (i = 0; i < all->len; i++)
  {
    pkg_t *pkg;
    char *pkgv;

    pkg = all->pkgs[i];

    /* check name */
    if (sstrcmp (pkg->name, name))
      continue;
    
    /* check version */
    pkgv = pkg_version_str_alloc (pkg);
    if (sstrcmp (pkgv, ver))
    {
      free (pkgv);
      continue;
    }
    free (pkgv);

    /* check architecture */
    if (arch)
    {
      if (sstrcmp (pkg->architecture, arch))
        continue;
    }

    /* check repository */
    if (repo)
    {
      if (sstrcmp (pkg->src->name, repo))
          continue;
    }

    /* match found */
    package = pkg_t_to_opkg_package_t (pkg);
    break;
  }

  pkg_vec_free (all);

  return package;
}

#ifdef HAVE_CURL
#include <curl/curl.h>
#endif
/**
 * @brief Check the accessibility of repositories. It will try to access the repository to check if the respository is accessible throught current network status. 
 * @return return how many repositories cannot access. 0 means all okay. 
 */ 
int opkg_repository_accessibility_check(void) 
{
  pkg_src_list_elt_t *iter;
  str_list_elt_t *iter1;
  str_list_t *src;
  int repositories=0;
  int ret=0;
  int err;
  char *repo_ptr;
  char *stmp;

  src = str_list_alloc();

  list_for_each_entry(iter, &conf->pkg_src_list.head, node)
  {
    if (strstr(((pkg_src_t *)iter->data)->value, "://") && 
		    index(strstr(((pkg_src_t *)iter->data)->value, "://") + 3, '/')) 
      stmp = xstrndup(((pkg_src_t *)iter->data)->value, 
		      (index(strstr(((pkg_src_t *)iter->data)->value, "://") + 3, '/') - ((pkg_src_t *)iter->data)->value)*sizeof(char));

    else
      stmp = xstrdup(((pkg_src_t *)iter->data)->value);

    for (iter1 = str_list_first(src); iter1; iter1 = str_list_next(src, iter1))
    {
      if (strstr(iter1->data, stmp)) 
        break;
    }
    if (iter1)
      continue;

    sprintf_alloc(&repo_ptr, "%s/index.html",stmp);
    free(stmp);

    str_list_append(src, repo_ptr);
    free(repo_ptr);
    repositories++;
  }
  while (repositories > 0) 
  {
    iter1 = str_list_pop(src);
    repositories--;

    err = opkg_download(iter1->data, "/dev/null", NULL, NULL);
#ifdef HAVE_CURL
    if (!(err == CURLE_OK || 
		err == CURLE_HTTP_RETURNED_ERROR || 
		err == CURLE_FILE_COULDNT_READ_FILE ||
		err == CURLE_REMOTE_FILE_NOT_FOUND || 
		err == CURLE_TFTP_NOTFOUND
		)) {
#else
    if (!(err == 0)) {
#endif
	    ret++;
    }
    str_list_elt_deinit(iter1);
  }
  free(src);
  return ret;
}
