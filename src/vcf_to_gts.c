#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <getopt.h>
#include <string.h>
#include <time.h>
#include <unistd.h> // needed for getopt
#include <sys/sysinfo.h> // needed for get_nprocs
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>

#include "vect.h"
#include "various.h"

#define INIT_N_ACCESSIONS 10000
#define INIT_N_MARKERS 10000

#define split_str "\t"

// There will be one of these structs for each thread,
// each thread which will process the markers in the range from first_marker to last_marker
typedef struct{
  long n_accessions;
  Vstr* marker_ids;
  Vstr* marker_lines; 
  long first_marker;
  long last_marker;
  bool use_alt_marker_id;
  double minGP;
  double minmaf;
  double maxmd;
  double delta;
  long ploidy;

  Vstr* gntps; // gntps->a[i]->[j] is genotype (dosage) of ith stored marker for this thread, jth accession
} TD; // thread data

void* process_marker_range(void* x);

char token_to_genotype_GT(char* token, long TDgtidx, long gpidx, double minGP);
char token_to_genotype_DS(char* token, long gtidx, long gpidx, double minGP, double delta);
char GTstr_to_dosage(char* tkn);
char DSstr_to_dosage(char* tkn, double delta);
void get_GT_GQ_GP_DS_indices(char* format, long* GTidx, long* GQidx, long* GPidx, long* DSidx);
bool GP_to_quality_ok(char* token, double minGP);
char* split_on_char(char* str, char c, long* iptr); 
void chomp(char* str); // remove any trailing newlines from str
void print_usage_info(FILE* ostream);
double clock_time(clockid_t the_clock){
  struct timespec tspec;
  clock_gettime(the_clock, &tspec);
  return (double)(tspec.tv_sec + 1.0e-9*tspec.tv_nsec);
}

extern int errno; 

