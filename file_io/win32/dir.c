/* ====================================================================
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2000 The Apache Software Foundation.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.apache.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Apache" and "Apache Software Foundation" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache",
 *    nor may "Apache" appear in their name, without prior written
 *    permission of the Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 */

#include "apr.h"
#include "fileio.h"
#include "apr_file_io.h"
#include "apr_strings.h"
#include "apr_portable.h"
#include "atime.h"

#if APR_HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#if APR_HAVE_DIRENT_H
#include <dirent.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif


static apr_status_t dir_cleanup(void *thedir)
{
    apr_dir_t *dir = thedir;
    if (dir->dirhand != INVALID_HANDLE_VALUE && !FindClose(dir->dirhand)) {
        return apr_get_os_error();
    }
    dir->dirhand = INVALID_HANDLE_VALUE;
    return APR_SUCCESS;
} 

APR_DECLARE(apr_status_t) apr_dir_open(apr_dir_t **new, const char *dirname,
                                       apr_pool_t *cont)
{
#if APR_HAS_UNICODE_FS
    apr_oslevel_e os_level;
#endif
    int len = strlen(dirname);
    (*new) = apr_pcalloc(cont, sizeof(apr_dir_t));
    /* Leave room here to add and pop the '*' wildcard for FindFirstFile 
     * and double-null terminate so we have one character to change.
     */
    (*new)->dirname = apr_palloc(cont, len + 3);
    memcpy((*new)->dirname, dirname, len);
    if (len && (*new)->dirname[len - 1] != '/') {
    	(*new)->dirname[len++] = '/';
    }
    (*new)->dirname[len++] = '\0';
    (*new)->dirname[len] = '\0';

#if APR_HAS_UNICODE_FS
    if (!apr_get_oslevel(cont, &os_level) && os_level >= APR_WIN_NT)
    {
        /* Create a buffer for the longest file name we will ever see 
         */
        (*new)->w.entry = apr_pcalloc(cont, sizeof(WIN32_FIND_DATAW));
        (*new)->name = apr_pcalloc(cont, APR_FILE_MAX * 3 + 1);        
    }
    else
#endif
    {
        /* Note that we won't open a directory that is greater than MAX_PATH,
         * including the trailing /* wildcard suffix.  If a * won't fit, then
         * neither will any other file name within the directory.
         * The length not including the trailing '*' is stored as rootlen, to
         * skip over all paths which are too long.
         */
        if (len >= APR_PATH_MAX) {
            (*new) = NULL;
            return APR_ENAMETOOLONG;
        }
        (*new)->n.entry = apr_pcalloc(cont, sizeof(WIN32_FIND_DATAW));
    }
    (*new)->rootlen = len - 1;
    (*new)->cntxt = cont;
    (*new)->dirhand = INVALID_HANDLE_VALUE;
    apr_register_cleanup((*new)->cntxt, (void *)(*new), dir_cleanup,
                        apr_null_cleanup);
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_dir_close(apr_dir_t *dir)
{
    if (dir->dirhand != INVALID_HANDLE_VALUE && !FindClose(dir->dirhand)) {
        return apr_get_os_error();
    }
    dir->dirhand = INVALID_HANDLE_VALUE;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_dir_read(apr_finfo_t *finfo, apr_int32_t wanted,
                                       apr_dir_t *thedir)
{
    apr_status_t rv;
    /* The while loops below allow us to skip all invalid file names, so that
     * we aren't reporting any files where their absolute paths are too long.
     */
#if APR_HAS_UNICODE_FS
    apr_oslevel_e os_level;
    if (!apr_get_oslevel(thedir->cntxt, &os_level) && os_level >= APR_WIN_NT)
    {
        if (thedir->dirhand == INVALID_HANDLE_VALUE) 
        {
            apr_wchar_t *eos, wdirname[APR_PATH_MAX];
            apr_status_t rv;
            if (rv = utf8_to_unicode_path(wdirname, sizeof(wdirname) 
                                                     / sizeof(apr_wchar_t), 
                                          thedir->dirname)) {
                return rv;
            }
            eos = wcschr(wdirname, '\0');
            eos[0] = '*';
            eos[1] = '\0';
            thedir->dirhand = FindFirstFileW(wdirname, thedir->w.entry);
            if (thedir->dirhand == INVALID_HANDLE_VALUE) {
                return apr_get_os_error();
            }
        }
        else if (!FindNextFileW(thedir->dirhand, thedir->w.entry)) {
            return apr_get_os_error();
        }
        while (thedir->rootlen &&
               thedir->rootlen + wcslen(thedir->w.entry->cFileName) >= APR_PATH_MAX)
        {
            if (!FindNextFileW(thedir->dirhand, thedir->w.entry)) {
                return apr_get_os_error();
            }
        }
        if (rv = unicode_to_utf8_path(thedir->name, APR_FILE_MAX * 3 + 1, 
                                      thedir->w.entry->cFileName))
            return rv;
        finfo->name = thedir->name;
    }
    else
#endif
    {
        if (thedir->dirhand == INVALID_HANDLE_VALUE) {
            /* '/' terminated, so add the '*' and pop it when we finish */
            *strchr(thedir->dirname, '\0') = '*';
            thedir->dirhand = FindFirstFileA(thedir->dirname, 
                                             thedir->n.entry);
            *(strchr(thedir->dirname, '\0') - 1) = '\0';
            if (thedir->dirhand == INVALID_HANDLE_VALUE) {
                return apr_get_os_error();
            }
        }
        else if (!FindNextFile(thedir->dirhand, thedir->n.entry)) {
            return apr_get_os_error();
        }
        while (thedir->rootlen &&
               thedir->rootlen + strlen(thedir->n.entry->cFileName) >= MAX_PATH)
        {
            if (!FindNextFileW(thedir->dirhand, thedir->w.entry)) {
                return apr_get_os_error();
            }
        }
        finfo->name = thedir->n.entry->cFileName;
    }
    finfo->valid = APR_FINFO_NAME | APR_FINFO_TYPE | APR_FINFO_CTIME
                 | APR_FINFO_ATIME | APR_FINFO_MTIME | APR_FINFO_SIZE;
    wanted |= ~finfo->valid;
    if (wanted) {
        /* Win32 apr_stat() is about to open a handle to this file.
         * we must create a full path that doesn't evaporate.
         */
        const char *fname = finfo->name;
        char *fspec = apr_pstrcat(thedir->cntxt, thedir->dirname, 
                                  finfo->name, NULL);
        finfo->valid = 0;
        rv = apr_stat(finfo, fspec, wanted, thedir->cntxt);
        if (rv == APR_SUCCESS || rv == APR_INCOMPLETE) {
            finfo->valid |= APR_FINFO_NAME;
            finfo->name = fname;
            finfo->fname = fspec;
        }
        return rv;
    }

    finfo->cntxt = thedir->cntxt;

    /* Do the best job we can determining the file type.
     * Win32 only returns device names in a directory in response to a specific
     * request (e.g. FindFirstFile("CON"), not to wildcards, so we will ignore
     * the BLK, CHR, and other oddballs, since they should -not- occur in this
     * context.
     */
    if (thedir->n.entry->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        finfo->filetype = APR_LNK;
        finfo->valid |= APR_FINFO_TYPE;
    }
    else if (thedir->n.entry->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        finfo->filetype = APR_DIR;
        finfo->valid |= APR_FINFO_TYPE;
    }
    else {
        finfo->filetype = APR_REG;
        finfo->valid |= APR_FINFO_TYPE;
    }
    FileTimeToAprTime(&finfo->ctime, &thedir->n.entry->ftCreationTime);
    FileTimeToAprTime(&finfo->mtime, &thedir->n.entry->ftLastWriteTime);
    FileTimeToAprTime(&finfo->atime, &thedir->n.entry->ftLastAccessTime);
    finfo->size = (thedir->n.entry->nFileSizeHigh * MAXDWORD)
                +  thedir->n.entry->nFileSizeLow;
    finfo->fname = NULL;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_dir_rewind(apr_dir_t *dir)
{
    dir_cleanup(dir);
    if (!FindClose(dir->dirhand)) {
        return apr_get_os_error();
    }    
    dir->dirhand = INVALID_HANDLE_VALUE;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_make_dir(const char *path, apr_fileperms_t perm,
                                       apr_pool_t *cont)
{
#if APR_HAS_UNICODE_FS
    apr_oslevel_e os_level;
    if (!apr_get_oslevel(cont, &os_level) && os_level >= APR_WIN_NT) 
    {
        apr_wchar_t wpath[APR_PATH_MAX];
        apr_status_t rv;
        if (rv = utf8_to_unicode_path(wpath, sizeof(wpath) 
                                              / sizeof(apr_wchar_t), path)) {
            return rv;
        }
        if (!CreateDirectoryW(wpath, NULL)) {
            return apr_get_os_error();
        }
    }
    else
#endif
        if (!CreateDirectory(path, NULL)) {
            return apr_get_os_error();
        }
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_remove_dir(const char *path, apr_pool_t *cont)
{
#if APR_HAS_UNICODE_FS
    apr_oslevel_e os_level;
    if (!apr_get_oslevel(cont, &os_level) && os_level >= APR_WIN_NT) 
    {
        apr_wchar_t wpath[APR_PATH_MAX];
        apr_status_t rv;
        if (rv = utf8_to_unicode_path(wpath, sizeof(wpath) 
                                              / sizeof(apr_wchar_t), path)) {
            return rv;
        }
        if (!RemoveDirectoryW(wpath)) {
            return apr_get_os_error();
        }
    }
    else
#endif
        if (!RemoveDirectory(path)) {
            return apr_get_os_error();
        }
    return APR_SUCCESS;
}






APR_DECLARE(apr_status_t) apr_get_os_dir(apr_os_dir_t **thedir,
                                         apr_dir_t *dir)
{
    if (dir == NULL) {
        return APR_ENODIR;
    }
    *thedir = dir->dirhand;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_put_os_dir(apr_dir_t **dir,
                                         apr_os_dir_t *thedir,
                                         apr_pool_t *cont)
{
    return APR_ENOTIMPL;
}
