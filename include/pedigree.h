// *****  typedefs for Pedigree, Vpedigree *****
typedef struct{
  ND agmr12;
  ND par1_hgmr;
  ND par1_R;
  ND par2_hgmr;
  ND par2_R;
  ND z; // (n00_1 + n22_1)/(n00_x + n22_x)
  ND d;
  // ND pseudo_hgmr;
  ND xhgmr1;
  ND xhgmr2;
  ND d_22; // both parents homozyg, delta = 2 
  ND d_21; // both parents homozyg, delta = 1
  ND d_11; // one parent homozyg, one heterozyg, delta = 1
}Pedigree_stats; // 

typedef struct{
  Accession* F; // female parent
  Accession* M; // male parent
  Accession* A; // accession
  Pedigree_stats* pedigree_stats; 
}Pedigree;

typedef struct{
  long capacity;
  long size;
  Pedigree** a;
}Vpedigree;

typedef struct{
  long idx;
  double hgmr;
}Idxhgmr;

typedef struct{
  long idxA;
  long idxFp;
  long idxMp;
  Vlong* progeny; // indices of progeny
}Three_generations;

// *****  function declarations  *****

// *****  Pedigree  *****
Pedigree* construct_pedigree(Accession* Acc, Accession* Fparent, Accession* Mparent);
//double hgmr(char* gts1, char* gts2);
Pedigree_stats* construct_pedigree_stats(Pedigree* the_pedigree, long ploidy); // just initializing to 0's
Pedigree_stats* calculate_pedigree_stats(Pedigree* the_pedigree, GenotypesSet* the_gtsset);
//, long* d0counts, long* d1counts, long* d2counts); // , GenotypesSet* the_gtsset);
long pedigree_ok(Pedigree_stats* p, double max_self_agmr12, double max_ok_hgmr, double max_self_r, double max_ok_d);
void free_pedigree(const Pedigree* the_pedigree);

// *****  Vpedigree  *****
Vpedigree* read_and_store_pedigrees_3col(FILE* p_stream, Vidxid* the_vidxid, GenotypesSet* the_gtsset);
Vpedigree* read_the_pedigrees_file_and_store(FILE* p_stream, Vidxid* the_vidxid, GenotypesSet* the_gtsset); 
Vpedigree* construct_vpedigree(long cap);
const Vlong* accessions_with_offspring(const Vpedigree* the_Vped); //, long n_accessions);

Vlong* alternative_parents(Accession* the_acc, const GenotypesSet* const the_gtsset, double max_ok_hgmr);
Vpedigree* pedigree_alternatives(const Pedigree* the_pedigree, const GenotypesSet* const the_gtsset, const Vlong* parent_idxs, double max_ok_hgmr, double max_ok_z, double max_ok_d);
Vpedigree* alternative_pedigrees(Accession* the_acc, const GenotypesSet* the_gtsset, Vlong* best_parent_candidate_idxs, long ub, double max_ok_d);
void print_pedigree_alternatives(FILE* fh, const Vpedigree* alt_pedigrees);
void push_to_vpedigree(Vpedigree* the_vped, Pedigree* the_ped);

void sort_vpedigree_by_d(Vpedigree* the_vped);
int compare_pedigree(const void* a, const void* b);
void free_vpedigree(const Vpedigree* the_vped);

// *****  array of Idxhgmr  *****
int cmpidxhgmr(const void* v1, const void* v2);
void sort_idxhgmr_by_hgmr(long size, Idxhgmr* array);

// *****  miscellaneous  *****
long long_min(long a, long b);
long long_max(long a, long b);

two_longs gamete_dosage_range(long d, long ploidy);
two_longs diploid_quick_and_dirty_triple_counts(Accession* acc1, Accession* acc2, Accession* progacc);
four_longs q_and_d_n22x_diploid(Accession* acc1, Accession* acc2, Accession* progacc);
ND tfc_tetraploid(char* gts1, char* gts2, char* proggt);
ND tfc_diploid(char* gts1, char* gts2, char* proggts);
ND TFC(char* gts1, char* gts2, char* proggts, long ploidy);
four_longs tfca(char* gts1, char* gts2, char* proggts, long ploidy);
four_longs triple_forbidden_counts(char* gts1, char* gts2, char* proggts, long ploidy);
Pedigree_stats* triple_counts_x(char* gts1, char* gts2, char* proggts,
			   long* d0counts, long* d1counts, long* d2counts);
Pedigree_stats* triple_counts(char* gts1, char* gts2, char* proggts, long ploidy);

long marker_d_counts(Pedigree* the_pedigree,
		     // char* gts1, char* gts2, char* proggts,
		     long* d0counts, long* d1counts, long* d2counts);
void print_pedigree_stats(FILE* fh, Pedigree_stats* the_pedigree_stats);
int pscmp(const void* v1, const void* v2);
void sort_pedigree_stats_by_d(Pedigree_stats** the_pss, long size); // sort in place

void print_d_r(FILE* fh, ND nd);
double n_over_d(ND nd);

long check_idxid_map(Vidxid* vidxid, const GenotypesSet* the_gtsset);
