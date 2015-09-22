#! /usr/bin/perl

# This script automates the generation of release packages/installers.
#
# The appropriate header files are updated with the release version,
# optional features with associated service keys.

use File::Basename;
use File::Copy;
use Cwd 'abs_path';
use File::Path qw(remove_tree);
use Getopt::Long;
use POSIX qw(tmpnam);

BEGIN
{
    # Global Constants
    $VERSION_HEADER           = "version.h";
    $OPTIONAL_FEATURES_HEADER = "OptionalFeatures.h";

    # Create a temporary directory to work in
    do
    {
        $scratchDir = tmpnam();
    } while (-e "$scratchDir");

    die "Cannot make temporary directory $scratchDir\n"
        unless mkdir $scratchDir;
}

END
{
    my @savedHeaders = glob("$scratchDir/*h");

    # Reinstate any modified header.
    foreach $file (@savedHeaders)
    {
        copy("$file", ".");
    }

    # Remove temporary directory.
    print STDERR "Error: Cannot remove temp directory $scratchDir $!\n"
        unless ((-d $scratchDir) && (remove_tree($scratchDir) > 0));
}

# Update the header file defining the version with that required.
sub updateVersion
{
    my ($version) = @_;

    die "ERROR: Cannot open version header file '$VERSION_HEADER' $!\n"
        unless open INPUT_FILE, "<$VERSION_HEADER";

    die "ERROR: Cannot open tmp version header file " .
        "'$scratchDir/$VERSION_HEADER.tmp' $!\n"
        unless open OUTPUT_FILE, ">$scratchDir/$VERSION_HEADER.tmp";

    while (<INPUT_FILE>)
    {
        # Replace the default version with the required version.
        if (/CURRENT_VERSION/)
        {
            s/".*"/"$version"/;
        }

        print OUTPUT_FILE;
    }

    close INPUT_FILE;
    close OUTPUT_FILE;

    # Save the original file for restoration on exit.
    copy($VERSION_HEADER, "$scratchDir/$VERSION_HEADER");

    # Overwrite the original file with our modified version.
    copy("$scratchDir/$VERSION_HEADER.tmp", $VERSION_HEADER);
}

