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

template <typename T, typename U>
using Pair = std::pair<T, U>;

template <typename T, typename C = std::less<T>>
using Set = std::set<T>;

template <typename T>
using SizeType = typename T::size_type;

/* ======== utilitários ======== */
template <typename C> inline auto size(const C& x) -> SizeType<C> { return x.size(); }
template <typename C> inline auto at_least_two_elements_in(const C& c) -> Boolean { return size(c) > SizeType<C>(1); }
template <typename T> inline auto first_element(const Set<T>& x) -> T { return *(x.begin()); }
template <typename T> inline auto remove(Set<T>& x, const T& e) -> Set<T>& { x.erase(e); return x; }
template <typename T> inline auto push(Set<T>& x, const T& e) -> Set<T>& { x.insert(e); return x; }
template <typename C> inline auto empty(const C& x) -> Boolean { return x.empty(); }

/* ======== lógica ======== */
Boolean is_prefix(const String& a, const String& b)
{
    if (size(a) > size(b)) return false;
    if (!(std::mismatch(a.begin(), a.end(), b.begin()).first == a.end())) return false;
    return true;
}

inline auto suffix_from_position(const String& x, SizeType<String> i) -> String { return x.substr(i); }
inline auto remove_prefix(const String& x, SizeType<String> n) -> String { return size(x) > n ? suffix_from_position(x, n) : x; }

auto all_suffixes(const String& x) -> Set<String>
{
    Set<String> ss;
    SizeType<String> n = size(x);
    while (--n) ss.insert(x.substr(n));
    return ss;
}

auto commom_suffix_and_prefix(const String& a, const String& b) -> String
{
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

inline auto pop_two_elements_and_push_overlap(Set<String>& ss, const Pair<String,String>& p) -> Set<String>& {
    ss = remove(ss, p.first);
    ss = remove(ss, p.second);
    ss = push(ss, overlap(p.first, p.second));
    return ss;
}

/* ======== medição ======== */
static double global_parallel_time = 0.0;

/* ======== versão paralela: all_distinct_pairs ======== */
auto all_distinct_pairs(const Set<String>& ss) -> Set<Pair<String,String>>
{
    std::vector<String> domain(ss.begin(), ss.end());
    const Size n = domain.size();

    int nthreads = 1;
    #ifdef _OPENMP
    #pragma omp parallel
    {
        #pragma omp single
        { nthreads = omp_get_num_threads(); }
    }
    #endif

    std::vector<std::vector<Pair<String,String>>> buckets(nthreads);

    auto t0 = std::chrono::high_resolution_clock::now();
    #ifdef _OPENMP
    #pragma omp parallel
    {
        const int tid = omp_get_thread_num();
        auto& local = buckets[tid];
        if (nthreads > 0) local.reserve((n > 1 ? (n*(n-1))/nthreads : 0));

        #pragma omp for schedule(static)
        for (Size i = 0; i < n; ++i) {
            const String& s1 = domain[i];
            for (Size j = 0; j < n; ++j) {
                if (i == j) continue;
                local.emplace_back(s1, domain[j]);
            }
        }
    }
    #else
    for (Size i = 0; i < n; ++i) {
        const String& s1 = domain[i];
        for (Size j = 0; j < n; ++j)
            if (i != j) buckets[0].emplace_back(s1, domain[j]);
    }
    #endif
    auto t1 = std::chrono::high_resolution_clock::now();
    global_parallel_time += std::chrono::duration<double>(t1 - t0).count();

    std::vector<Pair<String,String>> all;
    Size total = 0;
    for (auto& v : buckets) total += v.size();
    all.reserve(total);
    for (auto& v : buckets) all.insert(all.end(), v.begin(), v.end());

    return Set<Pair<String,String>>(all.begin(), all.end());
}

/* ======== versão paralela: highest_overlap_value ======== */
auto highest_overlap_value(const Set<Pair<String,String>>& sp) -> Pair<String,String>
{
    Pair<String,String> best_pair = first_element(sp);
    SizeType<String> best_val = overlap_value(best_pair.first, best_pair.second);
    std::vector<Pair<String,String>> pairs(sp.begin(), sp.end());
    const Size m = pairs.size();

    auto t0 = std::chrono::high_resolution_clock::now();
    #ifdef _OPENMP
    #pragma omp parallel
    {
        Pair<String,String> local_pair = best_pair;
        SizeType<String> local_val = best_val;

        #pragma omp for schedule(static) nowait
        for (Size i = 0; i < m; ++i) {
            const auto& p = pairs[i];
            auto v = overlap_value(p.first, p.second);
            if (v > local_val) { local_val = v; local_pair = p; }
        }

        #pragma omp critical
        {
            if (local_val > best_val) { best_val = local_val; best_pair = local_pair; }
        }
    }
    #else
    for (Size i = 0; i < m; ++i) {
        const auto& p = pairs[i];
        auto v = overlap_value(p.first, p.second);
        if (v > best_val) { best_val = v; best_pair = p; }
    }
    #endif
    auto t1 = std::chrono::high_resolution_clock::now();
    global_parallel_time += std::chrono::duration<double>(t1 - t0).count();

    return best_pair;
}

auto pair_of_strings_with_highest_overlap_value(const Set<String>& ss) -> Pair<String,String> {
    return highest_overlap_value(all_distinct_pairs(ss));
}

auto shortest_superstring(Set<String> t) -> String {
    if (empty(t)) return "";
    while (at_least_two_elements_in(t))
        t = pop_two_elements_and_push_overlap(t, pair_of_strings_with_highest_overlap_value(t));
    return first_element(t);
}

/* ======== IO ======== */
inline auto read_size(std::istream& in) -> Size { Size n; in >> n; return n; }
inline auto read_string(std::istream& in) -> String { String s; in >> s; return s; }
auto read_strings_from_standard_input() -> Set<String>
{
    Set<String> x; Size n = read_size(standard_input);
    while (n--) x.insert(read_string(standard_input));
    return x;
}
inline auto write_string_to_standard_ouput(const String& s) -> void { standard_output << s << std::endl; }

/* ======== main ======== */
auto main(int /*argc*/, char const* /*argv*/[]) -> int
{
    Set<String> ss = read_strings_from_standard_input();

    global_parallel_time = 0.0; // zera acumulador

    auto t0 = std::chrono::high_resolution_clock::now();
    String super = shortest_superstring(ss);
    auto t1 = std::chrono::high_resolution_clock::now();

    write_string_to_standard_ouput(super);

    std::chrono::duration<double> dt = t1 - t0;
    const double total = dt.count();
    const double seq_frac = (total > 0.0) ? (1.0 - (global_parallel_time / total)) : 0.0;

    std::cerr << total << " " << global_parallel_time << " " << seq_frac << "\n";
    return 0;
}