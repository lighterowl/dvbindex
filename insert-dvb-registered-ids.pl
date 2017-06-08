#!/usr/bin/perl

# dvbindex - a program for indexing DVB streams
# Copyright (C) 2017 Daniel Kamil Kozar
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
# Street, Fifth Floor, Boston, MA  02110-1301, USA.

use strict;
use warnings;

use Spreadsheet::XLSX;
use DBI;
use LWP::UserAgent;
use Readonly;

die "Usage : $0 dbfile" unless scalar(@ARGV) == 1;

my $dbh = DBI->connect( "dbi:SQLite:dbname=$ARGV[0]", "", "" );

sub handle_bouquet_id {
  my $xlsx = shift;
  $dbh->do(
'create table registered_bouquet_ids(range_start, range_end, name, operator)'
  );
  my $sth =
    $dbh->prepare('insert into registered_bouquet_ids values (?,?,?,?)');
  my $sheet = $xlsx->worksheet('Bouquet ID');
  my ( $row_min, $row_max ) = $sheet->row_range();
  my ( $col_min, $col_max ) = $sheet->col_range();
  $dbh->begin_work();
  for my $row ( 5 .. $row_max ) {

    for my $col ( $col_min .. $col_max ) {
      my $cell = $sheet->get_cell( $row, $col );
      next unless $cell;
      $sth->bind_param( $col + 1, $cell->unformatted() );
    }
    $sth->execute();
  }
  $dbh->commit();
}
sub handle_ca_system_id { }
sub handle_nid          { }
sub handle_onid         { }

Readonly::Scalar my $dvbservices_url_base =>
  'http://www.dvbservices.com/identifiers/export/';

Readonly::Hash my %ids => (
  'bouquet_id'          => \&handle_bouquet_id,
  'ca_system_id'        => \&handle_ca_system_id,
  'network_id'          => \&handle_nid,
  'original_network_id' => \&handle_onid
);

sub parse_xlsx {
  my ( $xlsx_raw, $handler ) = @_;
  open my $dh, "<", \$xlsx_raw;
  my $xlsx = Spreadsheet::XLSX->new($dh);
  $handler->($xlsx);
}

sub drop_if_exists {
  my $id_name = $_;
  $dbh->do("drop table if exists registered_${id_name}s");
}

my $ua = LWP::UserAgent->new;
$ua->agent("dvbindexRegisteredIDsFetcher/1.0");
foreach ( keys(%ids) ) {
  print STDERR "Fetching $_...";
  my $req = HTTP::Request->new( GET => $dvbservices_url_base . $_ );
  my $res = $ua->request($req);

  if ( not $res->is_success ) {
    print STDERR "failed : $res->status_line , skipping\n";
    next;
  }

  print STDERR "success.\n";
  drop_if_exists($_);
  parse_xlsx( $res->content, $ids{$_} );
}

