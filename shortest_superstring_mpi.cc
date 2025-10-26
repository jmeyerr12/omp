#include <mpi.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <cstring>

/* ---- Tipos auxiliares ---- */
using String = std::string;
using Size   = std::size_t;

/* ---- Funções básicas (iguais à sua versão) ---- */
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

/* ---- Broadcast de vetor de strings (usado só no início) ---- */
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
    MPI_Bcast(lens.data(), n, MPI_INT, root, comm);

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
    MPI_Bcast(buf.data(), (int)buf.size(), MPI_CHAR, root, comm);

    if (rank != root) {
        Size off = 0;
        for (int i = 0; i < n; ++i) {
            v[i].assign(buf.data()+off, lens[i]);
            off += lens[i];
        }
    }
}

/* ---- Estrutura e redução do melhor par (sem rlex) ---- */
struct BestPair {
    int ov;  // overlap
    int i, j; // índices
};

static inline bool better(const BestPair& A, const BestPair& B) {
    if (A.ov != B.ov) return A.ov > B.ov;
    if (A.i  != B.i ) return A.i  < B.i ;
    return A.j < B.j;
}

static void reduce_bestpair(void* in, void* inout, int* len, MPI_Datatype* dtype) {
    BestPair* a = (BestPair*)in;
    BestPair* b = (BestPair*)inout;
    for (int k = 0; k < *len; ++k) {
        if (better(a[k], b[k])) b[k] = a[k];
    }
}

static void create_bestpair_type_and_op(MPI_Datatype* T, MPI_Op* OP) {
    BestPair tmp;
    int blocklen[3] = {1,1,1};
    MPI_Aint disp[3], base;
    MPI_Get_address(&tmp, &base);
    MPI_Get_address(&tmp.ov, &disp[0]);
    MPI_Get_address(&tmp.i,  &disp[1]);
    MPI_Get_address(&tmp.j,  &disp[2]);
    for (int k = 0; k < 3; ++k) disp[k] -= base;

    MPI_Datatype types[3] = {MPI_INT,MPI_INT,MPI_INT};
    MPI_Type_create_struct(3, blocklen, disp, types, T);
    MPI_Type_commit(T);

    MPI_Op_create(&reduce_bestpair, /*commute=*/1, OP);
}

/* Mapeia índice linear k em par (i,j) com i!=j. */
static inline void linear_to_pair(long long k, int n, int& i, int& j) {
    i = (int)(k / (n - 1));
    int r = (int)(k % (n - 1));
    j = (r < i) ? r : (r + 1);
}

/* Melhor par global via MPI_Allreduce (tie-break por i,j) */
static BestPair find_global_best_pair_mpi(const std::vector<String>& v, MPI_Datatype TBest, MPI_Op OBest, MPI_Comm comm) {
    int nprocs, rank;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &nprocs);

    const int n = (int)v.size();
    const long long total = (long long)n * (n - 1);

    BestPair local{ -1, 0, 0 };

    // (opcional) distribuição cíclica melhora balanceamento:
    // for (long long k = rank; k < total; k += nprocs) { ... }
    long long chunk = total / nprocs;
    long long rem   = total % nprocs;
    long long beg   = rank * chunk + std::min<long long>(rank, rem);
    long long end   = beg + chunk + (rank < rem ? 1 : 0);

    for (long long k = beg; k < end; ++k) {
        int i, j;
        linear_to_pair(k, n, i, j);
        int ov = (int)overlap_value(v[i], v[j]);
        BestPair cand{ ov, i, j };
        if (better(cand, local)) local = cand;
    }

    BestPair global{ -1, 0, 0 };
    MPI_Allreduce(&local, &global, 1, TBest, OBest, comm);
    return global;
}

/* Enviar só o merge (i, j, merged) e aplicar localmente */
static void bcast_and_apply_merge(std::vector<String>& v, int i, int j, MPI_Comm comm) {
    int rank; MPI_Comm_rank(comm, &rank);

    if (rank == 0 && j < i) std::swap(i, j);

    MPI_Bcast(&i, 1, MPI_INT, 0, comm);
    MPI_Bcast(&j, 1, MPI_INT, 0, comm);

    int len = 0;
    String merged;
    if (rank == 0) {
        merged = overlap_merge(v[i], v[j]);
        len = (int)merged.size();
    }
    MPI_Bcast(&len, 1, MPI_INT, 0, comm);
    if (rank != 0) merged.resize(len);
    if (len > 0) MPI_Bcast(&merged[0], len, MPI_CHAR, 0, comm);

    if (j < i) std::swap(i, j);
    v[i] = std::move(merged);
    v.erase(v.begin() + j);
}

/* Guloso distribuído via MPI */
static String shortest_superstring_mpi(std::vector<String> v, MPI_Datatype TBest, MPI_Op OBest, MPI_Comm comm) {
    int rank;
    MPI_Comm_rank(comm, &rank);

    while ((int)v.size() > 1) {
        BestPair best = find_global_best_pair_mpi(v, TBest, OBest, comm);
        bcast_and_apply_merge(v, best.i, best.j, comm); // << só transmite o merge
    }

    return v.empty() ? "" : v[0];
}

/* ---- Leitura no rank 0 ---- */
static std::vector<String> read_input_rank0() {
    Size n; std::cin >> n;
    std::vector<String> v(n);
    for (Size i = 0; i < n; ++i) std::cin >> v[i];
    return v;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    MPI_Datatype TBest;
    MPI_Op       OBest;
    create_bestpair_type_and_op(&TBest, &OBest);

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    std::vector<String> v;
    if (rank == 0) v = read_input_rank0();
    bcast_strings(v, 0, MPI_COMM_WORLD); // broadcast inicial do vetor

    auto t0 = std::chrono::high_resolution_clock::now();
    String ans = shortest_superstring_mpi(v, TBest, OBest, MPI_COMM_WORLD);
    auto t1 = std::chrono::high_resolution_clock::now();

    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    if (rank == 0) {
        std::cout << ans << "\n";
        std::cerr << elapsed << "s\n";
    }

    MPI_Op_free(&OBest);
    MPI_Type_free(&TBest);
    MPI_Finalize();
    return 0;
}
