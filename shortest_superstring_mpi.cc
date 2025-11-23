#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cstring>

using String = std::string;
using Size   = std::size_t;

/* Funções básicas (compartilhadas)  */
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

/* ===  RAMO MPI (PARALELO)  === */
#ifdef USE_MPI
#include <mpi.h>

// broadcast do vetor de strings
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

//converte o índice linear k no par (i,j) com i!=j dentro dos n*(n-1) pares dirigidos possíveis.
static inline void linear_to_pair(long long k, int n, int& i, int& j) {
    i = (int)(k / (n - 1));
    int r = (int)(k % (n - 1));
    j = (r < i) ? r : (r + 1);
}

struct Best { //struct para a reducao, seleciona maior overlap e desempata lexicograficamente
    int ov; //overlap
    int i, j; //indices do par (i->j)
    int ri, rj; //ranks lexicograficos de v[i], v[j] (menor rank = string "menor")
};

//MPI_Datatype para Best
static MPI_Datatype BEST_TYPE;

static void create_best_datatype() {
    Best dummy;
    int blocklen[5] = {1,1,1,1,1};
    MPI_Aint disp[5];
    MPI_Aint base;
    MPI_Get_address(&dummy, &base);
    MPI_Get_address(&dummy.ov, &disp[0]);
    MPI_Get_address(&dummy.i,  &disp[1]);
    MPI_Get_address(&dummy.j,  &disp[2]);
    MPI_Get_address(&dummy.ri, &disp[3]);
    MPI_Get_address(&dummy.rj, &disp[4]);
    for (int k=0;k<5;++k) disp[k] -= base;
    MPI_Datatype types[5] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT, MPI_INT};
    MPI_Type_create_struct(5, blocklen, disp, types, &BEST_TYPE);
    MPI_Type_commit(&BEST_TYPE);
}

//operador de redução: escolhe o "melhor" Best
static MPI_Op BEST_OP;

static void best_reduce_func(void* invec, void* inoutvec, int* len, MPI_Datatype* dtype) {
    Best* in  = static_cast<Best*>(invec);
    Best* io  = static_cast<Best*>(inoutvec);
    for (int k = 0; k < *len; ++k) {
        const Best& A = in[k];
        Best&       B = io[k];
        //criterio: maior ov; empate -> menor ri; empate -> menor rj (ranks lexicograficos)
        bool takeA = (A.ov > B.ov) ||
                     (A.ov == B.ov && (A.ri < B.ri ||
                      (A.ri == B.ri && A.rj < B.rj)));
        if (takeA) B = A;
    }
}

//rank lexicografico das strings: mesmo em todos os processo
static std::vector<int> compute_lex_ranks(const std::vector<String>& v) {
    int n = (int)v.size();
    std::vector<int> idx(n);
    for (int i = 0; i < n; ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(),
        [&](int a, int b){ return v[a] < v[b]; });
    std::vector<int> rank(n);
    for (int r = 0; r < n; ++r) rank[idx[r]] = r;
    return rank;
}

//reducao global (MPI_Reduce) do melhor par usando Best
static void find_global_best_pair_mpi_reduce(const std::vector<String>& v,
                                             const std::vector<int>& lexrank,
                                             Best& out_best,
                                             MPI_Comm comm)
{
    int rank, nprocs;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &nprocs);

    const int n = (int)v.size();
    const long long total = 1LL * n * (n - 1);

    Best local{ -1, 0, 0, 0, 0 };

    //distribui o trabalho por stride: cada processo avalia k = rank, rank+nprocs
    for (long long k = rank; k < total; k += nprocs) {
        int i, j; linear_to_pair(k, n, i, j);
        int ov = (int)overlap_value(v[i], v[j]);
        Best cand{ ov, i, j, lexrank[i], lexrank[j] };
        bool take = (cand.ov > local.ov) ||
                    (cand.ov == local.ov && (cand.ri < local.ri ||
                    (cand.ri == local.ri && cand.rj < local.rj)));
        if (take) local = cand;
    }

    Best root_best{};
    MPI_Reduce(&local, &root_best, 1, BEST_TYPE, BEST_OP, 0, comm);

    //root difunde o resultado (3 ints bastam para seguir o fluxo)
    int triple[3];
    if (rank == 0) {
        triple[0] = root_best.i;
        triple[1] = root_best.j;
        triple[2] = root_best.ov; 
    }
    MPI_Bcast(triple, 3, MPI_INT, 0, comm);

    out_best.i  = triple[0];
    out_best.j  = triple[1];
    out_best.ov = triple[2];
    out_best.ri = lexrank[out_best.i];
    out_best.rj = lexrank[out_best.j];
}

//broadcast do merge decidido no root e aplicação local (ordem i->j)
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

//loop guloso distribuído usando Reduce (melhor par) + Bcast (decisão)
static String shortest_superstring_mpi(std::vector<String> v, MPI_Comm comm) {
    std::vector<int> lexrank = compute_lex_ranks(v); // ranks lexicograficos estaveis para desempate global

    while ((int)v.size() > 1) {
        Best best{};
        find_global_best_pair_mpi_reduce(v, lexrank, best, comm);
        bcast_and_apply_merge(v, best.i, best.j, comm);

        lexrank = compute_lex_ranks(v); //apos remover j, indices mudam; recomputa ranks
    }
    return v.empty() ? "" : v[0];
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    // prepara tipos/ops custom
    create_best_datatype();
    MPI_Op_create(best_reduce_func, /*commute=*/1, &BEST_OP);

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

    // libera recursos
    MPI_Op_free(&BEST_OP);
    MPI_Type_free(&BEST_TYPE);

    MPI_Finalize();
    return 0;
}

/* === RAMO SEQUENCIAL === */
#else

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

static String shortest_superstring_seq(std::vector<String> v, double& par_time) {
    while ((int)v.size() > 1) {

        auto t0p = std::chrono::high_resolution_clock::now();
        auto [i, j] = find_best_pair_seq(v);
        auto t1p = std::chrono::high_resolution_clock::now();

        par_time += std::chrono::duration<double>(t1p - t0p).count();

        String merged = overlap_merge(v[i], v[j]);
        v[i] = std::move(merged);
        v.erase(v.begin() + j);
    }
    return v.empty() ? "" : v[0];
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    auto v = read_input_from_stdin();

    double par_time = 0.0;
    auto t0 = std::chrono::high_resolution_clock::now();
    String ans = shortest_superstring_seq(v, par_time);
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::cout << ans << "\n";
    std::cout << elapsed << "\n";

    double seq_time = elapsed - par_time;
    double seq_frac = (elapsed > 0.0) ? (seq_time / elapsed) : 0.0;
    std::cerr << elapsed << " " << par_time << " " << seq_frac << "\n";

    return 0;
}

#endif
