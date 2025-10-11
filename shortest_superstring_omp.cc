// shortest_superstring_omp.cpp
#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <omp.h>

using String = std::string;
template <typename T> using Set = std::set<T>;

static inline bool is_prefix(const String& a, const String& b) {
    if (a.size() > b.size()) return false;
    return std::mismatch(a.begin(), a.end(), b.begin()).first == a.end();
}

static inline String suffix_from(const String& x, size_t i) {
    return x.substr(i);
}

// comportamento idêntico ao do professor:
// se n >= |x|, retorna a própria string (não remove)
static inline String remove_prefix(const String& x, size_t n) {
    return (x.size() > n) ? x.substr(n) : x;
}

// idêntico ao do professor:
// gera todos os sufixos, mas remove o sufixo inteiro da string
static Set<String> all_suffixes(const String& x) {
    Set<String> ss;
    if (x.empty()) return ss;
    for (size_t n = x.size(); n > 0; --n)
        ss.insert(x.substr(n - 1));
    ss.erase(x);  // remove o sufixo inteiro (compatível com a versão seq.)
    return ss;
}

static String common_suffix_prefix(const String& a, const String& b) {
    if (a.empty() || b.empty()) return "";
    String best = "";
    for (const String& s : all_suffixes(a)) {
        if (is_prefix(s, b) && s.size() > best.size()) best = s;
    }
    return best;
}

static inline size_t overlap_value(const String& s, const String& t) {
    return common_suffix_prefix(s, t).size();
}

static inline String overlap_merge(const String& s, const String& t) {
    String c = common_suffix_prefix(s, t);
    return s + remove_prefix(t, c.size());
}

// desempate determinístico (como sugerido no grupo):
// se overlap empata, escolhe o par com ordem lexicográfica menor
static inline bool better(size_t ov1, const String& a1, const String& b1,
                          size_t ov2, const String& a2, const String& b2) {
    if (ov1 != ov2) return ov1 > ov2;   // maior overlap primeiro
    if (a1 != a2)   return a1 < a2;     // menor A lex
    return b1 < b2;                      // menor B lex
}

static String shortest_superstring_parallel(Set<String> ss) {
    if (ss.empty()) return "";

    while (ss.size() > 1) {
        // snapshot em vetor p/ varrer pares com OpenMP
        std::vector<String> v(ss.begin(), ss.end());
        const int m = static_cast<int>(v.size());

        int best_i = -1, best_j = -1;
        size_t best_val = 0;

        // busca paralela do melhor par (mesmo critério do seq.)
        #pragma omp parallel
        {
            int li = -1, lj = -1;
            size_t lv = 0;

            #pragma omp for schedule(static) nowait
            for (int idx = 0; idx < m * m; ++idx) {
                int i = idx / m;
                int j = idx % m;
                if (i == j) continue;

                size_t ov = overlap_value(v[i], v[j]);
                if (li == -1 || better(ov, v[i], v[j], lv, v[li], v[lj])) {
                    lv = ov; li = i; lj = j;
                }
            }

            #pragma omp critical
            {
                if (li != -1 &&
                    (best_i == -1 ||
                     better(lv, v[li], v[lj],
                             best_val, v[best_i], v[best_j]))) {
                    best_val = lv; best_i = li; best_j = lj;
                }
            }
        }

        // fallback de segurança (não deve acontecer)
        if (best_i < 0 || best_j < 0 || best_i == best_j) {
            auto it1 = ss.begin();
            auto it2 = std::next(it1);
            String merged = overlap_merge(*it1, *it2);
            ss.erase(it1);
            ss.erase(it2);
            ss.insert(std::move(merged));
            continue;
        }

        // aplica a fusão e atualiza o conjunto
        const String &A = v[best_i], &B = v[best_j];
        String merged = overlap_merge(A, B);
        ss.erase(A);
        ss.erase(B);
        ss.insert(std::move(merged));
    }

    return *ss.begin();
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    size_t n;
    if (!(std::cin >> n)) return 0;
    Set<String> ss;
    for (size_t i = 0; i < n; ++i) {
        String s;
        std::cin >> s;
        ss.insert(std::move(s));
    }

    double t0 = omp_get_wtime();
    String ans = shortest_superstring_parallel(ss);
    double t1 = omp_get_wtime();

    std::cout << ans << "\n";
    std::cerr << (t1 - t0) << "\n";
    return 0;
}
