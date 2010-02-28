#ifndef _LIB_XQL_XPATH_NS_H_
#define _LIB_XQL_XPATH_NS_H_

#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#define MAX_NS 		64
#define XMLNS_PREFIX    "xmlns"

typedef struct nsMap {
    char *str;
    char *prefix[MAX_NS];
    char *ns[MAX_NS];
    size_t count;
} nsMap;

typedef nsMap *nsMapPtr;

extern nsMapPtr
parse_ns_string(char *str, size_t str_len);

extern void
free_ns_map(nsMapPtr map);

extern int
register_ns(nsMapPtr map, xmlXPathContextPtr ctx);

#endif /*_LIB_XQL_XPATH_NS_H_*/
