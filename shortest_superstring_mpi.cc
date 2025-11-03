// sss.cpp — versão com guards; MPI sem MPI_Op e sem MPI_Datatype custom
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cstring>

using String = std::string;
using Size   = std::size_t;

/* ---------- Funções básicas (compartilhadas) ---------- */
static inline bool is_prefix(const String& a, const String& b) {
    if (a.size() > b.size()) return false;
    return std::mismatch(a.begin(), a.end(), b.begin()).first == a.end();
}

static inline String remove_prefix(const String& x, Size n) {
    if (x.size() > n) return x.substr(n);
    return x;
}

static inline std::vector<String> all_suffixes(const String& x) {
    std::vector<String> ss;
    if (x.size() <= 1) return ss;
    for (Size n = x.size()-1; n > 0; --n) ss.push_back(x.substr(n));
    return ss;
}

static inline String common_suffix_prefix(const String& a, const String& b) {
    if (a.empty() || b.empty()) return "";
    String best = "";
    for (const auto& s : all_suffixes(a)) {
        if (is_prefix(s, b) && s.size() > best.size()) best = s;
    }
    return best;
}

static inline Size overlap_value(const String& s, const String& t) {
    return common_suffix_prefix(s, t).size();
}

static inline String overlap_merge(const String& s, const String& t) {
    String c = common_suffix_prefix(s, t);
    return s + remove_prefix(t, c.size());
}

static std::vector<String> read_input_from_stdin() {
    Size n; std::cin >> n;
    std::vector<String> v(n);
    for (Size i = 0; i < n; ++i) std::cin >> v[i];
    return v;
}

/* =======================================================
   ==================  RAMO MPI (PARALELO)  ==============
   ======================================================= */
#ifdef USE_MPI
#include <mpi.h>

/* ---- Broadcast de vetor de strings ---- */
static void bcast_strings(std::vector<String>& v, int root, MPI_Comm comm) {
    int rank;
    MPI_Comm_rank(comm, &rank);

    int n = (int)v.size();
    MPI_Bcast(&n, 1, MPI_INT, root, comm);
    if (rank != root) v.resize(n);

    std::vector<int> lens(n);
    if (rank == root) {
        for (int i = 0; i < n; ++i) lens[i] = (int)v[i].size();
    }
    if (n > 0) MPI_Bcast(lens.data(), n, MPI_INT, root, comm);

    Size total_bytes = 0;
    for (int i = 0; i < n; ++i) total_bytes += (Size)lens[i];

    std::vector<char> buf(total_bytes);
    if (rank == root) {
        Size off = 0;
        for (int i = 0; i < n; ++i) {
            std::memcpy(buf.data()+off, v[i].data(), lens[i]);
            off += lens[i];
        }
    }
    if (total_bytes > 0)
        MPI_Bcast(buf.data(), (int)buf.size(), MPI_CHAR, root, comm);

    if (rank != root) {
        Size off = 0;
        for (int i = 0; i < n; ++i) {
            v[i].assign(buf.data()+off, lens[i]);
            off += lens[i];
        }
    }
}

/* ---- Helpers para pares dirigidos (i != j) ---- */
static inline void linear_to_pair(long long k, int n, int& i, int& j) {
    i = (int)(k / (n - 1));
    int r = (int)(k % (n - 1));
    j = (r < i) ? r : (r + 1);
}

// Critério: maior overlap; em empate, ordem lexicografica
static inline bool better_triplet(const int A[3], const int B[3], const std::vector<String>& v) {
    // A = {ovA, iA, jA}
    // B = {ovB, iB, jB}
    if (A[0] != B[0]) return A[0] > B[0];
    if (v[A[1]] != v[B[1]]) return v[A[1]] < v[B[1]];
    return v[A[2]] < v[B[2]];
}

