from __future__ import absolute_import
"""This package provides json-based encoding and decoding for use in FreeOrion AI savegames.

The encoding is json-based with custom prefixes to support some objects
and dictionary keys of types which are not supported in standard json.

The decoder will only load trusted classes as defined in _definitions.py,
if an unknown/untrusted object is encountered, it will raise a InvalidSaveGameException.

When class instances are loaded, the __setstate__ method will be invoked if available or
the __dict__ content will be set directly. It is the responsibility of each trusted class
to define a __setstate__ method to verify and possibly sanitize the passed data.
The __setstate__ method is expected to raise an Exception if invalid data was passed.

In contrast to standard library json module, the type of dict keys (e.g. int) is preserved:
    import json
    json_string = json.dumps({1: 2})  ==> '{"1": 2}'
    json.loads(json_string) ==> {"1": 2}

    import savegame_codec  # load package
    json_string = savegame_codec.encode({1: 2})  ==> '{"__INT__1": 2'}'
    savegame_codec.decode(json_string)  ==> {1: 2}


Public functions:
    encode  -  encode a python object to an unambigious string representation
    decode  -  decode and interpret a json string to retreive python objects
    build_savegame_string  - encode the AIstate and return the zlib-compressed result
    load_savegame_string  - decode a savegame string generated by build_savegame_string

Defined Exceptions:
    CanNotSaveGameException   -  raised when an object could not be encoded
    InvalidSaveGameException  -  raised when a savegame string is not trusted or could not be decoded

Example usage:
    # saving the game
    import savegame_codec  # load package
    savegame_string = savegame_codec.build_savegame_string()

    # loading the game
    import savegame_codec
    aistate = savegame_codec.load_savegame_string(savegame_string)
"""

from ._decoder import decode, load_savegame_string
from ._definitions import CanNotSaveGameException, InvalidSaveGameException
from ._encoder import encode, build_savegame_string
