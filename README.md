Attention!
==========

_This project was moved in [plate-tectonics](https://github.com/Mindwerks/plate-tectonics)_.

A brief history:
* we originally created pyplatec as the python bindings of platec
* we since then forked platec as plate-tectonics, because the project was not maintained. Pyplatec became the python bindings of plate-tectonics instead of platec
* we decided it makes more sense to have a single project containing the library and the python bindings. That project is plate-tectonics](https://github.com/Mindwerks/plate-tectonics). This project (PyPlatec) is kept only as a reference for existing users.

PyPlatec
========

_Travis_
[![Build Status](https://travis-ci.org/Mindwerks/pyplatec.svg?branch=master)](https://travis-ci.org/Mindwerks/pyplatec) _AppVeyor_ [![Build status](https://ci.appveyor.com/api/projects/status/3qh8txg5bhf41fxv?svg=true)](https://ci.appveyor.com/project/ftomassetti/pyplatec)


Plate simulation for python (wrapper of an existing C/C++ library).

Take a look at the simulation at work: http://www.youtube.com/watch?v=bi4b45tMEPE#t=0

The original project is here: http://sourceforge.net/projects/platec/ . That project is part of a Bachelor of Engineering thesis in Metropolia University of Applied Sciences, Helsinki, Finland. The thesis is freely downloadable from http://urn.fi/URN:NBN:fi:amk-201204023993 .

Kudos to the original author: Lauri Viitanen!

Requirements
============

Supported versions:
* Python 2.6 (currently not supported on AppVeyor)
* Python 2.7
* Python 3.2 (currently not supported on AppVeyor)
* Python 3.3
* Python 3.4

Clone
=====

This project contains git submodules. After cloning it do:

```
git submodule update --init --recursive
cd plate-tectonics
git checkout v1.3.1
```

Compile
=======

```
python setup.py build
```

Usage
=====

The library is quite simple:

    p = platec.create(seed=3)
    while platec.is_finished(p)==0:
        platec.step(p)
    hm = platec.get_heightmap(p)
    platec.destroy(p)


Or if you want more control:

    p = platec.create(seed=3, width=1000, height=800,
                      sea_level=0.65,erosion_period=60,
                      folding_ratio=0.02,aggr_overlap_abs=1000000,
                      aggr_overlap_rel=0.33,cycle_count=2,num_plates=10)

Enjoy!

Projects using it
=================

I created this wrapper for using this fantastic piece of code in [worldengine](http://github.com/Mindwerks/worldengine)

Supporting windows wheels
=========================

Through AppVeyor we build the binary distributions for windows (several python versions, both win32 and win64).
However they need to be downloaded from AppVeyor and uploaded to Pypi manually.

