#!/usr/bin/perl -w
use strict;
use Getopt::Long;
use List::Util qw(min max sum shuffle);
use File::Spec;
use File::Basename 'dirname';
use List::Util qw 'min max sum';


use Cwd 'abs_path';
my $bindir;
BEGIN {     # this has to go in Begin block so happens at compile time
  $bindir =
    dirname( abs_path(__FILE__) ) ; # the directory containing this script
}

# runs duplicatesearch, then clusterer, and outputs a file
# with the same format as duplicatesearch input, but now with just one
# line representing each cluster.

my $input_dosages_filename = undef;
#my $do_remove_bad_accessions = shift // m
my $max_acc_missing_data_fraction = 0.5;
my $max_marker_missing_data_fraction = 0.25;
my $min_maf = 0.08;
my $output_dosages_filename = undef;
my $max_distance = 0.15;
my $cluster_max_distance = 'auto';
my $cluster_fraction = 0.0; # fraction of other cluster members to keep (aside from one representative which is always kept)
my $vote = 0;

GetOptions(
	   'input_file=s' => \$input_dosages_filename,
	   'output_file=s' => \$output_dosages_filename,
	   'acc_max_md_fraction=f' => \$max_acc_missing_data_fraction,
	   'marker_max_md_fraction=f' => \$max_marker_missing_data_fraction,
	   'distance_max=f' => \$max_distance,
	   'min_maf|maf_min=f' => \$min_maf,
	   'cluster_max_distance=s' => \$cluster_max_distance,
	   'fraction=f' => \$cluster_fraction,
	   'vote!' => \$vote,
	  );

if (!defined $input_dosages_filename) {
  print "Must supply input file name.\n";
  usage_message();
  exit;
}
if (!defined $output_dosages_filename) {
  (my $v, my $d, $output_dosages_filename) = File::Spec->splitpath( $input_dosages_filename );
  $output_dosages_filename .= "_duplicates_removed";
  print STDERR "$d    $output_dosages_filename \n";
}


# open my $fhin, "<", "$input_dosages_filename" or die "couldn't open $input_dosages_filename for reading.\n";
my $cleaned_dosages_filename = $input_dosages_filename . "_cleaned";

####   remove accessions with excessive missing data   #########################

# if (0) {
#   open my $fhout, ">", "$cleaned_dosages_filename";
#   my $n_markers = undef;
#   my $n_bad_accessions = 0;
#   print STDERR "# Removing accessions with too much missing data.\n";
#   while (my $line_in = <$fhin>) {
#     if ($line_in =~ /^\s*#/) {
#       # print STDERR "print comment line.\n";
#       print $fhout $line_in;
#     } elsif ($line_in =~ /^MARKER/) {
#       #  print STDERR "print marker ids line.\n";
#       print $fhout $line_in;
#       my @markers = split(" ", $line_in);
#       $n_markers = scalar @markers  - 1;
#     } else {
#       if($line_in =~ /^\s*(\S+)/){
# 	my $accession_id = $1;
#       die "File lacks line with MARKER and marker ids\n" if(! defined $n_markers);
#       my $n_bad = () = $line_in =~ /\sNA/g;
#       if ($n_bad/$n_markers <= $max_acc_missing_data_fraction) {
# 	print $fhout $line_in;
#       } else {
# 	$n_bad_accessions++;
# 	print STDERR "Removing accession $accession_id, which has missing data for $n_bad markers.\n";
#       }
# }
#     }
#   }
#   close $fhout;
#   print STDERR "# $n_bad_accessions accessions eliminated due to excessive missing data.\n";
#   print "# $n_bad_accessions accessions eliminated due to excessive missing data (>" ,
#     int($max_acc_missing_data_fraction*100 + 0.5), "\%)\n";
# } else {
# system "~/gtsimsrch/src/bad_accessions_begone.pl -i $input_dosages_filename -o $cleaned_dosages_filename -m $max_acc_missing_data_fraction";
# }

print STDERR "dosages file with high-missing data accessions removed: $cleaned_dosages_filename \n";
#exit;

my $duplicatesearch_command = "duplicatesearch  -i $input_dosages_filename -e $max_distance ";
$duplicatesearch_command .= " -accession_max_missing_data  $max_acc_missing_data_fraction ";
$duplicatesearch_command .= " -maf_min $min_maf ";
$duplicatesearch_command .= "-marker_max_missing_data $max_marker_missing_data_fraction " if(defined $max_marker_missing_data_fraction);

