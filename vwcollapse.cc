#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <utility>
#include <chrono> // medição de tempo
#include <mpi.h>
#include <cstring> // memcpy
#include <vector>

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

inline auto
write_string_and_break_line (OutStream& out, String s) -> void
{
    out << s << std::endl ;
}

inline auto
read_size (InStream& in) -> Size
{
    Size n ;
    in >>  n ;
    return n ;
}

inline auto
read_string (InStream& in) -> String
{
    String s ;
    in >>  s ;
    return s ;
}

auto
read_strings_from_standard_input () -> Set <String>
{
    using N = SizeType <Set <String>> ;
    Set <String> x ;
    N n = N (read_size (standard_input)) ;
    while (n --) x.insert (read_string (standard_input)) ;
    return x ;
}

inline auto
write_string_to_standard_ouput (const String& s) -> void
{
    write_string_and_break_line (standard_output, s) ;
}


// ---------- suas funções originais (inalteradas) ----------
template <typename C> inline auto size (const C& x) -> SizeType <C> { return x.size (); }
template <typename C> inline auto at_least_two_elements_in (const C& c) -> Boolean { return size (c) > SizeType <C> (1) ; }
template <typename T> inline auto first_element (const Set <T>& x) -> T { return *(x.begin ()) ; }
template <typename T> inline auto second_element (const Set <T>& x) -> T { return *(std::next (x.begin ())) ; }
template <typename T> inline auto remove (Set <T>& x, const T& e) -> Set <T>& { x.erase (e) ; return x ; }
template <typename T> inline auto push (Set <T>& x, const T& e) -> Set <T>& { x.insert (e) ; return x ; }
template <typename C> inline auto empty (const C& x) -> Boolean { return x.empty () ; }

Boolean is_prefix (const String& a, const String& b)
{
    if (size (a) > size (b)) return false ;
    if (!( std::mismatch( a.begin (), a.end (), b.begin () ).first == a.end () )) return false ;
    return true ;
}

inline auto suffix_from_position (const String& x, SizeType <String> i) -> String { return x.substr (i) ; }

inline auto remove_prefix (const String& x, SizeType <String> n) -> String
{
    if (size (x) > n) return suffix_from_position (x, n) ;
    return x ;
}

auto all_suffixes (const String& x) -> Set <String>
{
    Set <String> ss ;
    SizeType <String> n = size (x) ;
    while (-- n) { ss.insert (x.substr (n)) ; }
    return ss ;
}

auto commom_suffix_and_prefix (const String& a, const String& b) -> String
{
    if (empty (a)) return "" ;
    if (empty (b)) return "" ;
    String x = "" ;
    for (const String& s : all_suffixes (a)) {
        if (is_prefix (s, b) && size (s) > size (x)) x = s ;
    }
    return x ;
}

inline auto overlap_value (const String& s, const String& t) -> SizeType <String>
{
    return size (commom_suffix_and_prefix (s, t)) ;
}

auto overlap (const String& s, const String& t) -> String
{
    String c = commom_suffix_and_prefix (s, t) ;
    return s + remove_prefix (t, size (c)) ;
}

inline auto pop_two_elements_and_push_overlap
        (Set <String>& ss, const Pair <String, String>& p) -> Set <String>&
{
    ss = remove (ss, p.first)  ;
    ss = remove (ss, p.second) ;
    ss = push   (ss, overlap (p.first, p.second)) ;
    return ss ;
}

// ---------- helpers MPI ----------
static void mpi_bcast_strings(std::vector<String>& v, int root, MPI_Comm comm) {
    int rank; MPI_Comm_rank(comm, &rank);

    int n = (int)v.size();
    MPI_Bcast(&n, 1, MPI_INT, root, comm);
    if (rank != root) v.resize(n);

    std::vector<int> lens(n);
    if (rank == root) for (int i=0;i<n;++i) lens[i] = (int)v[i].size();
    MPI_Bcast(lens.data(), n, MPI_INT, root, comm);

    size_t total = 0; for (int i=0;i<n;++i) total += (size_t)lens[i];
    std::vector<char> buf(total);

    if (rank == root) {
        size_t off=0;
        for (int i=0;i<n;++i) { std::memcpy(buf.data()+off, v[i].data(), lens[i]); off += lens[i]; }
    }
    MPI_Bcast(buf.data(), (int)buf.size(), MPI_CHAR, root, comm);

    if (rank != root) {
        size_t off=0;
        for (int i=0;i<n;++i) { v[i].assign(buf.data()+off, lens[i]); off += lens[i]; }
    }
}

static void mpi_bcast_set(Set<String>& s, int root, MPI_Comm comm) {
    std::vector<String> v(s.begin(), s.end());  // set já é ordenado
    mpi_bcast_strings(v, root, comm);
    int rank; MPI_Comm_rank(comm, &rank);
    if (rank != root) {
        s.clear();
        for (auto& x: v) s.insert(std::move(x));
    }
}

// mapeia índice linear k -> (i,j) com i!=j
static inline void linear_to_pair(long long k, int n, int& i, int& j) {
    i = (int)(k / (n - 1));
    int r = (int)(k % (n - 1));
    j = (r < i) ? r : (r + 1);
}

// ---------- ADAPTAÇÃO MPI nas suas funções “externamente iguais” ----------