/* Cada processo calcula seu melhor local {ov,i,j}; rank 0 coleta, decide e difunde */
static void find_global_best_pair_mpi_gather(const std::vector<String>& v,
                                             int out_best[3],
                                             MPI_Comm comm)
{
    int rank, nprocs;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &nprocs);

    const int n = (int)v.size();
    const long long total = (long long)n * (n - 1);

    // Range balanceado (primeiros 'rem' ranks recebem +1)
    long long chunk = total / nprocs;
    long long rem   = total % nprocs;
    long long beg   = rank * chunk + std::min<long long>(rank, rem);
    long long end   = beg + chunk + (rank < rem ? 1 : 0);

    int local[3] = {-1, 0, 0};
    for (long long k = beg; k < end; ++k) {
        int i, j; linear_to_pair(k, n, i, j);
        int cand[3] = { (int)overlap_value(v[i], v[j]), i, j };
        if (better_triplet(cand, local, v)) {
            local[0] = cand[0]; local[1] = cand[1]; local[2] = cand[2];
        }
    }

    // Rank 0 coleta tudo; cada processo envia 3 ints
    std::vector<int> gathered;
    if (rank == 0) gathered.resize(3 * nprocs);
    MPI_Gather(local, 3, MPI_INT,
               (rank==0 ? gathered.data() : nullptr), 3, MPI_INT,
               0, comm);

    // Rank 0 escolhe o melhor e faz broadcast do triplet
    if (rank == 0) {
        int best[3] = {-1, 0, 0};
        for (int p = 0; p < nprocs; ++p) {
            int cand[3] = { gathered[3*p+0], gathered[3*p+1], gathered[3*p+2] };
            if (better_triplet(cand, best, v)) {
                best[0] = cand[0]; best[1] = cand[1]; best[2] = cand[2];
            }
        }
        out_best[0] = best[0]; out_best[1] = best[1]; out_best[2] = best[2];
    }
    MPI_Bcast(out_best, 3, MPI_INT, 0, comm);
}

/* Broadcast do merge decidido no root e aplicação local (ordem i->j) */
static void bcast_and_apply_merge(std::vector<String>& v, int i, int j, MPI_Comm comm) {
    MPI_Bcast(&i, 1, MPI_INT, 0, comm);
    MPI_Bcast(&j, 1, MPI_INT, 0, comm);

    int rank; MPI_Comm_rank(comm, &rank);

    int len = 0;
    String merged;
    if (rank == 0) {
        merged = overlap_merge(v[i], v[j]);
        len = (int)merged.size();
    }

    MPI_Bcast(&len, 1, MPI_INT, 0, comm);
    if (rank != 0) merged.resize(len);
    if (len > 0) MPI_Bcast(&merged[0], len, MPI_CHAR, 0, comm);

    v[i] = std::move(merged);
    v.erase(v.begin() + j);
}

/* Loop guloso distribuído usando Gather+Bcast para selecionar o melhor par */
static String shortest_superstring_mpi(std::vector<String> v, MPI_Comm comm) {
    while ((int)v.size() > 1) {
        int best[3]; // {ov,i,j}
        find_global_best_pair_mpi_gather(v, best, comm);
        bcast_and_apply_merge(v, best[1], best[2], comm);
    }
    return v.empty() ? "" : v[0];
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank; MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    std::vector<String> v;
    if (rank == 0) v = read_input_from_stdin();
    bcast_strings(v, 0, MPI_COMM_WORLD);

    auto t0 = std::chrono::high_resolution_clock::now();
    String ans = shortest_superstring_mpi(v, MPI_COMM_WORLD);
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    if (rank == 0) {
        std::cout << ans << "\n";
        std::cout << elapsed << "\n";
    }

    MPI_Finalize();
    return 0;
}

/* =======================================================
   =================  RAMO SEQUENCIAL (g++)  =============
   ======================================================= */
#else

/* Melhor par sequencial: varre todos os pares dirigidos (i != j) */
static std::pair<int,int> find_best_pair_seq(const std::vector<String>& v) {
    int n = (int)v.size();
    int best_ov = -1, bi = 0, bj = 1;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) if (i != j) {
            int ov = (int)overlap_value(v[i], v[j]);
            if (ov > best_ov || (ov == best_ov && (i < bi || (i == bi && j < bj)))) {
                best_ov = ov; bi = i; bj = j;
            }
        }
    }
    return {bi, bj};
}

/* Guloso sequencial */
static String shortest_superstring_seq(std::vector<String> v) {
    while ((int)v.size() > 1) {
        auto [i, j] = find_best_pair_seq(v);
        String merged = overlap_merge(v[i], v[j]); // ordem dirigida i->j
        v[i] = std::move(merged);
        v.erase(v.begin() + j);
    }
    return v.empty() ? "" : v[0];
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    auto v = read_input_from_stdin();

    auto t0 = std::chrono::high_resolution_clock::now();
    String ans = shortest_superstring_seq(v);
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::cout << ans << "\n";
    std::cout << elapsed << "\n";
    return 0;
}
#endif
