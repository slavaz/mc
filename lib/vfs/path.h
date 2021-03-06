#ifndef MC__VFS_PATH_H
#define MC__VFS_PATH_H

/*** typedefs(not structures) and defined constants **********************************************/

#define VFS_PATH_URL_DELIMITER "://"

/*** enums ***************************************************************************************/

typedef enum
{
    VPF_NONE = 0,
    VPF_NO_CANON = 1 << 0,
    VPF_USE_DEPRECATED_PARSER = 1 << 1
} vfs_path_flag_t;

/*** structures declarations (and typedefs of structures)*****************************************/

struct vfs_class;
struct vfs_url_struct;

typedef struct
{
    GList *path;
} vfs_path_t;

typedef struct
{
    char *user;
    char *password;
    char *host;
    gboolean ipv6;
    int port;
    char *path;
    struct vfs_class *class;
    char *encoding;
    char *vfs_prefix;

    struct
    {
        GIConv converter;
        DIR *info;
    } dir;
} vfs_path_element_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

vfs_path_t *vfs_path_new (void);
vfs_path_t *vfs_path_clone (const vfs_path_t * vpath);
void vfs_path_remove_element_by_index (vfs_path_t * vpath, int element_index);
void vfs_path_free (vfs_path_t * path);
int vfs_path_elements_count (const vfs_path_t * path);

char *vfs_path_to_str (const vfs_path_t * path);
char *vfs_path_to_str_elements_count (const vfs_path_t * path, int elements_count);
vfs_path_t *vfs_path_from_str (const char *path_str);
vfs_path_t *vfs_path_from_str_flags (const char *path_str, vfs_path_flag_t flags);

vfs_path_element_t *vfs_path_get_by_index (const vfs_path_t * path, int element_index);
vfs_path_element_t *vfs_path_element_clone (const vfs_path_element_t * element);
void vfs_path_element_free (vfs_path_element_t * element);

struct vfs_class *vfs_prefix_to_class (const char *prefix);

gboolean vfs_path_element_need_cleanup_converter (const vfs_path_element_t * element);

char *vfs_path_serialize (const vfs_path_t * vpath, GError ** error);
vfs_path_t *vfs_path_deserialize (const char *data, GError ** error);

/*** inline functions ****************************************************************************/

static inline gboolean
vfs_path_element_valid (const vfs_path_element_t * element)
{
    return (element != NULL && element->class != NULL);
}

#endif
