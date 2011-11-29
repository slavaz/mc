/** \file  ext.h
 *  \brief Header: extension dependent execution
 */

#ifndef MC__EXT_H
#define MC__EXT_H
/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

int regex_command (const vfs_path_t * filename_vpath, const char *action, int *move_dir);

/* Call it after the user has edited the mc.ext file,
 * to flush the cached mc.ext file
 */
void flush_extension_file (void);

/*** inline functions ****************************************************************************/
#endif /* MC__EXT_H */
