#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <chrono>
#include <omp.h>
#include <vector>

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

// ---------------- Funções auxiliares (mesmas do sequencial) ----------------
template <typename C> inline auto size(const C& x) -> SizeType<C> { return x.size(); }
template <typename C> inline auto empty(const C& x) -> Boolean { return x.empty(); }

template <typename T> inline auto first_element(const Set<T>& x) -> T { return *(x.begin()); }
template <typename T> inline auto remove(Set<T>& x, const T& e) -> Set<T>& { x.erase(e); return x; }
template <typename T> inline auto push(Set<T>& x, const T& e) -> Set<T>& { x.insert(e); return x; }

inline auto is_prefix(const String& a, const String& b) -> Boolean {
    if (size(a) > size(b)) return false;
    return std::mismatch(a.begin(), a.end(), b.begin()).first == a.end();
}

inline auto suffix_from_position(const String& x, SizeType<String> i) -> String { return x.substr(i); }
inline auto remove_prefix(const String& x, SizeType<String> n) -> String { return (n < size(x)) ? x.substr(n) : String(); }

auto all_suffixes(const String& x) -> Set<String> {
    Set<String> ss;
    if (empty(x)) return ss;
    for (SizeType<String> i = 0; i < size(x); ++i) ss.insert(x.substr(i));
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

auto all_distinct_pairs(const Set<String>& ss) -> Set<Pair<String, String>> {
    Set<Pair<String, String>> x;
    for (const String& s1 : ss)
        for (const String& s2 : ss)
            if (s1 != s2) x.insert(std::make_pair(s1, s2));
    return x;
}

// ---------------- Comparador ----------------
static inline bool better_tiebreak(size_t ov1, const String& a1, const String& b1,
                                   size_t ov2, const String& a2, const String& b2) {
    if (ov1 != ov2) return ov1 > ov2;
    if (a1 != a2)   return a1 < a2;
    return b1 < b2;
}

// ---------------- Parte paralelizada ----------------
auto highest_overlap_value_parallel(const Set<Pair<String, String>>& sp) -> Pair<String, String> {
    using namespace std::chrono;
    auto t0 = high_resolution_clock::now();

    std::vector<Pair<String, String>> vp(sp.begin(), sp.end());
    Pair<String, String> best = vp[0];
    size_t best_val = overlap_value(best.first, best.second);

    #pragma omp parallel
    {
        Pair<String, String> local_best = best;
        size_t local_val = best_val;

        #pragma omp for schedule(static) nowait
        for (int i = 0; i < (int)vp.size(); ++i) {
            const auto& p = vp[i];
            size_t ov = overlap_value(p.first, p.second);
            if (better_tiebreak(ov, p.first, p.second, local_val, local_best.first, local_best.second)) {
                local_val = ov;
                local_best = p;
            }
        }

        #pragma omp critical
        {
            if (better_tiebreak(local_val, local_best.first, local_best.second, best_val, best.first, best.second)) {
                best = local_best; best_val = local_val;
            }
        }
    }

    auto t1 = high_resolution_clock::now();
    global_parallel_time += duration<double>(t1 - t0).count();

    return best;
}

inline auto pop_two_elements_and_push_overlap(Set<String>& ss, const Pair<String, String>& p) -> Set<String>& {
    ss = remove(ss, p.first);
    ss = remove(ss, p.second);
    ss = push(ss, overlap(p.first, p.second));
    return ss;
}

auto pair_of_strings_with_highest_overlap_value_parallel(const Set<String>& ss) -> Pair<String, String> {
    return highest_overlap_value_parallel(all_distinct_pairs(ss));
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

// ---------------- I/O e medição ----------------
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

    auto start = std::chrono::high_resolution_clock::now();
    String super = shortest_superstring_parallel_fiel(ss);
    auto end = std::chrono::high_resolution_clock::now();

    double total = std::chrono::duration<double>(end - start).count();
    double seq_frac = 1.0 - (global_parallel_time / total);

    std::cout << super << "\n";
    std::cerr << total << " " << global_parallel_time << " " << seq_frac << "\n";

    return 0;
}