int main(int argc, char *argv[]){
  errno = 0;

  char* input_filename = NULL;
  FILE *in_stream = NULL;
  Vchar* output_filename = construct_vchar_from_str("vcftogts.out");
  FILE* out_stream = NULL; 

  // double minGQ = 0; // not implemented, but should be.
  double minGP = 0;

  long nprocs = (long)get_nprocs(); // returns 2*number of cores if hyperthreading.
  long Nthreads = (nprocs > 2)? nprocs/2 : 1; // default number of threads

  bool use_alt_marker_id = false; // default is to use marker ids in col 3 of vcf file.
  // (but if these are absent you can construct and use alternative marker ids from cols 1 and 2.)
  bool shuffle_accessions = false;
  long rand_seed = (unsigned)time(0);

  double delta = 0.1;
  double min_maf = 0.1;
  double max_marker_md = 0.25;
  long ploidy = 2;
  long n_markers_in_chunk = 5040; // perhaps give this a different name?
  long min_chunk_size = 60;
     
 
  while (1) {
    int an_option = 0;
    int option_index = 0;
    static struct option long_options[] = {
      {"input",   required_argument, 0,  'i'}, // vcf filename
	{"output",  required_argument, 0,  'o'}, // output filename
	{"pmin",  required_argument,  0,  'p'}, // min. 'estimated genotype probability'
	{"threads", required_argument, 0,  't'}, // number of threads to use. Default: set automatically based on nprocs()
	{"alternate_marker_ids",  no_argument, 0, 'a'}, // construct marker ids from cols 1 and 2 (in case garbage in col 3)
	{"randomize",    no_argument, 0,  'r' }, // shuffle the order of the accessions in output
	{"seed", required_argument, 0, 's'}, // rng seed. Only relevant if shuffling.
	{"min_maf", required_argument, 0, 'f'}, // filter out markers with minor allele frequency less than this.
	{"max_marker_md", required_argument, 0, 'm'}, // filter out markers with missing data fraction > this.
	{"delta", required_argument, 0, 'd'},  // if using DS, the dosage will be considered to be missing data if > delta from an integer.
	{"chunk_size", required_argument, 0, 'c'}, // number of lines (markers) to read and process at a time.
	{"help", no_argument, 0, 'h'},
	{0,         0,                 0,  0 }
    };
   
    an_option = getopt_long_only(argc, argv, "", long_options, &option_index);
    if(an_option == -1) break;
    switch(an_option){
    case 'i':
      input_filename = optarg;
      in_stream = fopen(input_filename, "r");
      if(in_stream == NULL){
	fprintf(stderr, "Failed to open %s for reading.\n", input_filename);
	exit(EXIT_FAILURE);
      }
      break;
    case 'o':
      free_vchar(output_filename);
      output_filename = construct_vchar_from_str(optarg);
      break;
    case 'p' :
      if(sscanf(optarg, "%lf ", &minGP) != 1  ||  errno != 0){
	fprintf(stderr, "# minGP; conversion of argument %s to double failed.\n", optarg);
	exit(EXIT_FAILURE);
      }else if(minGP > 1){
	fprintf(stderr, "# minGP was set to %8.4lf , must be <= 1\n", minGP);
	exit(EXIT_FAILURE);
      }
      fprintf(stdout, "# minGP set to: %8.5lf\n", minGP);
      break;
    case 'f' :
      if(sscanf(optarg, "%lf ", &min_maf) != 1  ||  errno != 0){
	fprintf(stderr, "# min_maf; conversion of argument %s to double failed.\n", optarg);
	exit(EXIT_FAILURE);
      }else if((min_maf >= 1) || (min_maf < 0)){
	fprintf(stderr, "# min_maf was set to %8.4lf , must be >=0 and < 1\n", min_maf);
	exit(EXIT_FAILURE);
      }
      fprintf(stdout, "# min_maf set to: %8.5lf\n", min_maf);
      break;
    case 'm' :
      if(sscanf(optarg, "%lf ", &max_marker_md) != 1  ||  errno != 0){
	fprintf(stderr, "# max_marker_md; conversion of argument %s to double failed.\n", optarg);
	exit(EXIT_FAILURE);
      }else if(max_marker_md > 1){
	fprintf(stderr, "# max_marker_md was set to %8.4lf , must be > 0 and <= 1\n", max_marker_md);
	exit(EXIT_FAILURE);
      }
      fprintf(stdout, "# max_marker_md set to: %8.5lf\n", max_marker_md);
      break;
    case 't' :
      if(sscanf(optarg, "%ld", &Nthreads) != 1  ||  errno != 0){
	fprintf(stderr, "# Nthreads; conversion of argument %s to long failed.\n", optarg);
	exit(EXIT_FAILURE);
      }else if(Nthreads > nprocs){
	fprintf(stderr, "# Setting Nthreads to max allowed value of %ld.\n", nprocs);
	Nthreads = nprocs;
      }else if(Nthreads < 0){
	fprintf(stderr, "# Setting Nthreads to min allowed value of 1.\n");
	Nthreads = 1;
      }
      break;
    case 'c' :
      if(sscanf(optarg, "%ld", &n_markers_in_chunk) != 1  ||  errno != 0){
	fprintf(stderr, "# n_markers_in_chunk; conversion of argument %s to long failed.\n", optarg);
	exit(EXIT_FAILURE);
      }else if(n_markers_in_chunk < min_chunk_size){
	fprintf(stderr, "# Setting n_markers_in_chunk to min allowed value of %ld.\n", min_chunk_size);
	n_markers_in_chunk = min_chunk_size;
      }
      break;
    case 'a' :
      use_alt_marker_id = true;
      break;
    case 'h' :
      print_usage_info(stdout);
      exit(EXIT_FAILURE);
      break;
    case 'r' :
      shuffle_accessions = true;
      break;
    case 's' :
      if(sscanf(optarg, "%ld", &rand_seed) != 1  ||  errno != 0){
	fprintf(stderr, "# rand_seed; conversion of argument %s to long failed.\n", optarg);
	exit(EXIT_FAILURE);
      }
      break;
      /*  case '?': */
      /* printf("? case in command line processing switch.\n"); */
      /* if ((optopt == 'g') || (optopt == 'x') || (optopt == 'o')) */
      /* 	fprintf(stderr, "Option -%c requires an argument.\n", optopt); */
      /* else if (isprint (optopt)) */
      /* 	fprintf(stderr, "Unknown option `-%c'.\n", optopt); */
      /* else */
      /* 	fprintf(stderr, "Unknown option character: %d\n", optopt); */
      /* exit(EXIT_FAILURE); */
    default:
      fprintf(stderr, "# Unknown option %c\n", (char)an_option);
      exit(EXIT_FAILURE);
    } // end of switch block
  } // end of command line processing loop

  if (argc < 2) {
    print_usage_info(stdout);
    exit(EXIT_FAILURE);
  }

  if(input_filename == NULL){
    fprintf(stderr, "No input (vcf) file specified.\n");
    print_usage_info(stdout);
    exit(EXIT_FAILURE);
  }
  out_stream = fopen(output_filename->a, "w");
  if(out_stream == NULL){
    fprintf(stderr, "Failed to open %s for writing.\n", output_filename->a);
    exit(EXIT_FAILURE);
  }

  clockid_t the_clock = CLOCK_MONOTONIC;
  struct timespec tspec;
  if(clock_getres(the_clock, &tspec) == 0){
    double t_resolution = tspec.tv_sec + 1.0e-9 * tspec.tv_nsec;
    if(t_resolution > 1.0e-3) fprintf(stderr, "# timing resolution is %8.5lf\n", t_resolution);
  }else{
    exit(EXIT_FAILURE);
  }
  double t0 = clock_time(the_clock);
  srand(rand_seed);

  if(Nthreads > 0){
    fprintf(stdout, "# Using %ld threads.\n", Nthreads);
  }else{
    fprintf(stdout, "# Unthreaded.\n");
  }

  
  // ****************************************************
  // *****   Read first line; store accession ids.  *****
  // ****************************************************

  char* line = NULL;
  size_t len = 0;
  ssize_t nread;
  
  long accid_count = 0;
  Vstr* accession_ids = construct_vstr(INIT_N_ACCESSIONS);
  while((nread = getline(&line, &len, in_stream)) != -1){
    char* saveptr = NULL;
    char* token = strtok_r(line, split_str, &saveptr);
    
    if((token == NULL) || (token[0] == '#' && token[1] == '#')) continue; // skip comments (starting with ##) and any empty lines
    if(token[0] == '#'){ // this the line with accession ids
      for(long ii = 1; ii <= 8; ii++){ // read in cols 1 through 8 "POS ID REF ..." but don't store them.
	token = strtok_r(NULL, split_str, &saveptr);
      }
      while(1){
	token = strtok_r(NULL, split_str, &saveptr);
	if(token == NULL) break;
	long tkn_length = strlen(token);
	for(long i=0; i< tkn_length; i++){
	  if(token[i] == ' ') token[i] = '_';
	}
	char* acc_id = strcpy((char*)malloc((strlen(token)+1)*sizeof(char)), token);

	push_to_vstr(accession_ids, acc_id); // store
	accid_count++;
      }
      break;
    }else{ 
      fprintf(stderr, "token: %s (should start with #)\n", token);
      exit(EXIT_FAILURE);
    }
  }
  chomp(accession_ids->a[accession_ids->size-1]); // chomp off any trailing newline(s) from the last acc_id 
  long n_accessions = accid_count;

  // *********************************************************
  // *****   Read the rest of the lines, one per marker  *****
  // *****   Each line has genotypes for all accessions  *****
  // *********************************************************
  
  fprintf(stderr, "# markers analyzed in each chunk: %ld ; which is %ld per thread\n",
	  n_markers_in_chunk, (Nthreads >= 1)? n_markers_in_chunk/Nthreads : n_markers_in_chunk);
  Vstr* marker_lines = construct_vstr(n_markers_in_chunk);

  Vstr* all_used_markerids = construct_vstr(1000);
  Vstr* all_used_genos = construct_vstr(1000);
  
  long total_lines_read = 0;
  while(nread >= 0){ // loop over chunks
    long line_count = 0;
    // read n_markers_in_chunk lines (or up to eof)
    while(
	  (line_count < n_markers_in_chunk) &&
	  ((nread = getline(&line, &len, in_stream)) != -1)
	  ){
      char* line_copy = strcpy( (char*)malloc((nread+1)*sizeof(char)), line);
      chomp(line_copy);
      push_to_vstr(marker_lines, line_copy);
      line_count++;
    }
    total_lines_read += line_count;
    fprintf(stdout, "lines read: %ld\n", total_lines_read);
    // ********************************************************
    // *****  Extract genotypes, and quality information  *****
    // *****  Filter if requested and store genotypes     *****
    // ********************************************************
    
    if(Nthreads == 0){ // process without creating any new threads
      TD td;
      td.n_accessions = n_accessions;
      td.marker_lines = marker_lines;
      td.first_marker = 0;
      td.last_marker = marker_lines->size-1;
      td.use_alt_marker_id = use_alt_marker_id;
      td.minGP = minGP;
      td.delta = delta;
      td.minmaf = min_maf;
      td.maxmd = max_marker_md;
      td.ploidy = ploidy;
      td.marker_ids = construct_vstr(1000);
      td.gntps = construct_vstr(100); // chunk_genos;
      process_marker_range((void*)(&td));
      for(long im=0; im<td.marker_ids->size; im++){ // loop over stored markers
	push_to_vstr(all_used_genos, td.gntps->a[im]);
	push_to_vstr(all_used_markerids, td.marker_ids->a[im]);
      }
      free(td.marker_ids); // but don't free the c strings containing the actual ids, which are stored in all_used_markerids.
      free(td.marker_ids->a);
      free(td.gntps); // but don't free the c strings containing the actual genotypes (dosages), which are stored in all_used_genos.
      free(td.gntps->a);
    }else{ // 1 or more pthreads
      TD* td = (TD*)malloc(Nthreads*sizeof(TD));
      for(long i_thread = 0; i_thread<Nthreads; i_thread++){
	td[i_thread].n_accessions = n_accessions;
	td[i_thread].marker_lines = marker_lines;
	td[i_thread].first_marker = (i_thread == 0)? 0 : td[i_thread-1].last_marker + 1;
	td[i_thread].last_marker = (long)((double)(i_thread+1)*(marker_lines->size)/Nthreads - 1);
	td[i_thread].use_alt_marker_id = use_alt_marker_id;
	td[i_thread].minGP = minGP;
	td[i_thread].delta = delta;
	td[i_thread].minmaf = min_maf;
	td[i_thread].maxmd = max_marker_md;
	td[i_thread].ploidy = ploidy;
	td[i_thread].marker_ids = construct_vstr(1000);
	td[i_thread].gntps = construct_vstr(1000); //chunk_genos[i_thread];
      }
      td[Nthreads-1].last_marker = marker_lines->size-1;
    
      pthread_t* thrids = (pthread_t*)malloc(Nthreads*sizeof(pthread_t));
      for(long i=0; i<Nthreads; i++){ // run the threads
	int iret = pthread_create( thrids+i, NULL, process_marker_range, (void*) (td+i));
	if(iret > 0) fprintf(stderr, "# warning. pthread_create returned non-zero value. Thread %ld \n", (long)thrids[i]);
      }
      for(long i_thread=0; i_thread<Nthreads; i_thread++){ // wait for threads to terminate.
	pthread_join(thrids[i_thread], NULL);
      }
    
      // store results from this chunk
      for(long ith=0; ith<Nthreads; ith++){ // loop over threads
	for(long im=0; im<td[ith].gntps->size; im++){ // loop over markers stored by thread ith
	  push_to_vstr(all_used_genos, td[ith].gntps->a[im]); //chunk_genos[ith]->a[im]);
	  push_to_vstr(all_used_markerids, td[ith].marker_ids->a[im]);
	}
	free(td[ith].marker_ids); // but don't free the c strings containing the actual ids, which are stored in all_used_markerids.
	free(td[ith].marker_ids->a);
	free(td[ith].gntps); // but don't free the c strings containing the actual genotypes (dosages), which are stored in all_used_genos.
	free(td[ith].gntps->a);
      }
      
      free(td);
      free(thrids);
    } // end >=1 pthreads branch

    for(long im=0; im < marker_lines->size; im++){
      free(marker_lines->a[im]); // free the c-strings containing the lines of this chunk.
    }
    marker_lines->size = 0; // done with this chunk, let next chunk overwrite marker_lines->a
    // if(nread == -1) break; // eof reached
  } // end of loop over chunks
  fprintf(stderr, "# marker_lines, size and capacity: %ld %ld\n", marker_lines->size, marker_lines->capacity);
  free_vstr(marker_lines);
  fprintf(stderr, "# number of markers left after filtering: %ld  %ld\n", all_used_markerids->size, all_used_genos->size);
  free(line);
  fclose(in_stream);
  
  // **********************
  // *****  output  *******
  // **********************
  fprintf(stderr, "# Begin output.\n");
  Vlong* accession_indices = construct_vlong_whole_numbers(accession_ids->size);
  if(shuffle_accessions) shuffle_vlong(accession_indices); 
    
  fprintf(out_stream, "MARKER");
  for(long i_marker=0; i_marker<all_used_markerids->size; i_marker++){
    fprintf(out_stream, " %s", all_used_markerids->a[i_marker]);
  }fprintf(out_stream, "\n");
  for(long iacc=0; iacc< accid_count; iacc++){
    long i_accession = accession_indices->a[iacc];
    fprintf(out_stream, "%s", accession_ids->a[i_accession]);
    for(long im=0; im<all_used_markerids->size; im++){
      fprintf(out_stream, " %c", all_used_genos->a[im][i_accession]);
    }
    fprintf(out_stream, "\n");
  }
  fclose(out_stream);

  double t3 = clock_time(the_clock);
  fprintf(stdout, "# Done. Time: %8.4lf\n", t3-t0);
  
  // *****  cleanup  *****
  //getchar();
  // free_vstr(marker_ids); // getting free() invalid pointer with this.
  fprintf(stderr, "# before final freeing of memory.\n");
  fprintf(stderr, "# size, capacity of accession_indices: %ld %ld\n", accession_indices->size, accession_indices->capacity);
  free_vlong(accession_indices);
  fprintf(stderr, "# size, capacity of accession_ids: %ld %ld\n", accession_ids->size, accession_ids->capacity);
  //  getchar();
  free_vstr(accession_ids);
  fprintf(stderr, "# size, capacity of all_used_markerids: %ld %ld\n", all_used_markerids->size, all_used_markerids->capacity);
  //  getchar();
  free_vstr(all_used_markerids);
  fprintf(stderr, "# size, capacity of all_used_genos: %ld %ld\n", all_used_genos->size, all_used_genos->capacity);
  // getchar();
  free_vstr(all_used_genos);
  // getchar();
  free_vchar(output_filename);
  //  getchar();
} // end of main


  //////////////////////////////////////////////////
  //         subroutine definitions               //  
  //////////////////////////////////////////////////

