#include <bits/stdc++.h>
#include <mpi.h>
using namespace std;

/* ======================== Utilidades básicas ======================== */

// Remove strings que são substrings de outras (reduz n sem alterar ótima).
static vector<string> dedup_substrings(const vector<string>& a) {
    int n = (int)a.size();
    vector<int> kill(n, 0);
    for (int i = 0; i < n; ++i) if (!kill[i]) {
        for (int j = 0; j < n; ++j) if (i != j && !kill[i]) {
            if (a[j].find(a[i]) != string::npos) kill[i] = 1;
        }
    }
    vector<string> b;
    for (int i = 0; i < n; ++i) if (!kill[i]) b.push_back(a[i]);
    return b;
}

static int overlap_ij(const string& A, const string& B) {
    int m = min((int)A.size(), (int)B.size());
    for (int k = m; k > 0; --k)
        if (A.compare((int)A.size()-k, k, B, 0, k) == 0) return k;
    return 0;
}

static inline int popc(int x){ return __builtin_popcount((unsigned)x); }

/* Codifica (mask,i) em 64 bits para lookup */
static inline uint64_t keyMI(int mask, int i) {
    return ( (uint64_t)mask << 6 ) | (uint64_t)i;
}

/* ================== Broadcast de vetor de strings =================== */

static void bcast_strings(vector<string>& v, int root, MPI_Comm comm) {
    int rank; MPI_Comm_rank(comm, &rank);
    int n = (int)v.size();
    MPI_Bcast(&n, 1, MPI_INT, root, comm);
    if (rank != root) v.resize(n);

    vector<int> L(n,0);
    if (rank == root) for (int i = 0; i < n; ++i) L[i] = (int)v[i].size();
    if (n > 0) MPI_Bcast(L.data(), n, MPI_INT, root, comm);

    int tot = 0; for (int x: L) tot += x;
    vector<char> buf(tot);
    if (rank == root) {
        int p=0;
        for (int i=0;i<n;++i){ memcpy(buf.data()+p, v[i].data(), L[i]); p+=L[i]; }
    }
    if (tot>0) MPI_Bcast(buf.data(), tot, MPI_CHAR, root, comm);

    if (rank != root) {
        int p=0;
        for (int i=0;i<n;++i){ v[i].assign(buf.data()+p, L[i]); p+=L[i]; }
    }
}

/* =============== Geração de máscaras por nível (popcount) =============== */

static vector<int> masks_with_k_bits(int n, int k) {
    vector<int> out;
    if (k < 0 || k > n) return out;
    if (k == 0) { out.push_back(0); return out; }

    int m = (1<<k) - 1;
    int limit = (1<<n);
    for (; m < limit; ) {
        if (popc(m) == k) out.push_back(m);

        int c = m & -m;
        int r = m + c;
        if (r == 0) break;
        m = (((r ^ m) >> 2) / c) | r;
    }
    return out;
}

/* ==== Estruturas para DP esparso por nível ==== */

struct LevelLayout {
    vector<int> masks;
    vector<vector<int>> elems;
    vector<int> off;
    int total = 0;
    unordered_map<uint64_t,int> map_mi_to_idx;

    void build_map() {
        map_mi_to_idx.reserve((size_t)total * 2);
        for (int im = 0; im < (int)masks.size(); ++im) {
            int base = off[im];
            for (int p = 0; p < (int)elems[im].size(); ++p) {
                int j = elems[im][p];
                map_mi_to_idx.emplace(keyMI(masks[im], j), base + p);
            }
        }
    }
};

static LevelLayout build_level_layout(int n, const vector<int>& masks_k) {
    LevelLayout L;
    L.masks = masks_k;
    L.elems.resize((int)masks_k.size());
    L.off.resize((int)masks_k.size());
    int cur = 0;
    for (int im = 0; im < (int)masks_k.size(); ++im) {
        int m = masks_k[im];
        for (int j = 0; j < n; ++j) if (m & (1<<j)) L.elems[im].push_back(j);
        L.off[im] = cur;
        cur += (int)L.elems[im].size();
    }
    L.total = cur;
    return L;
}

/* Particiona [0,total) em segmentos contíguos por rank */
static void my_segment(int total, int rank, int nprocs, int& L, int& R) {
    int base = total / nprocs;
    int rem  = total % nprocs;
    L = rank * base + min(rank, rem);
    R = L + base + (rank < rem ? 1 : 0);
}

