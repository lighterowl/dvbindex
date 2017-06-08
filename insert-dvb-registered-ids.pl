#!/usr/bin/env perl

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
use DBI qw(:sql_types);
use LWP::UserAgent;
use Readonly;

my $dbh;

sub sheet_to_sqlite {
  my ( $sheet, $table_name, $data_columns ) = @_;
  my @range_columns = ( 'range_start', 'range_end' );
  my @columns = ( @range_columns, @{$data_columns} );
  $dbh->do("DROP TABLE IF EXISTS $table_name");
  $dbh->do( "CREATE TABLE $table_name (" . join( ',', @columns ) . ')' );
  my $sth =
    $dbh->prepare( "INSERT INTO $table_name VALUES ("
      . join( ',', ( ('?') x scalar(@columns) ) )
      . ')' );
  my ( $row_min, $row_max ) = $sheet->row_range();
  my ( $col_min, $col_max ) = $sheet->col_range();
  $dbh->begin_work();

  for my $row ( 5 .. $row_max ) {

    for my $col ( $col_min .. 3 ) {
      my $cell = $sheet->get_cell( $row, $col );
      next unless $cell;
      my $value = $cell->value();
      my $type  = SQL_VARCHAR;
      if ( $col == 0 or $col == 1 ) {
        $value =~ s/^\s+|\s+$//g;
        $value = hex($value);
        $type  = SQL_INTEGER;
      }
      $sth->bind_param( $col + 1, $value, $type );
    }
    $sth->execute();
  }
  $dbh->commit();
  $dbh->do( "CREATE INDEX ${table_name}_range_idx ON ${table_name} ("
      . join( ',', @range_columns )
      . ')' );
}

sub handle_bouquet_id {
  my $xlsx = shift;
  sheet_to_sqlite( $xlsx->worksheet('Bouquet ID'),
    'registered_bouquet_ids', [ 'name', 'operator' ] );
}

sub handle_ca_system_id {
  my $xlsx = shift;
  sheet_to_sqlite( $xlsx->worksheet('CA System ID'),
    'registered_ca_system_ids', [ 'description', 'specifier' ] );
}

sub handle_nid {
  my $xlsx = shift;
  sheet_to_sqlite( $xlsx->worksheet('Network ID'),
    'registered_network_ids', [ 'name', 'operator' ] );
}

sub handle_onid {
  my $xlsx = shift;
  sheet_to_sqlite(
    $xlsx->worksheet('Original Network ID'),
    'registered_original_network_ids',
    [ 'name', 'operator' ]
  );
}

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

sub usage {
  my $supported_id_list = join( "\n", sort( keys(%ids) ) );
  my $msg = <<"END_MSG";
Usage : $0 dbfile

Fetches the list of registered DVB IDs from the official DVB site and saves them
as tables inside dbfile under the names registered_*_ids. Any tables with the
same names are dropped.

Currently supported IDs are :
$supported_id_list
END_MSG
  print STDERR $msg;
  exit 1;
}

usage() unless scalar(@ARGV) == 1;

$dbh = DBI->connect( "dbi:SQLite:dbname=$ARGV[0]", "", "" );

Readonly::Scalar my $dvbservices_url_base =>
  'http://www.dvbservices.com/identifiers/export/';

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
  parse_xlsx( $res->content, $ids{$_} );
}

exit 0;
