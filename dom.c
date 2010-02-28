/*
 * DOM libary for MySQL.
 * A set of MySQL user defined functions (udf) to evaluate XPATH expressions 
 * and modify XML data directly from MySQL using an SQL query
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

#if defined(_WIN32) || defined(_WIN64) || defined(__WIN32__) || defined(WIN32)
# define DLLEXP __declspec(dllexport)
#else
# define DLLEXP
#endif /*_WIN_*/

#ifdef STANDARD
# include <stdlib.h>
# include <stdio.h>
# include <string.h>
#else
# include <my_global.h>
# include <my_sys.h>
# include <m_string.h>
#endif /*STANDARD*/

#include <mysql.h>
#include <libxml/tree.h>
#include <libxml/xmlwriter.h>
#include <libxml/uri.h>
#include <libxml/xmlIO.h>
#include <libxml/c14n.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "ns.h"

#define XML_BUFFER_SIZE 1024
#define DOM_FUNC_MAX_ARGS 8

#define PRINT_ERROR(str) { strcpy(message, str); }

DLLEXP my_bool 
dom_xpath_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
DLLEXP void 
dom_xpath_deinit(UDF_INIT *initid);
DLLEXP char*
dom_xpath(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, 
		char *is_null, char *error);

DLLEXP my_bool 
dom_extract_node_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
DLLEXP void 
dom_extract_node_deinit(UDF_INIT *initid);
DLLEXP char*
dom_extract_node(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, 
		char *is_null, char *error);

DLLEXP my_bool 
dom_delete_node_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
DLLEXP void 
dom_delete_node_deinit(UDF_INIT *initid);
DLLEXP char*
dom_delete_node(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, 
		char *is_null, char *error);

DLLEXP my_bool 
dom_replace_node_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
DLLEXP void 
dom_replace_node_deinit(UDF_INIT *initid);
DLLEXP char*
dom_replace_node(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, 
		char *is_null, char *error);

DLLEXP my_bool 
dom_append_child_init(UDF_INIT *initid, UDF_ARGS *args, char *message);
DLLEXP void 
dom_append_child_deinit(UDF_INIT *initid);
DLLEXP char*
dom_append_child(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, 
		char *is_null, char *error);

/****************************************************************************************/
/* Misc holders for arguments.                                                          */
/****************************************************************************************/
typedef struct {
    xmlChar *str;
    int len;
} strHolder;

typedef strHolder *strHolderPtr;

static strHolderPtr
_create_str_holder(char *str, unsigned long len) {
    strHolderPtr h = (strHolderPtr) malloc(sizeof(strHolder));
    if (!h) {
        return NULL;
    };
    h->len = (int) len;
    h->str = (xmlChar*) str;
    return h;
}

static void
_free_str_holder(strHolderPtr h) {
    free(h);
}

typedef struct {
    xmlChar *str; 
    xmlXPathCompExprPtr expr;
} exprHolder;

typedef exprHolder *exprHolderPtr;

static exprHolderPtr
_compile_expr(char *str, unsigned long len) {
    exprHolderPtr h;
    if (!(h = (exprHolderPtr) malloc(sizeof(exprHolder)))) {
        goto ERR;
    }
    memset(h, '\0', sizeof(exprHolder));
    if (!(h->str = (xmlChar*) malloc(sizeof(xmlChar) * (len + 1)))) {
        goto ERR_FREE_H;
    }
    memcpy(h->str, str, len);
    h->str[len] = '\0';
    if (!(h->expr = xmlXPathCompile(h->str))) {
        goto ERR_FREE_STR;
    }
    return h;

ERR_FREE_STR:
    free(h->str);
ERR_FREE_H:
    free(h);
ERR:
    return NULL;
}

static xmlXPathObjectPtr
_eval_expr(exprHolderPtr expr, xmlXPathContextPtr ctx) {
    return xmlXPathCompiledEval(expr->expr, ctx);
}

static void
_free_expr(exprHolderPtr p) {
    if (p) {
        xmlXPathFreeCompExpr(p->expr);
        free(p->str);
        free(p);
    }
}

/****************************************************************************************/
/* Caching arguments handling                                                           */
/****************************************************************************************/
typedef enum {
    ARG_UNUSED = 0,
    ARG_DOC,
    ARG_EXPR,
    ARG_NS_MAP,
    ARG_STR,
    ARG_OPTIONS
} domArgType;

