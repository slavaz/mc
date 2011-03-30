/**
 * \file
 * \brief Header: SFTP FS
 */

#ifndef MC_VFS_SFTPFS_H
#define MC_VFS_SFTPFS_H

#include "lib/vfs/vfs.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/
extern int sftpfs_timeout;
extern char *sftpfs_privkey;
extern char *sftpfs_pubkey;
/*** declarations of public functions ************************************************************/

extern void init_sftpfs (void);
extern void sftpfs_load_param (void);
extern void sftpfs_save_param (void);

/*** inline functions ****************************************************************************/
#endif
