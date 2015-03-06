# RPOCO (Reflect Plain Old C++ Objects)

## Overview

RPOCO is a small header based reflection system with an accompanying JSON parser
to enable very simple parsing and writing of JSON and other data from the network
and disk to plain C++ objects without having manually write bindings.

## How it works

A RPOCO macro is placed within all classes specifying fields that needs
reading and writing and from that a set of macros and templates expands
a type info structures that the JSON parser/writer and other tools
can hook into to automate serialization work.

## license
Copyright 2015, Jonas Lund

json_parser test files are seprately copyrighted and licensed.

Copyright (C) 2012, 2013 James McLaughlin et al.  All rights reserved. 