typedef domArgType domArgsMapping[DOM_FUNC_MAX_ARGS];

typedef void *domArgs[DOM_FUNC_MAX_ARGS];
#define TO_DOC_PTR(dom_args, idx) ((xmlDocPtr) dom_args[idx])
#define TO_EXPR_PTR(dom_args, idx) ((exprHolderPtr) dom_args[idx])
#define TO_NS_MAP_PTR(dom_args, idx) ((nsMapPtr) dom_args[idx])
#define TO_STR_HOLDER_PTR(dom_args, idx) ((strHolderPtr) dom_args[idx])

typedef my_bool (*domFunctionPtr) (xmlBufferPtr out, domArgs args);

typedef struct {
    domArgs static_args;
    xmlBufferPtr out;
    domArgsMapping map;
    domFunctionPtr func;
} queryContext;

typedef queryContext* queryContextPtr;

static queryContextPtr
_create_query_context() {
    queryContextPtr b;

    if (!(b = (queryContextPtr) malloc (sizeof(queryContext)))) {
        goto ERR;
    }
    memset(b, '\0', sizeof(queryContext));
    if (!(b->out = xmlBufferCreateSize(XML_BUFFER_SIZE))) {
        goto ERR_FREE;
    }
    return b;

ERR_FREE:
    free(b);
ERR:
    return NULL;
}

static void
_free_query_context(queryContextPtr b) {
    xmlBufferFree(b->out);
    free(b);
}

/* copy string to internal buffer for row handling time */
static my_bool
_dump_output_str(xmlBufferPtr out, xmlChar *xmlStr) {
    if (xmlBufferAdd(out, xmlStr, -1)) {
        xmlBufferEmpty(out);
        return -1;
    }
    return 0;
}

static my_bool
_dump_output_node_c14n(xmlBufferPtr xml_buf, xmlNodePtr node) {
    xmlOutputBufferPtr out;
    xmlXPathContextPtr xpath_ctx;
    xmlXPathObjectPtr res_obj;
    xmlChar *xpath = (xmlChar*) "(. | .//node() | .//@* | .//namespace::*)";
    
    if (!(out = xmlOutputBufferCreateBuffer(xml_buf, NULL))) {
        goto ERR;
    }
    if (!(xpath_ctx = xmlXPathNewContext(node->doc))) {
        goto ERR_CLOSE_OUT_BUF;
    }
    xpath_ctx->node = node;
    if (!(res_obj = xmlXPathEval(xpath, xpath_ctx))) {
        goto ERR_FREE_CONTEXT;
    }
    if (0 > xmlC14NDocSaveTo(node->doc, res_obj->nodesetval, 0, NULL, 0, out)) {
        goto ERR_FREE_RES_OBJ;
    }
    xmlXPathFreeObject(res_obj);
    xmlXPathFreeContext(xpath_ctx);
    xmlOutputBufferClose(out);
    return 0;

ERR_FREE_RES_OBJ:
    xmlXPathFreeObject(res_obj);
ERR_FREE_CONTEXT:
    xmlXPathFreeContext(xpath_ctx);
ERR_CLOSE_OUT_BUF:
    xmlOutputBufferClose(out);
ERR:
    return -1;
}

static my_bool
_dump_output_doc_c14n(xmlBufferPtr xml_buf, xmlDocPtr doc) {
    return _dump_output_node_c14n(xml_buf, xmlDocGetRootElement(doc));
}

static my_bool
_dump_output_node(xmlBufferPtr xml_buf, xmlNodePtr node) {
    xmlNodePtr clone;

    if (!(clone = xmlCopyNode(node, 1))) {
        goto ERR;
    }
    if (-1 == xmlNodeDump(xml_buf, clone->doc, clone, 0, 0)) {
        goto ERR_FREE_CLONE;
    }
    xmlFreeNode(clone);
    return 0;

ERR_FREE_CLONE:
    xmlFreeNode(clone);    
ERR:
    return -1;
}

static my_bool
_dump_output_doc(xmlBufferPtr xml_buf, xmlDocPtr doc) {
    return _dump_output_node(xml_buf, xmlDocGetRootElement(doc));
}