void* process_marker_range(void* x){

  TD* td = (TD*)x;
  long n_accessions = td->n_accessions;
  Vstr* marker_lines = td->marker_lines;
  long first_marker = td->first_marker;
  long last_marker = td->last_marker;
  
  Vstr* marker_ids = td->marker_ids;

  long marker_count = 0;
  char* line;
  for(long i_marker=first_marker; i_marker<=last_marker; i_marker++){
    line = marker_lines->a[i_marker];

    char* one_marker_gts = (char*)malloc((n_accessions+1)*sizeof(char)); // 
      
    char* saveptr = line;
    long tidx = 0;
    Vchar* chromosome = construct_vchar_from_str(split_on_char(line, '\t', &tidx));
    Vchar* position = construct_vchar_from_str(split_on_char(line, '\t', &tidx)); 

    Vchar* marker_id;
    char* token = split_on_char(line, '\t', &tidx); 
    if(td->use_alt_marker_id){  
      marker_id = construct_vchar_from_str(chromosome->a);
      append_char_to_vchar(marker_id, '_');
      append_str_to_vchar(marker_id, position->a);
    }else{
      marker_id = construct_vchar_from_str(token); // strcpy((char*)malloc((strlen(token)+1)*sizeof(char)), token);
    }
  
    free_vchar(chromosome);
    free_vchar(position);
    char* ref_allele =  split_on_char(line, '\t', &tidx); // strtok_r(NULL, split_str, &saveptr);
    char* alt_allele =  split_on_char(line, '\t', &tidx); // strtok_r(NULL, split_str, &saveptr);
   
    for(long i = 1; i <= 3; i++){ // read the next 3 cols -
      token =  split_on_char(line, '\t', &tidx); // strtok_r(NULL, split_str, &saveptr);
    }

    token =  split_on_char(line, '\t', &tidx); // strtok_r(NULL, split_str, &saveptr);
    char* format = strcpy((char*)malloc((strlen(token)+1)*sizeof(char)), token); // format string, e.g. GT:DS:GP
    long GTidx, GQidx, GPidx, DSidx;
    get_GT_GQ_GP_DS_indices(format, &GTidx, &GQidx, &GPidx, &DSidx);
    free(format);
    long acc_index = 0;
    long md_count = 0;
    long alt_allele_count = 0;
    //if(1){
    if(GTidx >= 0){ // GT present, use it.
      while(1){ // read genotypes from one line, i.e. one marker
	token = split_on_char(line, '\t', &tidx);
	if(token == NULL)	break; // end of line has been reached.
	// fprintf(stderr, "token: [%s]     ", token);
	char genotype = token_to_genotype_GT(token, GTidx, GPidx, td->minGP);
	//	fprintf(stderr, "[%s]     [%c]\n", token, genotype);
	one_marker_gts[acc_index] = genotype;
	if(genotype == 'X') {
	  md_count++;
	}else{
	  alt_allele_count += (genotype - 48);
	}
	acc_index++;   
      } // done reading genotypes for all accessions of this marker
    }else if(DSidx >= 0){ // GT absent, but DS present, use it.

      while(1){ // read genotypes from one line, i.e. one marker
	token = split_on_char(line, '\t', &tidx);
	if(token == NULL){	  
	  break; // end of line has been reached.
	}
	char genotype = token_to_genotype_DS(token, DSidx, GPidx, td->minGP, td->delta);
	one_marker_gts[acc_index] = genotype;
	if(genotype == 'X') {
	  md_count++;
	}else{
	  alt_allele_count += (genotype - 48);
	}
	acc_index++;
      }
    }else{
      fprintf(stderr, "# Neither GT nor DS present. \n");
    }
    // have read dosages for one marker, now filter on missing data, maf
    double mdf = (double)md_count/n_accessions;
    double maf = (double)alt_allele_count/(td->ploidy*(n_accessions-md_count));
    if(maf > 0.5) maf = 1.0 - maf;
    if((mdf <= td->maxmd) && (maf >= td->minmaf)){ // keep this marker
      push_to_vstr(td->gntps, one_marker_gts);
      push_to_vstr(marker_ids, marker_id->a);
      free(marker_id); // but don't free marker_id->a
    }else{ // this marker is not used
      free(one_marker_gts); 
      free_vchar(marker_id);
    }
     
    assert(acc_index == td->n_accessions); // check that this line has number of accessions = number of accession ids.
    marker_count++;
  } // done reading all lines (markers)
  // fprintf(stdout, "# A thread is done processing markers %ld through %ld; %ld markers x %ld accessions\n", first_marker, last_marker, marker_count, td->n_accessions);
}

