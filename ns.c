/*
 * DOM libary for MySQL.
 * A set of MySQL user defined functions (udf) to evaluate XPATH expressions and modify XML data
 * directly from MySQL using an SQL query
 *
 * Copyright (C) 2008 Dmitry Smal <mialinx@gmail.com>
 * web:   http://www.mysqludf.com/lib_mysqludf_dom
 * email: mysql-udf-repository@googlegroups.com
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#ifdef STANDARD
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#else
#include <my_global.h>
#include <my_sys.h>
#include <m_string.h>
#endif /*STANDARD*/

#include <mysql.h>
#include <libxml/xpathInternals.h>

#include "ns.h"

#define ACTION_MASK     0xFFFFFF00
#define STATE_MASK      0x000000FF

#define APP     0x0100  /*set prefix str pointer at next char*/
#define ANP     0x0200  /*set namespase str pointer at next char */
#define ASZ     0x0400  /*set zero at current char*/
#define AXM     0x0800  /*check xmlns*/
#define APA     0x1000  /*next pair*/ 

typedef enum nsSymbol {
    CB, /* blank symbols*/
    CE, /* [=] */
    CC, /* [:] */
    CQ, /* ["] */
    CW, /* [a-zA-Z0-9] */
    CA  /* [^"] */
} nsSymbol;

typedef enum nsState {
    SWX, /* waiting xmlns */
    SWP, /* waiting prefix */
    SRP, /* reading prefix */
    SWE, /* waiting = */
    SWN, /* waiting namespace */
    SRN, /* reading namespace */
    SER  /* error */
} nsState;

/* ====== debug ====== */
static char*
dbg_sym(nsSymbol s) {
    switch(s) {
        case CB:
            return (char*)"CB";
        case CE:
            return (char*)"CE";
        case CC:
            return (char*)"CC";
        case CQ:
            return (char*)"CQ";
        case CW:
            return (char*)"CW";
        case CA:
            return (char*)"CA";
        default:
            return (char*)"UNKNOWN";
    }
}

static char*
dbg_state(nsState s) {
    switch(s) {
        case SWX:
            return (char*)"SWX";
        case SWP:
            return (char*)"SWP";
        case SRP:
            return (char*)"SRP";
        case SWE:
            return (char*)"SWE";
        case SWN:
            return (char*)"SWN";
        case SRN:
            return (char*)"SRN";
        case SER:
            return (char*)"SER";
        default:
            return (char*)"UNKNOWN";
    }
}
/* ====== debug ====== */

static unsigned long states[7][6] = {
/*          CB          CE          CC          CQ              CW          CA */    
/*SWX*/ {   SWX,        SER,        SER,        SER,            SWP|AXM,    SER },
/*SWP*/ {   SER,        SER,        SRP|APP,    SER,            SER,        SER },
/*SRP*/ {   SWE|ASZ,    SWN|ASZ,    SER,        SER,            SRP,        SER },
/*SWE*/ {   SWE,        SWN,        SER,        SER,            SER,        SER },
/*SWN*/ {   SWN,        SER,        SER,        SRN|ANP,        SER,        SER },
/*SRN*/ {   SRN,        SRN,        SRN,        SWX|ASZ|APA,    SRN,        SRN },
/*SER*/ {   SER,        SER,        SER,        SER,            SER,        SER }
};

static nsSymbol symbols[256] = {
    CA, CA, CA, CA, CA, CA, CA, CA,     CA, CB, CB, CA, CB, CB, CA, CA, 
    CA, CA, CA, CA, CA, CA, CA, CA,     CA, CA, CA, CA, CA, CA, CA, CA, 
    CB, CA, CQ, CA, CA, CA, CA, CA,     CA, CA, CA, CA, CA, CA, CA, CA, 
    CW, CW, CW, CW, CW, CW, CW, CW,     CW, CW, CC, CA, CA, CE, CA, CA, 
    CA, CW, CW, CW, CW, CW, CW, CW,     CW, CW, CW, CW, CW, CW, CW, CW, 
    CW, CW, CW, CW, CW, CW, CW, CW,     CW, CW, CW, CA, CA, CA, CA, CA, 
    CA, CW, CW, CW, CW, CW, CW, CW,     CW, CW, CW, CW, CW, CW, CW, CW, 
    CW, CW, CW, CW, CW, CW, CW, CW,     CW, CW, CW, CA, CA, CA, CA, CA, 

    CA, CA, CA, CA, CA, CA, CA, CA,     CA, CA, CA, CA, CA, CA, CA, CA, 
    CA, CA, CA, CA, CA, CA, CA, CA,     CA, CA, CA, CA, CA, CA, CA, CA, 
    CA, CA, CA, CA, CA, CA, CA, CA,     CA, CA, CA, CA, CA, CA, CA, CA, 
    CA, CA, CA, CA, CA, CA, CA, CA,     CA, CA, CA, CA, CA, CA, CA, CA, 
    CA, CA, CA, CA, CA, CA, CA, CA,     CA, CA, CA, CA, CA, CA, CA, CA, 
    CA, CA, CA, CA, CA, CA, CA, CA,     CA, CA, CA, CA, CA, CA, CA, CA, 
    CA, CA, CA, CA, CA, CA, CA, CA,     CA, CA, CA, CA, CA, CA, CA, CA, 
    CA, CA, CA, CA, CA, CA, CA, CA,     CA, CA, CA, CA, CA, CA, CA, CA
};

nsMapPtr
parse_ns_string(char *str, size_t str_len) {
    size_t i;
    nsSymbol chr;
    nsState state;
    u_int32_t data;

    nsMapPtr map = (nsMapPtr) malloc(sizeof(nsMap));
    memset(map, '\0', sizeof(nsMap));
    if (!(map->str = (char *) malloc(sizeof(char) * (str_len + 1)))) {
        goto ERR;
    }
    memcpy(map->str, str, str_len);
    map->str[str_len] = '\0';


    for (i = 0, state = SWX; i < str_len; i++) {
        chr = symbols[(unsigned char)map->str[i]];
        data = states[state][chr];
        state = (nsState) (data & STATE_MASK);
        if (data & ACTION_MASK & APP) {
            map->prefix[map->count] = &(map->str[i + 1]); 
        }
        if (data & ACTION_MASK & ANP) {
            map->ns[map->count] = &(map->str[i + 1]);
        }
        if (data & ACTION_MASK & ASZ) {
            map->str[i] = '\0';
        }
        if (data & ACTION_MASK & AXM) {
            if (strncmp(map->str + i, XMLNS_PREFIX, strlen(XMLNS_PREFIX))) {
                state = SER;
                break;
            } else {
                i += strlen(XMLNS_PREFIX) - 1;
            }
        }
        if (data & ACTION_MASK & APA) {
            if (++map->count >= MAX_NS) {
                state = SER;
                break;
            };
        }
    }
    if (state == SER) {
        goto ERR_STR;
    } else {
        return map;
    }

ERR_STR:
    free(map->str);
ERR:
    free(map);
    return NULL;
}

void
free_ns_map(nsMapPtr map) {
    if (map) {
        free(map->str);
        free(map);
    }
}

int
register_ns(nsMapPtr map, xmlXPathContextPtr ctx) {
    size_t i;

    if (!map || !ctx) return -1;
    for (i = 0; i < map->count; i++) {
        if (xmlXPathRegisterNs(ctx, (const xmlChar*) map->prefix[i], 
                               (const xmlChar*) map->ns[i])) 
        {
            return -1;
        }
    }
    return 0;
}
