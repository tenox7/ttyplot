# ttyplot
a realtime plotting utility for text mode consoles and terminals with data input from stdin / pipe

takes data from standard input / unix pipeline, most commonly some tool like *ping, snmpget, netstat, ip link, ifconfig, sar, vmstat*, etc. and plots in text mode on a terminal in real time, for example a simple **ping**:

![ttyplot ping](ttyplot-ping.png)

&nbsp;
&nbsp;


supports rate calculation for counters and up to two graphs on a single display using reverse video for second line, for example **snmpget**, **ip link**, **rrdtool**, etc:

![ttyplot snmp](ttyplot-snmp.png)


&nbsp;
&nbsp;

## get

### ubuntu

```
snap install ttyplot
```

### debian

maybe

```
apt install ttyplot
```

[Download Packages](https://packages.debian.org/sid/ttyplot) 

[Tracker](https://tracker.debian.org/pkg/ttyplot)

### macOS

```
brew install ttyplot
```

### misc

for other platforms see [releases tab](https://github.com/tenox7/ttyplot/releases)

## examples

### cpu usage from vmstat using awk to pick the right column
```
vmstat -n 1 | gawk '{ print 100-int($(NF-2)); fflush(); }' | ttyplot
```

### cpu usage from sar with title and fixed scale to 100%
```
sar 1 | gawk '{ print 100-int($NF); fflush(); }' | ttyplot -s 100 -t "cpu usage" -u "%"
```

### memory usage from sar, using perl to pick the right column
```
sar -r 1 | perl -lane 'BEGIN{$|=1} print "@F[5]"' | ttyplot -s 100 -t "memory used %" -u "%"
```

### memory usage on macOS
```
vm_stat 1 | awk '{ print int($2)*4096/1024^3; fflush(); }' | ttyplot -t "MacOS Memory Usage" -u GB
```

### number of processes in running and io blocked state
```
vmstat -n 1 | perl -lane 'BEGIN{$|=1} print "@F[0,1]"' | ttyplot -2 -t "procs in R and D state"
```

### load average via uptime and awk
```
{ while true; do uptime | gawk '{ gsub(/,/, ""); print $(NF-2) }'; sleep 1; done } | ttyplot -t "load average" -s load
```

### ping plot with sed
on macOS change `-u` to `-l`
```
ping 8.8.8.8 | sed -u 's/^.*time=//g; s/ ms//g' | ttyplot -t "ping to 8.8.8.8" -u ms
```

### wifi signal level in -dBM (higher is worse) using iwconfig
```
{ while true; do iwconfig 2>/dev/null | grep "Signal level" | sed -u 's/^.*Signal level=-//g; s/dBm//g'; sleep 1; done } | ttyplot -t "wifi signal" -u "-dBm" -s 90
```

### wifi signal on macOS
```
{ while true; do /System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport --getinfo | awk '/agrCtlRSSI/ {print -$2; fflush();}'; sleep 1; done } | ttyplot -t "wifi signal" -u "-dBm" -s 90
```

### cpu temperature from proc
```
{ while true; do awk '{ printf("%.1f\n", $1/1000) }' /sys/class/thermal/thermal_zone0/temp; sleep 1; done } | ttyplot -t "cpu temp" -u C
```

### fan speed from lm-sensors using grep, tr and cut
```
{ while true; do sensors | grep fan1: | tr -s " " | cut -d" " -f2; sleep 1; done } | ttyplot -t "fan speed" -u RPM
```

### memory usage from rrdtool and collectd using awk
```
{ while true; do rrdtool lastupdate /var/lib/collectd/rrd/$(hostname)/memory/memory-used.rrd | awk 'END { print ($NF)/1024/1024 }'; sleep 1; done } | ttyplot -m $(awk '/MemTotal/ { print ($2)/1024 }' /proc/meminfo) -t "Memoru Used" -u MB
```

### bitcoin price chart using curl and jq
```
{ while true; do curl -sL https://api.coindesk.com/v1/bpi/currentprice.json  | jq .bpi.USD.rate_float; sleep 600; done } | ttyplot -t "bitcoin price" -u usd
```

### stock quote chart
```
{ while true; do curl -sL https://api.iextrading.com/1.0/stock/googl/price; echo; sleep 600; done } | ttyplot -t "google stock price" -u usd
```

### prometheus load average via node_exporter
```
{ while true; do curl -s  http://10.4.7.180:9100/metrics | grep "^node_load1 " | cut -d" " -f2; sleep 1; done } | ttyplot
```


&nbsp;
&nbsp;



## network/disk throughput examples
ttyplot supports "two line" plot for in/out or read/write

### snmp network throughput for an interface using snmpdelta
```
snmpdelta -v 2c -c public -Cp 10 10.23.73.254 1.3.6.1.2.1.2.2.1.{10,16}.9 | gawk '{ print $NF/1000/1000/10; fflush(); }' | ttyplot -2 -t "interface 9 throughput" -u Mb/s
```

### local network throughput for all interfaces combined from sar
```
sar  -n DEV 1 | gawk '{ if($6 ~ /rxkB/) { print iin/1000; print out/1000; iin=0; out=0; fflush(); } iin=iin+$6; out=out+$7; }' | ttyplot -2 -u "MB/s"
```

### disk throughput from iostat
```
iostat -xmy 1 nvme0n1 | stdbuf -o0 tr -s " " | stdbuf -o0 cut -d " " -f 4,5 | ttyplot -2 -t "nvme0n1 throughput" -u MB/s
```

&nbsp;
&nbsp;



## rate calculator for counters
ttyplot also supports *counter* style metrics, calculating *rate* by measured time difference between samples

### snmp network throughput for an interface using snmpget
```
{ while true; do snmpget -v 2c -c public 10.23.73.254 1.3.6.1.2.1.2.2.1.{10,16}.9 | awk '{ print $NF/1000/1000; }'; sleep 10; done } | ttyplot -2 -r -u "MB/s"
```

### local interface throughput using ip link and jq
```
{ while true; do ip -s -j link show enp0s31f6 | jq .[].stats64.rx.bytes/1024/1024,.[].stats64.tx.bytes/1024/1024; sleep 1; done } | ttyplot -r -2 -u "MB/s"
```

### prometheus node exporter disk throughput for /dev/sda
```
{ while true; do curl -s http://10.11.0.173:9100/metrics | awk '/^node_disk_.+_bytes_total{device="sda"}/ { printf("%f\n", $2/1024/1024); }'; sleep 1; done } | ttyplot -r -2 -u MB/s -t "10.11.0.173 sda writes"
```

### network throughput from collectd with rrdtool and awk
```
{ while true; do rrdtool lastupdate /var/lib/collectd/rrd/$(hostname)/interface-enp1s0/if_octets.rrd | awk 'END { print ($2)/1000/1000, ($3)/1000/1000 }'; sleep 10; done } | ttyplot -2 -r -t "enp1s0 throughput" -u MB/s
```

&nbsp;
&nbsp;


## flags

```
  ttyplot [-2] [-r] [-c plotchar] [-s scale] [-m max] [-M min] [-t title] [-u unit]

  -2 read two values and draw two plots, the second one is in reverse video
  -r rate of a counter (divide value by measured sample interval)
  -c character to use for plot line, eg @ # % . etc
  -e character to use for plot error line when value exceeds hardmax (default: e)
  -E character to use for error symbol displayed when value is less than hardmin (default: v)
  -s initial scale of the plot (can go above if data input has larger value)
  -m maximum value, if exceeded draws error line (see -e), upper-limit of plot scale is fixed
  -M minimum value, if entered less than this, draws error symbol (see -E), lower-limit of the plot scale is fixed
  -t title of the plot
  -u unit displayed beside vertical bar
```

&nbsp;
&nbsp;



## frequently questioned answers
### stdio buffering
by default in unix stdio is buffered, you can work around it in [various ways](http://www.perkin.org.uk/posts/how-to-fix-stdio-buffering.html) also [this](https://collectd.org/wiki/index.php/Plugin:Exec#Output_buffering)

### ttyplot quits when there is no more data
this is by design; your problem is likely that the output is lost when ttyplot exits; this is explained in [the next question below](#ttyplot-erases-screen-when-exiting))

you can also "work around" by adding `sleep`, `read`, `cat`, etc:

```
{ echo 1 2 3; cat; } | ttyplot
```

### ttyplot erases screen when exiting
this is because of [alternate screen](https://invisible-island.net/xterm/xterm.faq.html#xterm_tite) in xterm-ish terminals; if you use one of these this will likely work around it:

```
echo 1 2 3 | TERM=vt100 ttyplot
```

you can also permanently fix terminfo entry (this will make a copy in $HOME/.terminfo/):

```
infocmp -I $TERM | sed -e 's/smcup=[^,]*,//g' -e 's/rmcup=[^,]*,//g' | tic -
```

### when running interactively and non-numeric data is entered (eg. some key) ttyplot hangs
press `ctrl^j` to re-set 

## legal stuff
```
License: Apache 2.0
Copyright (c) 2013-2018 Antoni Sawicki
Copyright (c) 2019-2021 Google LLC
```
