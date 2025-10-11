#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <chrono>
#include <omp.h>
#include <vector>  // <-- inclusão necessária

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

inline auto remove_prefix(const String& x, SizeType<String> n) -> String {
    if (n >= size(x)) return String();
    return suffix_from_position(x, n);
}

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

// ---------------- Comparador para empates ----------------
static inline bool better_tiebreak(size_t ov1, const String& a1, const String& b1,
                                   size_t ov2, const String& a2, const String& b2) {
    if (ov1 != ov2) return ov1 > ov2;
    if (a1 != a2)   return a1 < a2;
    return b1 < b2;
}

// ---------------- Parte paralelizada ----------------
auto highest_overlap_value_parallel(const Set<Pair<String, String>>& sp) -> Pair<String, String> {
    std::vector<Pair<String, String>> vp(sp.begin(), sp.end());
    if (vp.empty()) return *sp.begin();

    Pair<String, String> best = vp[0];
    size_t best_val = overlap_value(best.first, best.second);

    #pragma omp parallel
    {
        Pair<String, String> lbest = best;
        size_t lval = best_val;

        #pragma omp for schedule(static) nowait
        for (int i = 0; i < (int)vp.size(); ++i) {
            const auto& p = vp[i];
            size_t ov = overlap_value(p.first, p.second);
            if (better_tiebreak(ov, p.first, p.second, lval, lbest.first, lbest.second)) {
                lbest = p; lval = ov;
            }
        }

        #pragma omp critical
        {
            if (better_tiebreak(lval, lbest.first, lbest.second, best_val, best.first, best.second)) {
                best = lbest; best_val = lval;
            }
        }
    }

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
inline auto write_string_and_break_line(OutStream& out, String s) -> void { out << s << std::endl; }
inline auto read_size(InStream& in) -> Size { Size n; in >> n; return n; }
inline auto read_string(InStream& in) -> String { String s; in >> s; return s; }

auto read_strings_from_standard_input() -> Set<String> {
    using N = SizeType<Set<String>>;
    Set<String> x;
    N n = N(read_size(standard_input));
    while (n--) x.insert(read_string(standard_input));
    return x;
}

inline auto write_string_to_standard_ouput(const String& s) -> void { write_string_and_break_line(standard_output, s); }

auto main(int, char const*[]) -> int {
    Set<String> ss = read_strings_from_standard_input();

    auto t0 = std::chrono::high_resolution_clock::now();
    String super = shortest_superstring_parallel_fiel(ss);
    auto t1 = std::chrono::high_resolution_clock::now();

    write_string_to_standard_ouput(super);

    std::chrono::duration<double> dt = t1 - t0;
    standard_output.setf(std::ios::fixed);
    standard_output << dt.count() << std::endl;

    return 0;
}