# Update the header file defining optional features and associated keys to enable
# access to the required services.
sub updateOoptionalFeatures
{
    my ($enableMp3, $enableAac, $enableRadio, $tuneInPartnedId, $enableTidal,
        $tidalToken, $enableQobuz, $qobuzAppId, $qobuzSecret) = @_;

    die "ERROR: Cannot open optional feature heeder file " .
        "'$OPTIONAL_FEATURES_HEADER' $!\n"
        unless open INPUT_FILE, "<$OPTIONAL_FEATURES_HEADER";

    die "ERROR: Cannot open tmp optional feature header file " .
        "'$scratchDir/$OPTIONAL_FEATURES_HEADER.tmp' $!\n"
        unless open OUTPUT_FILE, ">$scratchDir/$OPTIONAL_FEATURES_HEADER.tmp";

    while (<INPUT_FILE>)
    {
        if (defined $enableMp3 && /\/\/#define ENABLE_MP3/)
        {
            s/\/\///;
            print OUTPUT_FILE;
            next;
        }

        if (defined $enableAac && /\/\/#define ENABLE_AAC/)
        {
            s/\/\///;
            print OUTPUT_FILE;
            next;
        }

        if (defined $enableRadio && /\/\/#define ENABLE_RADIO/)
        {
            s/\/\///;
            print OUTPUT_FILE;
            next;
        }

        if (defined $enableTidal && /\/\/#define ENABLE_TIDAL/)
        {
            s/\/\///;
            print OUTPUT_FILE;
            next;
        }

        if (defined $enableQobuz && /\/\/#define ENABLE_QOBUZ/)
        {
            s/\/\///;
            print OUTPUT_FILE;
            next;
        }

        if (defined $tidalToken && /^#define.*TIDAL_TOKEN_STRING/)
        {
            s/TIDAL_TOKEN_STRING/"$tidalToken"/;
            print OUTPUT_FILE;
            next;
        }

        if (defined $qobuzAppId && /^#define.*QOBUZ_APPID_STRING/)
        {
            s/QOBUZ_APPID_STRING/"$qobuzAppId"/;
            print OUTPUT_FILE;
            next;
        }

        if (defined $qobuzSecret && /^#define.*QOBUZ_SECRET_STRING/)
        {
            s/QOBUZ_SECRET_STRING/"$qobuzSecret"/;
            print OUTPUT_FILE;
            next;
        }

        if (defined $tuneInPartnedId && /^#define.*TUNEIN_PARTNER_ID_STRING/)
        {
            s/TUNEIN_PARTNER_ID_STRING/"$tuneInPartnedId"/;
            print OUTPUT_FILE;
            next;
        }

        print OUTPUT_FILE;
    }

    close INPUT_FILE;
    close OUTPUT_FILE;

    # Save the original file for restoration on exit.
    copy($OPTIONAL_FEATURES_HEADER, "$scratchDir/$OPTIONAL_FEATURES_HEADER");

    # Overwrite the original file with our modified version.
    copy("$scratchDir/$OPTIONAL_FEATURES_HEADER.tmp", $OPTIONAL_FEATURES_HEADER);
}

# Build the release package and installer for Linux based platforms.
sub buildLinuxRelease
{
    my ($platform, $version) = @_;

    # Cleanup any previous build.
    system("make clean");

    if ($? != 0)
    {
       die "ERROR: Cleanup Failed\n";
    }

    unlink glob("$platform/*sh");
    unlink glob("$platform/*deb");

    # Execute the build
    system("make $platform");

    if ($? != 0)
    {
       die "ERROR: Build Failed\n";
    }

    # Generate the release package.
    system("./GeneratePkg.pl -p=$platform -a=litepipe-test-app -v=\"$version\" --i");

    if ($? != 0)
    {
       die "ERROR: Package Generation Failed\n";
    }
}

###############################################################################
# MAIN
###############################################################################

my $USAGE = <<EndOfText;
Usage: MakeRelease.pl --platform=<ubuntu|raspbian> --version=<version>
                      [--enable-mp3] [--enable-aac]
                      [--enable-radio --tunein-partner-id=<tunein partner id]
                      [--enable-tidal --tidal-token=<tidal token>]s
                      [--enable-qobuz --qobuz-secret=<qobuz secret>
                       --qobuz-app-id=<qobuz app id>]
EndOfText

GetOptions("platform=s"          => \$platform,
           "version=s"           => \$version,
           "enable-mp3"          => \$enableMp3,
           "enable-aac"          => \$enableAac,
           "enable-radio"        => \$enableRadio,
           "tunein-partner-id=s" => \$tuneInPartnedId,
           "enable-tidal"        => \$enableTidal,
           "tidal-token=s"       => \$tidalToken,
           "enable-qobuz"        => \$enableQobuz,
           "qobuz-secret=s"      => \$qobuzSecret,
           "qobuz-app-id=s"      => \$qobuzAppId);

# Verify command line parameters
if (! defined $platform || ! defined $version)
{
    die "$USAGE\n";
}

if ($platform !~ /ubuntu|raspbian/)
{
    die "Invalid Platform: $platform\n";
}

if ((defined $enableRadio && ! defined $tuneInPartnedId) ||
    (! defined $enableRadio && defined $tuneInPartnedId))
{
    die "$USAGE\n";
}

if ((defined $enableTidal && ! defined $tidalToken) ||
    (! defined $enableTidal && defined $tidalToken))
{
    die "$USAGE\n";
}

if ((defined $enableQobuz && !(defined $qobuzSecret && defined $qobuzAppId)) ||
    (! defined $enableQobuz && (defined $qobuzSecret || defined $qobuzAppId)))
{
    die "$USAGE\n";
}

# Move to the directory containing the platform source.
my $sourceDir = abs_path(dirname($0));

if ($platform =~ /raspbian|ubuntu/)
{
    $sourceDir .= "/linux";
}
else
{
    $sourceDir .= "/$platform";
}

chdir $sourceDir or
    die "ERROR: Cannot move to source directory: $sourceDir. $!\n";

# Update 'version.h' with the release version.
&updateVersion("$version");

# Update 'OptionalFeatures.h with enabled features and service keys
&updateOoptionalFeatures($enableMp3, $enableAac, $enableRadio, $tuneInPartnedId,
                         $enableTidal, $tidalToken, $enableQobuz, $qobuzAppId,
                         $qobuzSecret);

# Generate the release
if ($platform =~ /raspbian|ubuntu/)
{
    &buildLinuxRelease($platform, $version);
}

;
