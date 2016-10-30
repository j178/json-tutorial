#include "leptjson.h"
#include <assert.h>  /* assert() */
#include <stdlib.h>  /* NULL, strtod(), malloc(), realloc(), free()*/
#include <math.h> //HUGEVAL
#include <errno.h> //errno, ERANGE
#include <string.h>

#ifndef LEPT_PARSE_STACK_INIT_SIZE
    #define LEPT_PARSE_STACK_INIT_SIZE 256
#endif

#define EXPECT(c, ch) do { assert(*c->json == (ch)); c->json++; } while(0)
#define ISDIGIT(c) ((c) >='0' && (c) <='9')
#define ISDIGIT1TO9(c) ((c)>='1' && (c) <= '9')
#define ISHEX(c) (ISDIGIT(c) || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))

#define PUTC(c, ch) do{ *(char *) lept_context_push(c, sizeof (char)) = (ch); } while(0)
#define STRING_ERROR(error) do{ c->top=head; return error; } while(0)

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
    switch (v->type) {
        case LEPT_STRING:
            free(v->u.s.s);
            break;
        case LEPT_ARRAY:
            //解析失败则类型为NULL, 就不会进入到这里面来
            //free 每一个元素指向的地址, 但容器本身没有 free
            for (size_t i = 0; i < v->u.a.size; i++)
                lept_free(&v->u.a.e[i]);
            free(v->u.a.e); //每一个 malloc 都要有相应的 free
            break;
        case LEPT_OBJECT:
            for (size_t i = 0; i < v->u.o.size; i++) {
                free(v->u.o.m[i].k);
                lept_free(&v->u.o.m[i].v);
            }
            free(v->u.o.m);
            break;
        default:
            break;
    }
    v->type = LEPT_NULL;
}

static int hex_to_int(char h) {

    //assert(ISHEX(h));

    if (h >= 'a' && h <= 'f')
        return h - 'a' + 10;
    if (h >= 'A' && h <= 'F')
        return h - 'A' + 10;
    return h - '0';
}

static const char *lept_parse_hex4(const char *p, unsigned *u) {

    *u = 0;
    for (int i = 0; i < 4; i++, p++) {
        if (!ISHEX(*p))
            return NULL;
        *u = ((*u) << 4) + hex_to_int(*p); //移位符的优先级太低
    }
    return p;
}

static void lept_encode_utf8(lept_context *c, unsigned u) {

    if (u <= 0x7F) {
        PUTC(c, u);
    } else if (u <= 0x07FF) {
        PUTC(c, 0xC0 | ((u >> 6) & 0xFF));
        PUTC(c, 0x80 | (u & 0x3F));
    } else if (u <= 0xFFFF) {
        PUTC(c, 0xE0 | ((u >> 12) & 0xFF)); /* 0xE0 = 11100000 */
        PUTC(c, 0x80 | ((u >> 6) & 0x3F)); /* 0x80 = 10000000 */
        PUTC(c, 0x80 | (u & 0x3F)); /* 0x3F = 00111111 */
    } else {
        assert(u <= 0x10FFFF); //果然是厉害的程序员, 这一招就比我厉害多了
        PUTC(c, 0xF0 | ((u >> 18) & 0xFF));
        PUTC(c, 0x80 | ((u >> 12) & 0x3F));
        PUTC(c, 0x80 | ((u >> 6) & 0x3F));
        PUTC(c, 0x80 | (u & 0x3F));
    }
}

