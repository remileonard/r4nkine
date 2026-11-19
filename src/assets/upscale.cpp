#include "r4/assets/upscale.h"

#include <cstdlib>
#include <cstring>

namespace r4::assets {
namespace {

using Pixel = uint32_t; // RGBA, R in the low byte

int ch(Pixel p, int i) {
    return (int)(p >> (8 * i) & 0xff);
}
Pixel pack(int r, int g, int b, int a) {
    return (Pixel)r | (Pixel)g << 8 | (Pixel)b << 16 | (Pixel)a << 24;
}

// the image as RGBA pixels, read with the wrap of its edges
struct Source {
    int w;
    int h;
    bool clampS;
    bool clampT;
    std::vector<Pixel> px;
    Pixel at(int x, int y) const {
        if (clampS) {
            x = x < 0 ? 0 : x >= w ? w - 1 : x;
        } else {
            x = ((x % w) + w) % w;
        }
        if (clampT) {
            y = y < 0 ? 0 : y >= h ? h - 1 : y;
        } else {
            y = ((y % h) + h) % h;
        }
        return px[(size_t)y * w + x];
    }
};

Source sourceOf(const Image &img) {
    Source s{img.width, img.height, img.clampS, img.clampT, {}};
    s.px.resize((size_t)img.width * img.height);
    for (size_t i = 0; i < s.px.size(); i++) {
        const uint8_t *p = &img.pixels[i * img.channels];
        switch (img.channels) {
        case 1:
            s.px[i] = pack(p[0], p[0], p[0], 255);
            break;
        case 2:
            s.px[i] = pack(p[0], p[0], p[0], p[1]);
            break;
        case 3:
            s.px[i] = pack(p[0], p[1], p[2], 255);
            break;
        default:
            s.px[i] = pack(p[0], p[1], p[2], p[3]);
            break;
        }
    }
    return s;
}

Image imageOf(const Image &like, int w, int h, const std::vector<Pixel> &px) {
    Image out;
    out.name = like.name;
    out.clampS = like.clampS;
    out.clampT = like.clampT;
    out.mipmaps = like.mipmaps;
    out.width = w;
    out.height = h;
    out.channels = 4;
    out.pixels.resize(px.size() * 4);
    std::memcpy(out.pixels.data(), px.data(), out.pixels.size());
    return out;
}

// the mean of 2 and of 4 pixels, by channel
Pixel mix2(Pixel a, Pixel b) {
    return pack((ch(a, 0) + ch(b, 0) + 1) / 2, (ch(a, 1) + ch(b, 1) + 1) / 2, (ch(a, 2) + ch(b, 2) + 1) / 2, (ch(a, 3) + ch(b, 3) + 1) / 2);
}
Pixel mix4(Pixel a, Pixel b, Pixel c, Pixel d) {
    int r[4];
    for (int i = 0; i < 4; i++) {
        r[i] = (ch(a, i) + ch(b, i) + ch(c, i) + ch(d, i) + 2) / 4;
    }
    return pack(r[0], r[1], r[2], r[3]);
}

// --- 2xSaI ------------------------------------------------------------------------------

int result1(Pixel a, Pixel b, Pixel c, Pixel d) {
    int x = 0;
    int y = 0;
    int r = 0;
    if (a == c) {
        x++;
    } else if (b == c) {
        y++;
    }
    if (a == d) {
        x++;
    } else if (b == d) {
        y++;
    }
    if (x <= 1) {
        r++;
    }
    if (y <= 1) {
        r--;
    }
    return r;
}
int result2(Pixel a, Pixel b, Pixel c, Pixel d) {
    int x = 0;
    int y = 0;
    int r = 0;
    if (a == c) {
        x++;
    } else if (b == c) {
        y++;
    }
    if (a == d) {
        x++;
    } else if (b == d) {
        y++;
    }
    if (x <= 1) {
        r--;
    }
    if (y <= 1) {
        r++;
    }
    return r;
}

Image sai2x(const Image &img) {
    Source s = sourceOf(img);
    const int W = s.w * 2;
    const int H = s.h * 2;
    std::vector<Pixel> out((size_t)W * H);
    for (int y = 0; y < s.h; y++) {
        for (int x = 0; x < s.w; x++) {
            //  I E F J
            //  G A B K
            //  H C D L
            //  M N O (P unused)
            Pixel I = s.at(x - 1, y - 1);
            Pixel E = s.at(x, y - 1);
            Pixel F = s.at(x + 1, y - 1);
            Pixel J = s.at(x + 2, y - 1);
            Pixel G = s.at(x - 1, y);
            Pixel A = s.at(x, y);
            Pixel B = s.at(x + 1, y);
            Pixel K = s.at(x + 2, y);
            Pixel H = s.at(x - 1, y + 1);
            Pixel C = s.at(x, y + 1);
            Pixel D = s.at(x + 1, y + 1);
            Pixel L = s.at(x + 2, y + 1);
            Pixel M = s.at(x - 1, y + 2);
            Pixel N = s.at(x, y + 2);
            Pixel O = s.at(x + 1, y + 2);
            Pixel right;
            Pixel below;
            Pixel diagonal;
            if (A == D && B != C) {
                right = (A == E && B == L) || (A == C && A == F && B != E && B == J) ? A : mix2(A, B);
                below = (A == G && C == O) || (A == B && A == H && G != C && C == M) ? A : mix2(A, C);
                diagonal = A;
            } else if (B == C && A != D) {
                right = (B == F && A == H) || (B == E && B == D && A != F && A == I) ? B : mix2(A, B);
                below = (C == H && A == F) || (C == G && C == D && A != H && A == I) ? C : mix2(A, C);
                diagonal = B;
            } else if (A == D && B == C) {
                if (A == B) {
                    right = below = diagonal = A;
                } else {
                    right = mix2(A, B);
                    below = mix2(A, C);
                    int r = 0;
                    r += result1(A, B, G, E);
                    r += result2(B, A, K, F);
                    r += result2(B, A, H, N);
                    r += result1(A, B, L, O);
                    diagonal = r > 0 ? A : r < 0 ? B : mix4(A, B, C, D);
                }
            } else {
                diagonal = mix4(A, B, C, D);
                if (A == C && A == F && B != E && B == J) {
                    right = A;
                } else if (B == E && B == D && A != F && A == I) {
                    right = B;
                } else {
                    right = mix2(A, B);
                }
                if (A == B && A == H && G != C && C == M) {
                    below = A;
                } else if (C == G && C == D && A != H && A == I) {
                    below = C;
                } else {
                    below = mix2(A, C);
                }
            }
            size_t o = (size_t)(2 * y) * W + 2 * x;
            out[o] = A;
            out[o + 1] = right;
            out[o + W] = below;
            out[o + W + 1] = diagonal;
        }
    }
    return imageOf(img, W, H, out);
}

// --- xBR (level 2, 2x) -----------------------------------------------------------------

// the distance of 2 colors in YUV, weighted as xBR and hqx (the luma first),
// the alpha as strong as the luma
int yuv(Pixel p, int &u, int &v) {
    int r = ch(p, 0);
    int g = ch(p, 1);
    int b = ch(p, 2);
    u = (-r * 38 - g * 74 + b * 112) / 256 + 128;
    v = (r * 112 - g * 94 - b * 18) / 256 + 128;
    return (r * 66 + g * 129 + b * 25) / 256 + 16;
}
int df(Pixel a, Pixel b) {
    if (a == b) {
        return 0;
    }
    int ua;
    int va;
    int ub;
    int vb;
    int ya = yuv(a, ua, va);
    int yb = yuv(b, ub, vb);
    return 48 * std::abs(ya - yb) + 7 * std::abs(ua - ub) + 6 * std::abs(va - vb) + 48 * std::abs(ch(a, 3) - ch(b, 3));
}
// close enough to be the same color
bool eq(Pixel a, Pixel b) {
    if (a == b) {
        return true;
    }
    int ua;
    int va;
    int ub;
    int vb;
    int ya = yuv(a, ua, va);
    int yb = yuv(b, ub, vb);
    return std::abs(ya - yb) <= 48 && std::abs(ua - ub) <= 7 && std::abs(va - vb) <= 6 && std::abs(ch(a, 3) - ch(b, 3)) <= 48;
}
// dst moved towards src by k / 256
void blend(Pixel &dst, Pixel src, int k) {
    int r[4];
    for (int i = 0; i < 4; i++) {
        r[i] = ch(dst, i) + ((ch(src, i) - ch(dst, i)) * k) / 256;
    }
    dst = pack(r[0], r[1], r[2], r[3]);
}

// one corner of the 2 x 2 of E, the one towards PI (Hyllian's FILTRO):
// n3 the corner, n2 its neighbor on the side of PH, n1 the one on the side of PF
void xbrCorner(Pixel *e, Pixel PE, Pixel PI, Pixel PH, Pixel PF, Pixel PG, Pixel PC, Pixel PD, Pixel PB, Pixel F4, Pixel I4, Pixel H5, Pixel I5, int n1, int n2, int n3) {
    if (PE == PH || PE == PF) {
        return;
    }
    int ed = df(PE, PC) + df(PE, PG) + df(PI, H5) + df(PI, F4) + (df(PH, PF) << 2);
    int id = df(PH, PD) + df(PH, I5) + df(PF, I4) + df(PF, PB) + (df(PE, PI) << 2);
    Pixel px = df(PE, PF) <= df(PE, PH) ? PF : PH;
    if (ed < id && ((!eq(PF, PB) && !eq(PH, PD)) || (eq(PE, PI) && !eq(PF, I4) && !eq(PH, I5)) || eq(PE, PG) || eq(PE, PC))) {
        int ke = df(PF, PG);
        int ki = df(PH, PC);
        bool ex2 = PE != PC && PB != PC;
        bool ex3 = PE != PG && PD != PG;
        if ((ke << 1) <= ki && ex3 && ke >= (ki << 1) && ex2) {
            blend(e[n3], px, 224);
            blend(e[n2], px, 64);
            e[n1] = e[n2];
        } else if ((ke << 1) <= ki && ex3) {
            blend(e[n3], px, 192);
            blend(e[n2], px, 64);
        } else if (ke >= (ki << 1) && ex2) {
            blend(e[n3], px, 192);
            blend(e[n1], px, 64);
        } else {
            blend(e[n3], px, 128);
        }
    } else if (ed <= id) {
        blend(e[n3], px, 64);
    }
}

Image xbr2x(const Image &img) {
    Source s = sourceOf(img);
    const int W = s.w * 2;
    const int H = s.h * 2;
    std::vector<Pixel> out((size_t)W * H);
    for (int y = 0; y < s.h; y++) {
        for (int x = 0; x < s.w; x++) {
            //       A1 B1 C1
            //    A0 PA PB PC C4
            //    D0 PD PE PF F4
            //    G0 PG PH PI I4
            //       G5 H5 I5
            Pixel A1 = s.at(x - 1, y - 2);
            Pixel B1 = s.at(x, y - 2);
            Pixel C1 = s.at(x + 1, y - 2);
            Pixel A0 = s.at(x - 2, y - 1);
            Pixel PA = s.at(x - 1, y - 1);
            Pixel PB = s.at(x, y - 1);
            Pixel PC = s.at(x + 1, y - 1);
            Pixel C4 = s.at(x + 2, y - 1);
            Pixel D0 = s.at(x - 2, y);
            Pixel PD = s.at(x - 1, y);
            Pixel PE = s.at(x, y);
            Pixel PF = s.at(x + 1, y);
            Pixel F4 = s.at(x + 2, y);
            Pixel G0 = s.at(x - 2, y + 1);
            Pixel PG = s.at(x - 1, y + 1);
            Pixel PH = s.at(x, y + 1);
            Pixel PI = s.at(x + 1, y + 1);
            Pixel I4 = s.at(x + 2, y + 1);
            Pixel G5 = s.at(x - 1, y + 2);
            Pixel H5 = s.at(x, y + 2);
            Pixel I5 = s.at(x + 1, y + 2);
            // 0 top left, 1 top right, 2 bottom left, 3 bottom right
            Pixel e[4] = {PE, PE, PE, PE};
            xbrCorner(e, PE, PI, PH, PF, PG, PC, PD, PB, F4, I4, H5, I5, 1, 2, 3);
            xbrCorner(e, PE, PC, PF, PB, PI, PA, PH, PD, B1, C1, F4, C4, 0, 3, 1);
            xbrCorner(e, PE, PA, PB, PD, PC, PG, PF, PH, D0, A0, B1, A1, 2, 1, 0);
            xbrCorner(e, PE, PG, PD, PH, PA, PI, PB, PF, H5, G5, D0, G0, 3, 0, 2);
            size_t o = (size_t)(2 * y) * W + 2 * x;
            out[o] = e[0];
            out[o + 1] = e[1];
            out[o + W] = e[2];
            out[o + W + 1] = e[3];
        }
    }
    return imageOf(img, W, H, out);
}

} // namespace

int upscaleFactor(Upscale u) {
    switch (u) {
    case Upscale::SaI2x:
    case Upscale::Xbr2x:
        return 2;
    case Upscale::Xbr4x:
        return 4;
    default:
        return 1;
    }
}

const char *upscaleName(Upscale u) {
    switch (u) {
    case Upscale::SaI2x:
        return "2xSaI";
    case Upscale::Xbr2x:
        return "xBR 2x";
    case Upscale::Xbr4x:
        return "xBR 4x";
    default:
        return "None";
    }
}

Image upscale(const Image &image, Upscale u) {
    if (!image.valid() || image.pixels.size() < (size_t)image.width * image.height * image.channels) {
        return image;
    }
    switch (u) {
    case Upscale::SaI2x:
        return sai2x(image);
    case Upscale::Xbr2x:
        return xbr2x(image);
    case Upscale::Xbr4x:
        return xbr2x(xbr2x(image));
    default:
        return image;
    }
}

} // namespace r4::assets
