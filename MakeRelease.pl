#! /usr/bin/perl

# This script automates the generation of release packages/installers.
#
# The appropriate header files are updated with the release version,
# optional features with associated service keys.

use File::Basename;
use File::Copy;
use Cwd ('abs_path', 'cwd');
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

    die "Cannot make temporary directory '$scratchDir'\n$!\n"
        unless mkdir $scratchDir;
}

END
{
    chdir $SOURCE_DIR if (defined $SOURCE_DIR);

    my @savedFiles = glob("$scratchDir/*h");
    push @savedFiles, glob("$scratchDir/*plist");

    # Reinstate any saved files.
    foreach $file (@savedFiles)
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

    die "ERROR: Cannot open version header file '$VERSION_HEADER'\n$!\n"
        unless open INPUT_FILE, "<$VERSION_HEADER";

    die "ERROR: Cannot open tmp version header file " .
        "'$scratchDir/$VERSION_HEADER.tmp'\n$!\n"
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
    copy("$VERSION_HEADER", "$scratchDir/$VERSION_HEADER") or
        die "ERROR: Cannot backup \"$VERSION_HEADER\"\n$!\n";

    # Overwrite the original file with our modified version.
    copy("$scratchDir/$VERSION_HEADER.tmp", "$VERSION_HEADER") or
        die "ERROR: Cannot overwrite \"$VERSION_HEADER\"\n$!\n";
}

# Update the header file defining optional features and associated keys to
# enable access to the required services.
sub updateOoptionalFeatures
{
    my ($enableMp3, $enableAac, $enableRadio, $tuneInPartnedId, $enableTidal,
        $tidalToken, $enableQobuz, $qobuzAppId, $qobuzSecret) = @_;

    die "ERROR: Cannot open optional feature heeder file " .
        "'$OPTIONAL_FEATURES_HEADER'\n$!\n"
        unless open INPUT_FILE, "<$OPTIONAL_FEATURES_HEADER";

    die "ERROR: Cannot open tmp optional feature header file " .
        "'$scratchDir/$OPTIONAL_FEATURES_HEADER.tmp'\n$!\n"
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
    copy("$OPTIONAL_FEATURES_HEADER", "$scratchDir/$OPTIONAL_FEATURES_HEADER")
        or die "ERROR: Cannot backup \"$OPTIONAL_FEATURES_HEADER\"\n$!\n";

    # Overwrite the original file with our modified version.
    copy("$scratchDir/$OPTIONAL_FEATURES_HEADER.tmp", "$OPTIONAL_FEATURES_HEADER")
        or die "ERROR: Cannot overwrite \"$OPTIONAL_FEATURES_HEADER\"\n$!\n";
}

# Build the release package and installer for Linux based platforms.
sub buildLinuxRelease
{
    my ($platform, $version, $debug, $nativeCodecs) = @_;

    # Cleanup any previous build.
    system("make clean");

    die "ERROR: Cleanup Failed\n" unless ($? == 0);

    unlink glob("$platform/*sh");
    unlink glob("$platform/*deb");

    my $options;

    $options .= " DEBUG=0" if (defined $debug);
    $options .= " USE_LIBAVCODEC=0" if (defined $nativeCodecs);
    $options .= " DISABLE_GTK=0" if (defined $headless);

    # Execute the build
    system("make $options $platform");

    die "ERROR: Build Failed\n" unless ($? == 0);

    # Generate the release package.
    system("./GeneratePkg.pl -p=$platform -a=openhome-player -v=\"$version\" --i");

    die "ERROR: Package Generation Failed\n" unless ($? == 0);
}

