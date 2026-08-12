/*
 * vc_json.h - small DOM style JSON parser / writer.
 *
 * Supports the full JSON grammar (objects, arrays, strings with
 * escapes incl. \uXXXX surrogate pairs, numbers, true/false/null).
 * Input and output are UTF-8.
 */
#ifndef VC_JSON_H
#define VC_JSON_H

#include "vc_common.h"
#include "vc_str.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vc_json_type {
    VC_JSON_NULL = 0,
    VC_JSON_BOOL,
    VC_JSON_NUMBER,
    VC_JSON_STRING,
    VC_JSON_ARRAY,
    VC_JSON_OBJECT
} vc_json_type;

typedef struct vc_json vc_json;

struct vc_json {
    vc_json_type type;
    char        *key;      /* set when this value is an object member */
    vc_json     *next;     /* sibling in parent container            */
    /* value */
    bool         boolean;
    double       number;
    char        *string;   /* UTF-8, owned                            */
    vc_json     *child;    /* first element/member of array/object    */
};

/* Parsing ------------------------------------------------------------ */
vc_json *vc_json_parse(const char *text);            /* NULL on error   */
vc_json *vc_json_parse_len(const char *text, size_t len);
void     vc_json_free(vc_json *v);

/* Serialising -------------------------------------------------------- */
/* Returns malloc'd (vc_alloc) UTF-8 string; caller frees with vc_free. */
char *vc_json_write(const vc_json *v);
int   vc_json_write_buf(const vc_json *v, vc_buf *out);
/* Escape a raw string into out as a JSON string literal (with quotes). */
int   vc_json_escape_str(const char *s, vc_buf *out);

/* Access -------------------------------------------------------------- */
vc_json    *vc_json_obj_get(const vc_json *obj, const char *key);      /* case sensitive   */
vc_json    *vc_json_obj_get_ci(const vc_json *obj, const char *key);   /* case insensitive */
const char *vc_json_get_str(const vc_json *obj, const char *key, const char *def);
double      vc_json_get_num(const vc_json *obj, const char *key, double def);
bool        vc_json_get_bool(const vc_json *obj, const char *key, bool def);
size_t      vc_json_array_size(const vc_json *arr);

/* Building ------------------------------------------------------------ */
vc_json *vc_json_new_null(void);
vc_json *vc_json_new_bool(bool b);
vc_json *vc_json_new_num(double n);
vc_json *vc_json_new_str(const char *s);            /* copies s */
vc_json *vc_json_new_array(void);
vc_json *vc_json_new_object(void);

/* Add to containers. Object setters copy the key; the value is adopted
 * (owned by the container afterwards). Return VC_OK / error. */
int vc_json_obj_set(vc_json *obj, const char *key, vc_json *value);
int vc_json_obj_set_str(vc_json *obj, const char *key, const char *s);
int vc_json_obj_set_num(vc_json *obj, const char *key, double n);
int vc_json_obj_set_bool(vc_json *obj, const char *key, bool b);
int vc_json_obj_set_null(vc_json *obj, const char *key);
int vc_json_arr_add(vc_json *arr, vc_json *value);

#ifdef __cplusplus
}
#endif

#endif
