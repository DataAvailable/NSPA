// poc_vim_gui_photon_realloc_fail_pgdrawtext_null.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#ifndef MB_LEN_MAX
#define MB_LEN_MAX 16
#endif

#define DRAW_TRANSP 0x01
#define DRAW_UNDERL 0x02
#define DRAW_BOLD   0x04

typedef unsigned char char_u;

typedef struct {
    int x;
    int y;
} PhPoint_t;

typedef struct {
    struct { int x, y; } ul;
    struct { int x, y; } lr;
} PhRect_t;

/*
 * 模拟 Vim 中的 static UTF-8 buffer。
 */
static char *utf8_buffer = NULL;
static int utf8_len = 0;

/*
 * 模拟进入受影响路径所需条件：
 *
 *     charset_translate != NULL && enc_utf8 == 0
 */
static int enc_utf8 = 0;
static void *charset_translate = (void *)0x41414141;

/*
 * 强制 realloc() 失败。
 */
static void *
fail_realloc(void *ptr, size_t size)
{
    (void)ptr;
    fprintf(stderr, "[PoC] forced realloc failure, size=%zu\n", size);
    return NULL;
}

/*
 * 模拟 QNX PxTranslateToUTF() 的关键语义：
 *
 * 当 dst == NULL 时，不写入 dst，而是计算 dst_made。
 * 这点更符合 QNX 文档，也更接近真实情况。
 */
static int
PxTranslateToUTF(void *ctrl,
                 const char *src,
                 int maxsrc,
                 int *srctaken,
                 char *dst,
                 int maxdst,
                 int *dstmade)
{
    (void)ctrl;
    (void)maxdst;

    fprintf(stderr,
            "[PoC] PxTranslateToUTF(src=%p, len=%d, dst=%p)\n",
            (const void *)src, maxsrc, (void *)dst);

    if (srctaken != NULL)
        *srctaken = maxsrc;

    /*
     * 模拟转换后产生了输出。
     * 对 ASCII 输入，输出长度可以近似等于输入长度。
     */
    if (dstmade != NULL)
        *dstmade = maxsrc;

    /*
     * 如果 dst 非 NULL，正常写入。
     * 如果 dst == NULL，不写入，直接返回。
     */
    if (dst != NULL && src != NULL && maxsrc > 0)
        memcpy(dst, src, (size_t)maxsrc);

    return 0;
}

/*
 * 模拟 Photon PgDrawText()。
 * 真实库函数通常期望 text 指向有效缓冲区。
 */
static void
PgDrawText(const char *text, int len, PhPoint_t *pos, int flags)
{
    (void)pos;
    (void)flags;

    fprintf(stderr,
            "[PoC] PgDrawText(text=%p, len=%d)\n",
            (const void *)text, len);

    /*
     * 这里模拟 GUI 绘制函数读取 text。
     * 如果 text == NULL 且 len > 0，会触发 NULL pointer dereference。
     */
    if (len > 0) {
        volatile char ch = text[0];
        (void)ch;
    }
}

/*
 * 复现 Vim gui_mch_draw_string() 的关键路径。
 */
static void
vulnerable_gui_mch_draw_string(int row, int col, char_u *s, int len, int flags)
{
    PhPoint_t pos = { col, row };
    PhRect_t rect;
    int src_taken;
    int dst_made;

    (void)rect;
    (void)flags;

    fprintf(stderr,
            "[PoC] before: utf8_buffer=%p, utf8_len=%d, input_len=%d\n",
            (void *)utf8_buffer, utf8_len, len);

    if (charset_translate != NULL && enc_utf8 == 0) {
        if (utf8_len < len) {
            /*
             * Vim 漏洞点：
             * realloc() 返回值未检查，直接覆盖 utf8_buffer。
             */
            utf8_buffer = fail_realloc(utf8_buffer, (size_t)len * MB_LEN_MAX);

            /*
             * 即使 realloc() 失败，utf8_len 仍然被更新。
             */
            utf8_len = len;
        }

        /*
         * QNX 文档允许 dst == NULL，因此这里不一定崩。
         */
        PxTranslateToUTF(charset_translate,
                         (const char *)s,
                         len,
                         &src_taken,
                         utf8_buffer,
                         utf8_len,
                         &dst_made);

        /*
         * Vim 真实源码中的后续赋值。
         */
        s = (char_u *)utf8_buffer;
        len = dst_made;
    }

    /*
     * 更可靠的崩溃点：
     * s == NULL, len > 0。
     */
    PgDrawText((const char *)s, len, &pos, 0);
}

int
main(void)
{
    char_u payload[4096];

    memset(payload, 'A', sizeof(payload));

    fprintf(stderr, "[PoC] triggering Vim gui_photon realloc failure path\n");

    vulnerable_gui_mch_draw_string(0, 0, payload, sizeof(payload), 0);

    fprintf(stderr, "[PoC] finished\n");
    return 0;
}