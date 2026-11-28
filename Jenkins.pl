#!/usr/bin/perl
# <FS:Outworldz> Jenkins.pl - build DreamStorm for Windows as a Beta or a Release.
#
#   perl Jenkins.pl                 asks Beta or Release
#   perl Jenkins.pl --beta          build, sign, copy to the Beta web folder, push, tag <ver>-beta,
#                                   GitHub pre-release with the .exe, start the Mac/Linux action.
#                                   The download page is not changed.
#   perl Jenkins.pl --release       build, sign, push, tag, GitHub release (Latest) with the .exe, start
#                                   the Mac/Linux GitHub action, wait for it, then update the web page
#   perl Jenkins.pl --publish TAG   only the web step, for a release that is already on GitHub
#   add --dry-run to print what would be done and change nothing
#
# Both need a clean working tree on branch Water, the DigiCert USB token, and gh logged in
# (run "gh auth login" once).
#
# The web page DreamStorm/Default.htm takes its download table from DreamStorm/download_links.txt
# with an Apache include. This script writes that file with links to the GitHub release.

use strict;
use warnings;
use 5.010;
use Getopt::Long;
use File::Copy;
use File::Path qw(make_path);
use File::Basename;
use File::Spec;
use Cwd qw(abs_path getcwd);
use JSON::PP;
use HTTP::Tiny;
use POSIX qw(strftime);
use Time::Local qw(timegm);

# ---------------------------------------------------------------- settings
my $Repo        = 'Outworldz/DreamStorm';
my $Branch      = 'Water';
my $Workflow    = 'water-build.yml';
my $TagPrefix   = 'dreamstorm-';
my $Thumbprint  = '950056772699712AEE2C29A39BE1FF4A9E980F3E';    # Outworldz code signing cert, 2026
my $DigiCert    = 'H:/Opensim/Certs/digicertutil.exe';
my $VcVars      = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat';
my $SdkBin      = 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64';    # mt.exe that works (LNK1327)
my $SignTool    = "$SdkBin/signtool.exe";
my $Venv        = 'H:/Opensim/DreamStorm-venv';
my $BuildVars   = 'H:/Opensim/fs-build-variables/variables';
my $BuildDir    = 'build-vc180-64';
my $WebRoot     = '//Mach-iii/o/Inetpub/Secondlife/Outworldz_Installer';
my $WebUrl      = 'https://www.outworldz.com/Outworldz_Installer';
my $PageDir     = "$WebRoot/DreamStorm";
my $BetaDir     = "$PageDir/Beta";
my $IncludeName = 'download_links.txt';
my $OldGridDir  = "$WebRoot/Grid/DreamStorm";    # old home of the .exe; its .exe goes once the page is updated
my $WebGitTop   = '//mach-III/o/Inetpub/Secondlife';    # the web server's own local git repo (no remote)
my $WebGit      = "git -c \"safe.directory=%(prefix)///mach-III/o/Inetpub/Secondlife\" -C \"$WebGitTop\"";
my @WebGitFiles = ( 'Outworldz_Installer/DreamStorm/Default.htm', "Outworldz_Installer/DreamStorm/$IncludeName" );

# ---------------------------------------------------------------- options
my ( $beta, $release, $publish, $dry );
GetOptions(
    'beta'      => \$beta,
    'release'   => \$release,
    'publish=s' => \$publish,
    'dry-run'   => \$dry,
) or die "Usage: perl Jenkins.pl [--beta | --release | --publish TAG] [--dry-run]\n";

my $Top = dirname( abs_path($0) );
chdir $Top or die "Cannot go to $Top: $!\n";

make_path("$Top/$BuildDir/logs");
my $LogFile = "$Top/$BuildDir/logs/jenkins-" . strftime( '%Y-%m-%d_%H-%M-%S', localtime ) . '.log';
open( my $LOG, '>', $LogFile ) or die "Cannot write $LogFile: $!\n";
$LOG->autoflush(1);
STDOUT->autoflush(1);

sub Say {
    my $line = join( '', @_ );
    print "$line\n";
    print $LOG "$line\n";
}

sub Fail {
    Say( "\nSTOPPED: ", @_ );
    Say("Log: $LogFile");
    exit 1;
}

