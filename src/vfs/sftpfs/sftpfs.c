/* Virtual File System: SFTP file system.

   Copyright (C)
   2011 Free Software Foundation, Inc.

   Authors:
   2011 Maslakov Ilia

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License
   as published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */


/**
 * \file
 * \brief Source: sftpfs FS
 */

#include <config.h>
#include <errno.h>
#include <sys/types.h>

#include <netdb.h>              /* struct hostent */
#include <sys/socket.h>         /* AF_INET */
#include <netinet/in.h>         /* struct in_addr */
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#include <libssh2.h>
#include <libssh2_sftp.h>

#include "lib/global.h"

#include "lib/util.h"
#include "lib/tty/tty.h"        /* tty_enable_interrupt_key () */
#include "lib/mcconfig.h"
#include "lib/vfs/vfs.h"
#include "lib/vfs/netutil.h"
#include "lib/vfs/utilvfs.h"
#include "lib/vfs/xdirentry.h"
#include "lib/vfs/gc.h"         /* vfs_stamp_create */
#include "lib/event.h"

#include "sftpfs.h"
#include "dialogs.h"

/*** global variables ****************************************************************************/

int sftpfs_timeout = 0;
char *sftpfs_privkey = NULL;
char *sftpfs_pubkey = NULL;

/*** file scope macro definitions ****************************************************************/
#define SUP ((sftpfs_super_data_t *) super->data)

#define SFTP_ESTABLISHED     1
#define SFTP_FAILED        500

#define SFTP_DEFAULT_PORT 22

/*** file scope type declarations ****************************************************************/

#define SFTP_HANDLE_MAXLEN 256  /* according to spec! */

typedef struct dir_entry
{
    char *text;
    struct dir_entry *next;
    struct stat my_stat;
    int merrno;
} dir_entry;

typedef struct
{
    char *dirname;
    char *path;                 /* the dir originally passed to sftpfs_opendir */
    dir_entry *entries;
    dir_entry *current;
} opendir_info;

typedef struct
{
    int sock;
    char *cwdir;
    char *host;
    char *user;
    char *password;
    gboolean auth_pw;
    int port;
    int flags;

    LIBSSH2_SESSION *session;
    LIBSSH2_SFTP *sftp_session;
    LIBSSH2_SFTP_HANDLE *sftpfs_handle;
    LIBSSH2_SFTP_HANDLE *sftpfile_handle;
    char *sftp_filename;
    int sftp_open_flags;
    const char *fingerprint;

} sftpfs_super_data_t;


static struct vfs_class vfs_sftpfs_ops;

static const char *vfs_my_name = "sftpfs";

/*** file scope variables ************************************************************************/

static int sftpfs_errno_int;

/*** file scope functions ************************************************************************/

void
sftpfs_load_param (void)
{
    char *profile;
    char *buffer;
    mc_config_t *sftpfs_config = NULL;

    profile = g_build_filename (mc_config_get_path (), "sftpfs.ini", NULL);
    sftpfs_config = mc_config_init (profile);
    g_free (profile);

    if (sftpfs_config == NULL)
        return;

    buffer = mc_config_get_string (sftpfs_config, "sftp", "privkey_path", "");
    if (buffer != NULL && buffer[0] != '\0')
        sftpfs_privkey = g_strdup (buffer);
    g_free (buffer);

    buffer = mc_config_get_string (sftpfs_config, "sftp", "pubkey_path", "");
    if (buffer != NULL && buffer[0] != '\0')
        sftpfs_pubkey = g_strdup (buffer);
    g_free (buffer);

    sftpfs_timeout = mc_config_get_int (sftpfs_config, "sftp", "vfs_timeout", 0);

    mc_config_deinit (sftpfs_config);
}

/* --------------------------------------------------------------------------------------------- */