/****************************************************************************************/
/* Generic functions for handling xpath and document                                    */
/****************************************************************************************/
static my_bool
generic_init(UDF_INIT *initid, UDF_ARGS *args, char *message, 
		domArgsMapping map, domFunctionPtr func) {
    queryContextPtr qctx;
    unsigned long i;

    initid->maybe_null = 1;
    if (args->arg_count > DOM_FUNC_MAX_ARGS) {
        PRINT_ERROR("too much arguments");
        goto ERR;
    }
    for (i = 0; i < args->arg_count; i++) {
        args->arg_type[i] = STRING_RESULT;
    }
    if (!(qctx = _create_query_context())) {
        PRINT_ERROR("can't alloc memory for buffer");
        goto ERR;
    }
    for (i = 0; i < args->arg_count; i++) {
        if (args->args[i]) {
            switch (map[i]) {
                case ARG_DOC:
                    if (!(qctx->static_args[i] = 
				xmlParseMemory(args->args[i], args->lengths[i]))) 
		    {
                        PRINT_ERROR("can't parse xml");
                        goto ERR_FREE_ALL;
                    }
                    break;
                case ARG_EXPR:
                    if (!(qctx->static_args[i] = 
				_compile_expr(args->args[i], args->lengths[i]))) 
		    {
                        PRINT_ERROR("can't compile xpath");
                        goto ERR_FREE_ALL;
                    }
                    break;
                case ARG_NS_MAP:
                    if (!(qctx->static_args[i] = 
				parse_ns_string(args->args[i], args->lengths[i]))) 
		    {
                        PRINT_ERROR("can't parse namespace string");
                        goto ERR_FREE_ALL;
                    }
                    break;
                case ARG_STR:
                    if (!(qctx->static_args[i] = 
				_create_str_holder(args->args[i], args->lengths[i]))) 
		    {
                        PRINT_ERROR("can't allocate memory for str");
                        goto ERR_FREE_ALL;
                    }
                    break;
                default:
                    PRINT_ERROR("unknown type of argument");
                    goto ERR_FREE_ALL;
                    break;
            }
        }
    }
    memcpy(qctx->map, map, sizeof(domArgsMapping));
    qctx->func = func;
    initid->ptr = (char*) qctx;
    return 0;

ERR_FREE_ALL:
    for (i = 0; i < args->arg_count; i++) {
        if (qctx->static_args[i]) {
            switch (map[i]) {
                case ARG_DOC:
                    xmlFreeDoc(TO_DOC_PTR(qctx->static_args, i));
                    break;
                case ARG_EXPR:
                    _free_expr(TO_EXPR_PTR(qctx->static_args, i));    
                    break;
                case ARG_NS_MAP:
                    free_ns_map(TO_NS_MAP_PTR(qctx->static_args, i));
                    break;
                case ARG_STR:
                    _free_str_holder(TO_STR_HOLDER_PTR(qctx->static_args, i));
                    break;
                default:
                    break;
            }
        }
    }
    _free_query_context(qctx);
ERR:
    return -1;
}

