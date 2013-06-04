#!/usr/bin/perl

my $test_count;

BEGIN {
    chdir($1) if ($0=~/(.*)\//);
    mkdir("tmp");
    $test_count = 18;
}

use Cwd;
use Test::More tests=>$test_count;
use Time::HiRes qw(sleep time);
use Getopt::Long;

my $DEFPROG="../grun";
my $prog=$DEFPROG;
GetOptions("prog=s"=>\$prog);

if (!($DEFPROG eq $prog)) {
    diag("Using program $prog\n");
}

my $tmpdir = cwd() . "/tmp";

`rm -rf $tmpdir/grun-test.*`;

# write out a config file listening on 127.0.0.1 port 9418

my $CONF = "$tmpdir/grun-test.conf";

mkdir "$tmpdir/grun-test.spool";

ok(-d "$tmpdir/grun-test.spool", "grun spool");

my $cwd = cwd();

open O, ">$CONF";
print O <<"EOF";
master:         127.0.0.1
port:           9417
services:       queue,exec
log_file:       $tmpdir/grun-test.log
spool:          $tmpdir/grun-test.spool
pid_file:       $tmpdir/grun-test.pid
cpus:           4
env:            *
auto_profile:   /etc/grun.prof
expire_secs:    1200
spread_pct:     .05
query_default:  \$self
log_types:      note error warn debug
param-io:       8
trace:          2
EOF


#start execution daemon

my $grun="$prog -C $CONF";

`pkill -9 -f $CONF`;

$SIG{ALRM} = sub { die "Timeout while trying to initiate daemon, aborting test\n" };
alarm 120;

diag("$grun -d");

is($ok=system("$grun -d 2> tmp/grun-test.d.err"),0,"grun daemon");
if ($ok) {
    $ok = $ok >> 8;
    diag("Daemon fail exit code:$ok, orig-code: $?, str: $!") if $ok;
}
alarm 0;
$SIG{ALRM} = sub {};

sleep(.25);

$pid =0+`cat $tmpdir/grun-test.pid`;
cmp_ok($pid,">",1,"grun pid");

SKIP: {
    skip "grun won't start", $test_count-3 unless $pid > 1; 

    $stat=`$grun -d -r`;
    ok($stat=~/Ok/i,"reload ok");

    # simple 'hello' test
    diag("$grun echo hello");
    $out=`$grun echo hello`;

    is($out, "hello\n", "hello works");

    my $out =`$grun -nowait sleep 10`;
    $jobs=`$grun -q jo`;
    like($jobs, qr/sleep/,"jobs list");

    my ($id) = $out =~ /Job_ID:\s*(\d+)/;

    ok($id>0, "Returns jobid on nowait");

    $out=`$grun -k $id 2>&1`;
    like($out,qr/$id aborted/,"Kill works");
    
    $out=`$grun -q hist -c 5000`;
    like($out,qr/sleep/,"Hist works");

    $out=0+`$grun "yes | head -1000" | wc -l`;
    is($out,1000,"test pipe 1000");
    
    local $SIG{ALRM} = sub { kill $pid }; 

    # creates an 'abusive' batch file
    my $count=100;
    open O, ">$tmpdir/grun-test.fork.sh";
    for ($i=0;$i<$count;++$i) {
        print O "$grun echo $i >> $tmpdir/grun-test.fork.out 2>&1 &\n";
    }
    print O "wait\n";
    close O;

    $took_too_long=0;

    if (!($pid = fork)) {
        # run a bunch of simultaneous gruns
        exec("bash $tmpdir/grun-test.fork.sh");
    } else {
        # meanwhile... wait for that to finishe
        use POSIX ":sys_wait_h";
        # wait for a while
        $start=time();
        diag("fork/timing test wait");
        while(waitpid($pid, WNOHANG)!=$pid) {
            sleep(.25);
            if (time() > $start+600) {
                $took_too_long=1;
                diag("took too long, killing");
                kill 2, $pid;
                last;
            }
        }
    }

    # did it take too long?
    ok(!$took_too_long, "grun fork speed");
    cmp_ok(0+`wc -l $tmpdir/grun-test.fork.out`, "==", $count, "fork got $count responses");

    # ok now try even more.. , but using the nicer "nowait" semantic
    $count=200;
    for ($i=0;$i<$count;++$i) {
        `$grun -nowait 'echo $i >> $tmpdir/grun-test.nofork.out 2>&1'`;
    }

    # do things respond nicely when the queue is loaded?
    $stat=`$grun -q st`;
    ok($stat=~/Hostname/,"stat return");

    # ok wait for stuff
    while(1) {
        sleep(.25);
        if (time() > $start+600) {
            $took_too_long=1;
            diag("took too long!");
            `pkill -INT $CONF`;
            last;
        }
        last if $count == `wc -l $tmpdir/grun-test.nofork.out 2>/dev/null`;
    }

    # did it return in time?
    ok(!$took_too_long, "grun nofork speed");
    cmp_ok(0+`wc -l $tmpdir/grun-test.nofork.out`, "==", $count, "nowait got $count responses");

    # shut down the daemon
    is(system("$grun -d -k"),0,"grun daemon kill");

    # linger a bit
    sleep(2);

    # is it running?
    $running = `pgrep -f $tmpdir/grun-test`;
    chomp($running);
    ok(!$running, "daemon stop worked");

    # sleep a bit
    sleep(.25);

    # ensure it's really gon
    if (`pgrep -f $CONF` =~ /\d+/) {
        `pkill -9 -f $CONF`;
        ok(0, "no stranded childredn");
    } else {
        ok(1, "no stranded childredn");
    }

}

sleep(1);