char token_to_genotype_GT(char* token, long gtidx, long gpidx, double minGP){
  char result;
  char* saveptr;
  bool quality_ok = true; // (gpidx >= 0  &&  minGP > 0)? false : true;
  long idx = 0;
  char* tkn = strtok_r(token, ":", &saveptr);
  if(idx == gtidx){ // tkn should be e.g. 0|1 or 0/1 or 1/1
    result = GTstr_to_dosage(tkn); // return result;
  }else if(idx == gpidx){
    quality_ok =
      GP_to_quality_ok(tkn, minGP);
  }
  idx++;
  
  while(1){ // get more subtokens
    tkn = strtok_r(NULL, ":", &saveptr);
    if(tkn == NULL)	break; // end of line has been reached.
    if(idx == gtidx){ // tkn should be e.g. 0|1 or 0/1 or 1/1
      result = GTstr_to_dosage(tkn);
    }else if(idx == gpidx){
      quality_ok =
	GP_to_quality_ok(tkn, minGP);
    }
    idx++;   
  }

  if(! quality_ok) result = 'X';
  return result;
}

char token_to_genotype_DS(char* token, long dsidx, long gpidx, double minGP, double delta){
  char result;
  char* saveptr;
  bool quality_ok = true; // (gpidx >= 0  &&  minGP > 0)? false : true;
  long idx = 0;
  long tidx = 0;
  if(dsidx == 0  &&  token[0] == '\0') return 'X';
  char* tkn = split_on_char(token, ':', &tidx);
  if(idx == dsidx){ // tkn should be e.g. 0.002  0.99
    result = DSstr_to_dosage(tkn, delta); // return result;
  }else if(idx == gpidx){
    quality_ok =
      GP_to_quality_ok(tkn, minGP);
  }
  idx++;
  
  while(1){ // get more subtokens
    tkn = split_on_char(token, ':', &tidx);
    if(tkn == NULL)	break; // end of line has been reached.
    if(idx == dsidx){ // tkn should be e.g. 0|1 or 0/1 or 1/1
      result = DSstr_to_dosage(tkn, delta);
    }else if(idx == gpidx){
      quality_ok =
	GP_to_quality_ok(tkn, minGP);
    }
    idx++;   
  }
  if(! quality_ok) result = 'X';
  return result;
}