/* ======================= Programa principal ======================= */

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    vector<string> s;
    if (rank == 0) {
        int N; cin >> N;
        s.resize(N);
        for (int i = 0; i < N; ++i) cin >> s[i];
        s = dedup_substrings(s);
    }
    bcast_strings(s, 0, MPI_COMM_WORLD);
    int n = (int)s.size();
    if (n == 0) {
        if (rank == 0) { cout << "\n0\n"; cout << "0\n"; }
        MPI_Finalize();
        return 0;
    }

    MPI_Barrier(MPI_COMM_WORLD);
    auto t0 = std::chrono::high_resolution_clock::now();

    // ===== Pré-cálculo de overlaps =====
    vector<vector<int>> ov(n, vector<int>(n, 0));
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (i != j) ov[i][j] = overlap_ij(s[i], s[j]);

    // ===== Construção dos layouts =====
    vector<LevelLayout> layout(n+1);
    {
        vector<int> m1;
        for (int j = 0; j < n; ++j) m1.push_back(1<<j);
        layout[1] = build_level_layout(n, m1);
    }
    for (int k = 2; k <= n; ++k) {
        auto mk = masks_with_k_bits(n, k);
        layout[k] = build_level_layout(n, mk);
    }

    // ===== Nível 1 =====
    vector<int> dp_prev(layout[1].total, INT_MAX),
                par_prev(layout[1].total, -1);

    if (rank == 0) {
        for (int im = 0; im < (int)layout[1].masks.size(); ++im) {
            int j = layout[1].elems[im][0];
            dp_prev[ layout[1].off[im] ] = s[j].size();
        }
    }
    MPI_Bcast(dp_prev.data(), (int)dp_prev.size(), MPI_INT, 0, MPI_COMM_WORLD);

    layout[1].build_map();
    vector<vector<int>> parents_per_level(n+1);
    parents_per_level[1] = par_prev;

    // ===== DP níveis 2..n =====
    for (int k = 2; k <= n; ++k) {
        const LevelLayout& Lprev = layout[k-1];
        const LevelLayout& Lcur  = layout[k];

        int Ls, Rs; my_segment(Lcur.total, rank, nprocs, Ls, Rs);

        vector<int> dp_local(Rs-Ls, INT_MAX), par_local(Rs-Ls, -1);

        auto& off   = Lcur.off;
        auto& masks = Lcur.masks;
        auto& elems = Lcur.elems;

        auto locate = [&](int g)->pair<int,int>{
            int im = int(upper_bound(off.begin(), off.end(), g) - off.begin()) - 1;
            if (im < 0) im = 0;
            while ((im+1) < (int)off.size() && off[im+1] <= g) ++im;
            int pos = g - off[im];
            return {im, pos};
        };

        for (int g = Ls; g < Rs; ++g) {
            auto [im, pos] = locate(g);
            int mask = masks[im];
            int j    = elems[im][pos];
            int pmask = mask ^ (1<<j);

            int best = INT_MAX, best_i = -1;

            for (int i = 0; i < n; ++i) if (pmask & (1<<i)) {
                auto it = Lprev.map_mi_to_idx.find(keyMI(pmask, i));
                if (it == Lprev.map_mi_to_idx.end()) continue;
                int prev_idx = it->second;
                int base = dp_prev[prev_idx];
                if (base == INT_MAX) continue;
                int cand = base + (int)s[j].size() - ov[i][j];
                if (cand < best || (cand == best && i < best_i)) {
                    best = cand; best_i = i;
                }
            }

            dp_local[g - Ls]  = best;
            par_local[g - Ls] = best_i;
        }

        vector<int> recv_counts(nprocs), displs(nprocs);
        for (int r=0;r<nprocs;++r) {
            int Lr,Rr; my_segment(Lcur.total, r, nprocs, Lr, Rr);
            recv_counts[r] = Rr - Lr;
        }
        displs[0] = 0;
        for (int r=1;r<nprocs;++r) displs[r] = displs[r-1] + recv_counts[r-1];

        vector<int> dp_curr(Lcur.total, INT_MAX), par_curr(Lcur.total, -1);

        MPI_Allgatherv(dp_local.data(), (int)dp_local.size(), MPI_INT,
                       dp_curr.data(), recv_counts.data(), displs.data(), MPI_INT,
                       MPI_COMM_WORLD);

        MPI_Allgatherv(par_local.data(), (int)par_local.size(), MPI_INT,
                       par_curr.data(), recv_counts.data(), displs.data(), MPI_INT,
                       MPI_COMM_WORLD);

        dp_prev.swap(dp_curr);
        par_prev.swap(par_curr);
        layout[k].build_map();
        parents_per_level[k] = par_prev;
    }

    // ===== Reconstrução: apenas rank 0 =====
    if (rank == 0) {
        const LevelLayout& Lfull = layout[n];
        int full_mask = (1<<n) - 1;

        int best_len = INT_MAX, best_j = -1;

        for (int im = 0; im < (int)Lfull.masks.size(); ++im) {
            if (Lfull.masks[im] != full_mask) continue;
            int base = Lfull.off[im];
            for (int p = 0; p < (int)Lfull.elems[im].size(); ++p) {
                int idx = base + p;
                int val = dp_prev[idx];
                if (val < best_len || (val == best_len && Lfull.elems[im][p] < best_j)) {
                    best_len = val;
                    best_j   = Lfull.elems[im][p];
                }
            }
        }

        vector<int> order; order.reserve(n);
        int cur_mask = full_mask, cur_j = best_j;

        for (int k = n; k >= 1; --k) {
            order.push_back(cur_j);
            if (k == 1) break;

            auto it = layout[k].map_mi_to_idx.find(keyMI(cur_mask, cur_j));
            int idx_k = it->second;
            int parent_i = parents_per_level[k][idx_k];

            cur_mask ^= (1<<cur_j);
            cur_j = parent_i;
        }

        reverse(order.begin(), order.end());

        string ans = s[order[0]];
        for (int t = 1; t < (int)order.size(); ++t) {
            int i = order[t-1], j = order[t];
            int ovl = ov[i][j];
            ans += s[j].substr(ovl);
        }

        MPI_Barrier(MPI_COMM_WORLD);
        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();

        cout << ans << "\n";
        cout << elapsed << "\n";
    }

    MPI_Finalize();
    return 0;
}