###############################################################################


####   Run duplicatesearch :   ################################################
system "$duplicatesearch_command";
###############################################################################

my $cluster_command = $bindir . "/clusterer.pl " . " -in duplicatesearch.out -out cluster.out ";
  $cluster_command .= " -cluster $cluster_max_distance  -nofull ";
print $cluster_command, "\n";
#exit();
system $cluster_command;
###############################################################################


####  Store ids of cluster accessions in    ###################################
open my $fh_clusters, "<", "cluster.out";
my %clusterids = ();

while (my $line = <$fh_clusters>) { # each line is one cluster
  next if($line =~ /^\s*#/);
  my @cols = split(" ", $line);
  my $cluster_size = shift @cols;
  my $min_d = shift @cols;
  my $max_d = shift @cols;
  my $n_bad = shift @cols;
  my $rep_id = $cols[0];     # id of the representative of the cluster
  for my $a_cluster_id (@cols) {
    $clusterids{$a_cluster_id} = 1; # all accessions in clusters get stored in @clusterids
  }
}
close $fh_clusters;
###############################################################################


####   Store dosages  #########################################################
print STDERR "before storing dosages\n";

# store individual lines of dosage file in hash
open my $fh_dosages, "<", "$input_dosages_filename";
open my $fhout, ">", "$output_dosages_filename" or die "Couldn't open $output_dosages_filename for writing.\n";

# store ids and genotypes of clusters (size >= 2), and output singletons.
my %id_gts  = ();  # key: ids; value: array ref of dosages (0,1,2,NA) 
my $first_line = <$fh_dosages>;
print $fhout $first_line;
while (my $line = <$fh_dosages>) {
  next if($line =~ /^\s*#/);	# skip comments
  my @cols = split(" ", $line);
  my $id = shift @cols;
  if (exists $clusterids{$id}) {
    $id_gts{$id} = \@cols;
  } else {
    print $fhout $line;
  }
}
close($fh_dosages);
print STDERR "after storing dosages\n";

###############################################################################
# my $file_delete_success = unlink($cleaned_dosages_filename);
# warn "Deleting of $cleaned_dosages_filename failed.\n" if($file_delete_success != 1);

my @duplicate_lines = ();
####   Cluster members vote on correct genotypes   ############################
open  $fh_clusters, "<", "cluster.out";
if ($vote  and  $cluster_fraction == 0) { # cluster members vote, and then output just representative id with 'elected' dosages. 
  while (my $line = <$fh_clusters>) { # each line is one cluster
    next if($line =~ /^\s*#/);
    my @cols = split(" ", $line);
    my $cluster_size = shift @cols;
    my $min_d = shift @cols;
    my $max_d = shift @cols;
    my $n_bad = shift @cols;
    my $rep_id = $cols[0];   # id of the representative of the cluster

    #  print STDERR "done storing cluster of size $cluster_size \n";
    # print STDERR "before vote\n";
    my $elected_gts = vote(\@cols, \%id_gts);
    #print STDERR "Done with cluster vote. size of elected_gts: ", scalar @$elected_gts, "\n";
    #  print STDERR "done with cluster vote \n";
    print $fhout "$rep_id  ", join(" ", @$elected_gts), "\n";
  }

} else { # output representative, and fraction $cluster_fraction of other cluster members
  while (my $line = <$fh_clusters>) { # each line is one cluster
    next if($line =~ /^\s*#/);
   
    my @cols = split(" ", $line);
    # print join("  ", @cols), "\n";
    next if(scalar @cols  < 10);
    my $cluster_size = shift @cols;
    my $min_d = shift @cols;
    my $avg_d = shift @cols;
    my $max_d = shift @cols;
    my $min_intraextra_d = shift @cols;
    my $n_near1 = shift @cols;
    my $n_near2 = shift @cols;
    my $n_big_intra_d = shift @cols;
    my $n_missing_dist = shift @cols;
    my $rep_id = shift @cols; # id of the representative of the cluster
    # print "rep_id: $rep_id\n";
    #  print STDERR "done storing cluster of size $cluster_size \n";
    # print STDERR "before vote\n";
    #my $elected_gts = vote(\@cols, \%id_gts);
    #print STDERR "Done with cluster vote. size of elected_gts: ", scalar @$elected_gts, "\n";
    #  print STDERR "done with cluster vote \n";
    print $fhout "$rep_id  ", join(" ", @{$id_gts{$rep_id}}), "\n"; # output representative and its dosages.
    for my $an_id (@cols) {
     # if (rand() < $cluster_fraction) {
     # print $fhout
	my $dupe_acc_line = 'DDDD' . "$an_id  " . join(" ", @{$id_gts{$an_id}}) . "\n"; # other cluster members and dosages.
      push @duplicate_lines, $dupe_acc_line;
    
    }
  }
}
close $fh_clusters;
###############################################################################
# output some fraction of duplicates:
@duplicate_lines = shuffle @duplicate_lines;
my $n_duplicates_to_output = int($cluster_fraction * scalar @duplicate_lines + 0.5);
for my $i (1..$n_duplicates_to_output){
  print $fhout $duplicate_lines[$i];
}
close $fhout;


# while (my $line = <$fh_clusters>) { # each line is one cluster
#   next if($line =~ /^\s*#/);
#   my @cols = split(" ", $line);
#   my $min_md_id = $cols[4];
#   my $min_md_count = $id_mdcount{$min_md_id};
#  # if (1) { # uniquified file has accession with the least missing data.
#     for my $a_cluster_id (@cols[4..$#cols]) {
#       $clusterids{$a_cluster_id} = 1; # all accessions in clusters get stored in @clusterids
#       if (0) {
# 	if ($id_mdcount{$a_cluster_id} < $min_md_count) { # find the member of cluster with the least missing data
# 	  $min_md_count =  $id_mdcount{$a_cluster_id};
# 	  $min_md_id = $a_cluster_id;
# 	}
#       }
#     }
#     print $id_line{$min_md_id};
#   # } else { # uniquified file just has first-occurring accession in cluster 
#   #   print $id_line{$cols[4]};
#   # }
# }

# print the lines of the singletons (accessions in clusters of size 1)
# my @sorted_ids = sort {$a cmp $b} keys %id_gts; # sort keys and output in this order for repeatability
# #while (my($an_id, $a_line) = each %id_line) {
# for my $an_id (@sorted_ids) {
#   if (! exists $clusterids{$an_id}) {
#     print "$an_id  ", join(" ", $id_gts{$an_id}), "\n";
#   }
# }

# ************************************************

sub vote{
  my $cluster_ids = shift; # array ref holding the ids of the accessions in the cluster.
  my $id_dosages = shift; # hash ref. key: id, value: array ref of dosages for this id.
  # print STDERR "in vote. new cluster:  \n";
  my $first_id = $cluster_ids->[0];
  my $first_dosages = $id_dosages->{$first_id};
  my $n_markers = scalar @$first_dosages;
  my @elected_dosages = (0) x $n_markers;
  my $cluster_size = scalar @$cluster_ids;
  if ($cluster_size > 2) {
    my @marker_votes = ();;
    for (1..$n_markers) {
      push @marker_votes, [0, 0, 0, 0];
    }
    for my $an_id (@$cluster_ids) {
      my $dosages = $id_dosages->{$an_id}; # array ref of dosages for $an_id
      while (my($i, $d) = each @$dosages) {
	$d = 3 if($d eq 'NA');
	$marker_votes[$i]->[$d]++;
      }
      while (my($i, $mv) = each @marker_votes) {
	my $e = 'NA';
	#   print STDERR "    $i  ", join(" ", @$mv), "\n";
	for my $j (0..2) {
	  my $vote = $mv->[$j]; # the number of votes for dosage = $j for marker $i
	  if (2*$vote > $cluster_size) { # must have > 50% for one dosage, or 'NA'
	    $e = $j;
	    last;
	  }
	}
	$elected_dosages[$i] = $e;
      }
    }
  } else {			# cluster size == 2
    while (my($i, $d1) = each @$first_dosages) {
      my $second_id = $cluster_ids->[1];
      my $d2 = $id_dosages->{$second_id}->[$i];
      if ($d1 eq $d2) {
	$elected_dosages[$i] = $d1;
      } elsif ($d1 eq 'NA') { # d1 is NA, use d2 (which might also be NA)
	$elected_dosages[$i] = $d2;
      } elsif ($d2 eq 'NA') {	# d2 is NA, use d1 (which is not NA)
	$elected_dosages[$i] = $d1;
      } else {		    # neither is NA, and they disagree; use NA
	$elected_dosages[$i] = 'NA';
      }
    }
  } 
  return \@elected_dosages;
}

sub usage_message{
  print "Usage: uniquify -i <input filename> [-o <output filename>] [-m <max allowed fraction missing data>].\n";
}

