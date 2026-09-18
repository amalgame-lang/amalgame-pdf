/*
 * ttf_subset.h -- allègement des PDF (v0.4) : sous-ensemble de police
 * TrueType + compression Flate des flux.
 *
 * Sous-ensemble « à identifiants conservés » : le fichier garde le même
 * nombre de glyphes et les mêmes identifiants (cmap, hmtx inchangés,
 * donc toujours cohérents avec /Widths et WinAnsiEncoding), mais le
 * contour des glyphes NON utilisés est vidé (entrée loca de longueur
 * nulle = glyphe vide, valide en TrueType). Les composants des glyphes
 * composites (ex. « é » = « e » + accent) sont suivis récursivement. Les
 * tables inutiles à un PDF (GSUB, GPOS, kern, name, DSIG…) sont retirées ;
 * post est réduit à sa version 3 (sans noms de glyphes).
 *
 * Liberation Sans : ~400 Ko → ~40 Ko sous-ensemblé → ~20 Ko compressé,
 * pour un document courant (une centaine de caractères distincts).
 *
 * Tout est alloué avec malloc et libéré avant de rendre la main ; les
 * résultats sont remis au code Amalgame sous forme d'AmalgameList.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>

static void ttfs_w16(unsigned char* p, unsigned int v) { p[0] = (v >> 8) & 0xFF; p[1] = v & 0xFF; }
static void ttfs_w32(unsigned char* p, unsigned int v) {
    p[0] = (v >> 24) & 0xFF; p[1] = (v >> 16) & 0xFF; p[2] = (v >> 8) & 0xFF; p[3] = v & 0xFF;
}
static unsigned int ttfs_checksum(const unsigned char* p, unsigned int len) {
    unsigned int sum = 0, i;
    unsigned int n = (len + 3) & ~3u;
    for (i = 0; i < n; i += 4) {
        unsigned int v = 0, k;
        for (k = 0; k < 4; k++) { v = (v << 8) | (i + k < len ? p[i + k] : 0); }
        sum += v;
    }
    return sum;
}

/* Décalage/longueur du glyphe gid dans glyf, d'après loca (court ou long). */
static int ttfs_glyph_range(const unsigned char* buf, unsigned int loca_off, int long_fmt, int gid,
                            unsigned int* off, unsigned int* len) {
    unsigned int a, b;
    if (long_fmt) {
        a = ttf_u32(buf + loca_off + (unsigned int)gid * 4);
        b = ttf_u32(buf + loca_off + (unsigned int)(gid + 1) * 4);
    } else {
        a = (unsigned int)ttf_u16(buf + loca_off + (unsigned int)gid * 2) * 2;
        b = (unsigned int)ttf_u16(buf + loca_off + (unsigned int)(gid + 1) * 2) * 2;
    }
    if (b < a) { return 0; }
    *off = a; *len = b - a;
    return 1;
}

typedef struct { char tag[5]; const unsigned char* data; unsigned int len; unsigned char* owned; } TtfsTable;

/* Sous-ensemble : used[256] = 1 pour chaque octet WinAnsi dessiné.
   Renvoie un buffer malloc (à libérer) et sa longueur, ou 0 en cas
   d'échec (l'appelant embarque alors la police complète). */
