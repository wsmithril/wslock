wslock
========

`wslock` is a simple X screen locker. Similar to `slock` and `i3lock`. In fact,
`wslock` is more like a stripped version of i3lock, implementing its own
event-loop to drop `libev` dependancy. `i3lock` just using `libev` for its timer
anyway. No pam support, for now.

Installation
-------------

    make

Needs root privilege to set `suid` primission properly if not using PAM to
auth.

Usage:
--------

Just

   ./wslock

and then input your password then `Enter` to exit.




