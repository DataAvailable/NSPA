package OpenSSL::safe::installdata;

use strict;
use warnings;
use Exporter;
our @ISA = qw(Exporter);
our @EXPORT = qw(
    @PREFIX
    @libdir
    @BINDIR @BINDIR_REL_PREFIX
    @LIBDIR @LIBDIR_REL_PREFIX
    @INCLUDEDIR @INCLUDEDIR_REL_PREFIX
    @APPLINKDIR @APPLINKDIR_REL_PREFIX
    @MODULESDIR @MODULESDIR_REL_LIBDIR
    @PKGCONFIGDIR @PKGCONFIGDIR_REL_LIBDIR
    @CMAKECONFIGDIR @CMAKECONFIGDIR_REL_LIBDIR
    $COMMENT $VERSION @LDLIBS
);

our $COMMENT                    = 'This file should be used when building against this OpenSSL build, and should never be installed';
our @PREFIX                     = ( '/home/lxh/Projects/NSPA/open-source-soft/openssl-master' );
our @libdir                     = ( '/home/lxh/Projects/NSPA/open-source-soft/openssl-master' );
our @BINDIR                     = ( '/home/lxh/Projects/NSPA/open-source-soft/openssl-master/apps' );
our @BINDIR_REL_PREFIX          = ( 'apps' );
our @LIBDIR                     = ( '/home/lxh/Projects/NSPA/open-source-soft/openssl-master' );
our @LIBDIR_REL_PREFIX          = ( '' );
our @INCLUDEDIR                 = ( '/home/lxh/Projects/NSPA/open-source-soft/openssl-master/include', '/home/lxh/Projects/NSPA/open-source-soft/openssl-master/include' );
our @INCLUDEDIR_REL_PREFIX      = ( 'include', './include' );
our @APPLINKDIR                 = ( '/home/lxh/Projects/NSPA/open-source-soft/openssl-master/ms' );
our @APPLINKDIR_REL_PREFIX      = ( 'ms' );
our @MODULESDIR                 = ( '/home/lxh/Projects/NSPA/open-source-soft/openssl-master/providers' );
our @MODULESDIR_REL_LIBDIR      = ( 'providers' );
our @PKGCONFIGDIR               = ( '/home/lxh/Projects/NSPA/open-source-soft/openssl-master' );
our @PKGCONFIGDIR_REL_LIBDIR    = ( '' );
our @CMAKECONFIGDIR             = ( '/home/lxh/Projects/NSPA/open-source-soft/openssl-master' );
our @CMAKECONFIGDIR_REL_LIBDIR  = ( '' );
our $VERSION                    = '4.0.1';
our @LDLIBS                     =
    # Unix and Windows use space separation, VMS uses comma separation
    $^O eq 'VMS'
    ? split(/ *, */, '-ldl -pthread ')
    : split(/ +/, '-ldl -pthread ');

1;
