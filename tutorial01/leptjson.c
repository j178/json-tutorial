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

    while (*bool) {
        if (*c->json++ != *bool++) {
            return LEPT_PARSE_INVALID_VALUE;
        }
    }

    if (bool_type == 't') {
        v->type = LEPT_TRUE;
    } else {
        v->type = LEPT_FALSE;
    }
    return 0;
}

static int lept_parse_true(lept_context *c, lept_value *v) {

    return lept_parse_bool(c, v, "true");
}

static int lept_parse_false(lept_context *c, lept_value *v) {

    return lept_parse_bool(c, v, "false");
}

static int lept_is_number(char ch) {

    return ch >= '0' && ch <= '9';
}

static int lept_parse_number(lept_context *c, lept_value *v) {

    const char *start;
    if (*c->json == '+') start = ++c->json;
    else if (*c->json == '-') start = c->json;

    int allow_point = 1;
    double result = 0;
    double factor = 10;
    while (lept_is_number(*c->json) || *c->json == '.') {
        if (*c->json == '.') {
            if (allow_point) {
                allow_point = 0;
                factor = 0.1;
                result += (*c->json - '0') * factor;
            } else {
                return LEPT_PARSE_INVALID_VALUE;
            }

        }

        result = result * factor + (*c->json - '0');
        ++c->json;
    }
    return result;
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
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '+':
        case '-':
            r = lept_parse_number(c, v);
            break;
        case '\0':
            return LEPT_PARSE_EXPECT_VALUE;
        default:
            return LEPT_PARSE_INVALID_VALUE;
    }

    if (r != 0) return r;

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

int lept_get_value(const lept_value *v) {

    return 0;
}
