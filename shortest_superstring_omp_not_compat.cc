#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <chrono> // medição de tempo
#include <vector>

#ifdef _OPENMP
  #include <omp.h>
#endif

#define standard_input  std::cin
#define standard_output std::cout

using Boolean = bool ;
using Size    = std::size_t ;
using String  = std::string ;

using InStream  = std::istream ;
using OutStream = std::ostream ;

template <typename T, typename U>
using Pair = std::pair <T, U> ;

template <typename T, typename C = std::less <T>>
using Set = std::set <T> ;

template <typename T>
using SizeType = typename T :: size_type ;

template <typename C> inline auto size (const C& x) -> SizeType <C> { return x.size (); }
template <typename C> inline auto at_least_two_elements_in (const C& c) -> Boolean { return size (c) > SizeType <C> (1) ; }
template <typename T> inline auto first_element (const Set <T>& x) -> T { return *(x.begin ()) ; }
template <typename T> inline auto remove (Set <T>& x, const T& e) -> Set <T>& { x.erase (e) ; return x ; }
template <typename T> inline auto push   (Set <T>& x, const T& e) -> Set <T>& { x.insert (e) ; return x ; }
template <typename C> inline auto empty (const C& x) -> Boolean { return x.empty () ; }

Boolean is_prefix (const String& a, const String& b) {
    if (size (a) > size (b)) return false ;
    if (!( std::mismatch( a.begin(), a.end(), b.begin() ).first == a.end() )) return false ;
    return true ;
}
inline auto suffix_from_position (const String& x, SizeType <String> i) -> String { return x.substr (i) ; }
inline auto remove_prefix (const String& x, SizeType <String> n) -> String {
    if (n >= size(x)) return String(); // correção
    return suffix_from_position (x, n) ;
}
auto all_suffixes (const String& x) -> Set <String> {
    Set <String> ss ;
    if (empty(x)) return ss ;
    for (SizeType<String> i = 0; i < size(x); ++i) ss.insert(x.substr(i));
    return ss ;
}
auto commom_suffix_and_prefix (const String& a, const String& b) -> String {
    if (empty (a) || empty (b)) return "" ;
    String x = "" ;
    for (const String& s : all_suffixes (a)) {
        if (is_prefix (s, b) && size (s) > size (x)) x = s ;
    }
    return x ;
}
inline auto overlap_value (const String& s, const String& t) -> SizeType <String> {
    return size (commom_suffix_and_prefix (s, t)) ;
}
auto overlap (const String& s, const String& t) -> String {
    String c = commom_suffix_and_prefix (s, t) ;
    return s + remove_prefix (t, size (c)) ;
}
inline auto pop_two_elements_and_push_overlap (Set <String>& ss, const Pair <String, String>& p) -> Set <String>& {
    ss = remove (ss, p.first)  ;
    ss = remove (ss, p.second) ;
    ss = push   (ss, overlap (p.first, p.second)) ;
    return ss ;
}

/* ========= PARTE NOVA: paralelização + medição do trecho paralelizável ========= */

struct BestPick {
    size_t ov;
    String a, b;
    bool has;
};
static inline bool better_pick(const BestPick& x, const BestPick& y) {
    if (!y.has) return x.has;
    if (!x.has) return false;
    if (x.ov != y.ov) return x.ov > y.ov;
    if (x.a  != y.a ) return x.a  < y.a;
    return x.b < y.b;
}

#ifdef _OPENMP
  #pragma omp declare reduction( bestpair : BestPick : \
      omp_out = better_pick(omp_in, omp_out) ? omp_in : omp_out ) \
      initializer( omp_priv = BestPick{0, String(), String(), false} )
#endif

// acumulador do “tempo paralelizável” (só o laço que busca o melhor par)
static double global_parallel_time = 0.0;