static unsigned char* ttf_subset(const unsigned char* buf, unsigned int len, const int* used,
                                 unsigned int* out_len) {
    unsigned int head_off, head_len, loca_off, loca_len, glyf_off, glyf_len, maxp_off, maxp_len,
                 cmap_off, cmap_len, t_off, t_len;
    if (!ttf_find_table(buf, len, "head", &head_off, &head_len) || head_len < 54) { return 0; }
    if (!ttf_find_table(buf, len, "loca", &loca_off, &loca_len)) { return 0; }
    if (!ttf_find_table(buf, len, "glyf", &glyf_off, &glyf_len)) { return 0; }
    if (!ttf_find_table(buf, len, "maxp", &maxp_off, &maxp_len)) { return 0; }
    if (!ttf_find_table(buf, len, "cmap", &cmap_off, &cmap_len)) { return 0; }
    int num_glyphs = ttf_u16(buf + maxp_off + 4);
    int long_fmt = ttf_s16(buf + head_off + 50) == 1;
    if (num_glyphs <= 0) { return 0; }

    /* 1. glyphes à garder : .notdef, ceux des caractères utilisés, puis
       les composants des composites jusqu'à point fixe. */
    unsigned char* keep = (unsigned char*) calloc((size_t)num_glyphs, 1);
    if (!keep) { return 0; }
    keep[0] = 1;
    int c;
    for (c = 0; c < 256; c++) {
        if (!used[c]) { continue; }
        int gid = ttf_cmap_lookup(buf, cmap_off, winansi_to_unicode(c));
        if (gid > 0 && gid < num_glyphs) { keep[gid] = 1; }
    }
    int change = 1;
    while (change) {
        change = 0;
        int g;
        for (g = 0; g < num_glyphs; g++) {
            unsigned int go, gl;
            if (!keep[g] || !ttfs_glyph_range(buf, loca_off, long_fmt, g, &go, &gl) || gl < 10) { continue; }
            const unsigned char* gp = buf + glyf_off + go;
            if (ttf_s16(gp) >= 0) { continue; }          /* glyphe simple */
            unsigned int p = 10;
            int flags;
            do {
                if (p + 4 > gl) { break; }
                flags = ttf_u16(gp + p);
                int comp = ttf_u16(gp + p + 2);
                if (comp < num_glyphs && !keep[comp]) { keep[comp] = 1; change = 1; }
                p += 4;
                p += (flags & 0x0001) ? 4 : 2;           /* ARG_1_AND_2_ARE_WORDS */
                if (flags & 0x0008) { p += 2; }           /* WE_HAVE_A_SCALE */
                else if (flags & 0x0040) { p += 4; }      /* WE_HAVE_AN_X_AND_Y_SCALE */
                else if (flags & 0x0080) { p += 8; }      /* WE_HAVE_A_TWO_BY_TWO */
            } while (flags & 0x0020);                     /* MORE_COMPONENTS */
        }
    }

    /* 2. nouveaux glyf + loca (format long). */
    unsigned int total = 0;
    int g;
    for (g = 0; g < num_glyphs; g++) {
        unsigned int go, gl;
        if (keep[g] && ttfs_glyph_range(buf, loca_off, long_fmt, g, &go, &gl)) { total += (gl + 3) & ~3u; }
    }
    unsigned char* nglyf = (unsigned char*) calloc(total ? total : 4, 1);
    unsigned char* nloca = (unsigned char*) malloc(((size_t)num_glyphs + 1) * 4);
    unsigned char* nhead = (unsigned char*) malloc(head_len);
    unsigned char* npost = (unsigned char*) calloc(32, 1);
    if (!nglyf || !nloca || !nhead || !npost) {
        free(keep); free(nglyf); free(nloca); free(nhead); free(npost); return 0;
    }
    unsigned int pos = 0;
    for (g = 0; g < num_glyphs; g++) {
        unsigned int go, gl;
        ttfs_w32(nloca + (unsigned int)g * 4, pos);
        if (keep[g] && ttfs_glyph_range(buf, loca_off, long_fmt, g, &go, &gl) && gl > 0) {
            memcpy(nglyf + pos, buf + glyf_off + go, gl);
            pos += (gl + 3) & ~3u;
        }
    }
    ttfs_w32(nloca + (unsigned int)num_glyphs * 4, pos);
    free(keep);

    memcpy(nhead, buf + head_off, head_len);
    ttfs_w32(nhead + 8, 0);             /* checkSumAdjustment, recalculé à la fin */
    ttfs_w16(nhead + 50, 1);            /* indexToLocFormat = long */

    int has_post = ttf_find_table(buf, len, "post", &t_off, &t_len) && t_len >= 32;
    if (has_post) { memcpy(npost, buf + t_off, 32); }
    ttfs_w32(npost, 0x00030000);        /* version 3 : pas de noms de glyphes */

    /* 3. tables conservées, triées par étiquette (exigence du format). */
    static const char* garder[] = { "OS/2", "cmap", "cvt ", "fpgm", "glyf", "head", "hhea",
                                    "hmtx", "loca", "maxp", "post", "prep" };
    TtfsTable tabs[12];
    int nt = 0, k;
    for (k = 0; k < 12; k++) {
        const char* tg = garder[k];
        TtfsTable t;
        memcpy(t.tag, tg, 4); t.tag[4] = 0; t.owned = 0;
        if (!strcmp(tg, "glyf")) { t.data = nglyf; t.len = pos ? pos : 4; }
        else if (!strcmp(tg, "loca")) { t.data = nloca; t.len = ((unsigned int)num_glyphs + 1) * 4; }
        else if (!strcmp(tg, "head")) { t.data = nhead; t.len = head_len; }
        else if (!strcmp(tg, "post")) { if (!has_post) { continue; } t.data = npost; t.len = 32; }
        else {
            if (!ttf_find_table(buf, len, tg, &t_off, &t_len)) { continue; }
            t.data = buf + t_off; t.len = t_len;
        }
        tabs[nt++] = t;
    }

    unsigned int dir_len = 12 + 16 * (unsigned int)nt;
    unsigned int size = dir_len;
    for (k = 0; k < nt; k++) { size += (tabs[k].len + 3) & ~3u; }
    unsigned char* out = (unsigned char*) calloc(size, 1);
    if (!out) { free(nglyf); free(nloca); free(nhead); free(npost); return 0; }
    int es = 0; while ((1 << (es + 1)) <= nt) { es++; }
    ttfs_w32(out, 0x00010000);
    ttfs_w16(out + 4, (unsigned int)nt);
    ttfs_w16(out + 6, (unsigned int)(16 << es));
    ttfs_w16(out + 8, (unsigned int)es);
    ttfs_w16(out + 10, (unsigned int)(nt * 16 - (16 << es)));
    unsigned int off = dir_len;
    unsigned int head_pos = 0;
    for (k = 0; k < nt; k++) {
        unsigned char* d = out + 12 + 16 * (unsigned int)k;
        memcpy(d, tabs[k].tag, 4);
        ttfs_w32(d + 4, ttfs_checksum(tabs[k].data, tabs[k].len));
        ttfs_w32(d + 8, off);
        ttfs_w32(d + 12, tabs[k].len);
        memcpy(out + off, tabs[k].data, tabs[k].len);
        if (!strcmp(tabs[k].tag, "head")) { head_pos = off; }
        off += (tabs[k].len + 3) & ~3u;
    }
    if (head_pos) { ttfs_w32(out + head_pos + 8, 0xB1B0AFBAu - ttfs_checksum(out, size)); }
    free(nglyf); free(nloca); free(nhead); free(npost);
    *out_len = size;
    return out;
}

