/*
 * ttf_parse.h -- minimal TrueType/OpenType table reader, just enough to
 * embed a font in a PDF as a simple (non-CID) TrueType font with a
 * WinAnsiEncoding: per-WinAnsi-byte advance widths (via cmap format 4 +
 * hmtx) and the handful of scalar metrics a /FontDescriptor needs
 * (ascent/descent/bbox from head+hhea). Does NOT parse glyph outlines
 * (glyf/loca) -- we never rasterize ourselves, the embedded font PROGRAM
 * (raw file bytes, unchanged) is what a PDF viewer/RIP rasterizes, we
 * only need its *metrics* to lay text out correctly and to satisfy the
 * PDF font dictionary's required fields.
 *
 * cmap format 4 only (Microsoft Unicode BMP, platform 3 encoding 1, or
 * platform 0) -- covers WinAnsi's full range (Latin-1 + a handful of
 * CP1252 specials in 0x80-0x9F), and is what virtually every TrueType
 * font ships (including both vendored Liberation Sans weights, verified
 * against real character widths -- see tests/).
 */

#include <string.h>

typedef struct {
    int ok;
    int ascent, descent, cap_height;
    int bbox_x0, bbox_y0, bbox_x1, bbox_y1;
    int widths[256];
} TtfMetrics;

static int ttf_u16(const unsigned char* p) { return (p[0] << 8) | p[1]; }
static int ttf_s16(const unsigned char* p) {
    int v = ttf_u16(p);
    return v >= 0x8000 ? v - 0x10000 : v;
}
static unsigned int ttf_u32(const unsigned char* p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16)
         | ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

static int ttf_find_table(const unsigned char* buf, unsigned int len, const char* tag,
                           unsigned int* out_off, unsigned int* out_len) {
    if (len < 12) { return 0; }
    int num_tables = ttf_u16(buf + 4);
    unsigned int p = 12;
    int i;
    for (i = 0; i < num_tables; i++) {
        if (p + 16 > len) { return 0; }
        if (buf[p] == (unsigned char)tag[0] && buf[p+1] == (unsigned char)tag[1]
                && buf[p+2] == (unsigned char)tag[2] && buf[p+3] == (unsigned char)tag[3]) {
            *out_off = ttf_u32(buf + p + 8);
            *out_len = ttf_u32(buf + p + 12);
            return 1;
        }
        p += 16;
    }
    return 0;
}

/* Codepoint -> Glyph ID, cmap format 4 only. Returns 0 (.notdef) if not found
   or if the font's cmap isn't format 4. */
static int ttf_cmap_lookup(const unsigned char* buf, unsigned int cmap_off, int codepoint) {
    const unsigned char* cmap = buf + cmap_off;
    int num_subtables = ttf_u16(cmap + 2);
    unsigned int chosen = 0;
    int found = 0;
    int i;
    for (i = 0; i < num_subtables; i++) {
        unsigned int rec = 4 + (unsigned int)i * 8;
        int platform_id = ttf_u16(cmap + rec);
        int encoding_id = ttf_u16(cmap + rec + 2);
        unsigned int sub_off = ttf_u32(cmap + rec + 4);
        if (platform_id == 3 && encoding_id == 1) { chosen = sub_off; found = 1; break; }
        if (platform_id == 0 && !found) { chosen = sub_off; found = 1; }
    }
    if (!found) { return 0; }
    const unsigned char* sub = cmap + chosen;
    if (ttf_u16(sub) != 4) { return 0; }
    int seg_x2 = ttf_u16(sub + 6);
    int seg_count = seg_x2 / 2;
    const unsigned char* end_codes = sub + 14;
    const unsigned char* start_codes = end_codes + seg_x2 + 2; /* +2 skips reservedPad */
    const unsigned char* id_deltas = start_codes + seg_x2;
    const unsigned char* id_range_offsets = id_deltas + seg_x2;
    int s;
    for (s = 0; s < seg_count; s++) {
        int end_code = ttf_u16(end_codes + s * 2);
        if (codepoint > end_code) { continue; }
        int start_code = ttf_u16(start_codes + s * 2);
        if (codepoint < start_code) { return 0; }
        int id_delta = ttf_s16(id_deltas + s * 2);
        int id_range_offset = ttf_u16(id_range_offsets + s * 2);
        if (id_range_offset == 0) {
            return (codepoint + id_delta) & 0xFFFF;
        }
        const unsigned char* addr = id_range_offsets + s * 2 + id_range_offset
                                     + (codepoint - start_code) * 2;
        int gid = ttf_u16(addr);
        if (gid == 0) { return 0; }
        return (gid + id_delta) & 0xFFFF;
    }
    return 0;
}

