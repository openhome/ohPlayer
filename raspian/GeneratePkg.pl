#! /usr/bin/perl

###############################################################################
# This script generates a debian installation package for a given application
# and version.
###############################################################################

use File::Path qw(remove_tree);
use Getopt::Long;
use POSIX qw(tmpnam);

BEGIN
{
    # Create a temporary directory for package creation.
    do
    {
        $scratchDir = tmpnam();
    } while (-e "$scratchDir");

    die "Cannot make temporary directory $scratchDir\n"
        unless mkdir $scratchDir;
}

END
{
    # Remove temporary directory.
    print STDERR "Error: Cannot remove temp pkg directory $scratchDir $!\n"
        unless ((-d $scratchDir) && (remove_tree($scratchDir) > 0));

}

# Create and return a reference to a hash of required packages and their
# associated version numbers, keyed on package name.
sub GetDependencies
{
    my ($application) = @_;
    my %pkgHash       = ();
    my @libraryDeps   = `ldd $scratchDir/usr/bin/$application`;

    print "Calculating pkg dependencies ....\n";

    # Process each dependent library string.
    foreach $library (@libraryDeps)
    {
        chomp $library;

        # Isolate the location of the library file.
        #
        # We look for a path starting with '/'
        if ($library !~ /\s+(\/[^\s]+)/)
        {
            print "Warning: Cannot isolate installed file: $libraryDeps\n";
            next;
        }

        my $installedFile = $1;

        # Look up the package that installed the library.
        my $packageInfo   = `dpkg --search $installedFile`;

        if ($? != 0)
        {
           print "Warning: Cannot obtain package info for '$installedFile'\n";
           next;
        }

        # The package name is the first text on the line, prior to a ':'
        if ($packageInfo !~ /^([^:]+)/)
        {
            print "Warning: Cannot isolate packge file: $packageInfo\n";
            next;
        }

        my $packageName = $1;

        # Get the package status.
        my @packageVersionInfo = `dpkg -s $packageName`;

        if ($? != 0)
        {
            print "Warning: Cannot obtain packge info for : $packageName\n";
            next;
        }

        my $packageVersion = undef;

        # Isolate the version from the status info.
        foreach (@packageVersionInfo)
        {
            chomp;

            if (/Version:\s+(.*)/)
            {
                $packageVersion = $1;
                last;
            }
        }

        if (! defined $packageVersion)
        {
            print "Warning: Cannot isolate packge version for '$packageName'\n";
            next;
        }

        # Add the package to the hash.
        $pkgHash{$packageName} = $packageVersion;
    }

    print "Finished calculating pkg dependencies\n";

    # Return a reference to the pkg hash.
    return \%pkgHash;
}

###############################################################################
# MAIN
###############################################################################

# Parse command line options.
GetOptions("application=s"  => \$application,
           "version=s"      => \$version);

$USAGE = <<EndOfText;
Usage: GeneratePkg.pl --application=<Application Executable> --version=<Version>
EndOfText

die "$USAGE" unless (defined $application && defined $version);

# Install the application to a temporary folder.
system("make DESTDIR=$scratchDir install");

if ($? != 0)
{
   die "ERROR: Cannot install applicaiton to '$scratchDir'\n";
}

# Obtain the application dependencies.
my $pkgHashRef = &GetDependencies($application);


# Use the 'fpm' utility to create the pkg file.

my $fpmCmd = "fpm -s dir -t deb -f -n $application -v $version  -C $scratchDir -p $application-VERSION_ARCH.deb";

foreach (keys %{$pkgHashRef})
{
    $fpmCmd .= " -d \"$_ >= $pkgHashRef->{$_}\"";
}

$fpmCmd .= ' -m "Openhome Admin <admin@openhome.org>"';
$fpmCmd .= ' --vendor "Openhome Admin <admin@openhome.org>"';
$fpmCmd .= ' --url "http://www.openhome.org/"';
$fpmCmd .= ' --license "Do Not Ship"';
$fpmCmd .= ' --description "LitePipe Test Application"';
$fpmCmd .= " usr";

system("$fpmCmd");