// Gera APENAS a fatia local de pares (mantém assinatura e retorno)
auto all_distinct_pairs (const Set <String>& ss) -> Set <Pair <String, String>>
{
    // Convertemos para vetor para indexar
    std::vector<String> v(ss.begin(), ss.end());
    const int n = (int)v.size();
    const long long total = (long long)n * (n - 1);

    int rank=0, nprocs=1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    // partição [beg,end)
    long long chunk = total / nprocs;
    long long rem   = total % nprocs;
    long long beg   = rank * chunk + std::min<long long>(rank, rem);
    long long end   = beg + chunk + (rank < rem ? 1 : 0);

    Set<Pair<String,String>> x;
    for (long long k = beg; k < end; ++k) {
        int i,j; linear_to_pair(k, n, i, j);
        if (i != j) x.insert(std::make_pair(v[(size_t)i], v[(size_t)j]));
    }
    return x; // <- conjunto LOCAL de pares
}

// Decide melhor par global reunindo os melhores locais
auto highest_overlap_value (const Set <Pair <String, String>>& sp) -> Pair <String, String>
{
    // melhor local
    Pair<String,String> best = sp.empty() ? Pair<String,String>{"",""} : first_element(sp);
    SizeType<String> best_ov = sp.empty() ? 0 : overlap_value(best.first, best.second);

    for (const auto& p : sp) {
        auto ov = overlap_value(p.first, p.second);
        if (ov > best_ov || (ov == best_ov && (p.first < best.first || (p.first == best.first && p.second < best.second)))) {
            best = p; best_ov = ov;
        }
    }

    int rank=0, nprocs=1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    // empacota: [ov, len1, s1, len2, s2]
    int ov_local = (int)best_ov;
    int l1 = (int)best.first.size();
    int l2 = (int)best.second.size();

    if (rank == 0) {
        Pair<String,String> best_g = best;
        int ov_g = ov_local;

        for (int src=1; src<nprocs; ++src) {
            int ov, a, b;
            MPI_Recv(&ov, 1, MPI_INT, src, 100, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&a,  1, MPI_INT, src, 101, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            std::string s1(a, '\0');
            if (a>0) MPI_Recv(&s1[0], a, MPI_CHAR, src, 102, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&b,  1, MPI_INT, src, 103, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            std::string s2(b, '\0');
            if (b>0) MPI_Recv(&s2[0], b, MPI_CHAR, src, 104, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            Pair<String,String> cand{s1,s2};
            if (ov > ov_g || (ov == ov_g && (cand.first < best_g.first ||
                  (cand.first == best_g.first && cand.second < best_g.second)))) {
                ov_g = ov; best_g = std::move(cand);
            }
        }

        // broadcast do vencedor
        int bl1 = (int)best_g.first.size();
        int bl2 = (int)best_g.second.size();
        MPI_Bcast(&ov_g, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(&bl1,  1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(&bl2,  1, MPI_INT, 0, MPI_COMM_WORLD);
        if (bl1>0) MPI_Bcast((void*)best_g.first.data(),  bl1, MPI_CHAR, 0, MPI_COMM_WORLD);
        if (bl2>0) MPI_Bcast((void*)best_g.second.data(), bl2, MPI_CHAR, 0, MPI_COMM_WORLD);

        return best_g;
    } else {
        // envia candidato local
        MPI_Send(&ov_local, 1, MPI_INT, 0, 100, MPI_COMM_WORLD);
        MPI_Send(&l1,       1, MPI_INT, 0, 101, MPI_COMM_WORLD);
        if (l1>0) MPI_Send((void*)best.first.data(),  l1, MPI_CHAR, 0, 102, MPI_COMM_WORLD);
        MPI_Send(&l2,       1, MPI_INT, 0, 103, MPI_COMM_WORLD);
        if (l2>0) MPI_Send((void*)best.second.data(), l2, MPI_CHAR, 0, 104, MPI_COMM_WORLD);

        // recebe vencedor
        int ov_g, bl1, bl2;
        MPI_Bcast(&ov_g, 1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(&bl1,  1, MPI_INT, 0, MPI_COMM_WORLD);
        MPI_Bcast(&bl2,  1, MPI_INT, 0, MPI_COMM_WORLD);
        std::string s1(bl1, '\0'), s2(bl2, '\0');
        if (bl1>0) MPI_Bcast(&s1[0], bl1, MPI_CHAR, 0, MPI_COMM_WORLD);
        if (bl2>0) MPI_Bcast(&s2[0], bl2, MPI_CHAR, 0, MPI_COMM_WORLD);
        return Pair<String,String>{std::move(s1), std::move(s2)};
    }
}

// Usa suas funções acima para escolher o par ótimo; apenas coordena o merge+broadcast
static String shortest_superstring_mpi(Set<String> t)
{
    int rank; MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // sincroniza estado inicial
    mpi_bcast_set(t, /*root=*/0, MPI_COMM_WORLD);

    while (at_least_two_elements_in(t)) {
        Pair<String,String> best = highest_overlap_value( all_distinct_pairs(t) );
        if (rank == 0) {
            pop_two_elements_and_push_overlap(t, best);
        }
        mpi_bcast_set(t, /*root=*/0, MPI_COMM_WORLD);
    }
    return empty(t) ? String("") : first_element(t);
}

// ---------- main MPI ----------
int main (int argc, char** argv)
{
    MPI_Init(&argc, &argv);

    int rank; MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    Set<String> ss;
    if (rank == 0) {
        ss = read_strings_from_standard_input();
    }
    // todos terão o mesmo set inicial
    mpi_bcast_set(ss, /*root=*/0, MPI_COMM_WORLD);

    auto start = std::chrono::high_resolution_clock::now();
    String ans = shortest_superstring_mpi(ss);
    auto end   = std::chrono::high_resolution_clock::now();

    if (rank == 0) {
        write_string_to_standard_ouput(ans);
        std::chrono::duration<double> elapsed = end - start;
        standard_output << elapsed.count() << std::endl;
    }

    MPI_Finalize();
    return 0;
}
