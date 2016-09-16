#include "leptjson.h"
#include <assert.h>  /* assert() */
#include <stdlib.h>  /* NULL */

#define EXPECT(c, ch)       do { assert(*c->json == (ch)); c->json++; } while(0)

typedef struct {
    const char *json;
} lept_context;

//static的全局变量, 表示只有文件内部链接, 无法在其他文件引用, 相当于是这个文件的私有变量
static void lept_parse_whitespace(lept_context *c) {

    const char *p = c->json;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    c->json = p;
}

static int lept_parse_null(lept_context *c, lept_value *v) {

    EXPECT(c, 'n');
    if (c->json[0] != 'u' || c->json[1] != 'l' || c->json[2] != 'l') {
        return LEPT_PARSE_INVALID_VALUE;
    }
    c->json += 3;
    v->type = LEPT_NULL;
    return 0;
}

static int lept_parse_bool(lept_context *c, lept_value *v, const char *bool) {

    char bool_type = bool[0];
    EXPECT(c, *bool++);
    const char *p = c->json;

    while (*bool) {
        if (*p++ != *bool++) {
            return LEPT_PARSE_INVALID_VALUE;
        }
    }
    c->json = p;

    if (bool_type == 't') {
        v->type = LEPT_TRUE;
    } else {
        v->type = LEPT_FALSE;
    }
    return 0;
}

static int lept_parse_true(lept_context *c, lept_value *v) {
    //内部改变一下函数的实现
    return lept_parse_bool(c, v, "true");
}

static int lept_parse_false(lept_context *c, lept_value *v) {

    EXPECT(c, 'f');
    if (c->json[0] != 'a' || c->json[1] != 'l' || c->json[2] != 's' || c->json[3] != 'e') {
        return LEPT_PARSE_INVALID_VALUE;
    }
    c->json += 4;
    v->type = LEPT_FALSE;
    return 0;
}

static int lept_parse_value(lept_context *c, lept_value *v) {

    int r = 0;
    switch (*c->json) {
        case 'n':
            r = lept_parse_null(c, v);
            break;
        case 't':
            r = lept_parse_true(c, v);
            break;
        case 'f':
            r = lept_parse_false(c, v);
            break;
        case '\0':
            return LEPT_PARSE_EXPECT_VALUE;
        default:
            return LEPT_PARSE_INVALID_VALUE;
    }

    if (r != 0) {
        return r;
    }

    lept_parse_whitespace(c);
    if (*c->json != '\0') {
        v->type = LEPT_NULL;
        return LEPT_PARSE_ROOT_NOT_SINGULAR;
    }
    return r;
}

int lept_parse(lept_value *v, const char *json) {

    lept_context c;
    assert(v != NULL);
    c.json = json;
    v->type = LEPT_NULL;
    lept_parse_whitespace(&c);
    return lept_parse_value(&c, v);
}

lept_type lept_get_type(const lept_value *v) {

    assert(v != NULL);
    return v->type;
}