# Runs a command that only reads. Returns its output; dies if it fails unless $ok_to_fail.
sub Read {
    my ( $cmd, $ok_to_fail ) = @_;
    my $out = `$cmd 2>&1`;
    my $code = $? >> 8;
    Fail("'$cmd' failed (exit $code):\n$out") if $code && !$ok_to_fail;
    return wantarray ? ( $out, $code ) : $out;
}

# Runs a command that changes something, showing and logging its output as it comes.
# In --dry-run it only prints the command.
sub Run {
    my ($cmd) = @_;
    Say("> $cmd");
    return 0 if $dry;
    open( my $p, "$cmd 2>&1 |" ) or Fail("Cannot start: $cmd");
    while (<$p>) { print; print $LOG $_ }
    close $p;
    my $code = $? >> 8;
    return $code;
}

sub RunOrFail {
    my ($cmd) = @_;
    my $code = Run($cmd);
    Fail("'$cmd' failed (exit $code)") if $code;
}

sub Win { my $p = shift; $p =~ s{/}{\\}g; return $p }

sub Ask {
    my ($q) = @_;
    print "$q ";
    my $a = <STDIN>;
    $a = '' unless defined $a;
    chomp $a;
    print $LOG "$q $a\n";
    return $a;
}

Say( "DreamStorm Jenkins, ", strftime( '%Y-%m-%d %H:%M', localtime ), $dry ? '  (DRY RUN: nothing is changed)' : '' );

# ---------------------------------------------------------------- publish only
if ($publish) {
    CheckGh();
    Publish($publish);
    Done();
}

# ---------------------------------------------------------------- Beta or Release
unless ( $beta || $release ) {
    my $a = Ask('Build a [B]eta or a [R]elease?');
    if    ( $a =~ /^b/i ) { $beta    = 1 }
    elsif ( $a =~ /^r/i ) { $release = 1 }
    else                  { Fail("Answer B or R.") }
}
Fail("Pick --beta or --release, not both.") if $beta && $release;
my $Mode = $release ? 'Release' : 'Beta';
Say("Mode: $Mode");

# ---------------------------------------------------------------- checks
my $branch = Read('git rev-parse --abbrev-ref HEAD');
chomp $branch;
Fail("You are on branch '$branch'. Switch to $Branch first.") if $branch ne $Branch;

for ( [ $DigiCert, 'DigiCertUtil' ], [ $VcVars, 'Visual Studio 2026 vcvars64.bat' ], [ $SignTool, 'signtool' ],
    [ "$Venv/Scripts/autobuild.exe", 'autobuild (venv)' ], [ $BuildVars, 'fs-build-variables' ] )
{
    Fail("$_->[1] not found: $_->[0]") unless -e $_->[0];
}

my $Sha = Read('git rev-parse HEAD');
chomp $Sha;
my $Count = Read('git rev-list --count HEAD');
chomp $Count;
open( my $VF, '<', 'indra/newview/VIEWER_VERSION_FS.txt' ) or Fail("Cannot read VIEWER_VERSION_FS.txt: $!");
my $Base = <$VF>;
close $VF;
$Base =~ s/\s+//g;
my $Version = "$Base.$Count";
my $Tag     = $TagPrefix . $Version . ( $beta ? '-beta' : '' );
Say("Commit $Sha, version $Version, tag $Tag");

# Both kinds go on GitHub (Beta as a pre-release), so both need the exact commit they were built from.
my $dirty = Read('git status --porcelain --untracked-files=no');
Fail("The working tree has changes. Commit them first, so the build number matches the commit:\n$dirty") if $dirty =~ /\S/;
CheckGh();
my ($tags) = Read("git tag -l $Tag");
Fail("Tag $Tag is already here. Make a new commit for a new $Mode.") if $tags =~ /\S/;
my ( undef, $relcode ) = Read( "gh release view $Tag --repo $Repo --json tagName", 1 );
Fail("GitHub already has release $Tag.") if $relcode == 0;

Say("Plug in the DigiCert USB token now: every program file and the installer are signed.");
Ask('Press Enter to start the build.') unless $dry;

