Python Bindings Notes
======================

The python integration is written with Python 2/3 in mind, however, there is a bias to Python 3. Because of this, there are a few caveats:
- I am only testing against Python 2.7, and have no intention to support/test < 2.7 but am more than happy to accept PRs for fixes as long as it does not break 2.7/3
- repr/print of native types is dumbed down for < 2.7.9, I highly recommend using this version or new. (This is due to a [bug](http://bugs.python.org/issue22023) in python)

# TGL Module Level Fuctions

