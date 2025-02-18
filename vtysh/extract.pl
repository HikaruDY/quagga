#! /usr/bin/perl
##
## vtysh/extract.pl.  Generated from extract.pl.in by configure.
##
## Virtual terminal interface shell command extractor.
## Copyright (C) 2000 Kunihiro Ishiguro
## (C)2024 Hikaru Yamatohimemiya
##
## This file is part of GNU Zebra.
##
## GNU Zebra is free software; you can redistribute it and/or modify it
## under the terms of the GNU General Public License as published by the
## Free Software Foundation; either version 2, or (at your option) any
## later version.
##
## GNU Zebra is distributed in the hope that it will be useful, but
## WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
## General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with GNU Zebra; see the file COPYING.  If not, write to the Free
## Software Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
## 02111-1307, USA.
##

print <<EOF;
#include <zebra.h>
#include "command.h"
#include "vtysh.h"

EOF

$ignore{'"interface IFNAME"'} = "ignore";
$ignore{'"interface IFNAME " "vrf <0-65535>"'} = "ignore";
$ignore{'"link-params"'} = "ignore";
$ignore{'"ip vrf NAME"'} = "ignore";
$ignore{'"router rip"'} = "ignore";
$ignore{'"router ripng"'} = "ignore";
$ignore{'"router ospf"'} = "ignore";
$ignore{'"router ospf <0-65535>"'} = "ignore";
$ignore{'"router ospf6"'} = "ignore";
$ignore{'"router bgp " "<1-4294967295>"'} = "ignore";
$ignore{'"router bgp " "<1-4294967295>" " view WORD"'} = "ignore";
$ignore{'"router isis WORD"'} = "ignore";
$ignore{'"router zebra"'} = "ignore";
$ignore{'"address-family ipv4"'} = "ignore";
$ignore{'"address-family ipv4 (unicast|multicast)"'} = "ignore";
$ignore{'"address-family ipv6"'} = "ignore";
$ignore{'"address-family ipv6 (unicast|multicast)"'} = "ignore";
$ignore{'"address-family vpnv4"'} = "ignore";
$ignore{'"address-family vpnv4 unicast"'} = "ignore";
$ignore{'"address-family vpnv6"'} = "ignore";
$ignore{'"address-family vpnv6 unicast"'} = "ignore";
$ignore{'"address-family ipv4 vrf NAME"'} = "ignore";
$ignore{'"address-family encap"'} = "ignore";
$ignore{'"address-family encapv4"'} = "ignore";
$ignore{'"address-family encapv6"'} = "ignore";
$ignore{'"exit-address-family"'} = "ignore";
$ignore{'"exit-link-params"'} = "ignore";
$ignore{'"vnc defaults"'} = "ignore";
$ignore{'"vnc nve-group NAME"'} = "ignore";
$ignore{'"exit-vnc"'} = "ignore";
$ignore{'"key chain WORD"'} = "ignore";
$ignore{'"key <0-2147483647>"'} = "ignore";
$ignore{'"route-map WORD (deny|permit) <1-65535>"'} = "ignore";
$ignore{'"show route-map"'} = "ignore";
$ignore{'"line vty"'} = "ignore";
$ignore{'"who"'} = "ignore";
$ignore{'"terminal monitor"'} = "ignore";
$ignore{'"terminal no monitor"'} = "ignore";
$ignore{'"show history"'} = "ignore";

my $cli_stomp = 0;

