#include "leptjson.h"
#include <assert.h>  /* assert() */
#include <stdlib.h>  /* NULL */
#include <math.h>
#include <regex.h>
#include <stdio.h>

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

//看看人家这命名!! literal !! 你想半天还只能弄出个fixed_string...
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

static int lept_parse_null(lept_context *c, lept_value *v) {

    return lept_parse_literal(c, v, LEPT_NULL);
}

static int lept_parse_true(lept_context *c, lept_value *v) {

    return lept_parse_literal(c, v, LEPT_TRUE);
}

static int lept_parse_false(lept_context *c, lept_value *v) {

    return lept_parse_literal(c, v, LEPT_FALSE);
}

static int lept_parse_number(lept_context *c, lept_value *v) {

    char *end;
    //todo validate number
    //1. 只允许十进制
    //2. 不允许无穷和NaN
    //4. 不允许+号开头
    //5. 不允许前导0 (以0开头要么是单个0, 要么后面是小数点)

    regex_t regex;
    //fixme -0.0 1E10 等都无法匹配
    const char *pattern = "^-?(0|[1-9]\\d*)(\\.\\d+)?([eE][+-]?\\d+)?$";
    assert(!regcomp(&regex, pattern, REG_EXTENDED));

    int status = regexec(&regex, c->json, 0, NULL, 0);
    regfree(&regex);

    char msg[100];
    if (status == REG_NOMATCH) {
        regerror(status, &regex, msg, sizeof(msg));
        fprintf(stderr, "Regex match failed: %s with %s\n", msg, c->json);
        return LEPT_PARSE_INVALID_VALUE;
    }


    double n = strtod(c->json, &end);
    if (n == HUGE_VAL) return LEPT_PARSE_NUMBER_TOO_BIG; //HUGE_VAL == inf
    if (c->json == end) return LEPT_PARSE_INVALID_VALUE; //转换错误, 返回0, end没有前进, 还是指向字符串开始

    c->json = end;
    v->n = n;
    v->type = LEPT_NUMBER;

    return LEPT_PARSE_OK;
}

static int lept_parse_value(lept_context *c, lept_value *v) {

    switch (*c->json) {
        case 'n': return lept_parse_null(c, v);
        case 't': return lept_parse_true(c, v);
        case 'f': return lept_parse_false(c, v);
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