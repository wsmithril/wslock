wslock
========

`wslock` is a simple X screen locker. Similar to `slock` and `i3lock`. In fact,
`wslock` is more like a stripped version of i3lock, implementing its own
event-loop to drop `libev` dependancy. `i3lock` just using `libev` for its timer
anyway. With PAM support (optional).

Installation
-------------

    make && sudo make install

Needs root privilege to set `setuid` permission properly if not using PAM to
auth. for `wslock` needs root to read from shadow file for hashed password, and
then drop root privilege.


Usage:
--------

Just

    ./wslock

and then input your password then `Enter` to exit.