auto pair_of_strings_with_highest_overlap_value (const Set <String>& ss) -> Pair <String, String>
{
    std::vector<String> v; v.reserve(ss.size());
    for (const auto& s : ss) v.push_back(s);
    const int n = (int)v.size();
    if (n < 2) return { v[0], v[0] };

#ifdef _OPENMP
    BestPick best{0, "", "", false};
    double t0=0.0, t1=0.0;

    #pragma omp parallel reduction(bestpair:best)
    {
        #pragma omp single
        t0 = omp_get_wtime();

        #pragma omp for collapse(2) schedule(static)
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                if (i == j) continue;
                const String& a = v[(size_t)i];
                const String& b = v[(size_t)j];
                size_t ov = overlap_value(a, b);
                BestPick cand{ov, a, b, true};
                if (better_pick(cand, best)) best = cand;
            }
        }

        #pragma omp single
        {
            t1 = omp_get_wtime();
            global_parallel_time += (t1 - t0); // soma só o miolo paralelizável
        }
    }
    return {best.a, best.b};
#else
    using clock = std::chrono::high_resolution_clock;
    auto t0 = clock::now();

    BestPick best{0, "", "", false};
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            const String& a = v[(size_t)i];
            const String& b = v[(size_t)j];
            size_t ov = overlap_value(a, b);
            BestPick cand{ov, a, b, true};
            if (better_pick(cand, best)) best = cand;
        }
    }

    auto t1 = clock::now();
    global_parallel_time += std::chrono::duration<double>(t1 - t0).count(); // mesmo trecho no serial

    return {best.a, best.b};
#endif
}

/* ========= FIM DA PARTE NOVA ========= */

auto shortest_superstring (Set <String> t) -> String
{
    if (empty (t)) return "" ;
    while (at_least_two_elements_in (t)) {
        t = pop_two_elements_and_push_overlap
            ( t
            , pair_of_strings_with_highest_overlap_value (t) ) ;
    }
    return first_element (t) ;
}

inline auto write_string_and_break_line (OutStream& out, String s) -> void { out << s << std::endl ; }
inline auto read_size   (InStream& in) -> Size   { Size n ; in >>  n ; return n ; }
inline auto read_string (InStream& in) -> String { String s ; in >>  s ; return s ; }

auto read_strings_from_standard_input () -> Set <String>
{
    using N = SizeType <Set <String>> ;
    Set <String> x ;
    N n = N (read_size (standard_input)) ;
    while (n --) x.insert (read_string (standard_input)) ;
    return x ;
}

inline auto write_string_to_standard_ouput (const String& s) -> void { write_string_and_break_line (standard_output, s) ; }

auto main (int /*argc*/, char const* /*argv*/[]) -> int
{
    Set<String> ss = read_strings_from_standard_input();

    global_parallel_time = 0.0; // zera acumulador

    auto t0 = std::chrono::high_resolution_clock::now();
    String super = shortest_superstring(ss);
    auto t1 = std::chrono::high_resolution_clock::now();

    write_string_to_standard_ouput(super);
    std::chrono::duration<double> dt = t1-t0;
    // mesma linha de sempre em STDERR: total, tempo_paralelizavel, frac_seq
    const double total = dt.count();
    const double seq_frac = (total > 0.0) ? (1.0 - (global_parallel_time / total)) : 0.0;
    std::cerr << total << " " << global_parallel_time << " " << seq_frac << "\n";

    return 0;
}

/* seq frac output
╰─$ ./get_seq_frac.sh
===========================================
Média do tempo TOTAL (s):       11.295580
Média do tempo SEQUENCIAL (s):  0.132678
Porcentagem SEQUENCIAL média:   1.17%
===========================================
╭─meyer@mey ~/d/p6/paralela/omp ‹main●› 
╰─$ ./get_seq_frac.sh
===========================================
Média do tempo TOTAL (s):       11.342800
Média do tempo SEQUENCIAL (s):  0.132345
Porcentagem SEQUENCIAL média:   1.17%
===========================================
*/