static char*
generic_execute(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, 
		char *is_null, char *err) 
{
    queryContextPtr qctx;    
    domArgs dynamic_args;
    char *buffered;
    unsigned long i;

    memset(&dynamic_args, '\0', sizeof(dynamic_args));
    qctx = (queryContextPtr) initid->ptr;
    xmlBufferEmpty(qctx->out);
    /* parse row specific arguments */
    for (i = 0; i < DOM_FUNC_MAX_ARGS; i++) {
        if (qctx->static_args[i]) {
            dynamic_args[i] = qctx->static_args[i];
        } else {
            switch (qctx->map[i]) {
                case ARG_DOC:
                    if (!(dynamic_args[i] = 
				xmlParseMemory(args->args[i], args->lengths[i]))) 
		    {
                        goto ERR_FREE_DYNAMIC;
                    }
                    break;
                case ARG_EXPR:
                    if (!(dynamic_args[i] = 
				_compile_expr(args->args[i], args->lengths[i]))) 
		    {
                        goto ERR_FREE_DYNAMIC;
                    }
                    break;
                case ARG_NS_MAP:
                    if (!(dynamic_args[i] = 
				parse_ns_string(args->args[i], args->lengths[i]))) 
		    {
                        goto ERR_FREE_DYNAMIC;
                    }
                    break;
                case ARG_STR:
                    if (!(dynamic_args[i] = 
				_create_str_holder(args->args[i], args->lengths[i]))) 
		    {
                        goto ERR_FREE_DYNAMIC;
                    }
                    break;
                default:
                    break;
            }
        }
    }
    /* execute dom funtion with parsed arguments */
    if ((*qctx->func)(qctx->out, dynamic_args)) {
        goto ERR_FREE_DYNAMIC;
    }
    /* free row specific arguments */
    for (i = 0; i < args->arg_count; i++) {
        if (dynamic_args[i] && !qctx->static_args[i]) {
            switch (qctx->map[i]) {
                case ARG_DOC:
                    xmlFreeDoc(TO_DOC_PTR(dynamic_args, i));
                    break;
                case ARG_EXPR:
                    _free_expr(TO_EXPR_PTR(dynamic_args, i));    
                    break;
                case ARG_NS_MAP:
                    free_ns_map(TO_NS_MAP_PTR(dynamic_args, i));
                    break;
                case ARG_STR:
                    _free_str_holder(TO_STR_HOLDER_PTR(dynamic_args, i));
                    break;
                default:
                    break;
            }
        }
    }
    buffered = (char*) xmlBufferContent(qctx->out);
    *length = strlen(buffered);
    return buffered;

ERR_FREE_DYNAMIC:
    /* free row specific arguments */
    for (i = 0; i < args->arg_count; i++) {
        if (dynamic_args[i] && !qctx->static_args[i]) {
            switch (qctx->map[i]) {
                case ARG_DOC:
                    xmlFreeDoc(TO_DOC_PTR(dynamic_args, i));
                    break;
                case ARG_EXPR:
                    _free_expr(TO_EXPR_PTR(dynamic_args, i));    
                    break;
                case ARG_NS_MAP:
                    free_ns_map(TO_NS_MAP_PTR(dynamic_args, i));
                    break;
                case ARG_STR:
                    _free_str_holder(TO_STR_HOLDER_PTR(dynamic_args, i));
                    break;
                default:
                    break;
            }
        }
    }
    *is_null = 1;
    return NULL;
}

static void
generic_deinit(UDF_INIT *initid) {
    unsigned long i;
    queryContextPtr qctx = (queryContextPtr) initid->ptr;

    for (i = 0; i < DOM_FUNC_MAX_ARGS; i++) {
        if (qctx->static_args[i]) {
            switch (qctx->map[i]) {
                case ARG_DOC:
                    xmlFreeDoc(TO_DOC_PTR(qctx->static_args, i));
                    break;
                case ARG_EXPR:
                    _free_expr(TO_EXPR_PTR(qctx->static_args, i));    
                    break;
                case ARG_NS_MAP:
                    free_ns_map(TO_NS_MAP_PTR(qctx->static_args, i));
                    break;
                default:
                    break;
            }
        }
    }
    _free_query_context(qctx);
}

static xmlXPathObjectPtr
generic_eval_xpath(xmlDocPtr doc, exprHolderPtr expr, nsMapPtr ns_map) {
    xmlXPathContextPtr ctx;
    xmlXPathObjectPtr res;

    if (!(ctx = xmlXPathNewContext(doc))) {
        goto ERR;
    }
    if (register_ns(ns_map, ctx)) {
        goto ERR_FREE_CTX;
    }
    if (!(res = _eval_expr(expr, ctx))) {
        goto ERR_CLEANUP_NS;
    }
    xmlXPathRegisteredNsCleanup(ctx);
    xmlXPathFreeContext(ctx);
    return res;

ERR_CLEANUP_NS:
    xmlXPathRegisteredNsCleanup(ctx);
ERR_FREE_CTX:
    xmlXPathFreeContext(ctx);
ERR:
    return NULL;
}

/****************************************************************************************/
/* dom_xpath(xml, xpath, ns_string)                                                     */
/****************************************************************************************/
static domArgsMapping dom_xpath_map = {ARG_DOC, ARG_EXPR, ARG_NS_MAP};

static my_bool
do_xpath(xmlBufferPtr out, domArgs args) {
    xmlXPathObjectPtr res;
    xmlChar *res_str;

    if (!(res = generic_eval_xpath(TO_DOC_PTR(args, 0), 
				   TO_EXPR_PTR(args, 1), 
				   TO_NS_MAP_PTR(args, 2)))) 
    {
        goto ERR;
    }
    if (!(res_str = xmlXPathCastToString(res))) {
        goto ERR_FREE_RES;
    }
    if(_dump_output_str(out, res_str)) {
        goto ERR_FREE_RES_STR;
    }
    xmlFree(res_str);
    xmlXPathFreeObject(res);
    return 0;

ERR_FREE_RES_STR:
    xmlFree(res_str);
ERR_FREE_RES:
    xmlXPathFreeObject(res);
ERR:
    return -1;
}