/* 解析 JSON 字符串，把结果写入 str 和 len */
/* str 指向 c->stack 中的元素，需要在 c->stack  */
static int lept_parse_string_raw(lept_context *c, char **str, size_t *len) {
    //1. 尽可能多的解析字符吗?
    //2. 解析完成的字符如何拷贝?
    //3. 对于转义字符, 读入内存后会以什么样的形式给我呢? 比如说 '\n' 是两个字符还是一个 ord==10 的字符?
    //那么对于C语言中非法的转义, 结果会是怎么样的呢? => 对于非法的转义如 '\x', 编译器会去掉 '\', 只剩下 'x'

    size_t head = c->top;
    unsigned u;
    const char *p = c->json;
    for (;;) {
        char ch = *p++;
        switch (ch) {
            case '\\':
                switch (*p++) {
                    case '"':
                        PUTC(c, '"');
                        break;
                    case '\\':
                        PUTC(c, '\\');
                        break;
                    case '/':
                        PUTC(c, '/');
                        break;
                    case 'b':
                        PUTC(c, '\b');
                        break;
                    case 'f':
                        PUTC(c, '\f');
                        break;
                    case 'n':
                        PUTC(c, '\n');
                        break;
                    case 'r':
                        PUTC(c, '\r');
                        break;
                    case 't':
                        PUTC(c, '\t');
                        break;
                    case 'u':
                        //遇到 \uXXXX, 则解析出四位 hex 字符, 解析成功返回 p 的新位置, u 为解析出的 code point, 失败返回 NULL
                        if (!(p = lept_parse_hex4(p, &u)))
                            STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);

                        //todo 如果处于低代理范围, 则报错
                        //如果解析出的 u 位于高代理范围内, 则继续解析低代理对
                        if (u >= 0xD800 && u <= 0xDBFF) {
                            unsigned u2;
                            if (*p++ != '\\' || *p++ != 'u')
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);

                            if (!(p = lept_parse_hex4(p, &u2)))
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_HEX);

                            if (u2 > 0xDFFF || u2 < 0xDC00)
                                STRING_ERROR(LEPT_PARSE_INVALID_UNICODE_SURROGATE);
                            //将 (H,L) 代理对转换为真实的 code point
                            u = 0x10000 + (u - 0xD800) * 0x400 + (u2 - 0xDC00);
                        }
                        //将 code point 按照 utf8 编码为多个字节, 写入到缓冲区中
                        lept_encode_utf8(c, u);
                        break;
                    default:
                        STRING_ERROR(LEPT_PARSE_INVALID_STRING_ESCAPE);
                }
                break;
            case '"':
                *len = c->top - head;
                *str = lept_context_pop(c, *len);
                c->json = p;
                return LEPT_PARSE_OK;
            case '\0':
                //JSON 中允许字符串中间出现\0, 怎么办? 是以\u0000的形式出现的, 在转义中处理了
                STRING_ERROR(LEPT_PARSE_MISS_QUOTATION_MARK);
            default:
                //处理不合法的非转义字符
                //unescaped = %x20-21 / %x23-5B / %x5D-10FFFF
                //0x22 为双引号, 0x5C 为反斜线都已经处理了
                //char 带不带符号由编译器实现决定, 如果编译器定义 char带符号的话，(usigned char) ch >=0x80 的字符都会变成负数
                //所以这里要强制的转换为无符号的 char
                if ((unsigned char) ch < 0x20)
                    STRING_ERROR(LEPT_PARSE_INVALID_STRING_CHAR);
                PUTC(c, ch);
        }
    }
}

static int lept_parse_string(lept_context *c, lept_value *v) {

    EXPECT(c, '"');
    char *str = NULL;
    size_t len = 0;
    //应该先定义变量分配了内存之后, 取地址传给函数, 而不是声明指针(没有指向实体)
    int ret = lept_parse_string_raw(c, &str, &len);
    if (ret != LEPT_PARSE_OK) return ret;

    lept_set_string(v, str, len);
    return ret;
}

static int lept_parse_value(lept_context *c, lept_value *v);

static int lept_parse_array(lept_context *c, lept_value *v) {

    size_t size = 0;
    int ret;
    EXPECT(c, '[');
    lept_parse_whitespace(c);
    if (*c->json == ']') {
        c->json++;
        v->type = LEPT_ARRAY;
        v->u.a.size = 0;
        v->u.a.e = NULL;
        return LEPT_PARSE_OK;
    }
    for (;;) {
        lept_value e;
        //lept_value *e = lept_context_push(c, sizeof(lept_value)); //一个有bug的写法, 但是还不知道有什么bug
        //这种写法, 如果后面的realloc ... 就会产生指针悬挂
        lept_init(&e);
        lept_parse_whitespace(c);
        if ((ret = lept_parse_value(c, &e)) != LEPT_PARSE_OK) {
            break; //解析出错, 跳出循环
        }
        //在数组完全解析完成之前, 每个元素(而不是不是指针)都临时放在堆栈中
        memcpy(lept_context_push(c, sizeof(lept_value)), &e, sizeof(lept_value));
        size++;
        lept_parse_whitespace(c);
        if (*c->json == ',')
            c->json++;
        else if (*c->json == ']') {
            c->json++;
            v->type = LEPT_ARRAY;
            v->u.a.size = size;
            size *= sizeof(lept_value); //整个 array 的大小
            memcpy(v->u.a.e = (lept_value *) malloc(size), lept_context_pop(c, size), size); //弹出整个数组
            return LEPT_PARSE_OK;
        } else {
            ret = LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET;
            break;
        }
    }
    //c->top = head; //移动栈顶,空出来的位置后面就可以给后面的元素用了, 就相当于完成了pop
    for (size_t i = 0; i < size; i++) {
        lept_free(lept_context_pop(c, sizeof(lept_value)));
    }
    return ret;
}