# ---------------------------------------------------------------- build
my $Started = time;
my $bat     = "$Top/$BuildDir/logs/jenkins-build.bat";
my $vars    = Win($BuildVars);
open( my $B, '>', $bat ) or Fail("Cannot write $bat: $!");
print $B <<"BAT";
\@echo off
call "@{[Win($VcVars)]}" || exit /b 1
cd /d "@{[Win($Top)]}"
set AUTOBUILD_VSVER=180
set CMAKE_POLICY_VERSION_MINIMUM=3.5
set AUTOBUILD_VARIABLES_FILE=$vars
set VIRTUAL_ENV=@{[Win($Venv)]}
set PATH=@{[Win($SdkBin)]};@{[Win("$Venv/Scripts")]};%PATH%
set DIGICERTUTIL_PATH=@{[Win($DigiCert)]}
set DIGICERT_THUMBPRINT=$Thumbprint
set revision=
set AUTOBUILD_BUILD_ID=
autobuild configure -A 64 -c ReleaseFS_open -- --chan DreamStorm -DLL_TESTS:BOOL=FALSE -DPACKAGE:BOOL=ON || exit /b 1
autobuild build -A 64 -c ReleaseFS_open --no-configure || exit /b 1
BAT
close $B;
Say( "Build script: $bat" );
RunOrFail( 'cmd /c "' . Win($bat) . '"' );

my $Exe;
my $Ver = $Version;
unless ($dry) {
    my @exes = sort { -M $a <=> -M $b } grep { ( stat $_ )[9] >= $Started - 5 }
      glob("$BuildDir/newview/Release/*_Setup.exe");
    Fail("The build made no new *_Setup.exe in $BuildDir/newview/Release.") unless @exes;
    $Exe = abs_path( $exes[0] );
    if ( open( my $v, '<', "$BuildDir/newview/viewer_version.txt" ) ) {
        $Ver = <$v>;
        $Ver =~ s/\s+//g;
    }
    Fail("Built version $Ver is not $Version (from git). Is 'revision' or AUTOBUILD_BUILD_ID set?")
      if $Ver ne $Version;
    Say("Installer: $Exe");

    # signature check
    for my $f ( $Exe, "$BuildDir/newview/Release/FirestormOS-DreamStorm.exe" ) {
        next unless -f $f;
        my ( $out, $code ) = Read( '"' . Win($SignTool) . '" verify /pa "' . Win($f) . '"', 1 );
        if ($code) {
            Say($out);
            Fail("$f is not signed.");
        }
        else { Say( 'Signed OK: ', basename($f) ) }
    }
}
else { $Exe = "$Top/$BuildDir/newview/Release/Phoenix-FirestormOS-DreamStorm_LEGACY-" . join( '-', split /\./, $Version ) . '_Setup.exe' }

# ---------------------------------------------------------------- Beta
if ($beta) {
    my $name = basename($Exe);
    Say("Copying to $BetaDir");
    unless ($dry) {
        make_path($BetaDir);
        for my $old ( glob("$BetaDir/*_Setup.exe") ) {
            next if basename($old) eq $name;
            unlink $old or Say("Could not delete old Beta $old: $!");
            Say( 'Deleted old Beta ', basename($old) );
        }
        copy( $Exe, "$BetaDir/$name" ) or Fail("Copy to $BetaDir failed: $!");
        Fail("The copy in $BetaDir is not the same size.") if -s $Exe != -s "$BetaDir/$name";
    }
}

# ---------------------------------------------------------------- GitHub (both)
RunOrFail("git push origin $Branch");
RunOrFail("git tag -a $Tag -m \"DreamStorm $Ver $Mode\" $Sha");
RunOrFail("git push origin $Tag");

# release notes: commits since the last Release (for a Beta, since the last Release or Beta)
my $prev = '';
for ( split /\n/, Read("git tag -l $TagPrefix* --sort=-creatordate") ) {
    next if $_ eq $Tag;
    next if $release && /-beta$/;
    $prev = $_;
    last;
}
my $log = $prev ? Read("git log --oneline --no-merges $prev..$Sha") : '';
my $notesFile = "$Top/$BuildDir/logs/jenkins-notes.md";
open( my $N, '>', $notesFile ) or Fail("Cannot write $notesFile: $!");
print $N "**Beta: a test build, not for general use.**\n\n" if $beta;
print $N "DreamStorm $Ver for OpenSim, built from $Sha on branch $Branch.\n\n";
print $N "Download page: $WebUrl/DreamStorm/\n\n";
print $N "Windows installer is signed by Outworldz. The macOS .dmg and Linux .tar.xz are added by the GitHub action.\n";
print $N "\nChanges since $prev:\n\n" . join( '', map { "- $_\n" } split /\n/, $log ) if $log =~ /\S/;
close $N;