my_bool
dom_xpath_init(UDF_INIT *initid, UDF_ARGS *args, char *message) {
    if (args->arg_count != 3) {
        PRINT_ERROR("dom_xpath() requires 3 arguments");
        return -1;
    }
    return generic_init(initid, args, message, dom_xpath_map, &do_xpath);
}

char*
dom_xpath(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, 
		char *is_null, char *err) 
{
    return generic_execute(initid, args, result, length, is_null, err);
}

void
dom_xpath_deinit(UDF_INIT *initid) {
    generic_deinit(initid);
}

/****************************************************************************************/
/* dom_extract_node(xml, xpath, ns_string)                                              */
/****************************************************************************************/
static domArgsMapping dom_extract_node_map = {ARG_DOC, ARG_EXPR, ARG_NS_MAP};

static my_bool
do_extract_node(xmlBufferPtr out, domArgs args) {
    xmlXPathObjectPtr res;

    if (!(res = generic_eval_xpath(TO_DOC_PTR(args, 0), 
				   TO_EXPR_PTR(args, 1), 
				   TO_NS_MAP_PTR(args, 2)))) 
    {
        goto ERR;
    }
    if (res->type != XPATH_NODESET || !res->nodesetval) {
        goto ERR_FREE_RES;
    }
    if (res->nodesetval->nodeNr != 1) {
        goto ERR_FREE_RES;
    }
    if (_dump_output_node(out, res->nodesetval->nodeTab[0])) {
        goto ERR_FREE_RES;
    }
    xmlXPathFreeObject(res);
    return 0;

ERR_FREE_RES:
    xmlXPathFreeObject(res);
ERR:
    return -1;
}

my_bool
dom_extract_node_init(UDF_INIT *initid, UDF_ARGS *args, char *message) {
    if (args->arg_count != 3) {
        PRINT_ERROR("dom_extract_node() requires 3 arguments");
        return -1;
    }
    return generic_init(initid, args, message, dom_extract_node_map, &do_extract_node);
}

void 
dom_extract_node_deinit(UDF_INIT *initid) {
    generic_deinit(initid);
}

char*
dom_extract_node(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, 
		char *is_null, char *err) 
{
    return generic_execute(initid, args, result, length, is_null, err);
}

/****************************************************************************************/
/* dom_delete_node(xml, xpath, ns_string)                                               */
/****************************************************************************************/
static domArgsMapping dom_delete_node_map = {ARG_DOC, ARG_EXPR, ARG_NS_MAP};

static my_bool
do_delete_node(xmlBufferPtr out, domArgs args) {
    xmlXPathObjectPtr res;
    int i;

    if (!(res = generic_eval_xpath(TO_DOC_PTR(args, 0), 
				   TO_EXPR_PTR(args, 1), 
				   TO_NS_MAP_PTR(args, 2)))) 
    {
        goto ERR;
    }
    if (res->type != XPATH_NODESET || !res->nodesetval) {
        goto ERR_FREE_RES;
    }
    for (i = 0; i < res->nodesetval->nodeNr; i++) {
        xmlUnlinkNode(res->nodesetval->nodeTab[i]);
        xmlFreeNode(res->nodesetval->nodeTab[i]);
    }
    if (_dump_output_doc(out, TO_DOC_PTR(args, 0))) {
        goto ERR_FREE_RES;
    }
    xmlXPathFreeObject(res);
    return 0;

ERR_FREE_RES:
    xmlXPathFreeObject(res);
ERR:
    return -1;
}

my_bool
dom_delete_node_init(UDF_INIT *initid, UDF_ARGS *args, char *message) {
    if (args->arg_count != 3) {
        PRINT_ERROR("dom_delete_node() requires 3 arguments");
        return -1;
    }
    return generic_init(initid, args, message, dom_delete_node_map, &do_delete_node);
}

void
dom_delete_node_deinit(UDF_INIT *initid) {
    generic_deinit(initid);
}

char*
dom_delete_node(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, 
		char *is_null, char *err) 
{
    return generic_execute(initid, args, result, length, is_null, err);
}

