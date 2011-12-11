/** \file hostlist.h
 *  \brief Header: hostlist
 */

#ifndef MC__HOSTLIST_H
#define MC__HOSTLIST_H

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

void hostlist_cmd (void);
char *hostlist_show (void);
int hostlist_save (void);
void hostlist_done (void);

/*** inline functions ****************************************************************************/
#endif /* MC__HOSTLIST_H */