bool GP_to_quality_ok(char* token, double minGP){
  if(minGP > 0.0){
    bool quality_ok = false;
    float p0, p1, p2;
    if(sscanf(token, "%f,%f,%f", &p0, &p1, &p2) == 3){
      quality_ok = (p0 >= minGP  ||  p1 >= minGP  || p2 >= minGP);
    }
    return quality_ok;
  }else{
    return true; 
  }
}

char GTstr_to_dosage(char* tkn){
  long d = 0;
  char a1 = tkn[0];
  char a2 = tkn[2];
  if(a1 == '1'){
    d++;
  }else if(a1 == '.'){
    return 'X';
  }
  if(a2 == '1'){
    d++;
  }else if(a2 == '.'){
    return 'X';
  }
  return (char)(d + 48);
}

char DSstr_to_dosage(char* tkn, double delta){
  if(tkn[0] == '\0') return 'X';
  double ds = -1;
  long n_ok = sscanf(tkn, "%lf", &ds);
  int lds = round(ds);
  if(fabs(ds - lds) > delta) return 'X';
  char chards = (char)round(ds) + 48;
  return (n_ok == 1)? chards : 'X';
}

void get_GT_GQ_GP_DS_indices(char* format, long* GTp, long* GQp, long* GPp, long* DSp){
  *GTp = -1;
  *GQp = -1;
  *GPp = -1;
  *DSp = -1;
  long index = 0;
  char* saveptr = format;
  char* token = strtok_r(format, ":", &saveptr);
  if(strcmp(token, "GT") == 0){ *GTp = index;}
  else if(strcmp(token, "GQ") == 0){ *GQp = index;}
  else if(strcmp(token, "GP") == 0){ *GPp = index;}
  else if(strcmp(token, "DS") == 0){ *DSp = index;}
  while(1){ // read in cols 1 through 8 "POS ID REF ..."
    index++;
    token = strtok_r(NULL, ":", &saveptr);
    if(token == NULL) break; 
    if(strcmp(token, "GT") == 0){ *GTp = index;}
    else if(strcmp(token, "GQ") == 0){ *GQp = index;}
    else if(strcmp(token, "GP") == 0){ *GPp = index;}
    else if(strcmp(token, "DS") == 0){ *DSp = index;}
  }
  return;
}