# Build the release package and installer for the Win32 platform.
#
# It is required that the script be run from a VS2013 command prompt and
# the path *MUST* contain the INNO Setup utility location
# (C:\Program Files (x86)\Inno Setup 5)
sub buildWin32Release
{
    my ($version, $debug, $nativeCodecs) = @_;

    my $config;

    # Check 'msbuild' is available and executable.
    eval {my @dummy = `msbuild 2>nul` or die "$!\n"};

    if ($@)
    {
        die "ERROR: Cannot execute 'msbuild'\n" .
            "Please ensure this script is run from a VS2013 command prompt\n";
    }

    # Check INNO Setup 5 is available and executable.
    eval {my @dummy = `iscc 2>nul` or die "$!\n"};

    if ($@)
    {
        die "ERROR: Cannot execute 'iscc'\n" .
            "Please ensure Inno Setup 5 is in the PATH \n";
    }

    # Decide which configuration is required.
    if (defined $debug)
    {
        $config = "Debug";
    }
    else
    {
        $config = "Release";
    }

    if (defined $nativeCodecs)
    {
        $config .= "IMF";
    }

    # Cleanup any previous build.
    system("msbuild OpenHomePlayer.sln /target:Clean /p:Platform=Win32 " .
           "/p:Configuration=$config");

    die "ERROR: Cleanup Failed\n" unless ($? == 0);

    unlink glob("../Win32Installer/*.exe");

    # Execute the build
    system("msbuild OpenHomePlayer.sln /p:Platform=Win32 " .
           "/p:Configuration=$config");

    die "ERROR: Build Failed\n" unless ($? == 0);

    # Generate the release package.

    # Move to the folder containing the ISetup installer generation script.

    # Note the source directory before moving away. We must change back to
    # the source diorectory on exit to restore the modified header file.
    $SOURCE_DIR = cwd();

    chdir "../Win32Installer" or
        die "Error: Cannot move to Win32Installer folder\n$!\n";

    my $parentFolder = dirname(cwd());

    system("iscc \/dMySrcDir=\"$parentFolder\" "    .
           "\/dMyAppVersion=\"$version\" \/dReleaseType=\"$config\" " .
           "OpenHomePlayerInstaller.iss");

    die "ERROR: Package Generation Failed\n" unless ($? == 0);

    # Annotate the installer with the version.
    $version =~ s/\./-/g;
    rename("setup.exe", "OpenHomePlayerSetup-$version.exe") or
        die "ERROR: Cannot rename 'setup.exe' -> " .
            "'OpenHomePlayerSetup-$version.exe'\n$!\n";
}

sub buildOsxRelease
{
    my ($version, $debug) = @_;
    my $plistBuddy        = "/usr/libexec/PlistBuddy";
    my $plistFile         = "Info.plist";

    # Check the PlistBuddy utility is present and correct.
    eval {my @dummy = `$plistBuddy 2>&1` or die "$!\n"};

    if ($@)
    {
        die "ERROR: Cannot execute '$plistBuddy'\n" .
            "Please ensure it is avaiable and executable\n";
    }

    # Save the original Info.plist file for restoration on exit.
    copy("$plistFile", "$scratchDir") or
        die "ERROR: Cannot backup '$plistFile'. $!\n";

    # Update the Info.plist file with the current release information.
    system("$plistBuddy -c 'Set :CFBundleShortVersionString \"$version\"' " .
           "$plistFile");

    die "ERROR: Cannot update version in '$plistFile'.\n$1\n"
        unless ($? == 0);

    # Note the source directory before moving away. We must change back to
    # the source directory on exit to restore the modified header file.
    $SOURCE_DIR = cwd();

    # Move to folder containing the xcode project.
    chdir "..";

    # Build and install the application to our temporary folder.
    if (defined $debug)
    {
        system("xcodebuild -project OpenHomePlayer.xcodeproj -configuration " .
               "Debug clean install DSTROOT=$scratchDir/OpenHomePlayer.dst");
    }
    else
    {
        system("xcodebuild -project OpenHomePlayer.xcodeproj -configuration " .
               "Release clean install DSTROOT=$scratchDir/OpenHomePlayer.dst");
    }

    die "ERROR: Installation Build Failed\n" unless ($? == 0);

    # Copy the latest resources into the pkg
    my $pkgResDir = "$scratchDir/OpenHomePlayer.dst/Applications/" .
                    "OpenHomePlayer.app/Contents/Resources/";

    # Clear out any existing, possibly out of date, resources.
    remove_tree("$pkgResDir/SoftPlayer") or
        die "ERROR: Cannot remove pkg resource directory. $!\n";

    # Copy over the latest resources from the dependencies
    system("cp -R ../dependencies/Mac-x64/ohMediaPlayer/res $pkgResDir");

    die "ERROR: Failed to copy current resources into pkg. $!\n"
        unless ($? == 0);

    # Name the resource folder correctly.
    move("$pkgResDir/res", "$pkgResDir/SoftPlayer") or
        die "ERROR: Failed to rename resource fodler $!\n";

    # Duplicate the English language UI strings in the fallback location.
    copy("$pkgResDir/SoftPlayer/lang/en-gb/ConfigOptions.txt",
         "$pkgResDir/SoftPlayer/lang") or
        die "ERROR: Failed to install default string resources. $!\n";

    # Generate the OpenHomePlayer component package.
    #
    # The package is configured to install to '/Applications/OpenHomePlayer'
    system("pkgbuild --root $scratchDir/OpenHomePlayer.dst/ " .
           "--install-location \"/\" $scratchDir/OpenHomePlayer.pkg");

    die "ERROR: Component Package Generation Failed\n" unless ($? == 0);

    # Generate the installer package.
    system("productbuild --distribution OpenHomePlayer/Distribution.xml " .
           "--package-path $scratchDir --resources OpenHomePlayer " .
           "OpenHomePlayer-$version.pkg");

    die "ERROR: Installer Package Generation Failed\n" unless ($? == 0);
}