/****************************************************************************************/
/* dom_replace_node(xml, xpath, ns_string, value)                                       */
/****************************************************************************************/
static domArgsMapping dom_replace_node_map = {ARG_DOC, ARG_EXPR, ARG_NS_MAP, ARG_DOC};

static my_bool
do_replace_node(xmlBufferPtr out, domArgs args) {
    xmlXPathObjectPtr res;
    xmlNodePtr n_node, o_node;
    int i;

    if (!(res = generic_eval_xpath(TO_DOC_PTR(args, 0), 
				   TO_EXPR_PTR(args, 1), 
				   TO_NS_MAP_PTR(args, 2)))) 
    {
        goto ERR;
    }
    if (res->type != XPATH_NODESET || !res->nodesetval) {
        goto ERR_FREE_RES;
    }
    for (i = 0; i < res->nodesetval->nodeNr; i++) {
        o_node = res->nodesetval->nodeTab[i];
        if (o_node->type != XML_ELEMENT_NODE &&
            o_node->type != XML_TEXT_NODE &&
            o_node->type != XML_CDATA_SECTION_NODE)
        {
            goto ERR_FREE_RES;
        }
        if (!(n_node = xmlCopyNode(xmlDocGetRootElement(TO_DOC_PTR(args, 3)), 1))) {
            goto ERR_FREE_RES;
        }
        xmlUnlinkNode(n_node);
        xmlSetTreeDoc(n_node, TO_DOC_PTR(args, 0));
        xmlReplaceNode(o_node, n_node);
        xmlFreeNode(o_node);
    }
    if (_dump_output_doc(out, TO_DOC_PTR(args, 0))) {
        goto ERR_FREE_RES;
    }
    xmlXPathFreeObject(res);
    return 0;

ERR_FREE_RES:
    xmlXPathFreeObject(res);
ERR:
    return -1;
}

static domArgsMapping dom_replace_node_value_map = 
			{ARG_DOC, ARG_EXPR, ARG_NS_MAP, ARG_STR};

static my_bool
do_replace_node_value(xmlBufferPtr out, domArgs args) {
    xmlXPathObjectPtr res;
    xmlNodePtr n_node, o_node;
    strHolderPtr str;
    int i;

    if (!(res = generic_eval_xpath(TO_DOC_PTR(args, 0), 
				   TO_EXPR_PTR(args, 1), 
				   TO_NS_MAP_PTR(args, 2)))) 
    {
        goto ERR;
    }
    if (res->type != XPATH_NODESET || !res->nodesetval) {
        goto ERR_FREE_RES;
    }
    for (i = 0; i < res->nodesetval->nodeNr; i++) {
        str = TO_STR_HOLDER_PTR(args, 3);
        o_node = res->nodesetval->nodeTab[i];
        if (o_node->type != XML_ELEMENT_NODE &&
            o_node->type != XML_TEXT_NODE &&
            o_node->type != XML_CDATA_SECTION_NODE)
        {
            goto ERR_FREE_RES;
        }
        if (!(n_node = xmlNewTextLen(str->str, str->len))) {
            goto ERR_FREE_RES;
        }
        xmlSetTreeDoc(n_node, TO_DOC_PTR(args, 0));
        xmlReplaceNode(o_node, n_node);
        xmlFreeNode(o_node);
    }
    if (_dump_output_doc(out, TO_DOC_PTR(args, 0))) {
        goto ERR_FREE_RES;
    }
    xmlXPathFreeObject(res);
    return 0;

ERR_FREE_RES:
    xmlXPathFreeObject(res);
ERR:
    return -1;
}

my_bool
dom_replace_node_init(UDF_INIT *initid, UDF_ARGS *args, char *message) {
    if (args->arg_count != 4) {
        PRINT_ERROR("dom_replace_node() requires 4 arguments");
        return -1;
    }
    if (!strncmp("XML", args->attributes[3], args->attribute_lengths[3])) {
	return generic_init(initid, args, message, 
			dom_replace_node_map, &do_replace_node);
    } else {
        return generic_init(initid, args, message, 
			dom_replace_node_value_map, &do_replace_node_value);
    }
}

void
dom_replace_node_deinit(UDF_INIT *initid) {
    generic_deinit(initid);
}

char*
dom_replace_node(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length, 
		char *is_null, char *err) 
{
    return generic_execute(initid, args, result, length, is_null, err);
}

