
#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <omp.h>
#include <chrono>

using ll = long long;
using OverlapSize = std::string::size_type;

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

double global_paralel_time = 0.0;

template <typename C> inline auto
size (const C& x) -> SizeType <C> { 
    return x.size (); 
}

template <typename C> inline auto
at_least_two_elements_in (const C& c) -> Boolean 
{ 
    return size (c) > SizeType <C> (1); 
}

template <typename T> inline auto
first_element (const Set <T>& x) -> T 
{ 
    return *(x.begin ()); 
}

template <typename T> inline auto
second_element (const Set <T>& x) -> T 
{ 
    return *(std::next (x.begin ()));
 }

template <typename T> inline auto
remove (Set <T>& x, const T& e) -> Set <T>& 
{ 
    x.erase (e);
    return x; 
}

template <typename T> inline auto
push (Set <T>& x, const T& e) -> Set <T>& 
{ 
    x.insert (e); 
    return x; 
}

template <typename C> inline auto
empty (const C& x) -> Boolean 
{ 
    return x.empty (); 
}

Boolean is_prefix (const String& a, const String& b)
{
    if (size (a) > size (b)) return false;
    if (! (std::mismatch(a.begin(), a.end(), b.begin()).first == a.end())) return false;
    return true;
}

inline auto suffix_from_position (const String& x, SizeType <String> i) -> String 
{ 
    return x.substr (i); 
}

inline auto remove_prefix (const String& x, SizeType <String> n) -> String
{
    if (size (x) > n) return suffix_from_position (x, n);
    return x;
}

auto all_suffixes (const String& x) -> Set <String>
{
    Set <String> ss;
    SizeType <String> n = size (x);
    while (--n) { ss.insert (x.substr (n)); }
    return ss;
}

auto commom_suffix_and_prefix (const String& a, const String& b) -> String
{
    if (empty (a) || empty (b)) return "";
    String x = "";
    for (const String& s : all_suffixes (a)) {
        if (is_prefix (s, b) && size (s) > size (x)) x = s;
    }
    return x;
}

inline auto overlap_value (const String& s, const String& t) -> SizeType <String>
{
    return size (commom_suffix_and_prefix (s, t));
}

auto overlap (const String& s, const String& t) -> String
{
    String c = commom_suffix_and_prefix (s, t);
    return s + remove_prefix (t, size (c));
}

inline auto pop_two_elements_and_push_overlap (Set <String>& ss, const Pair <String, String>& p) -> Set <String>&
{
    ss = remove (ss, p.first);
    ss = remove (ss, p.second);
    ss = push   (ss, overlap (p.first, p.second));
    return ss;
}

auto all_distinct_pairs (const Set <String>& ss) -> Set <Pair <String, String>>
{
    Set <Pair <String, String>> x;
    for (const String& s1 : ss) {
        for (const String& s2 : ss) {
            if (s1 != s2) x.insert (std::make_pair (s1, s2));
        }
    }
    return x;
}

auto highest_overlap_value (const Set <Pair <String, String>>& sp) -> Pair <String, String>
{
    Pair <String, String> x = first_element (sp);
    for (const Pair <String, String>& p : sp) {
        if (overlap_value (p.first, p.second) > overlap_value (x.first, x.second)) {
            x = p;
        }
    }
    return x;
}

static inline bool lex_compare(const Pair<String,String>& a, const Pair<String,String>& b) {
    return (a.first < b.first) || (a.first == b.first && a.second < b.second);
}


static auto find_best_overlap_pair(const std::vector<String>& v) -> Pair<String,String> {
    Pair<String,String> best_pair = { v[0], v[1] };
    OverlapSize best_ov = overlap_value(best_pair.first, best_pair.second);
    auto tstart = std::chrono::high_resolution_clock::now();

#ifdef _OPENMP //parte paralelizada
    #pragma omp parallel
    {
        Pair<String,String> local_pair = best_pair;
        OverlapSize local_ov = best_ov;

        #pragma omp for schedule(dynamic) collapse(2) //collapse(2) - melhora muito o speedup por algum motivo, rodar sem de noite por backup
        for (ll i = 0; i < (ll)v.size(); ++i) {
            for (ll j = 0; j < (ll)v.size(); ++j) {
                if (i == j) continue;
                OverlapSize ov = overlap_value(v[(size_t)i], v[(size_t)j]);
                Pair<String,String> cand = { v[(size_t)i], v[(size_t)j] };
                if (ov > local_ov || (ov == local_ov && lex_compare(cand, local_pair))) {
                    local_ov = ov;
                    local_pair = cand;
                }
            }
        }

        #pragma omp critical
        {
            if (local_ov > best_ov || (local_ov == best_ov && lex_compare(local_pair, best_pair))) {
                best_ov = local_ov;
                best_pair = local_pair;
            }
        }
    }
#else //parte sequencial
     for (ll i = 0; i < (ll)v.size(); ++i) {
        for (ll j = 0; j < (ll)v.size(); ++j) {
            if (i == j) continue;
            OverlapSize ov = overlap_value(v[i], v[j]);
            Pair<String,String> cand = { v[i], v[j] };
            if (ov > best_ov || (ov == best_ov && lex_compare(cand, best_pair))) {
                best_ov = ov;
                best_pair = cand;
            }
        }
    }
#endif

    auto tend = std::chrono::high_resolution_clock::now();
    global_paralel_time += std::chrono::duration<double>(tend - tstart).count();
    return best_pair;
}

auto pair_of_strings_with_highest_overlap_value (const Set <String>& ss) -> Pair <String, String>
{
    std::vector<String> v(ss.begin(), ss.end());

    if (v.size () < 2) return v.empty() ? Pair<String,String>{"",""} : Pair<String,String>{v[0], v[0]};

    return find_best_overlap_pair(v); 
}

auto
shortest_superstring (Set <String> t) -> String
{
    if (empty (t)) return "" ;
    while (at_least_two_elements_in (t)) {
        t = pop_two_elements_and_push_overlap
            ( t
            , pair_of_strings_with_highest_overlap_value (t) ) ;
    }
    return first_element (t) ;
}

inline auto write_string_and_break_line (OutStream& out, String s) -> void 
{ 
    out << s << std::endl; 
}

inline auto read_size (InStream& in) -> Size 
{ 
    Size n; 
    in >> n; 
    return n; 
}

inline auto read_string (InStream& in) -> String { 
    String s; 
    in >> s; 
    return s; 
}

auto read_strings_from_standard_input () -> Set <String>
{
    using N = SizeType <Set <String>>;
    Set <String> x;
    N n = N (read_size (standard_input));
    while (n--) x.insert (read_string (standard_input));
    return x;
}

inline auto write_string_to_standard_ouput (const String& s) -> void 
{ 
    write_string_and_break_line (standard_output, s); 
}

auto main () -> int
{
    auto start = std::chrono::high_resolution_clock::now();
    Set <String> ss = read_strings_from_standard_input ();
    write_string_to_standard_ouput (shortest_superstring (ss));
    auto end = std::chrono::high_resolution_clock::now();

    double total = std::chrono::duration<double>(end - start).count();

    std::cerr << total << " " << global_paralel_time << " " << 1.0 - (global_paralel_time / total) << "\n";
    return 0;
}