###############################################################################
# MAIN
###############################################################################

my $USAGE = <<EndOfText;
Usage: MakeRelease.pl --platform=<ubuntu|raspbian|Win32|osx> --version=<version>
                      [--debug] [--use-native-codecs] [--headless]
                      [--enable-mp3] [--enable-aac]
                      [--enable-radio --tunein-partner-id=<tunein partner id]
                      [--enable-tidal --tidal-token=<tidal token>]s
                      [--enable-qobuz --qobuz-secret=<qobuz secret>
                       --qobuz-app-id=<qobuz app id>]
EndOfText

GetOptions("platform=s"          => \$platform,
           "version=s"           => \$version,
           "debug"               => \$debug,
           "use-native-codecs"   => \$nativeCodecs,
           "headless"            => \$headless,
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

if ($platform !~ /ubuntu|raspbian|Win32|osx/)
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

if (defined $nativeCodecs && ($platform eq "osx"))
{
    die "Native Codecs currently unavailable for this platform\n";
}

if (defined $headless && ($platform ne "raspbian"))
{
    die "Headless target is currently unavailable for this platform\n";
}

# Move to the directory containing the platform source.
my $sourceDir = abs_path(dirname($0));

if ($platform =~ /raspbian|ubuntu/)
{
    $sourceDir .= "/linux";
}
elsif ($platform =~ /osx/)
{
    $sourceDir .= "/$platform/OpenHomePlayer";
}
else
{
    $sourceDir .= "/$platform";
}

chdir $sourceDir or
    die "ERROR: Cannot move to source directory: $sourceDir. $!\n";

# Update 'version.h' with the release version.
#
# On Osx this isn't required as the version is read from the application
# Info.plist
if ($platform !~ /osx/)
{
    &updateVersion("$version");
}

# Update 'OptionalFeatures.h with enabled features and service keys
&updateOoptionalFeatures($enableMp3, $enableAac, $enableRadio, $tuneInPartnedId,
                         $enableTidal, $tidalToken, $enableQobuz, $qobuzAppId,
                         $qobuzSecret);

# Generate the release
if ($platform =~ /raspbian|ubuntu/)
{
    &buildLinuxRelease($platform, $version, $debug, $nativeCodecs, $headless);
}
elsif ($platform =~ /Win32/)
{
    &buildWin32Release($version, $debug, $nativeCodecs);
}
elsif ($platform =~ /osx/)
{
    &buildOsxRelease($version, $debug);
}

1;
