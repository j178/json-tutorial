#include "leptjson.h"
#include <assert.h>  /* assert() */
#include <stdlib.h>  /* NULL, strtod(), malloc(), realloc(), free()*/
#include <math.h> //HUGEVAL
#include <errno.h> //errno, ERANGE
#include <string.h>
#include <stdio.h>

#ifndef LEPT_PARSE_STACK_INIT_SIZE
    #define LEPT_PARSE_STACK_INIT_SIZE 256
#endif

#define EXPECT(c, ch) do { assert(*c->json == (ch)); c->json++; } while(0)

typedef struct {
    const char *json;
    char *stack;
    size_t size; //当前已分配的栈大小
    size_t top; //当前栈顶位置
} lept_context;

static void *lept_context_push(lept_context *c, size_t size) {

    assert(size > 0);
    if (c->top + size >= c->size) {
        if (c->size == 0) {
            c->size = LEPT_PARSE_STACK_INIT_SIZE;
        }
        while (c->top + size >= c->size) {
            c->size += c->size >> 1; //每次扩大1.5倍
        }
        c->stack = realloc(c->stack, c->size);
    }

    void *ret = c->stack + c->top; //可用的空间从这里开始
    c->top += size;
    return ret; //如果使用的程序不听话, 写入了后面的内存呢?
}

static void *lept_context_pop(lept_context *c, size_t size) {

    assert(c->top >= size); //保证已有空间大于要pop的size
    c->top -= size;
    return c->stack + c->top;
}

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
    v->u.n = strtod(c->json, NULL);
    if (errno == ERANGE && (v->u.n == HUGE_VAL || v->u.n == -HUGE_VAL))
        return LEPT_PARSE_NUMBER_TOO_BIG; //HUGE_VAL == inf

    c->json = p;
    v->type = LEPT_NUMBER;

    return LEPT_PARSE_OK;
}

void lept_free(lept_value *v) {

    assert(v != NULL);
    if (lept_get_type(v) == LEPT_STRING) {
        free(v->u.s.s);
    }
    v->type = LEPT_NULL;
}

#define PUTC(c, ch) do{ *(char *) lept_context_push(c, sizeof (char)) = (ch); } while(0)

static char unescape(const char **p) {

    char ch = **p;
    (*p)++;
    switch (ch) {
        case '"':
            return '"';
        case '\\':
            return '\\';
        case '/':
            return '/';
        case 'b':
            return '\b';
        case 'f':
            return '\f';
        case 'n':
            return '\n';
        case 'r':
            return '\r';
        case 't':
            return '\t';
        case 'u':
            (*p) += 4;
        default:
            return 0;
    }
}

#define IS_VALID_CHAR(c) ((c) == '\x20' || (c) == '\x21' || ((c) >= '\x23' && (c) <= '\x5B') \
    || ((c) >= '\x5D' && (c) <= '\x10\xFF\xFF'))

//static int is_valid_char(char c) {
//    //C语言中的非ASCII字符是怎么表示的? 通用的表示方法是什么样的?
//
//    //int bool1 = (c) >= '\x23' && (c) <= '\x5B';
//    //int bool2 = (c) >= '\x5D';
//    //int bool3 = (c) <= '\x10\xFF\xFF'; //一个字符分成三个字节表示
//
//    return (c) == '\x20' || (c) == '\x21' || ((c) >= '\x23' && (c) <= '\x5B')
// || ((c) >= '\x5D' && (c) <= '\x10\xFF\xFF');
//}

static int lept_parse_string(lept_context *c, lept_value *v) {
    //1. 尽可能多的解析字符吗?
    //2. 解析完成的字符如何拷贝?
    //3. 对于转义字符, 读入内存后会以什么样的形式给我呢? 比如说 '\n' 是两个字符还是一个 ord==10 的字符?
    //那么对于C语言中非法的转义, 结果会是怎么样的呢? => 对于非法的转义如 '\x', 编译器会去掉 '\', 只剩下 'x'
    EXPECT(c, '"');
    size_t head = c->top;
    size_t len;
    const char *p = c->json;
    for (;;) {
        char ch = *p++;
        switch (ch) {
            case '\\':
                if (!(ch = unescape(&p))) {
                    c->top = head;
                    return LEPT_PARSE_INVALID_STRING_ESCAPE;
                }
                PUTC(c, ch);
                break;
            case '"':
                len = c->top - head;
                lept_set_string(v, lept_context_pop(c, len), len);
                c->json = p;
                return LEPT_PARSE_OK;
            case '\0':
                //JSON 中允许字符串中间出现\0, 怎么办?
                c->top = head;
                return LEPT_PARSE_MISS_QUOTATION_MARK;
            default:
                if (!IS_VALID_CHAR(ch))
                    return LEPT_PARSE_INVALID_STRING_CHAR;
                PUTC(c, ch);
        }
    }
}

static int lept_parse_value(lept_context *c, lept_value *v) {

    switch (*c->json) {
        case 'n':
            return lept_parse_literal(c, v, LEPT_NULL);
        case 't':
            return lept_parse_literal(c, v, LEPT_TRUE);
        case 'f':
            return lept_parse_literal(c, v, LEPT_FALSE);
        case '"':
            return lept_parse_string(c, v);
        case '\0':
            return LEPT_PARSE_EXPECT_VALUE;
        default:
            return lept_parse_number(c, v);
    }
}

int lept_parse(lept_value *v, const char *json) {

    lept_context c;
    assert(v != NULL);
    c.json = json;
    c.stack = NULL;
    c.size = c.top = 0;

    lept_init(v);
    lept_parse_whitespace(&c);
    int ret = lept_parse_value(&c, v);

    if (ret == LEPT_PARSE_OK) {
        lept_parse_whitespace(&c);
        if (*c.json != '\0') {
            v->type = LEPT_NULL;
            ret = LEPT_PARSE_ROOT_NOT_SINGULAR;
        }
    }

    assert(c.top == 0);
    free(c.stack);

    return ret;
}

lept_type lept_get_type(const lept_value *v) {

    assert(v != NULL);
    return v->type;
}

int lept_get_boolean(const lept_value *v) {

    assert(v != NULL && (lept_get_type(v) == LEPT_TRUE || lept_get_type(v) == LEPT_FALSE));
    return v->type == LEPT_TRUE;
}

void lept_set_boolean(lept_value *v, int bool) {

    assert(v != NULL);
    if (bool)
        v->type = LEPT_TRUE;
    else
        v->type = LEPT_FALSE;
}

double lept_get_number(const lept_value *v) {

    assert(v != NULL && lept_get_type(v) == LEPT_NUMBER);
    return v->u.n;
}

void lept_set_number(lept_value *v, double n) {

    assert(v != NULL);
    v->u.n = n;
    v->type = LEPT_NUMBER;
}

char *lept_get_string(const lept_value *v) {

    assert(v != NULL && lept_get_type(v) == LEPT_STRING);
    return v->u.s.s;
}

void lept_set_string(lept_value *v, const char *s, size_t len) {

    assert(v != NULL && (s != NULL || len == 0));
    lept_free(v);
    v->u.s.s = (char *) malloc(len + 1);
    memcpy(v->u.s.s, s, len);
    v->u.s.s[len] = '\0';
    v->u.s.len = len;
    v->type = LEPT_STRING;
}

size_t lept_get_string_length(const lept_value *v) {

    assert(v != NULL && lept_get_type(v) == LEPT_STRING);
    return v->u.s.len;
}