/* Compression Flate (zlib) d'un buffer → AmalgameList d'octets ; 0 si échec. */
static AmalgameList* pdf_deflate_buf(const unsigned char* src, unsigned long n) {
    uLongf cap = compressBound(n);
    unsigned char* dst = (unsigned char*) malloc(cap);
    if (!dst) { return 0; }
    if (compress2(dst, &cap, src, n, Z_BEST_COMPRESSION) != Z_OK) { free(dst); return 0; }
    AmalgameList* l = AmalgameList_new();
    uLongf i;
    for (i = 0; i < cap; i++) { AmalgameList_add(l, (void*)(intptr_t) dst[i]); }
    free(dst);
    return l;
}

/* AmalgameList d'octets → AmalgameList compressée (Flate). */
static AmalgameList* pdf_deflate_list(AmalgameList* in) {
    int n = (int) AmalgameList_count(in);
    unsigned char* src = (unsigned char*) malloc(n > 0 ? (size_t) n : 1);
    if (!src) { return 0; }
    int i;
    for (i = 0; i < n; i++) { src[i] = (unsigned char)(intptr_t) AmalgameList_get(in, i); }
    AmalgameList* r = pdf_deflate_buf(src, (unsigned long) n);
    free(src);
    return r;
}

/* Programme de police prêt à embarquer : sous-ensemble (si possible) puis
   compressé. *raw_len reçoit la taille NON compressée (/Length1). */
static AmalgameList* pdf_font_program(int font_id, AmalgameList* used_list, int* raw_len) {
    const unsigned char* buf = font_id == 0 ? kLiberationSansRegular : kLiberationSansBold;
    unsigned int len = font_id == 0 ? (unsigned int) kLiberationSansRegularLen : (unsigned int) kLiberationSansBoldLen;
    int used[256];
    int c;
    for (c = 0; c < 256; c++) {
        used[c] = c < (int) AmalgameList_count(used_list) ? (int)(intptr_t) AmalgameList_get(used_list, c) : 0;
    }
    unsigned int sub_len = 0;
    unsigned char* sub = ttf_subset(buf, len, used, &sub_len);
    AmalgameList* r;
    if (sub) {
        r = pdf_deflate_buf(sub, sub_len);
        *raw_len = (int) sub_len;
        free(sub);
    } else {
        r = pdf_deflate_buf(buf, len);
        *raw_len = (int) len;
    }
    return r;
}
