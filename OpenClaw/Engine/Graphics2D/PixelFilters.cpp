#include "PixelFilters.h"

#include <cstdlib>

namespace PixelFilters
{
    namespace
    {
        struct Src
        {
            const uint32_t* px;
            int w, h;
            // (edges repeated past the picture's sides)
            uint32_t operator()(int x, int y) const
            {
                x = x < 0 ? 0 : (x >= w ? w - 1 : x);
                y = y < 0 ? 0 : (y >= h ? h - 1 : y);
                return px[(size_t)y * w + x];
            }
        };

        inline int A(uint32_t c) { return (int)(c >> 24); }
        inline int R(uint32_t c) { return (int)((c >> 16) & 0xFF); }
        inline int G(uint32_t c) { return (int)((c >> 8) & 0xFF); }
        inline int B(uint32_t c) { return (int)(c & 0xFF); }

        // Mixes of colours weighted by their alpha (a see-through pixel adds
        // no colour of its own: no dark edges round the sprites).
        uint32_t Mix(const uint32_t* c, const int* wgt, int n)
        {
            int wsum = 0, asum = 0, ra = 0, ga = 0, ba = 0;
            for (int i = 0; i < n; ++i)
            {
                const int a = A(c[i]) * wgt[i];
                wsum += wgt[i];
                asum += a;
                ra += R(c[i]) * a; ga += G(c[i]) * a; ba += B(c[i]) * a;
            }
            const int alpha = wsum ? asum / wsum : 0;
            if (asum == 0) return 0;
            return ((uint32_t)alpha << 24) | ((uint32_t)(ra / asum) << 16) | ((uint32_t)(ga / asum) << 8) | (uint32_t)(ba / asum);
        }
        uint32_t Mix2(uint32_t a, uint32_t b)
        {
            if (a == b) return a;
            const uint32_t c[2] = { a, b };
            const int w[2] = { 1, 1 };
            return Mix(c, w, 2);
        }
        uint32_t Mix4(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
        {
            const uint32_t cc[4] = { a, b, c, d };
            const int w[4] = { 1, 1, 1, 1 };
            return Mix(cc, w, 4);
        }

        void Put(std::vector<uint32_t>& out, int w2, int x, int y, uint32_t e0, uint32_t e1, uint32_t e2, uint32_t e3)
        {
            uint32_t* row = &out[(size_t)(2 * y) * w2 + 2 * x];
            row[0] = e0; row[1] = e1;
            row[w2] = e2; row[w2 + 1] = e3;
        }

        // ------------------------------------------------------------ Scale2x
        void DoScale2x(const Src& s, std::vector<uint32_t>& out)
        {
            const int w2 = s.w * 2;
            for (int y = 0; y < s.h; ++y)
                for (int x = 0; x < s.w; ++x)
                {
                    const uint32_t b = s(x, y - 1), d = s(x - 1, y), e = s(x, y), f = s(x + 1, y), h = s(x, y + 1);
                    if (b != h && d != f)
                        Put(out, w2, x, y, d == b ? d : e, b == f ? f : e, d == h ? d : e, h == f ? f : e);
                    else
                        Put(out, w2, x, y, e, e, e, e);
                }
        }

        // -------------------------------------------------------------- 2xSaI
        int Result1(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
        {
            int x = 0, y = 0, r = 0;
            if (a == c) ++x; else if (b == c) ++y;
            if (a == d) ++x; else if (b == d) ++y;
            if (x <= 1) r += 1;
            if (y <= 1) r -= 1;
            return r;
        }
        int Result2(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
        {
            int x = 0, y = 0, r = 0;
            if (a == c) ++x; else if (b == c) ++y;
            if (a == d) ++x; else if (b == d) ++y;
            if (x <= 1) r -= 1;
            if (y <= 1) r += 1;
            return r;
        }

        void DoSai2x(const Src& s, std::vector<uint32_t>& out)
        {
            const int w2 = s.w * 2;
            for (int y = 0; y < s.h; ++y)
                for (int x = 0; x < s.w; ++x)
                {
                    //  I E F J
                    //  G A B K
                    //  H C D L
                    //  M N O P
                    const uint32_t I = s(x - 1, y - 1), E = s(x, y - 1), F = s(x + 1, y - 1), J = s(x + 2, y - 1);
                    const uint32_t G = s(x - 1, y), A = s(x, y), B = s(x + 1, y), K = s(x + 2, y);
                    const uint32_t H = s(x - 1, y + 1), C = s(x, y + 1), D = s(x + 1, y + 1), L = s(x + 2, y + 1);
                    const uint32_t M = s(x - 1, y + 2), N = s(x, y + 2), O = s(x + 1, y + 2), P = s(x + 2, y + 2);
                    uint32_t right, below, diag;
                    if (A == D && B != C)
                    {
                        right = ((A == E && B == L) || (A == C && A == F && B != E && B == J)) ? A : Mix2(A, B);
                        below = ((A == G && C == O) || (A == B && A == H && G != C && C == M)) ? A : Mix2(A, C);
                        diag = A;
                    }
                    else if (B == C && A != D)
                    {
                        right = ((B == F && A == H) || (B == E && B == D && A != F && A == I)) ? B : Mix2(A, B);
                        below = ((C == H && A == F) || (C == G && C == D && A != H && A == I)) ? C : Mix2(A, C);
                        diag = B;
                    }
                    else if (A == D && B == C)
                    {
                        if (A == B)
                        {
                            right = below = diag = A;
                        }
                        else
                        {
                            below = Mix2(A, C);
                            right = Mix2(A, B);
                            int r = 0;
                            r += Result1(A, B, G, E);
                            r += Result2(B, A, K, F);
                            r += Result2(B, A, H, N);
                            r += Result1(A, B, L, O);
                            diag = r > 0 ? A : (r < 0 ? B : Mix4(A, B, C, D));
                        }
                    }
                    else
                    {
                        diag = Mix4(A, B, C, D);
                        if (A == C && A == F && B != E && B == J) right = A;
                        else if (B == E && B == D && A != F && A == I) right = B;
                        else right = Mix2(A, B);
                        if (A == B && A == H && G != C && C == M) below = A;
                        else if (C == G && C == D && A != H && A == I) below = C;
                        else below = Mix2(A, C);
                    }
                    (void)P;
                    Put(out, w2, x, y, A, right, below, diag);
                }
        }

        // ---------------------------------------------------------------- xBR
        // Colour distance in YUV (and alpha), as xBR does.
        int Dist(uint32_t a, uint32_t b)
        {
            if (a == b) return 0;
            const int r = R(a) - R(b), g = G(a) - G(b), bl = B(a) - B(b);
            const int yy = std::abs(r * 299 + g * 587 + bl * 114) / 1000;
            const int u = std::abs(-r * 169 - g * 331 + bl * 500) / 1000;
            const int v = std::abs(r * 500 - g * 419 - bl * 81) / 1000;
            return 48 * yy + 7 * u + 6 * v + 64 * std::abs(A(a) - A(b));
        }

        void DoXbr2x(const Src& s, std::vector<uint32_t>& out)
        {
            const int w2 = s.w * 2;
            // The corner's neighbours for the bottom-right one; the others are
            // the same turned by 90 degrees: (dx, dy) -> (dy, -dx).
            //        A1 B1 C1
            //     A0 A  B  C  C4
            //     D0 D  E  F  F4
            //     G0 G  H  I  I4
            //        G5 H5 I5
            for (int y = 0; y < s.h; ++y)
                for (int x = 0; x < s.w; ++x)
                {
                    const uint32_t E = s(x, y);
                    uint32_t corner[4] = { E, E, E, E };   // bottom-right, top-right, top-left, bottom-left
                    for (int k = 0; k < 4; ++k)
                    {
                        auto at = [&](int dx, int dy) {
                            for (int t = 0; t < k; ++t) { const int nx = dy, ny = -dx; dx = nx; dy = ny; }
                            return s(x + dx, y + dy);
                        };
                        const uint32_t H = at(0, 1), F = at(1, 0), I = at(1, 1);
                        if (E == H || E == F) continue;
                        const uint32_t C = at(1, -1), Gp = at(-1, 1), F4 = at(2, 0), H5 = at(0, 2);
                        const uint32_t I4 = at(2, 1), I5 = at(1, 2), D = at(-1, 0), Bp = at(0, -1);
                        const int e = Dist(E, C) + Dist(E, Gp) + Dist(I, F4) + Dist(I, H5) + 4 * Dist(H, F);
                        const int i = Dist(H, D) + Dist(H, I5) + Dist(F, I4) + Dist(F, Bp) + 4 * Dist(E, I);
                        if (e < i)
                        {
                            const uint32_t px = Dist(E, F) <= Dist(E, H) ? F : H;
                            corner[k] = Mix2(E, px);
                        }
                    }
                    Put(out, w2, x, y, corner[2], corner[1], corner[3], corner[0]);
                }
        }
    }

    void Upscale2x(Kind kind, const uint32_t* src, int w, int h, std::vector<uint32_t>& out)
    {
        out.assign((size_t)w * h * 4, 0);
        const Src s{ src, w, h };
        switch (kind)
        {
            case Sai2x: DoSai2x(s, out); break;
            case Xbr2x: DoXbr2x(s, out); break;
            case Scale2x:
            default: DoScale2x(s, out); break;
        }
    }

    const char* Name(Kind kind)
    {
        switch (kind)
        {
            case Scale2x: return "Scale2x";
            case Sai2x: return "2xSaI";
            case Xbr2x: return "xBR";
            default: return "none";
        }
    }
}
