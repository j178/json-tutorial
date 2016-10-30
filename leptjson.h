#ifndef LEPTJSON_H__
#define LEPTJSON_H__

#include <stddef.h> //size_t

typedef enum { LEPT_NULL, LEPT_FALSE, LEPT_TRUE, LEPT_NUMBER, LEPT_STRING, LEPT_ARRAY, LEPT_OBJECT } lept_type;

typedef struct lept_value lept_value; //后面直接使用 lept_value 声明变量的地方就等于使用了 struct lept_value
typedef struct lept_member lept_member;

struct lept_value { // 放在 struct 关键字后面的是结构体类型的名字，放在后面的是这个结构体类型的一个变量
    lept_type type;
    union {
        struct { lept_member *m; size_t size; } o;
        struct { char *s; size_t len; } s;
        double n;
        struct { lept_value *e; size_t size; } a; //数组用什么实现, 动态数组还是链表? 还要实现访问, 添加, 插入的函数
    } u;
};

struct lept_member {
    char *k; //memebr key string
    size_t ken; //member key string length
    lept_value *v; //member key value
};

enum {
    LEPT_PARSE_OK = 0,
    LEPT_PARSE_EXPECT_VALUE,
    LEPT_PARSE_INVALID_VALUE,
    LEPT_PARSE_ROOT_NOT_SINGULAR,
    LEPT_PARSE_NUMBER_TOO_BIG,
    LEPT_PARSE_MISS_QUOTATION_MARK,
    LEPT_PARSE_INVALID_STRING_ESCAPE,
    LEPT_PARSE_INVALID_STRING_CHAR,
    LEPT_PARSE_INVALID_UNICODE_HEX,
    LEPT_PARSE_INVALID_UNICODE_SURROGATE,
    LEPT_PARSE_MISS_COMMA_OR_SQUARE_BRACKET
};

#define lept_init(v) do{ (v)->type = LEPT_NULL; } while(0)


int lept_parse(lept_value *v, const char *json);

void lept_free(lept_value *v);

lept_type lept_get_type(const lept_value *v);

#define lept_set_null(v) lept_free(v)

int lept_get_boolean(const lept_value *v);
void lept_set_boolean(lept_value *v, int bool);

double lept_get_number(const lept_value *v);
void lept_set_number(lept_value *v, double n);

char *lept_get_string(const lept_value *v);
size_t lept_get_string_length(const lept_value *v);
void lept_set_string(lept_value *v, const char *s, size_t len);

size_t lept_get_array_size(const lept_value *v);

lept_value *lept_get_array_element(const lept_value *v, size_t index);


size_t lept_get_object_size(const lept_value *v);
const char *lept_get_object_key(const lept_value *v, size_t index);
size_t lept_get_object_key_length(const lept_value *v, size_t index);
lept_value *lept_get_object_value(const lept_value *v, size_t index);
#endif /* LEPTJSON_H__ */