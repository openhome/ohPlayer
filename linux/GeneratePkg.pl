#! /usr/bin/perl

###############################################################################
# This script generates a debian installation package for a given application
# and version.
###############################################################################

use Cwd;
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

    die "Cannot temporary directory $scratchDir\n"
        unless mkdir $scratchDir;

    $ORIG_DIR = cwd();
}

END
{
    # Remove temporary directory.
    print STDERR "Error: Cannot remove temp pkg directory $scratchDir $!\n"
        unless ((-d $scratchDir) && (remove_tree($scratchDir) > 0));

}

# Create a self extracting installer for the release pkg
sub CreateSelfExtarctingInstaller
{
    # Obtain the name of the generated release package.
    my @pkg = glob("*deb");

    if ($#pkg < 0)
    {
        print STDERR "Error: Cannot locate release package for the installer.\n";
        return;
    }
    elsif ($#pkg > 0)
    {
        print STDERR "Error: Too many release packages found. Only one " .
                     "expected:\n";

        foreach (@pkg)
        {
            print STDERR "       $_\n";
        }

        return;
    }

    # Open the installer template
    if (! open SCRIPT_TEMPLATE, "<$ORIG_DIR/Installer-Template.txt")
    {
        print STDERR "Error: Cannot open 'Installer-Template.sh\n";
        return;
    }

    # Generate the name of the installer script by replacing the release package
    # extension with 'sh'.
    my $installerScript = $pkg[0];
    $installerScript =~ s/\.deb$/\.sh/;

    # Open the installer script,.overwriting any existing file.
    if (! open INSTALLER_SCRIPT, ">:raw", "$installerScript")
    {
        print STDERR "Error: Cannot create installer script. $!\n";

        close SCRIPT_TEMPLATE;
        return;
    }

    # Open the release package.
    if (! open PKG_FILE, "<:raw", $pkg[0])
    {
        print STDERR "Error: Cannot open application package. $!\n";

        close SCRIPT_TEMPLATE;
        close INSTALLER_SCRIPT;
        return;
    }

    print "Generating installer sctipt\n";

    # Create the installer script from it's template.
    foreach (<SCRIPT_TEMPLATE>)
    {
        # Replace the release package marker with the real name.
        s/<PUT PKG FILE HERE>/$pkg[0]/;

        print INSTALLER_SCRIPT "$_";
    }

    # Append the application package to script.
    my $READ_BLOCK_SIZE = 4096;

    while (read(PKG_FILE, $data, $READ_BLOCK_SIZE) > 0)
    {
        print INSTALLER_SCRIPT "$data";
    }

    close SCRIPT_TEMPLATE;
    close PKG_FILE;
    close INSTALLER_SCRIPT;

    # Ensure the installer is executable.
    if (chmod(oct("755"), "$installerScript") != 1)
    {
        print STDERR "Error: Cannot set permissions of install script. $!\n";
    }
}

# Create and return a reference to a hash of required packages and their
# associated version numbers, keyed on package name.
sub GetDependencies
{
    my ($application) = @_;
    my %pkgHash       = ();
    my @libraryDeps   = `ldd $scratchDir/usr/bin/$application`;

    # General dependencies
    $pkgHash{'notify-osd'} = undef;  # Notification service

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
           "platform=s"     => \$platform,
           "version=s"      => \$version,
           "installer"      => \$installer);

$USAGE = <<EndOfText;
Usage: GeneratePkg.pl --platform=<OS Variant> --application=<Application Executable> --version=<Version> [--installer]
EndOfText

die "$USAGE" unless (defined $application && defined $platform && defined $version);

# Install the application to a temporary folder.
system("make DESTDIR=$scratchDir $platform-install");

if ($? != 0)
{
   die "ERROR: Cannot install applicaiton to '$scratchDir'\n";
}

# Create the package in the same folder as the application
chdir $platform or
   die "ERROR: Cannot move to platform folder '$platform'\n";

# Obtain the application dependencies.
my $pkgHashRef = &GetDependencies($application);

# Use the 'fpm' utility to create the pkg file.

my $fpmCmd = "fpm -s dir -t deb -f -n $application -v $version  -C $scratchDir -p $application-VERSION_ARCH.deb";

foreach (keys %{$pkgHashRef})
{
    if (defined $pkgHashRef->{$_})
    {
        # Minimum package version known
        $fpmCmd .= " -d \"$_ >= $pkgHashRef->{$_}\"";
    }
    else
    {
        # Package version not important
        $fpmCmd .= " -d \"$_\"";
    }
}

$fpmCmd .= ' -m "Openhome Admin <admin@openhome.org>"';
$fpmCmd .= ' --vendor "Openhome Admin <admin@openhome.org>"';
$fpmCmd .= ' --url "http://www.openhome.org/"';
$fpmCmd .= ' --license "Do Not Ship"';
$fpmCmd .= ' --description "OpenHome Player"';
$fpmCmd .= " usr";

system("$fpmCmd");

# Dpkg does not install missing dependencies. On platforms that do not support
# an alternative method which does install missing dependencies, such as 'gdebi',
# we use a self extracting installer to accomplish this.
if (defined $installer)
{
    &CreateSelfExtarctingInstaller();
}

print "Done.\n";