/***************************************************************************************/
/* dom_append_child(xml, xpath, ns_string, value)                                      */
/***************************************************************************************/
static domArgsMapping dom_append_child_map = {ARG_DOC, ARG_EXPR, ARG_NS_MAP, ARG_DOC};

static my_bool
do_append_child(xmlBufferPtr out, domArgs args) {
    xmlXPathObjectPtr res;
    xmlNodePtr n_node, o_node;
    int i;

    if (!(res = generic_eval_xpath(TO_DOC_PTR(args, 0), 
				   TO_EXPR_PTR(args, 1), 
				   TO_NS_MAP_PTR(args, 2)))) 
    {
        goto ERR;
    }
    if (res->type != XPATH_NODESET || !res->nodesetval) {
        goto ERR_FREE_RES;
    }
    for (i = 0; i < res->nodesetval->nodeNr; i++) {
        o_node = res->nodesetval->nodeTab[i];
        if (o_node->type != XML_ELEMENT_NODE &&
            o_node->type != XML_TEXT_NODE &&
            o_node->type != XML_CDATA_SECTION_NODE)
        {
            goto ERR_FREE_RES;
        }
        if (!(n_node = xmlCopyNode(xmlDocGetRootElement(TO_DOC_PTR(args, 3)), 1))) {
            goto ERR_FREE_RES;
        }
        xmlUnlinkNode(n_node);
        xmlSetTreeDoc(n_node, TO_DOC_PTR(args, 0));
        xmlAddChild(o_node, n_node);
    }
    if (_dump_output_doc(out, TO_DOC_PTR(args, 0))) {
        goto ERR_FREE_RES;
    }
    xmlXPathFreeObject(res);
    return 0;

ERR_FREE_RES:
    xmlXPathFreeObject(res);
ERR:
    return -1;
}

static domArgsMapping dom_append_child_value_map = 
		{ARG_DOC, ARG_EXPR, ARG_NS_MAP, ARG_STR};

static my_bool
do_append_child_value(xmlBufferPtr out, domArgs args) {
    xmlXPathObjectPtr res;
    xmlNodePtr n_node, o_node;
    strHolderPtr str;
    int i;

    if (!(res = generic_eval_xpath(TO_DOC_PTR(args, 0), 
				   TO_EXPR_PTR(args, 1), 
				   TO_NS_MAP_PTR(args, 2)))) 
    {
        goto ERR;
    }
    if (res->type != XPATH_NODESET || !res->nodesetval) {
        goto ERR_FREE_RES;
    }
    for (i = 0; i < res->nodesetval->nodeNr; i++) {
        str = TO_STR_HOLDER_PTR(args, 3);
        o_node = res->nodesetval->nodeTab[i];
        if (o_node->type != XML_ELEMENT_NODE &&
            o_node->type != XML_TEXT_NODE &&
            o_node->type != XML_CDATA_SECTION_NODE)
        {
            goto ERR_FREE_RES;
        }
        if (!(n_node = xmlNewTextLen(str->str, str->len))) {
            goto ERR_FREE_RES;
        }
        xmlSetTreeDoc(n_node, TO_DOC_PTR(args, 0));
        xmlAddChild(o_node, n_node);
    }
    if (_dump_output_doc(out, TO_DOC_PTR(args, 0))) {
        goto ERR_FREE_RES;
    }
    xmlXPathFreeObject(res);
    return 0;

ERR_FREE_RES:
    xmlXPathFreeObject(res);
ERR:
    return -1;
}

my_bool
dom_append_child_init(UDF_INIT *initid, UDF_ARGS *args, char *message) {
    if (args->arg_count != 4) {
        PRINT_ERROR("dom_append_child() requires 4 arguments");
        return -1;
    }
    if (!strncmp("XML", args->attributes[3], args->attribute_lengths[3])) {
    	return generic_init(initid, args, message, 
			dom_append_child_map, &do_append_child);
    } else {
    	return generic_init(initid, args, message, 
			dom_append_child_value_map, &do_append_child_value);
    }
}

void 
dom_append_child_deinit(UDF_INIT *initid) {
    generic_deinit(initid);
}

char*
dom_append_child(UDF_INIT *initid, UDF_ARGS *args, char *result, unsigned long *length,
	       	char *is_null, char *err) 
{
    return generic_execute(initid, args, result, length, is_null, err);
}
