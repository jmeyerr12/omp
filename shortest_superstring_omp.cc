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

// Checa se 'a' é prefixo de 'b'
static inline bool is_prefix(const String& a, const String& b) {
    if (a.size() > b.size()) return false;
    return std::mismatch(a.begin(), a.end(), b.begin()).first == a.end();
}

static inline String remove_prefix(const String& x, size_t n) {
    return (x.size() > n) ? x.substr(n) : String();
}

// Calcula o maior k tal que o sufixo de tamanho k de s == prefixo de t.
// Implementação iterativa sem alocações (mais rápida que gerar todos sufixos).
static inline size_t overlap_value(const String& s, const String& t) {
    const size_t maxk = std::min(s.size(), t.size());
    for (size_t k = maxk; k > 0; --k) {
        // compara s[s.size()-k .. s.size()) com t[0 .. k)
        if (std::equal(s.end() - k, s.end(), t.begin())) return k;
    }
    return 0;
}

static inline String overlap_merge(const String& s, const String& t) {
    size_t k = overlap_value(s, t);
    return s + remove_prefix(t, k);
}

static String shortest_superstring_parallel(Set<String> ss) {
    if (ss.empty()) return "";

    while (ss.size() > 1) {
        // snapshot em vetor p/ avaliar pares com OpenMP
        std::vector<String> v(ss.begin(), ss.end());
        const int m = static_cast<int>(v.size());

        int best_i = -1, best_j = -1;
        size_t best_val = 0;

        // Paraleliza a busca do par com maior overlap
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
                if (ov > lv) {
                    lv = ov; li = i; lj = j;
                }
            }

            #pragma omp critical
            {
                if (lv > best_val) { best_val = lv; best_i = li; best_j = lj; }
            }
        }

        // Se por algum motivo não achou (não deve acontecer), junte dois quaisquer
        if (best_i < 0 || best_j < 0 || best_i == best_j) {
            auto it1 = ss.begin();
            auto it2 = std::next(it1);
            String merged = overlap_merge(*it1, *it2);
            // Em std::set, apagar um iterador não invalida os demais (exceto o apagado).
            ss.erase(it1);
            ss.erase(it2);
            ss.insert(std::move(merged));
            continue;
        }

        // Recupera as strings escolhidas por valor e aplica a mescla
        const String &A = v[best_i], &B = v[best_j];
        String merged = overlap_merge(A, B);

        // Remove A e B do set original e insere a mesclada
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
        String s; std::cin >> s;
        ss.insert(std::move(s)); // Set remove duplicadas automaticamente
    }

    double t0 = omp_get_wtime();
    String ans = shortest_superstring_parallel(ss);
    double t1 = omp_get_wtime();

    std::cout << ans << "\n";
    std::cerr << "Tempo total: " << (t1 - t0) << " s\n";
    return 0;
}