my $kind  = $beta ? '--prerelease --latest=false' : '--latest';
my $title = "DreamStorm $Ver" . ( $beta ? ' Beta' : '' );
RunOrFail("gh release create $Tag \"$Exe\" --repo $Repo --verify-tag $kind --title \"$title\" --notes-file \"$notesFile\"");

my $dispatched = time;
RunOrFail("gh workflow run $Workflow --repo $Repo --ref $Tag -f release_tag=$Tag");

if ($beta) {
    # A Beta does not wait for the Mac/Linux build and does not touch the download page.
    Say( "\nBeta $Ver is ready.\n  Windows: $WebUrl/DreamStorm/Beta/" . basename($Exe) );
    Say("  GitHub pre-release: https://github.com/$Repo/releases/tag/$Tag");
    Say("  The .dmg and .tar.xz are added there by the GitHub action in about 1-2 hours:");
    Say("  https://github.com/$Repo/actions/workflows/$Workflow");
    Done();
}

unless ($dry) {
    my $id;
    for ( 1 .. 30 ) {
        sleep 5;
        my $json = Read("gh run list --repo $Repo --workflow $Workflow --event workflow_dispatch -L 10 --json databaseId,headBranch,createdAt");
        for my $r ( @{ decode_json($json) } ) {    # newest first
            my ($y, $mo, $d, $h, $mi, $s) = $r->{createdAt} =~ /^(\d+)-(\d+)-(\d+)T(\d+):(\d+):(\d+)/ or next;
            my $made = timegm( $s, $mi, $h, $d, $mo - 1, $y );
            next unless $r->{headBranch} eq $Tag || $made >= $dispatched - 15;
            $id = $r->{databaseId};
            last;
        }
        last if $id;
    }
    Fail("Could not find the $Workflow run for $Tag. Look in the Actions tab, then run: perl Jenkins.pl --publish $Tag")
      unless $id;
    Say("Mac/Linux build: https://github.com/$Repo/actions/runs/$id  (about 1-2 hours)");
    my $code = Run("gh run watch $id --repo $Repo --exit-status --interval 60");
    Fail(   "The Mac/Linux build failed. The web page was not changed. Re-run the failed jobs on GitHub, "
          . "then run: perl Jenkins.pl --publish $Tag" )
      if $code;
}
if ($dry) {
    Say("> (wait for the Mac/Linux build, then publish $Tag on the web page)");
    Done();
}
Publish($Tag);
Done();

# ---------------------------------------------------------------- subs
sub CheckGh {
    my ( $out, $code ) = Read( 'gh auth status', 1 );
    return unless $code;
    if ($dry) { Say("WARNING: gh is not logged in. A real run stops here; run 'gh auth login' once."); return }
    Fail("gh is not logged in. Run 'gh auth login' once, then try again.");
}