foreach (@ARGV) {
    $file = $_;

    open (FH, "gcc -E -DHAVE_CONFIG_H -DVTYSH_EXTRACT_PL -DHAVE_IPV6 -I.. -I./ -I./.. -I../lib -I../lib -I../isisd/topology  $file |");
    local $/; undef $/;
    $line = <FH>;
    close (FH);

    @defun = ($line =~ /(?:DEFUN|ALIAS)\s*\((.+?)\);?\s?\s?\n/sg);
    @install = ($line =~ /install_element\s*\(\s*[0-9A-Z_]+,\s*&[^;]*;\s*\n/sg);

    # DEFUN process
    foreach (@defun) {
	my (@defun_array);
	@defun_array = split (/,/);
	$defun_array[0] = '';


	# Actual input command string.
	$str = "$defun_array[2]";
	$str =~ s/^\s+//g;
	$str =~ s/\s+$//g;

	# Get VTY command structure.  This is needed for searching
	# install_element() command.
	$cmd = "$defun_array[1]";
	$cmd =~ s/^\s+//g;
	$cmd =~ s/\s+$//g;

        # $protocol is VTYSH_PROTO format for redirection of user input
        if ($file =~ /lib\/keychain\.c$/) {
            $protocol = "VTYSH_RIPD";
        }
        elsif ($file =~ /lib\/routemap\.c$/) {
            $protocol = "VTYSH_RIPD|VTYSH_RIPNGD|VTYSH_OSPFD|VTYSH_OSPF6D|VTYSH_BGPD|VTYSH_ZEBRA";
        }
        elsif ($file =~ /lib\/filter\.c$/) {
            $protocol = "VTYSH_ALL";
        }
        elsif ($file =~ /lib\/vrf\.c$/) {
            $protocol = "VTYSH_ZEBRA";
        }
        elsif ($file =~ /lib\/plist\.c$/) {
            if ($defun_array[1] =~ m/ipv6/) {
                $protocol = "VTYSH_RIPNGD|VTYSH_OSPF6D|VTYSH_BGPD|VTYSH_ZEBRA";
            } else {
                $protocol = "VTYSH_RIPD|VTYSH_OSPFD|VTYSH_BGPD|VTYSH_ZEBRA";
            }
        }
        elsif ($file =~ /lib\/distribute\.c$/) {
            if ($defun_array[1] =~ m/ipv6/) {
                $protocol = "VTYSH_RIPNGD";
            } else {
                $protocol = "VTYSH_RIPD";
            }
        }
        elsif ($file =~ /lib\/if_rmap\.c$/) {
            if ($defun_array[1] =~ m/ipv6/) {
                $protocol = "VTYSH_RIPNGD";
            } else {
                $protocol = "VTYSH_RIPD";
            }
        }
        elsif ($file =~ /lib\/vty\.c$/) {
           $protocol = "VTYSH_ALL";
        }
	else {
           ($protocol) = ($file =~ /^.*\/([a-z0-9]+)\/[a-zA-Z0-9_\-]+\.c$/);
           $protocol = "VTYSH_" . uc $protocol;
        }

	# Append _vtysh to structure then build DEFUN again
	$defun_array[1] = $cmd . "_vtysh";
	$defun_body = join (", ", @defun_array);

	# $cmd -> $str hash for lookup
       if (exists($cmd2str{$cmd})) {
           warn "Duplicate CLI Function: $cmd\n";
           warn "\tFrom cli: $cmd2str{$cmd} to New cli: $str\n";
           warn "\tOriginal Protocol: $cmd2proto{$cmd} to New Protocol: $protocol\n";
	   $cli_stomp++;
       }
	$cmd2str{$cmd} = $str;
	$cmd2defun{$cmd} = $defun_body;
	$cmd2proto{$cmd} = $protocol;
    }

    # install_element() process
    foreach (@install) {
	my (@element_array);
	@element_array = split (/,/);

	# Install node
	$enode = $element_array[0];
	$enode =~ s/^\s+//g;
	$enode =~ s/\s+$//g;
	($enode) = ($enode =~ /([0-9A-Z_]+)$/);

	# VTY command structure.
	($ecmd) = ($element_array[1] =~ /&([^\)]+)/);
	$ecmd =~ s/^\s+//g;
	$ecmd =~ s/\s+$//g;

	# Register $ecmd
	if (defined ($cmd2str{$ecmd})
	    && ! defined ($ignore{$cmd2str{$ecmd}})) {
	    my ($key);
	    $key = $enode . "," . $cmd2str{$ecmd};
	    $ocmd{$key} = $ecmd;
	    $odefun{$key} = $cmd2defun{$ecmd};
	    push (@{$oproto{$key}}, $cmd2proto{$ecmd});
	}
    }
}

my $bad_cli_stomps = 102;
# Currently we have $bad_cli_stomps.  This was determined by
# running this script and counting up the collisions from what
# was returned.
#
# When we have cli commands that map to the same function name, we
# can introduce subtle bugs due to code not being called when
# we think it is.
#
# If extract.pl fails with a error message and you've been
# modifying the cli, then go back and fix your code to
# not have cli command function collisions.
#
# If you've removed a cli overwrite, you can safely subtract
# one from $bad_cli_stomps.  If you've added to the problem
# please fix your code before submittal
if ($cli_stomp != $bad_cli_stomps) {
    warn "Expected $bad_cli_stomps command line stomps, but got $cli_stomp instead\n";
    exit $cli_stomp;
}

# Check finaly alive $cmd;
foreach (keys %odefun) {
    my ($node, $str) = (split (/,/));
    my ($cmd) = $ocmd{$_};
    $live{$cmd} = $_;
}

# Output DEFSH
foreach (keys %live) {
    my ($proto);
    my ($key);
    $key = $live{$_};
    $proto = join ("|", @{$oproto{$key}});
    printf "DEFSH ($proto$odefun{$key})\n\n";
}

# Output install_element
print <<EOF;
void
vtysh_init_cmd (void)
{
EOF

foreach (keys %odefun) {
    my ($node, $str) = (split (/,/));
    $cmd = $ocmd{$_};
    $cmd =~ s/_cmd/_cmd_vtysh/;
    printf "  install_element ($node, &$cmd);\n";
}

print <<EOF
}
EOF