static int lept_parse_object(lept_context *c, lept_value *v) {

    EXPECT(c, '{');
    lept_parse_whitespace(c);
    if (*c->json == '}') {
        c->json++;
        v->type = LEPT_OBJECT;
        v->u.o.m = 0;
        v->u.o.size = 0;
        return LEPT_PARSE_OK;
    }

    int ret;
    size_t size = 0;
    lept_member m;
    m.k = NULL;
    for (;;) {
        lept_init(&m.v);
        char *str = NULL;

        lept_parse_whitespace(c);
        if (*c->json != '"') {
            ret = LEPT_PARSE_MISS_KEY;
            break;
        }
        c->json++; //跳过"

        ret = lept_parse_string_raw(c, &str, &m.klen);
        if (ret != LEPT_PARSE_OK) {
            if (ret == LEPT_PARSE_MISS_QUOTATION_MARK) {
                ret = LEPT_PARSE_MISS_KEY;
            }
            break;
        }

        m.k = (char *) malloc(m.klen + 1);
        memcpy(m.k, str, m.klen);
        m.k[m.klen] = '\0';

        //解析中间的冒号
        lept_parse_whitespace(c);
        if (*c->json != ':') {
            ret = LEPT_PARSE_MISS_COLON;
            //从这里中途退出, size还没有++, 所以为key分配的内存就无法释放, 需要在这里手动释放
            free(m.k);
            break;
        }
        c->json++; //skip :
        lept_parse_whitespace(c);

        if ((ret = lept_parse_value(c, &m.v)) != LEPT_PARSE_OK) break;

        memcpy(lept_context_push(c, sizeof(lept_member)), &m, sizeof(m)); //解析完一个成员, 暂存到堆栈中
        size++;

        m.k = NULL; //原来为key分配的内存已经有新的指针指向了(memcpy)

        lept_parse_whitespace(c);
        if (*c->json == ',') {
            c->json++;
        } else if (*c->json == '}') {
            c->json++;
            v->type = LEPT_OBJECT;
            v->u.o.size = size;
            size *= sizeof(lept_member);
            memcpy((v->u.o.m = (lept_member *) malloc(size)), lept_context_pop(c, size), size); //退出整个对象
            return LEPT_PARSE_OK;
        } else {
            ret = LEPT_PARSE_MISS_COMMA_OR_CURLY_BRACKET;
            break;
        }
    }

    //解析失败, free 栈中的暂存内容
    for (size_t i = 0; i < size; i++) {
        lept_member *member = (lept_member *) lept_context_pop(c, sizeof(lept_member));
        free(member->k);
        lept_free(&member->v);
    }

    return ret;
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
        case '[':
            return lept_parse_array(c, v);
        case '{':
            return lept_parse_object(c, v);
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

    assert(v != NULL && (v->type == LEPT_TRUE || v->type == LEPT_FALSE));
    return v->type == LEPT_TRUE;
}

void lept_set_boolean(lept_value *v, int bool) {

    assert(v != NULL);
    lept_free(v); //容易内存泄露
    v->type = bool ? LEPT_TRUE : LEPT_FALSE;
}

double lept_get_number(const lept_value *v) {

    assert(v != NULL && v->type == LEPT_NUMBER);
    return v->u.n;
}

void lept_set_number(lept_value *v, double n) {

    assert(v != NULL);
    lept_free(v); //memory leak
    v->u.n = n;
    v->type = LEPT_NUMBER;
}

char *lept_get_string(const lept_value *v) {

    assert(v != NULL && v->type == LEPT_STRING);
    return v->u.s.s;
}

void lept_set_string(lept_value *v, const char *s, size_t len) {

    assert(v != NULL && (s != NULL || len == 0));
    lept_free(v);
    v->u.s.s = (char *) malloc(len + 1);
    // Copies count characters from the object pointed to by src to the object pointed to by dest.
    // Both objects are interpreted as arrays of unsigned char.
    memcpy(v->u.s.s, s, len);
    v->u.s.s[len] = '\0';
    v->u.s.len = len;
    v->type = LEPT_STRING;
}

size_t lept_get_string_length(const lept_value *v) {

    assert(v != NULL && v->type == LEPT_STRING);
    return v->u.s.len;
}

size_t lept_get_array_size(const lept_value *v) {

    assert(v != NULL && v->type == LEPT_ARRAY);
    return v->u.a.size;
}

lept_value *lept_get_array_element(const lept_value *v, size_t index) {

    assert(lept_get_type(v) == LEPT_ARRAY);
    assert(index < v->u.a.size);
    return &v->u.a.e[index];
}

size_t lept_get_object_size(const lept_value *v) {

    assert(v != NULL && v->type == LEPT_OBJECT);
    return v->u.o.size;
}

const char *lept_get_object_key(const lept_value *v, size_t index) {

    assert(v != NULL && v->type == LEPT_OBJECT);
    assert(index < v->u.o.size);
    return v->u.o.m[index].k;
}

size_t lept_get_object_key_length(const lept_value *v, size_t index) {

    assert(v != NULL && v->type == LEPT_OBJECT);
    assert(index < v->u.o.size);
    return v->u.o.m[index].klen;
}

lept_value *lept_get_object_value(const lept_value *v, size_t index) {

    assert(v != NULL && v->type == LEPT_OBJECT);
    assert(index < v->u.o.size);
    return &v->u.o.m[index].v;
}
