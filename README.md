wslock
========

`wslock` is a simple X screen locker. Similar to `slock` and `i3lock`. In fact,
`wslock` is more like a stripped version of i3lock, implementing its own
event-loop to drop `libev` dependancy. `i3lock` just using `libev` for its timer
anyway. No pam support, for now.

Installation
-------------

    make

Needs root privilege to set `setuid` permission properly if not using PAM to
auth. `wslock` needs root to read from shadow file for hashed password.

You may run

    make setsuid

with root to set permissions properly.


Usage:
--------

Just

    ./wslock

and then input your password then `Enter` to exit.