char* split_on_char(char* str, char c, long* iptr){
  long i_begin = *iptr;
  long i = i_begin;
  if(str[i_begin] == '\0'){
    return NULL; // indicates have reached the end of the string - no more token
  }
  while(1){
    if(str[i] == '\0'){
      *iptr = i;
      break;
    }
    if(str[i] == c){
      str[i] = '\0';
      *iptr = i+1;
      break;
    }
    i++;
  }
  return str+i_begin;
} 

void chomp(char* str){ // remove any trailing newlines from str
  long len = strlen(str);
  while(str[len-1] == '\n'){
    str[len-1] = '\0';
    len--;
  }
}

void print_usage_info(FILE* ostream){
  fprintf(stdout, "Options:\n");
  fprintf(stdout, "  -input       Input vcf filename (required).\n");
  fprintf(stdout, "  -out         Output filename (default: vcftogts.out)\n");
  fprintf(stdout, "  -threads     Number of threads to use. (Default: automatic, from get_nprocs.\n");
  fprintf(stdout, "  -chunk_size  Store and process this many input lines at a time. (Default: 5040)\n");
  fprintf(stdout, "  -alternate_marker_ids  Construct marker ids from chromosome, position. (Default: 0 (false))\n");
  fprintf(stdout, "  -pmin        Min. estimated genotype probibility (if GP field present; default: 0.9)\n");
  fprintf(stdout, "  -min_maf     Exclude markers with minor allele frequency < this. (Default: 0.1)\n");
  fprintf(stdout, "  -max_marker_md  Exclude markers with proportion of missing data > this. (Default: 0.25)\n");
  fprintf(stdout, "  -delta       If using DS field, must be within this of an integer or call it missing data. (Default: 0.1)\n");
  fprintf(stdout, "  -randomize   Randomize the order of accessions in output (Default: 0 (false))\n");
  fprintf(stdout, "  -seed        Random number generator seed. Only relevant if randomizing accession output order.\n");
}