# Writes the download table from the release's files, puts the include in Default.htm (first time),
# checks the live page, then deletes the old .exe in Grid/DreamStorm.
sub Publish {
    my ($tag) = @_;
    Fail("$tag is a Beta. Only Releases go on the download page.") if $tag =~ /-beta$/;
    ( my $ver = $tag ) =~ s/^\Q$TagPrefix\E//;
    Say("\nPublishing $tag on the web page");

    my $rel    = decode_json( Read("gh release view $tag --repo $Repo --json assets,createdAt") );
    my %assets = map { $_->{name} => $_ } @{ $rel->{assets} };
    my ($released) = ( $rel->{createdAt} // '' ) =~ /^(\d{4}-\d\d-\d\d)/;
    # if a release holds more than one build of a file (a re-run), take the newest build number
    my ($exe) = reverse grep { /_Setup\.exe$/ } sort keys %assets;
    my ($dmg) = reverse grep { /\.dmg$/ } sort keys %assets;
    my ($txz) = reverse grep { /\.tar\.xz$/ } sort keys %assets;
    Fail("Release $tag has no _Setup.exe.") unless $exe;
    Fail("Release $tag has no .dmg or .tar.xz yet. Wait for the GitHub action, then run: perl Jenkins.pl --publish $tag")
      unless $dmg && $txz;

    my $row = sub {
        my ( $name, $label, $kind ) = @_;
        my $mb = int( $assets{$name}{size} / 1048576 + 0.5 );
        return <<"ROW";
		<tr>
			<td><img src="/images/download.png" alt="Download"></td>
			<td><strong><a href="https://github.com/$Repo/releases/download/$tag/$name">$label</a></strong></td>
			<td>$kind, $mb MB</td>
		</tr>
ROW
    };
    my $date = strftime( '%Y-%m-%d', localtime );
    my $html =
        "<!-- Written by Jenkins.pl on $date for $tag. Do not edit: the next release overwrites it. -->\n"
      . "\t<p>Version <strong>$ver</strong>, released " . ( $released // $date ) . ' '
      . "(<a href=\"https://github.com/$Repo/releases/tag/$tag\">release notes and source</a>).</p>\n"
      . "\t<table class=\"dsdl\">\n"
      . $row->( $exe, 'Windows 64-bit installer', '.exe' )
      . $row->( $dmg, 'macOS', '.dmg' )
      . $row->( $txz, 'Linux 64-bit', '.tar.xz' )
      . "\t</table>\n";

    my $inc = "$PageDir/$IncludeName";
    Say("Writing $inc");
    unless ($dry) {
        open( my $I, '>:raw', "$inc.new" ) or Fail("Cannot write $inc.new: $!");
        print $I $html;
        close $I;
        unlink $inc if -e $inc;
        rename( "$inc.new", $inc ) or Fail("Cannot rename $inc.new: $!");
    }

    # Default.htm: replace the hand-made table with the include, once
    my $page = "$PageDir/Default.htm";
    open( my $P, '<:raw', $page ) or Fail("Cannot read $page: $!");
    my $text = do { local $/; <$P> };
    close $P;
    if ( $text !~ /#include virtual="\Q$IncludeName\E"/ ) {
        my $n = ( $text =~ s{[ \t]*<table class="dsdl">.*?</table>[ \t]*(\r?\n)}{\t<!--#include virtual="$IncludeName" -->$1}s );
        Fail("Could not find <table class=\"dsdl\"> in $page.") unless $n;
        Say("Putting the include in $page (the old page stays in the web server's git)");
        unless ($dry) {
            open( my $W, '>:raw', $page ) or Fail("Cannot write $page: $!");
            print $W $text;
            close $W;
        }
    }

    # record the page change in the web server's git. Only these two files are committed: anything
    # else you have staged there stays staged.
    my $files = join( ' ', map { "\"$_\"" } @WebGitFiles );
    if ($dry) {
        Say("> $WebGit add $files");
        Say("> $WebGit commit -m \"DreamStorm $tag download links\" -- $files");
        return;
    }
    if ( Read("$WebGit status --porcelain -- $files") =~ /\S/ ) {
        RunOrFail("$WebGit add $files");
        RunOrFail("$WebGit commit -m \"DreamStorm $ver download links (Jenkins.pl)\" -- $files");
    }
    else { Say("Web git: page already committed for $tag.") }

    # check the live page, then remove the old copy
    my $url  = "$WebUrl/DreamStorm/";
    my $want = "releases/download/$tag/$exe";
    my $ok;
    for ( 1 .. 3 ) {
        my $r = HTTP::Tiny->new( timeout => 30 )->get( $url . '?jenkins=' . time );
        $ok = $r->{success} && index( $r->{content}, $want ) >= 0;
        last if $ok;
        sleep 5;
    }
    Fail("$url does not show the new links yet, so the old .exe in $OldGridDir was kept. Check the page.") unless $ok;
    Say("Live page shows $tag.");

    for my $old ( glob("$OldGridDir/*.exe") ) {
        if   ( unlink $old ) { Say( 'Deleted ', Win($old) ) }
        else                 { Say("Could not delete $old: $!") }
    }

    Say("\nRelease $ver is out.");
    Say("  Page:    $url");
    Say("  Release: https://github.com/$Repo/releases/tag/$tag");
}

sub Done {
    Say("Log: $LogFile");
    exit 0;
}
