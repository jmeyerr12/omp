#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <chrono>
#include <vector>

#ifdef _OPENMP
  #include <omp.h>
#endif

#define standard_input  std::cin
#define standard_output std::cout

using Boolean = bool;
using Size    = std::size_t;
using String  = std::string;

using InStream  = std::istream;
using OutStream = std::ostream;

template <typename T, typename U>
using Pair = std::pair<T, U>;

template <typename T, typename C = std::less<T>>
using Set = std::set<T>;

template <typename T>
using SizeType = typename T::size_type;

double global_parallel_time = 0.0;

// ---------------- utilitários ----------------
template <typename C> inline auto size(const C& x) -> SizeType<C> { return x.size(); }
template <typename C> inline auto empty(const C& x) -> Boolean { return x.empty(); }

template <typename T> inline auto first_element(const Set<T>& x) -> T { return *(x.begin()); }
template <typename T> inline auto remove(Set<T>& x, const T& e) -> Set<T>& { x.erase(e); return x; }
template <typename T> inline auto push(Set<T>& x, const T& e) -> Set<T>& { x.insert(e); return x; }

inline auto is_prefix(const String& a, const String& b) -> Boolean {
    if (size(a) > size(b)) return false;
    return std::mismatch(a.begin(), a.end(), b.begin()).first == a.end();
}

inline auto remove_prefix(const String& x, SizeType<String> n) -> String {
    return (n < size(x)) ? x.substr(n) : String();
}

auto all_suffixes(const String& x) -> Set<String> {
    Set<String> ss;
    if (empty(x)) return ss;
    for (SizeType<String> i = 0; i < size(x); ++i) ss.insert(x.substr(i)); // inclui i=0
    return ss;
}

auto commom_suffix_and_prefix(const String& a, const String& b) -> String {
    if (empty(a) || empty(b)) return "";
    String x = "";
    for (const String& s : all_suffixes(a)) {
        if (is_prefix(s, b) && size(s) > size(x)) x = s;
    }
    return x;
}

inline auto overlap_value(const String& s, const String& t) -> SizeType<String> {
    return size(commom_suffix_and_prefix(s, t));
}

auto overlap(const String& s, const String& t) -> String {
    String c = commom_suffix_and_prefix(s, t);
    return s + remove_prefix(t, size(c));
}

// ---------------- construir pares (i!=j) ----------------
static std::vector<Pair<String,String>>
make_pairs_parallel(const std::vector<String>& base) {
    std::vector<Pair<String,String>> vp;

#ifdef _OPENMP
    #pragma omp parallel
    {
        std::vector<Pair<String,String>> local;
        local.reserve(base.size()*2);

        #pragma omp for schedule(static) nowait
        for (int i = 0; i < (int)base.size(); ++i) {
            for (int j = 0; j < (int)base.size(); ++j) {
                if (i == j) continue;
                local.emplace_back(base[(size_t)i], base[(size_t)j]);
            }
        }
        #pragma omp critical
        {
            if (!local.empty()) {
                auto old = vp.size();
                vp.resize(old + local.size());
                std::move(local.begin(), local.end(), vp.begin() + (std::ptrdiff_t)old);
            }
        }
    }
#else
    for (size_t i = 0; i < base.size(); ++i) {
        for (size_t j = 0; j < base.size(); ++j) {
            if (i == j) continue;
            vp.emplace_back(base[i], base[j]);
        }
    }
#endif
    return vp;
}

// ---------------- redução/“melhor par” ----------------
struct Best {
    size_t ov;
    String a, b;
    bool has;
};
static inline bool better(const Best& x, const Best& y) {
    if (!y.has) return x.has;
    if (!x.has) return false;
    if (x.ov != y.ov) return x.ov > y.ov;
    if (x.a  != y.a ) return x.a  < y.a;
    return x.b < y.b;
}

#ifdef _OPENMP
  #pragma omp declare reduction( bestred : Best : \
      omp_out = better(omp_in, omp_out) ? omp_in : omp_out ) \
      initializer( omp_priv = Best{0, String(), String(), false} )
#endif

static Pair<String, String>
highest_overlap_from_vector_parallel(const std::vector<Pair<String,String>>& vp) {
#ifdef _OPENMP
    double t0 = 0.0, t1 = 0.0;
    Best global_best{0, "", "", false};

    #pragma omp parallel reduction(bestred:global_best)
    {
        #pragma omp single
        t0 = omp_get_wtime();

        #pragma omp for schedule(static)
        for (int i = 0; i < (int)vp.size(); ++i) {
            const auto& p = vp[(size_t)i];
            size_t ov = overlap_value(p.first, p.second);
            Best cand{ov, p.first, p.second, true};
            if (better(cand, global_best)) {
                global_best = cand; // acumulador privado
            }
        }

        #pragma omp single
        {
            t1 = omp_get_wtime();
            global_parallel_time += (t1 - t0); // só o miolo paralelizável
        }
    }
    return {global_best.a, global_best.b};
#else
    using clock = std::chrono::high_resolution_clock;
    auto t0 = clock::now();

    Best best{0, "", "", false};
    for (size_t i = 0; i < vp.size(); ++i) {
        const auto& p = vp[i];
        size_t ov = overlap_value(p.first, p.second);
        Best cand{ov, p.first, p.second, true};
        if (better(cand, best)) best = cand;
    }

    auto t1 = clock::now();
    global_parallel_time += std::chrono::duration<double>(t1 - t0).count(); // mede o mesmo miolo

    return {best.a, best.b};
#endif
}

// ---------------- guloso ----------------
inline auto pop_two_elements_and_push_overlap(Set<String>& ss, const Pair<String, String>& p) -> Set<String>& {
    ss = remove(ss, p.first);
    ss = remove(ss, p.second);
    ss = push(ss, overlap(p.first, p.second));
    return ss;
}

auto pair_of_strings_with_highest_overlap_value_parallel(const Set<String>& ss) -> Pair<String, String> {
    std::vector<String> base(ss.begin(), ss.end());   // conversão sequencial
    auto vp = make_pairs_parallel(base);              // construção dos pares (não conta no “paralelizável”)
    return highest_overlap_from_vector_parallel(vp);  // varredura (conta no “paralelizável”)
}

auto shortest_superstring_parallel_fiel(Set<String> t) -> String {
    if (empty(t)) return "";
    while (t.size() > 1) {
        t = pop_two_elements_and_push_overlap(
            t, pair_of_strings_with_highest_overlap_value_parallel(t)
        );
    }
    return first_element(t);
}

// ---------------- I/O + medição ----------------
inline auto read_size(InStream& in) -> Size { Size n; in >> n; return n; }
inline auto read_string(InStream& in) -> String { String s; in >> s; return s; }
auto read_strings_from_standard_input() -> Set<String> {
    using N = SizeType<Set<String>>;
    Set<String> x;
    N n = N(read_size(standard_input));
    while (n--) x.insert(read_string(standard_input));
    return x;
}

int main() {
    Set<String> ss = read_strings_from_standard_input();

    global_parallel_time = 0.0;

    auto start = std::chrono::high_resolution_clock::now();
    String super = shortest_superstring_parallel_fiel(ss);
    auto end = std::chrono::high_resolution_clock::now();

    double total = std::chrono::duration<double>(end - start).count();
    double seq_frac = 1.0 - (global_parallel_time / total);

    std::cout << super << "\n";
    std::cerr << total << " " << global_parallel_time << " " << seq_frac << "\n";
    return 0;
}