// unused

/* bool GP_to_quality_ok_xxx(char* token, double minGP){ */
/*    if(minGP > 0.0){ */
/*      char* saveptr; */
/*      char* tkn = strtok_r(token, ",", &saveptr); */
/*      if(atof(tkn) >= minGP) return true; */
/*      tkn = strtok_r(NULL, ",", &saveptr); */
/*      if(atof(tkn) >= minGP) return true; */
/*      tkn = strtok_r(NULL, ",", &saveptr); */
/*      if(atof(tkn) >= minGP) return true; */
/*      return false; */
/*    }else{ */
/*      return true; */
/*    } */
/*  } */

/* Vchar* token_to_plink_genotype(char* token, Vchar** alleles, long gtidx, long gpidx, double minGP){ */
/*   Vchar* plnkgt; */
/*   char* saveptr; */
/*   long idx = 0; */
/*   bool quality_ok = true; */
/*   char* tkn = strtok_r(token, ":", &saveptr); */
/*   if(idx == gtidx){ // tkn should be e.g. 0|1 or 0/1 or 1/1 */
/*     plnkgt = GT_to_plnkgt(tkn, alleles); */
/*   }else if(idx == gpidx){ */
/*     quality_ok = GP_to_quality_ok(tkn, minGP); */
/*   } */
/*   idx++;  */
/*   while(1){ // read genotypes from one line, i.e. one marker */
/*     tkn = strtok_r(NULL, ":", &saveptr); */
/*     if(tkn == NULL)	break; // end of line has been reached. */
/*     if(idx == gtidx){ // tkn should be e.g. 0|1 or 0/1 or 1/1 */
/* 	plnkgt = GT_to_plnkgt(tkn, alleles); */
/*     }else if(idx == gpidx){ */
/* 	quality_ok = GP_to_quality_ok(tkn, minGP); */
/*     } */
/*     idx++; */
/*   } */
/*   if(! quality_ok){ */
/*     free_vchar(plnkgt); */
/*     plnkgt = construct_vchar_from_str("\t0\t0"); */
/*   } */
/*   return plnkgt; */
/* } */

/* Vchar* GT_to_plnkgt(char* token, Vchar** alleles){ */
/*   // token would be for example "0/1", returns plnkgt, whose string part plnkgt->a would be e.g. "\tA\tACTA" */
/*   Vchar* plnkgt = construct_vchar(16); */
/*   append_char_to_vchar(plnkgt, '\t'); */
/*   if(token[0] == '0'){ */
/*     append_str_to_vchar(plnkgt, alleles[0]->a); */
/*   }else if(token[0] == '1'){ */
/*     append_str_to_vchar(plnkgt, alleles[1]->a); */
/*   }else{ */
/*     append_char_to_vchar(plnkgt, '0'); */
/*   } */
/*   append_char_to_vchar(plnkgt, '\t'); */
/*   if(token[2] == '0'){ */
/*     append_str_to_vchar(plnkgt, alleles[0]->a); */
/*   }else if(token[2] == '1'){ */
/*     append_str_to_vchar(plnkgt, alleles[1]->a); */
/*   }else{ */
/*     append_char_to_vchar(plnkgt, '0'); */
/*   } */
/*   return plnkgt; */
/* } */
