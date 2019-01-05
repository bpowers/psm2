psm - simple, accurate memory reporting for Linux
=================================================

`psm` makes it easy to see who is resident in memory, and who is
significantly swapped out.

`psm` is based off the ideas and implementation of
[ps_mem.py](https://github.com/pixelb/scripts/commits/master/scripts/ps_mem.py).
It requires root privileges to run.  It is implemented in Rust, and
since the executable is a binary it can be made setuid root so that
unprivileged users can get a quick overview of the current memory
situation.

installation
------------

Classic Rust:

    git clone https://github.com/bpowers/psm2
    cd psm
    cargo build --release
    sudo make install


example output
--------------

    bpowers@python-worker-01:~$ psm -filter=celery
        MB RAM    SHARED   SWAPPED	PROCESS (COUNT)
          60.6       1.1     134.2	[celeryd@notifications:MainProcess] (1)
          62.6       1.1          	[celeryd@health:MainProcess] (1)
         113.7       1.2          	[celeryd@uploads:MainProcess] (1)
         155.1       1.1          	[celeryd@triggers:MainProcess] (1)
         176.7       1.2          	[celeryd@updates:MainProcess] (1)
         502.9       1.2          	[celeryd@lookbacks:MainProcess] (1)
         623.8       1.2      28.5	[celeryd@stats:MainProcess] (1)
         671.3       1.2          	[celeryd@default:MainProcess] (1)
    #   2366.7               164.7	TOTAL USED BY PROCESSES

The `MB RAM` column is the sum of the Pss value of each mapping in
`/proc/$PID/smaps` for each process.


license
-------

psm is offered under the MIT license, see LICENSE for details.
