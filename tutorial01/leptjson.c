#include "leptjson.h"
#include <assert.h>  /* assert() */
#include <stdlib.h>  /* NULL, strtod() */
#include <math.h> //HUGEVAL
#include <errno.h> //errno, ERANGE

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

static int lept_parse_literal(lept_context *c, lept_value *v, lept_type type) {

    assert(type == LEPT_TRUE || type == LEPT_FALSE || type == LEPT_NULL);

    const char *str;
    if (type == LEPT_TRUE) {
        str = "true";
    } else if (type == LEPT_FALSE) {
        str = "false";
    } else {
        str = "null";
    }

    EXPECT(c, *str++);

    while (*str) {
        if (*c->json++ != *str++) {
            return LEPT_PARSE_INVALID_VALUE;
        }
    }

    v->type = type;
    return 0;
}

#define ISDIGIT(c) ((c) >='0' && (c) <='9')
#define ISDIGIT1TO9(c) ((c)>='1' && (c) <= '9')

/**
 * number = [ "-" ] int [ frac ] [ exp ]
 * int = "0" / digit1-9 *digit
 * frac = "." 1*digit
 * exp = ("e" / "E") ["-" / "+"] 1*digit
 * @param c
 * @param v
 * @return
 */
static int lept_parse_number(lept_context *c, lept_value *v) {

    //负号->整数->小数->指数
    //^-?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?$
    const char *p = c->json;
    if (*p == '-') p++;
    if (*p == '0') p++;
    else {
        if (!ISDIGIT1TO9(*p)) return LEPT_PARSE_INVALID_VALUE;
        for (p++; ISDIGIT(*p); p++);
    }
    if (*p == '.') {
        p++;
        if (!ISDIGIT(*p)) return LEPT_PARSE_INVALID_VALUE;
        for (p++; ISDIGIT(*p); p++);
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (!ISDIGIT(*p)) return LEPT_PARSE_INVALID_VALUE;
        for (p++; ISDIGIT(*p); p++);
    }

    errno = 0;
    double n = strtod(c->json, NULL);
    if (errno == ERANGE && (n == HUGE_VAL || n == -HUGE_VAL)) return LEPT_PARSE_NUMBER_TOO_BIG; //HUGE_VAL == inf

    c->json = p;
    v->n = n;
    v->type = LEPT_NUMBER;

    return LEPT_PARSE_OK;
}

static int lept_parse_value(lept_context *c, lept_value *v) {

    switch (*c->json) {
        case 'n': return lept_parse_literal(c, v, LEPT_NULL);
        case 't': return lept_parse_literal(c, v, LEPT_TRUE);
        case 'f': return lept_parse_literal(c, v, LEPT_FALSE);
        case '\0': return LEPT_PARSE_EXPECT_VALUE;
        default: return lept_parse_number(c, v);
    }
}

int lept_parse(lept_value *v, const char *json) {

    lept_context c;
    assert(v != NULL);
    c.json = json;
    v->type = LEPT_NULL;
    lept_parse_whitespace(&c);
    int ret = lept_parse_value(&c, v);

    if (ret == LEPT_PARSE_OK) {
        lept_parse_whitespace(&c);
        if (*c.json != '\0') {
            v->type = LEPT_NULL;
            ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
        }
    }

    return ret;
}

lept_type lept_get_type(const lept_value *v) {

    assert(v != NULL);
    return v->type;
}

double lept_get_number(const lept_value *v) {

    assert(v != NULL && v->type == LEPT_NUMBER);
    return v->n;
}