static int ttf_hmtx_width(const unsigned char* buf, unsigned int hmtx_off, int num_h,
                           int gid, int units_per_em) {
    int aw;
    if (gid < num_h) {
        aw = ttf_u16(buf + hmtx_off + (unsigned int)gid * 4);
    } else if (num_h > 0) {
        aw = ttf_u16(buf + hmtx_off + (unsigned int)(num_h - 1) * 4);
    } else {
        aw = 0;
    }
    return (aw * 1000) / units_per_em;
}

/* WinAnsi (CP1252) byte -> Unicode codepoint. 0x00-0x7F et 0xA0-0xFF sont
   identiques a l'Unicode par construction (Latin-1) ; seul 0x80-0x9F a une
   table dediee (memes valeurs que Pdf.cpToWinAnsi cote Amalgame, inversees). */
static int winansi_to_unicode(int b) {
    static const int t[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
    };
    if (b >= 0x80 && b <= 0x9F) { return t[b - 0x80]; }
    return b;
}

static int ttf_parse(const unsigned char* buf, unsigned int len, TtfMetrics* out) {
    memset(out, 0, sizeof(*out));
    unsigned int head_off, head_len, hhea_off, hhea_len, hmtx_off, hmtx_len,
                 cmap_off, cmap_len, maxp_off, maxp_len;
    if (!ttf_find_table(buf, len, "head", &head_off, &head_len)) { return 0; }
    if (!ttf_find_table(buf, len, "hhea", &hhea_off, &hhea_len)) { return 0; }
    if (!ttf_find_table(buf, len, "hmtx", &hmtx_off, &hmtx_len)) { return 0; }
    if (!ttf_find_table(buf, len, "cmap", &cmap_off, &cmap_len)) { return 0; }
    if (!ttf_find_table(buf, len, "maxp", &maxp_off, &maxp_len)) { return 0; }

    int units_per_em = ttf_u16(buf + head_off + 18);
    if (units_per_em <= 0) { units_per_em = 1000; }
    int x_min = ttf_s16(buf + head_off + 36);
    int y_min = ttf_s16(buf + head_off + 38);
    int x_max = ttf_s16(buf + head_off + 40);
    int y_max = ttf_s16(buf + head_off + 42);

    int ascent = ttf_s16(buf + hhea_off + 4);
    int descent = ttf_s16(buf + hhea_off + 6);
    int num_h_metrics = ttf_u16(buf + hhea_off + 34);

    out->ascent = (ascent * 1000) / units_per_em;
    out->descent = (descent * 1000) / units_per_em;
    out->bbox_x0 = (x_min * 1000) / units_per_em;
    out->bbox_y0 = (y_min * 1000) / units_per_em;
    out->bbox_x1 = (x_max * 1000) / units_per_em;
    out->bbox_y1 = (y_max * 1000) / units_per_em;
    /* sCapHeight (OS/2) non lu -- l'ascent est une approximation haute
       raisonnable, un PDF viewer ne s'en sert que pour des heuristiques
       d'affichage (ex. hauteur d'un curseur de saisie), jamais pour le
       rendu du glyphe lui-meme (deja dans le fichier de police embarque). */
    out->cap_height = out->ascent;

    int c;
    for (c = 0; c < 256; c++) {
        int uni = winansi_to_unicode(c);
        int gid = ttf_cmap_lookup(buf, cmap_off, uni);
        out->widths[c] = (gid == 0) ? 0 : ttf_hmtx_width(buf, hmtx_off, num_h_metrics, gid, units_per_em);
    }
    out->ok = 1;
    return 1;
}

/* Cache -- parse chaque police AU PLUS UNE FOIS par processus (statics de
   fonction, persistent entre appels Amalgame -- meme principe que le cache
   fichier de cms_geoip.am, juste en RAM ici puisque les octets sont deja
   dans le binaire). font_id : 0 = Regular, 1 = Bold. */
static TtfMetrics g_ttf_cache[2];
static int g_ttf_cached[2] = {0, 0};

static TtfMetrics* ttf_get(int font_id) {
    if (font_id < 0 || font_id > 1) { return 0; }
    if (!g_ttf_cached[font_id]) {
        if (font_id == 0) {
            ttf_parse(kLiberationSansRegular, kLiberationSansRegularLen, &g_ttf_cache[0]);
        } else {
            ttf_parse(kLiberationSansBold, kLiberationSansBoldLen, &g_ttf_cache[1]);
        }
        g_ttf_cached[font_id] = 1;
    }
    return &g_ttf_cache[font_id];
}
