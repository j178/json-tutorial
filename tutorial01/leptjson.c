#include "leptjson.h"
#include <assert.h>  /* assert() */
#include <stdlib.h>  /* NULL, strtod() */
#include <math.h> //HUGEVAL
#include <errno.h> //errno, ERANGE
#include <string.h>

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
 */
static int lept_parse_number(lept_context *c, lept_value *v) {

    //负号->整数->小数->指数
    //^-?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?$
    //takes as many characters as possible to form a valid floating point representation
    //做数字转换都是这种思想, 基本不会出错, 转换尽可能多的字符, 剩下的不动, 原样返回
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
    v->n = strtod(c->json, NULL);
    if (errno == ERANGE && (v->n == HUGE_VAL || v->n == -HUGE_VAL)) return LEPT_PARSE_NUMBER_TOO_BIG; //HUGE_VAL == inf

    c->json = p;
    v->type = LEPT_NUMBER;

    return LEPT_PARSE_OK;
}

static int lept_parse_string(lept_context *c, lept_value *v) {
    //1. 尽可能多的解析字符吗?
    //2. 解析完成的字符如何拷贝?
    //3. 对于转义字符, 读入内存后会以什么样的形式给我呢? 比如说 '\n' 是两个字符还是一个 ord==10 的字符?
    //那么对于C语言中非法的转义, 结果会是怎么样的呢? => 对于非法的转义如 '\x', 编译器会去掉 '\', 只剩下 'x'
    EXPECT(c, '"');
    const char *p = c->json;
    while (*(p - 1) == '\\' || *p != '"') { //退出时 *p == '"'
        if (*p == '\0')
            return LEPT_PARSE_INVALID_VALUE;
        p++;
    }

    unsigned long len = p - c->json + 2; //两个引号
    v->string = (char *) calloc(sizeof(char), len + 1); //最后的\0
    strncpy(v->string, c->json - 1, len);    //遗留问题: 长度刚刚好, 所以结尾应该没有\0, 使用calloc全部初始化为0
    c->json = ++p;
    v->type = LEPT_STRING;
    return LEPT_PARSE_OK;
}

static int lept_parse_value(lept_context *c, lept_value *v) {

    switch (*c->json) {
        case 'n': return lept_parse_literal(c, v, LEPT_NULL);
        case 't': return lept_parse_literal(c, v, LEPT_TRUE);
        case 'f': return lept_parse_literal(c, v, LEPT_FALSE);
        case '"': return lept_parse_string(c, v);
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

char *lept_get_string(const lept_value *v) {

    return v->string;
}