void
sftpfs_save_param (void)
{
    char *profile;
    mc_config_t *sftpfs_config = NULL;

    profile = g_build_filename (mc_config_get_path (), "sftpfs.ini", NULL);
    sftpfs_config = mc_config_init (profile);
    g_free (profile);

    if (sftpfs_config == NULL)
        return;

    mc_config_del_group (sftpfs_config, "sftp");
    mc_config_set_string (sftpfs_config, "sftp", "privkey_path", sftpfs_privkey);
    mc_config_set_string (sftpfs_config, "sftp", "pubkey_path", sftpfs_pubkey);
    mc_config_set_int (sftpfs_config, "sftp", "vfs_timeout", sftpfs_timeout);
    mc_config_save_file (sftpfs_config, NULL);
    mc_config_deinit (sftpfs_config);
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_archive_same (struct vfs_class *me, struct vfs_s_super *super,
                     const char *archive_name, char *op, void *cookie)
{

    char *host, *user, *pass;
    int result, port;

    (void) me;
    (void) archive_name;
    (void) cookie;

    op = vfs_split_url (strchr (op, ':') + 1, &host, &user, &port, &pass, SFTP_DEFAULT_PORT,
                        URL_NOSLASH | URL_USE_ANONYMOUS);

    g_free (op);

    if (user == NULL)
        user = vfs_get_local_username ();

    result = ((strncmp (SUP->host, host, strlen (SUP->host)) == 0)
              && (SUP->port == port) && (strcmp (user, SUP->user) == 0));

    g_free (host);
    g_free (user);
    g_free (pass);

    return result;
}

/* --------------------------------------------------------------------------------------------- */

static struct vfs_s_super *
sftpfs_get_super (struct vfs_class *me, const char *url)
{
    GList *iter;
    char *host, *user, *pass;
    int port;
    char *op;

    op = vfs_split_url (strchr (url, ':') + 1, &host, &user, &port, &pass, SFTP_DEFAULT_PORT,
                        URL_NOSLASH | URL_USE_ANONYMOUS);
    g_free (user);
    g_free (pass);

    for (iter = MEDATA->supers; iter != NULL; iter = g_list_next (iter))
    {
        const struct vfs_s_super *super = (const struct vfs_s_super *) iter->data;
        if (strncmp (SUP->host, host, strlen (SUP->host)) == 0)
        {
            g_free (host);
            return (void *) super;
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
static int
sftpfs_waitsocket (int socket_fd, LIBSSH2_SESSION * session)
{
    struct timeval timeout;
    int rc;
    fd_set fd;
    fd_set *writefd = NULL;
    fd_set *readfd = NULL;
    int dir;

    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    FD_ZERO (&fd);

    FD_SET (socket_fd, &fd);

    /* now make sure we wait in the correct direction */
    dir = libssh2_session_block_directions (session);

    if (dir & LIBSSH2_SESSION_BLOCK_INBOUND)
        readfd = &fd;

    if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
        writefd = &fd;

    rc = select (socket_fd + 1, readfd, writefd, NULL, &timeout);

    return rc;
}

/* --------------------------------------------------------------------------------------------- */

static char *
sftpfs_translate_path (const char *path)
{
    char *rpath;
    char *host;
    char *user;
    int port;
    char *pass;
    char *ppath;
    ppath = strchr (path, ':');

    if (ppath != NULL)
    {
        ppath++;
        rpath = vfs_split_url (ppath, &host, &user, &port, &pass, 0, 0);
        g_free (host);
        g_free (user);
        g_free (pass);
    }
    else
        rpath = g_strdup (path);

    return rpath;
}

/* --------------------------------------------------------------------------------------------- */

static void *
sftpfs_open (struct vfs_class *me, const char *file, int flags, mode_t mode)
{
    char *remote_path;
    struct vfs_s_super *super;
    unsigned long sftp_open_flags = 0;
    int sftp_open_mode = 0;

    (void) mode;

    super = sftpfs_get_super (me, file);

    if (super == NULL)
        return NULL;

    if ((flags & O_CREAT) || (flags & O_WRONLY))
    {
        sftp_open_flags = ((flags & O_WRONLY) ? LIBSSH2_FXF_WRITE : 0) |
                          ((flags & O_CREAT) ? LIBSSH2_FXF_CREAT : 0) |
                          ((flags & ~O_APPEND) ? LIBSSH2_FXF_TRUNC : 0);
        sftp_open_mode = LIBSSH2_SFTP_S_IRUSR |
            LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH;

    }
    else
    {
        sftp_open_flags = LIBSSH2_FXF_READ;
    }

    remote_path = sftpfs_translate_path (file);
    do
    {
        SUP->sftpfile_handle = libssh2_sftp_open (SUP->sftp_session, remote_path,
                                                  sftp_open_flags, sftp_open_mode);

        if (SUP->sftpfile_handle == NULL)
        {
            if (libssh2_session_last_errno (SUP->session) != LIBSSH2_ERROR_EAGAIN)
                return NULL;
            else
                sftpfs_waitsocket (SUP->sock, SUP->session);
        }

    }
    while (SUP->sftpfile_handle == NULL);

    g_free (remote_path);

    vfs_print_message ("sftpfs: (Ctrl-G break) Reading...");
    tty_enable_interrupt_key ();

    SUP->sftp_open_flags = flags;
    SUP->sftp_filename = g_strdup (file);

    return super;
}

/* --------------------------------------------------------------------------------------------- */

static void *
sftpfs_opendir (struct vfs_class *me, const char *dirname)
{

    char *remote_path;
    struct vfs_s_super *super;

    tty_disable_interrupt_key ();
    tty_enable_interrupt_key ();
    super = sftpfs_get_super (me, dirname);

    if (super == NULL)
        return NULL;

    remote_path = sftpfs_translate_path (dirname);

    do
    {
        SUP->sftpfs_handle = libssh2_sftp_opendir (SUP->sftp_session, remote_path);

        if (SUP->sftpfs_handle == NULL)
        {
            if (libssh2_session_last_errno (SUP->session) != LIBSSH2_ERROR_EAGAIN)
                return NULL;
            else
                sftpfs_waitsocket (SUP->sock, SUP->session);
        }

    }
    while (SUP->sftpfs_handle == NULL);

    g_free (remote_path);
    if (SUP->sftpfs_handle == NULL)
        return NULL;
    return super;
}

/* --------------------------------------------------------------------------------------------- */

static void *
sftpfs_readdir (void *data)
{
    char mem[BUF_MEDIUM];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    struct vfs_s_super *super = (struct vfs_s_super *) data;
    LIBSSH2_SFTP_HANDLE *fh = SUP->sftpfs_handle;

    static union vfs_dirent sftpfs_readdir_data;
    static char *const dirent_dest = sftpfs_readdir_data.dent.d_name;

    int rc;
    if (tty_got_interrupt ())
    {
        tty_disable_interrupt_key ();
        return NULL;
    }

    /* loop until we fail */
    while ((rc = libssh2_sftp_readdir (fh, mem, sizeof (mem), &attrs)) == LIBSSH2_ERROR_EAGAIN)
        ;

    if (mem[0] != '\0')
        vfs_print_message (_("sftpfs: (Ctrl-G break) Listing... %s"), mem);

    /* rc is the length of the file name in the mem buffer */

    if (rc <= 0)
        return NULL;

    g_strlcpy (dirent_dest, mem, BUF_MEDIUM);
    compute_namelen (&sftpfs_readdir_data.dent);
    return &sftpfs_readdir_data;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_closedir (void *data)
{
    struct vfs_s_super *super = (struct vfs_s_super *) data;
    LIBSSH2_SFTP_HANDLE *sftpfs_handle = SUP->sftpfs_handle;

    return libssh2_sftp_closedir (sftpfs_handle);
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_lstat (struct vfs_class *me, const char *path, struct stat *buf)
{

    struct vfs_s_super *super;
    char *remote_path;
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int res;

    super = sftpfs_get_super (me, path);


    if (super == NULL || SUP->sftp_session == NULL)
        return -1;

    remote_path = sftpfs_translate_path (path);


    do
    {
        res = libssh2_sftp_stat_ex (SUP->sftp_session, remote_path,
                                    strlen (remote_path), LIBSSH2_SFTP_LSTAT, &attrs);

        if (res < 0)
        {
            if (libssh2_session_last_errno (SUP->session) != LIBSSH2_ERROR_EAGAIN)
                return -1;
            else
                sftpfs_waitsocket (SUP->sock, SUP->session);
        }

    }
    while (res < 0);

    g_free (remote_path);

    if (attrs.flags & LIBSSH2_SFTP_ATTR_UIDGID)
    {
        buf->st_uid = attrs.uid;
        buf->st_gid = attrs.gid;
    }

    if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME)
    {
        buf->st_atime = attrs.atime;
        buf->st_mtime = attrs.mtime;
        buf->st_ctime = attrs.mtime;
    }

    if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE)
        buf->st_size = attrs.filesize;

    if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS)
        buf->st_mode = attrs.permissions;

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_fstat (void *data, struct stat *buf)
{

    LIBSSH2_SFTP_ATTRIBUTES attrs;
    struct vfs_s_super *super = (struct vfs_s_super *) data;
    LIBSSH2_SFTP_HANDLE *fh = SUP->sftpfile_handle;
    int res;

    if (fh == NULL)
        return -1;

    do
    {
        res = libssh2_sftp_fstat_ex (fh, &attrs, 0);

        if (res < 0)
        {
            if (libssh2_session_last_errno (SUP->session) != LIBSSH2_ERROR_EAGAIN)
                return -1;
            else
                sftpfs_waitsocket (SUP->sock, SUP->session);
        }

    }
    while (res < 0);


    if (res < 0)
        return -1;

    if (attrs.flags & LIBSSH2_SFTP_ATTR_UIDGID)
    {
        buf->st_uid = attrs.uid;
        buf->st_gid = attrs.gid;
    }

    if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME)
    {
        buf->st_atime = attrs.atime;
        buf->st_mtime = attrs.mtime;
        buf->st_ctime = attrs.mtime;
    }

    if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE)
        buf->st_size = attrs.filesize;

    if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS)
        buf->st_mode = attrs.permissions;

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_chmod (struct vfs_class *me, const char *path, int mode)
{
    (void) me;
    (void) path;
    (void) mode;
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_chown (struct vfs_class *me, const char *path, uid_t owner, gid_t group)
{
    (void) me;
    (void) path;
    (void) owner;
    (void) group;
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_utime (struct vfs_class *me, const char *path, struct utimbuf *times)
{
    (void) me;
    (void) path;
    (void) times;

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_readlink (struct vfs_class *me, const char *path, char *buf, size_t size)
{
    char *remote_path;
    struct vfs_s_super *super;
    super = sftpfs_get_super (me, path);
    if (super != NULL)
    {
        int res;
        remote_path = sftpfs_translate_path (path);
        do
        {
            res = libssh2_sftp_readlink (SUP->sftp_session, remote_path, buf, size);

            if (res < 0)
            {
                if (libssh2_session_last_errno (SUP->session) != LIBSSH2_ERROR_EAGAIN)
                    return -1;
                else
                    sftpfs_waitsocket (SUP->sock, SUP->session);
            }

        }
        while (res < 0);

        g_free (remote_path);
        return res;
    }
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_unlink (struct vfs_class *me, const char *path)
{
    char *remote_path;
    struct vfs_s_super *super;
    super = sftpfs_get_super (me, path);

    if (super != NULL)
    {
        remote_path = sftpfs_translate_path (path);
        libssh2_sftp_unlink_ex (SUP->sftp_session, remote_path, strlen (remote_path));
        g_free (remote_path);
        return 0;
    }
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_symlink (struct vfs_class *me, const char *n1, const char *n2)
{
    char *remote_path_n1;
    char *remote_path_n2;
    struct vfs_s_super *super;

    super = sftpfs_get_super (me, n1);
    if (super != NULL)
    {
        remote_path_n1 = sftpfs_translate_path (n1);
        remote_path_n2 = sftpfs_translate_path (n2);
        libssh2_sftp_symlink_ex (SUP->sftp_session, remote_path_n1, strlen (remote_path_n1),
                                 remote_path_n2, strlen (remote_path_n2), LIBSSH2_SFTP_SYMLINK);
        g_free (remote_path_n1);
        g_free (remote_path_n2);
        return 0;
    }
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static ssize_t
sftpfs_write (void *data, const char *buf, size_t nbyte)
{
    int rc;
    struct vfs_s_super *super = (struct vfs_s_super *) data;
    LIBSSH2_SFTP_HANDLE *fh = SUP->sftpfile_handle;


    if (fh == NULL)
    {
        return -1;
    }

    rc = libssh2_sftp_write (fh, buf, nbyte);
    return rc;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_rename (struct vfs_class *me, const char *a, const char *b)
{
    char *remote_path_a;
    char *remote_path_b;
    struct vfs_s_super *super;

    super = sftpfs_get_super (me, a);
    if (super != NULL)
    {
        remote_path_a = sftpfs_translate_path (a);
        remote_path_b = sftpfs_translate_path (b);
        libssh2_sftp_rename_ex (SUP->sftp_session, remote_path_a, strlen (remote_path_a),
                                remote_path_b, strlen (remote_path_b), 0);
        g_free (remote_path_a);
        g_free (remote_path_b);
        return 0;
    }
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_chdir (struct vfs_class *me, const char *path)
{
    struct vfs_s_super *super;
    char *mpath;
    char *rpath;

    super = sftpfs_get_super (me, path);
    if (super == NULL || SUP->session == NULL)
    {
        mpath = g_strdup (path);
        rpath = g_strdup (vfs_s_get_path_mangle (me, mpath, &super, 0));
        g_free (mpath);
        g_free (rpath);
    }
    else
    {
        vfs_print_message (_("sftpfs: already established"));
    }
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_mknod (struct vfs_class *me, const char *path, mode_t mode, dev_t dev)
{
    (void) me;
    (void) path;
    (void) mode;
    (void) dev;

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_link (struct vfs_class *me, const char *p1, const char *p2)
{
    (void) me;
    (void) p1;
    (void) p2;

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_mkdir (struct vfs_class *me, const char *path, mode_t mode)
{
    char *remote_path;
    struct vfs_s_super *super;

    (void) mode;

    super = sftpfs_get_super (me, path);
    if (super != NULL)
    {
        int rc;
        remote_path = sftpfs_translate_path (path);
        rc = libssh2_sftp_mkdir (SUP->sftp_session, remote_path,
                                 LIBSSH2_SFTP_S_IRWXU | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IXGRP
                                 | LIBSSH2_SFTP_S_IROTH | LIBSSH2_SFTP_S_IXOTH);
        g_free (remote_path);
        return 0;
    }
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_rmdir (struct vfs_class *me, const char *path)
{
    char *remote_path;
    struct vfs_s_super *super;

    super = sftpfs_get_super (me, path);
    if (super != NULL)
    {
        remote_path = sftpfs_translate_path (path);
        libssh2_sftp_rmdir_ex (SUP->sftp_session, remote_path, strlen (remote_path));
        g_free (remote_path);
        return 0;
    }
    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sftpfs_plugin_name_for_config_dialog (const gchar * event_group_name, const gchar * event_name,
                                      gpointer init_data, gpointer data)
{
    GList **list = data;

    (void) event_group_name;
    (void) event_name;
    (void) init_data;

    *list = g_list_append (*list, (gpointer) vfs_my_name);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sftpfs_plugin_show_config_dialog (const gchar * event_group_name, const gchar * event_name,
                                  gpointer init_data, gpointer data)
{
    (void) event_group_name;
    (void) event_name;
    (void) init_data;

    if ((const char *) data != vfs_my_name)
        return TRUE;

    configure_sftpfs ();

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static ssize_t
sftpfs_read (void *data, char *buffer, size_t count)
{
    int rc;
    struct vfs_s_super *super = (struct vfs_s_super *) data;
    LIBSSH2_SFTP_HANDLE *fh = SUP->sftpfile_handle;

    if (tty_got_interrupt ())
    {
        tty_disable_interrupt_key ();
        return 0;
    }

    if (fh == NULL)
        return -1;

    do
    {
        rc = libssh2_sftp_read (fh, buffer, count);
        if (rc < 0)
        {
            if (libssh2_session_last_errno (SUP->session) != LIBSSH2_ERROR_EAGAIN)
                return -1;
            else
                sftpfs_waitsocket (SUP->sock, SUP->session);
        }

    }
    while (rc < 0);


    if (rc >= 0)
        return rc;
    else
        return -1;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_close (void *data)
{
    struct vfs_s_super *super = (struct vfs_s_super *) data;
    LIBSSH2_SFTP_HANDLE *sftp_handle = SUP->sftpfile_handle;

    if (sftp_handle != NULL)
    {
        g_free (SUP->sftp_filename);
        libssh2_sftp_close (sftp_handle);
        return 0;
    }
    else
        return -1;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_errno (struct vfs_class *me)
{
    (void) me;
    /*
       struct vfs_s_super *super;
       super = sftpfs_get_super (me);
       if (super != NULL && SUP->session != NULL)
       return libssh2_session_last_errno (SUP->session);
     */
    return errno;
}

/* --------------------------------------------------------------------------------------------- */

static off_t
sftpfs_lseek (void *data, off_t offset, int whence)
{
    struct vfs_s_super *super = (struct vfs_s_super *) data;
    LIBSSH2_SFTP_HANDLE *fh = SUP->sftpfile_handle;
    off_t cur;

    (void) whence;

    cur = (off_t) libssh2_sftp_tell64 (fh);
    if (offset > 0 && cur < offset)
    {
        libssh2_sftp_seek (fh, offset);
    }
    else
    {
        libssh2_sftp_close (fh);
        sftpfs_open (super->me, SUP->sftp_filename, SUP->sftp_open_flags, (mode_t *) NULL);
        fh = SUP->sftpfile_handle;
        libssh2_sftp_seek (fh, offset);
    }
    cur = (off_t) libssh2_sftp_tell64 (fh);
    return cur;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_open_socket (struct vfs_s_super *super)
{
    struct addrinfo hints, *res, *curr_res;
    int my_socket = 0;
    char *host = NULL;
    char *port = NULL;
    int tmp_port;
    int e;

    host = g_strdup (SUP->host);

    if (!host || !*host)
    {
        vfs_print_message (_("sftpfs: Invalid host name."));
        g_free (host);
        return -1;
    }

    tmp_port = SUP->port;
    port = g_strdup_printf ("%hu", (unsigned short) tmp_port);
    if (port == NULL)
    {
        g_free (host);
        vfs_print_message (_("sftpfs: Invalid port value."));
        return -1;
    }

    tty_enable_interrupt_key ();        /* clear the interrupt flag */

    memset (&hints, 0, sizeof (struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

#ifdef AI_ADDRCONFIG
    /* By default, only look up addresses using address types for
     * which a local interface is configured (i.e. no IPv6 if no IPv6
     * interfaces, likewise for IPv4 (see RFC 3493 for details). */
    hints.ai_flags = AI_ADDRCONFIG;
#endif

    e = getaddrinfo (host, port, &hints, &res);

#ifdef AI_ADDRCONFIG
    if (e == EAI_BADFLAGS)
    {
        /* Retry with no flags if AI_ADDRCONFIG was rejected. */
        hints.ai_flags = 0;
        e = getaddrinfo (host, port, &hints, &res);
    }
#endif

    g_free (port);
    port = NULL;

    if (e != 0)
    {
        tty_disable_interrupt_key ();
        vfs_print_message (_("sftpfs: %s"), gai_strerror (e));
        g_free (host);
        return -1;
    }

    for (curr_res = res; curr_res != NULL; curr_res = curr_res->ai_next)
    {

        my_socket = socket (curr_res->ai_family, curr_res->ai_socktype, curr_res->ai_protocol);

        if (my_socket < 0)
        {

            if (curr_res->ai_next != NULL)
                continue;

            tty_disable_interrupt_key ();
            vfs_print_message (_("sftpfs: %s"), unix_error_string (errno));
            g_free (host);
            freeaddrinfo (res);
            sftpfs_errno_int = errno;
            return -1;
        }

        vfs_print_message (_("sftpfs: making connection to %s"), host);
        g_free (host);
        host = NULL;

        if (connect (my_socket, curr_res->ai_addr, curr_res->ai_addrlen) >= 0)
            break;

        sftpfs_errno_int = errno;
        close (my_socket);

        if (errno == EINTR && tty_got_interrupt ())
        {
            vfs_print_message (_("sftpfs: connection interrupted by user"));
        }
        else if (res->ai_next == NULL)
        {
            vfs_print_message (_("sftpfs: connection to server failed: %s"),
                               unix_error_string (errno));
        }
        else
        {
            continue;
        }

        freeaddrinfo (res);
        tty_disable_interrupt_key ();
        return -1;
    }

    freeaddrinfo (res);
    tty_disable_interrupt_key ();
    return my_socket;
}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_do_connect (struct vfs_class *me, struct vfs_s_super *super)
{
    LIBSSH2_SESSION *session;
    LIBSSH2_SFTP *sftp_session;
    int rc;

    (void) me;

    rc = libssh2_init (0);
    if (rc != 0)
    {
        return -1;
    }

    /*
     * The application code is responsible for creating the socket
     * and establishing the connection
     */
    SUP->sock = sftpfs_open_socket (super);

    /* Create a session instance */
    session = libssh2_session_init ();

    if (session == NULL)
        goto err_conn;

    SUP->session = session;


    /* ... start it up. This will trade welcome banners, exchange keys,
     * and setup crypto, compression, and MAC layers
     */

    rc = libssh2_session_startup (SUP->session, SUP->sock);

    if (rc != 0)
    {
        vfs_print_message (_("sftpfs: Failure establishing SSH session: (%d)"), rc);
        goto err_conn;
    }

    /* At this point we havn't yet authenticated.  The first thing to do
     * is check the hostkey's fingerprint against our known hosts Your app
     * may have it hard coded, may go to a file, may present it to the
     * user, that's your call
     */
    SUP->fingerprint = libssh2_hostkey_hash (SUP->session, LIBSSH2_HOSTKEY_HASH_SHA1);

    if (SUP->auth_pw && SUP->password != NULL)
    {
        /* We could authenticate via password */
        if (libssh2_userauth_password (SUP->session, SUP->user, SUP->password))
        {
            vfs_print_message (_("Authentication by password failed"));
            goto err_conn;
        }
    }
    else
    {
        /* Or by public key */
        if (libssh2_userauth_publickey_fromfile (session, SUP->user,
                                                 sftpfs_pubkey, sftpfs_privkey, SUP->password))
        {
            vfs_print_message (_("sftpfs: Authentication by public key failed"));
            if (SUP->password == NULL)
            {
                char *p;
                p = g_strconcat (_("sftpfs: Password required for"), " ", SUP->user, " ", NULL);
                SUP->password = vfs_get_password (p);
                g_free (p);
            }
            /* Try authenticate via password */
            if (libssh2_userauth_password (SUP->session, SUP->user, SUP->password))
            {
                vfs_print_message (_("Authentication by password failed"));
                goto err_conn;
            }
        }
    }

    sftp_session = libssh2_sftp_init (SUP->session);

    if (sftp_session == NULL)
    {
        goto err_conn;
    }
    SUP->sftp_session = sftp_session;
    /* Since we have not set non-blocking, tell libssh2 we are blocking */
    libssh2_session_set_blocking (SUP->session, 1);

    return SFTP_ESTABLISHED;

  err_conn:

    libssh2_session_disconnect (SUP->session, "Normal Shutdown");
    libssh2_session_free (SUP->session);

    return SFTP_FAILED;

}

/* --------------------------------------------------------------------------------------------- */

static int
sftpfs_open_archive (struct vfs_class *me, struct vfs_s_super *super,
                     const char *archive_name, char *op)
{
    char *host, *user, *password;
    int port;
    char *path;
    gboolean auth_pw = FALSE;

    (void) archive_name;

    path = vfs_split_url (strchr (op, ':') + 1, &host, &user, &port, &password, 22, 0);

    if (host == NULL || *host == '\0')
    {
        vfs_print_message (_("sftpfs: Invalid host name."));
        ERRNOR (EPERM, 0);
    }

    if (user == NULL)
    {
        user = vfs_get_local_username ();
        if (user == NULL)
            ERRNOR (EPERM, 0);
    }

    if (super->data == NULL )
    {
        super->data = g_new0 (sftpfs_super_data_t, 1);
        SUP->auth_pw = auth_pw;
        SUP->host = host;
        SUP->user = user;
        SUP->port = port;
        SUP->cwdir = NULL;
        SUP->password = password;
        super->name = g_strdup (path);
        super->root = vfs_s_new_inode (me, super, vfs_s_default_stat (me, S_IFDIR | 0755));
        g_free (path);
        return sftpfs_do_connect (me, super);
    }
    else
    {
        return SFTP_ESTABLISHED;
    }

}

/* --------------------------------------------------------------------------------------------- */

static void
sftpfs_free_archive (struct vfs_class *me, struct vfs_s_super *super)
{
    (void) me;
    (void) super;

    libssh2_sftp_shutdown (SUP->sftp_session);
    libssh2_session_disconnect (SUP->session, "Normal Shutdown");
    libssh2_session_free (SUP->session);

    close (SUP->sock);
    libssh2_exit ();

    g_free (SUP->host);
    g_free (SUP->user);
    g_free (SUP->cwdir);
    g_free (SUP->password);
    g_free (super->data);
    super->data = NULL;

    return;
}

/* --------------------------------------------------------------------------------------------- */

static void
sftpfs_done (struct vfs_class *me)
{

    (void) me;
    g_free (sftpfs_privkey);
    g_free (sftpfs_pubkey);
}

static int
sftpfs_init (struct vfs_class *me)
{
    (void) me;
    sftpfs_load_param ();
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

void
init_sftpfs (void)
{
    static struct vfs_s_subclass sftpfs_subclass;

    tcp_init ();

    sftpfs_subclass.open_archive = sftpfs_open_archive;
    sftpfs_subclass.free_archive = sftpfs_free_archive;
    sftpfs_subclass.archive_same = sftpfs_archive_same;

    vfs_s_init_class (&vfs_sftpfs_ops, &sftpfs_subclass);

    vfs_sftpfs_ops.name = vfs_my_name;
    vfs_sftpfs_ops.prefix = "sftp:";
    vfs_sftpfs_ops.flags = VFSF_NOLINKS;
    vfs_sftpfs_ops.init = sftpfs_init;
    vfs_sftpfs_ops.done = sftpfs_done;
    vfs_sftpfs_ops.open = sftpfs_open;
    vfs_sftpfs_ops.close = sftpfs_close;
    vfs_sftpfs_ops.read = sftpfs_read;
    vfs_sftpfs_ops.write = sftpfs_write;
    vfs_sftpfs_ops.opendir = sftpfs_opendir;
    vfs_sftpfs_ops.readdir = sftpfs_readdir;
    vfs_sftpfs_ops.closedir = sftpfs_closedir;
    vfs_sftpfs_ops.stat = sftpfs_lstat;
    vfs_sftpfs_ops.lstat = sftpfs_lstat;
    vfs_sftpfs_ops.fstat = sftpfs_fstat;
    vfs_sftpfs_ops.chmod = sftpfs_chmod;
    vfs_sftpfs_ops.chown = sftpfs_chown;
    vfs_sftpfs_ops.utime = sftpfs_utime;
    vfs_sftpfs_ops.readlink = sftpfs_readlink;
    vfs_sftpfs_ops.symlink = sftpfs_symlink;
    vfs_sftpfs_ops.link = sftpfs_link;
    vfs_sftpfs_ops.unlink = sftpfs_unlink;
    vfs_sftpfs_ops.rename = sftpfs_rename;
    vfs_sftpfs_ops.chdir = sftpfs_chdir;
    vfs_sftpfs_ops.ferrno = sftpfs_errno;
    vfs_sftpfs_ops.lseek = sftpfs_lseek;
    vfs_sftpfs_ops.mknod = sftpfs_mknod;
    vfs_sftpfs_ops.mkdir = sftpfs_mkdir;
    vfs_sftpfs_ops.rmdir = sftpfs_rmdir;
    vfs_register_class (&vfs_sftpfs_ops);

    mc_event_add ("vfs", "plugin_name_for_config_dialog", sftpfs_plugin_name_for_config_dialog,
                  NULL, NULL);
    mc_event_add ("vfs", "plugin_show_config_dialog", sftpfs_plugin_show_config_dialog, NULL, NULL);
}

/* --------------------------------------------------------------------------------------------- */
