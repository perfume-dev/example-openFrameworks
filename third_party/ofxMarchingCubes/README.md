# ofxMarchingCubes

This directory vendors the `src` directory from
[atduskgreg/ofxMarchingCubes](https://github.com/atduskgreg/ofxMarchingCubes)
at commit `4e54316b7c5eb7637e78934557c4236bea6a78a6` so that the historical
`marching-cubes` example remains reproducible after the original Google Code
download disappeared.

The implementation is Copyright (c) 2009 Rui Madeira and is distributed under
the GNU Lesser General Public License, version 2.1 or later. The complete
license notice is retained at the top of every vendored source file, and the
full terms are included in [`LICENSE.txt`](LICENSE.txt). Greg Borenstein
adapted the original code for openFrameworks 0.0071.

All four source files carry dated modification notices. Source formatting has
also been normalized in this maintained copy.

This is a locally maintained vendored copy rather than an unmodified upstream
snapshot. It makes the unused STL exporter integration opt-in with
`OFX_MARCHING_CUBES_ENABLE_STL`, so the example does not require the separate
historical `ofxSTL` addon. It also includes lifecycle and grid-boundary guards,
finite metaball handling, a correction to equal-value vertex interpolation,
and current openFrameworks drawing/vector API calls. These maintenance changes
preserve the upstream copyright and LGPL terms.

Upstream provenance:

- <https://github.com/atduskgreg/ofxMarchingCubes>
- <https://github.com/atduskgreg/ofxMarchingCubes/tree/4e54316b7c5eb7637e78934557c4236bea6a78a6/src>
